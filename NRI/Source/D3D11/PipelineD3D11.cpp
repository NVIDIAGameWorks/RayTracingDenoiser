/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "SharedD3D11.h"
#include "PipelineD3D11.h"

#include "DescriptorD3D11.h"
#include "DescriptorSetD3D11.h"

#include "NVAPI/nvapi.h"

using namespace nri;

PipelineD3D11* PipelineD3D11::s_NullGraphicsPipeline = nullptr;

PipelineD3D11::PipelineD3D11(DeviceD3D11& device, const VersionedDevice& versionedDevice) :
    m_VersionedDevice(versionedDevice),
    m_Device(device),
    m_InputAssemplyStrides(device.GetStdAllocator()),
    m_RasterizerStates(device.GetStdAllocator())
{
}

PipelineD3D11::~PipelineD3D11()
{
    Deallocate(m_Device.GetStdAllocator(), m_RasterizerStateExDesc);
}

Result PipelineD3D11::Create(const GraphicsPipelineDesc& pipelineDesc)
{
    const InputAssemblyDesc& ia = *pipelineDesc.inputAssembly;
    const RasterizationDesc& rs = *pipelineDesc.rasterization;
    const OutputMergerDesc& om = *pipelineDesc.outputMerger;
    const DepthAttachmentDesc& ds = om.depth;
    const StencilAttachmentDesc& ss = om.stencil;
    const ShaderDesc* vertexShader = nullptr;
    HRESULT hr;

    // shaders

    for (uint32_t i = 0; i < pipelineDesc.shaderStageNum; i++)
    {
        const ShaderDesc* shaderDesc = pipelineDesc.shaderStages + i;

        if (shaderDesc->stage == ShaderStage::VERTEX)
        {
            vertexShader = shaderDesc;
            hr = m_VersionedDevice->CreateVertexShader(shaderDesc->bytecode, shaderDesc->size, nullptr, &m_VertexShader);
            RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "ID3D11Device::CreateVertexShader() - FAILED!");
        }
        else if (shaderDesc->stage == ShaderStage::TESS_CONTROL)
        {
            hr = m_VersionedDevice->CreateHullShader(shaderDesc->bytecode, shaderDesc->size, nullptr, &m_TessControlShader);
            RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "ID3D11Device::CreateHullShader() - FAILED!");
        }
        else if (shaderDesc->stage == ShaderStage::TESS_EVALUATION)
        {
            hr = m_VersionedDevice->CreateDomainShader(shaderDesc->bytecode, shaderDesc->size, nullptr, &m_TessEvaluationShader);
            RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "ID3D11Device::CreateDomainShader() - FAILED!");
        }
        else if (shaderDesc->stage == ShaderStage::GEOMETRY)
        {
            hr = m_VersionedDevice->CreateGeometryShader(shaderDesc->bytecode, shaderDesc->size, nullptr, &m_GeometryShader);
            RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "ID3D11Device::CreateGeometryShader() - FAILED!");
        }
        else if (shaderDesc->stage == ShaderStage::FRAGMENT)
        {
            hr = m_VersionedDevice->CreatePixelShader(shaderDesc->bytecode, shaderDesc->size, nullptr, &m_FragmentShader);
            RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "ID3D11Device::CreatePixelShader() - FAILED!");
        }
        else
            return Result::UNSUPPORTED;
    }

    // resources

    m_PipelineLayout = (const PipelineLayoutD3D11*)pipelineDesc.pipelineLayout;

    // input assembly

    m_Topology = GetD3D11TopologyFromTopology(ia.topology, ia.tessControlPointNum);

    if (ia.attributes)
    {
        uint32_t maxBindingSlot = 0;
        for (uint32_t i = 0; i < ia.streamNum; i++)
        {
            const VertexStreamDesc& stream = ia.streams[i];
            if (stream.bindingSlot > maxBindingSlot )
                maxBindingSlot = stream.bindingSlot;
        }
        m_InputAssemplyStrides.resize(maxBindingSlot + 1);

        D3D11_INPUT_ELEMENT_DESC* inputElements = STACK_ALLOC(D3D11_INPUT_ELEMENT_DESC, ia.attributeNum);

        for (uint32_t i = 0; i < ia.attributeNum; i++)
        {
            const VertexAttributeDesc& attrIn = ia.attributes[i];
            const VertexStreamDesc& stream = ia.streams[attrIn.streamIndex];
            D3D11_INPUT_ELEMENT_DESC& attrOut = inputElements[i];
            const FormatInfo& formatInfo = GetFormatInfo(attrIn.format);

            attrOut.SemanticName = attrIn.d3d.semanticName;
            attrOut.SemanticIndex = attrIn.d3d.semanticIndex;
            attrOut.Format = formatInfo.typed;
            attrOut.InputSlot = stream.bindingSlot;
            attrOut.AlignedByteOffset = attrIn.offset;
            attrOut.InstanceDataStepRate = stream.stepRate == VertexStreamStepRate::PER_VERTEX ? 0 : 1;
            attrOut.InputSlotClass = stream.stepRate == VertexStreamStepRate::PER_VERTEX ?
                D3D11_INPUT_PER_VERTEX_DATA : D3D11_INPUT_PER_INSTANCE_DATA;

            m_InputAssemplyStrides[stream.bindingSlot] = stream.stride;
        };

        hr = m_VersionedDevice->CreateInputLayout(&inputElements[0], ia.attributeNum, vertexShader->bytecode, vertexShader->size, &m_InputLayout);
        RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "ID3D11Device::CreateInputLayout() - FAILED!");
    }

    // rasterization

    D3D11_RASTERIZER_DESC2 rasterizerDesc = {};
    rasterizerDesc.FillMode = rs.fillMode == FillMode::SOLID ? D3D11_FILL_SOLID : D3D11_FILL_WIREFRAME;
    rasterizerDesc.CullMode = GetD3D11CullModeFromCullMode(rs.cullMode);
    rasterizerDesc.FrontCounterClockwise = rs.frontCounterClockwise;
    rasterizerDesc.DepthBias = rs.depthBiasConstantFactor;
    rasterizerDesc.DepthBiasClamp = rs.depthBiasClamp;
    rasterizerDesc.SlopeScaledDepthBias = rs.depthBiasSlopeFactor;
    rasterizerDesc.DepthClipEnable = rs.depthClamp;
    rasterizerDesc.ScissorEnable = TRUE;
    rasterizerDesc.MultisampleEnable = rs.sampleNum > 1 ? TRUE : FALSE;
    rasterizerDesc.AntialiasedLineEnable = rs.antialiasedLines;
    rasterizerDesc.ConservativeRaster = rs.conservativeRasterization ?
        D3D11_CONSERVATIVE_RASTERIZATION_MODE_ON : D3D11_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    RasterizerState rasterizerState;
    if (m_VersionedDevice.version >= 3)
    {
        hr = m_VersionedDevice->CreateRasterizerState2(&rasterizerDesc, &rasterizerState.ptr);
        RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "ID3D11Device3::CreateRasterizerState2() - FAILED!");
    }
    else
    {
        hr = m_VersionedDevice->CreateRasterizerState((D3D11_RASTERIZER_DESC*)&rasterizerDesc, (ID3D11RasterizerState**)&rasterizerState.ptr);
        RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "ID3D11Device::CreateRasterizerState() - FAILED!");
    }

    m_RasterizerStateExDesc = Allocate<NvAPI_D3D11_RASTERIZER_DESC_EX>(m_Device.GetStdAllocator());
    memset(m_RasterizerStateExDesc, 0, sizeof(*m_RasterizerStateExDesc));
    memcpy(m_RasterizerStateExDesc, &rasterizerDesc, sizeof(D3D11_RASTERIZER_DESC));
    m_RasterizerStateExDesc->ConservativeRasterEnable = rs.conservativeRasterization;
    m_RasterizerStateExDesc->ProgrammableSamplePositionsEnable = true;
    m_RasterizerStateExDesc->SampleCount = rs.sampleNum;

    m_RasterizerStates.push_back(rasterizerState);
    m_RasterizerState = rasterizerState.ptr;

    // depth-stencil

    const bool isDepthWrite = rs.rasterizerDiscard ? false : ds.write;

    D3D11_DEPTH_STENCIL_DESC depthStencilState = {};
    depthStencilState.DepthEnable = ds.compareFunc == CompareFunc::NONE ? FALSE : TRUE;
    depthStencilState.DepthWriteMask = isDepthWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    depthStencilState.DepthFunc = GetD3D11ComparisonFuncFromCompareFunc(ds.compareFunc);
    depthStencilState.StencilEnable = ss.front.compareFunc == CompareFunc::NONE ? FALSE : TRUE;
    depthStencilState.StencilReadMask = ss.compareMask;
    depthStencilState.StencilWriteMask = ss.writeMask;

    depthStencilState.FrontFace.StencilFailOp = GetD3D11StencilOpFromStencilFunc(ss.front.fail);
    depthStencilState.FrontFace.StencilDepthFailOp = GetD3D11StencilOpFromStencilFunc(ss.front.depthFail);
    depthStencilState.FrontFace.StencilPassOp = GetD3D11StencilOpFromStencilFunc(ss.front.pass);
    depthStencilState.FrontFace.StencilFunc = GetD3D11ComparisonFuncFromCompareFunc(ss.front.compareFunc);

    depthStencilState.BackFace.StencilFailOp = GetD3D11StencilOpFromStencilFunc(ss.front.fail);
    depthStencilState.BackFace.StencilDepthFailOp = GetD3D11StencilOpFromStencilFunc(ss.front.depthFail);
    depthStencilState.BackFace.StencilPassOp = GetD3D11StencilOpFromStencilFunc(ss.front.pass);
    depthStencilState.BackFace.StencilFunc = GetD3D11ComparisonFuncFromCompareFunc(ss.back.compareFunc);

    hr = m_VersionedDevice->CreateDepthStencilState(&depthStencilState, &m_DepthStencilState);
    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "ID3D11Device::CreateDepthStencilState() - FAILED!");

    // output merger

    D3D11_BLEND_DESC1 blendState1 = {};
    blendState1.AlphaToCoverageEnable = rs.alphaToCoverage;
    blendState1.IndependentBlendEnable = TRUE;
    for (uint32_t i = 0; i < om.colorNum; i++)
    {
        const ColorAttachmentDesc& bs = om.color[i];
        const uint8_t colorWriteMask = rs.rasterizerDiscard ? 0 : uint8_t(bs.colorWriteMask);

        blendState1.RenderTarget[i].BlendEnable = bs.blendEnabled;
        blendState1.RenderTarget[i].SrcBlend = GetD3D11BlendFromBlendFactor(bs.colorBlend.srcFactor);
        blendState1.RenderTarget[i].DestBlend = GetD3D11BlendFromBlendFactor(bs.colorBlend.dstFactor);
        blendState1.RenderTarget[i].BlendOp = GetD3D11BlendOpFromBlendFunc(bs.colorBlend.func);
        blendState1.RenderTarget[i].SrcBlendAlpha = GetD3D11BlendFromBlendFactor(bs.alphaBlend.srcFactor);
        blendState1.RenderTarget[i].DestBlendAlpha = GetD3D11BlendFromBlendFactor(bs.alphaBlend.dstFactor);
        blendState1.RenderTarget[i].BlendOpAlpha = GetD3D11BlendOpFromBlendFunc(bs.alphaBlend.func);
        blendState1.RenderTarget[i].RenderTargetWriteMask = colorWriteMask;
        blendState1.RenderTarget[i].LogicOpEnable = om.colorLogicFunc == LogicFunc::NONE ? FALSE : TRUE;
        blendState1.RenderTarget[i].LogicOp = GetD3D11LogicOpFromLogicFunc(om.colorLogicFunc);
    }

    if (m_VersionedDevice.version >= 1)
        hr = m_VersionedDevice->CreateBlendState1(&blendState1, &m_BlendState);
    else
    {
        D3D11_BLEND_DESC blendState = {};
        blendState.AlphaToCoverageEnable = blendState1.AlphaToCoverageEnable;
        blendState.IndependentBlendEnable = blendState1.IndependentBlendEnable;
        for (uint32_t i = 0; i < om.colorNum; i++)
        {
            blendState.RenderTarget[i].BlendEnable = blendState1.RenderTarget[i].BlendEnable;
            blendState.RenderTarget[i].SrcBlend = blendState1.RenderTarget[i].SrcBlend;
            blendState.RenderTarget[i].DestBlend = blendState1.RenderTarget[i].DestBlend;
            blendState.RenderTarget[i].BlendOp = blendState1.RenderTarget[i].BlendOp;
            blendState.RenderTarget[i].SrcBlendAlpha = blendState1.RenderTarget[i].SrcBlendAlpha;
            blendState.RenderTarget[i].DestBlendAlpha = blendState1.RenderTarget[i].DestBlendAlpha;
            blendState.RenderTarget[i].BlendOpAlpha = blendState1.RenderTarget[i].BlendOpAlpha;
            blendState.RenderTarget[i].RenderTargetWriteMask = blendState1.RenderTarget[i].RenderTargetWriteMask;
        }

        hr = m_VersionedDevice->CreateBlendState(&blendState, (ID3D11BlendState**)&m_BlendState);
    }

    RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "ID3D11Device1::CreateBlendState1() - FAILED!");

    m_BlendFactor = om.blendConsts;
    m_SampleMask = rs.sampleMask;
    m_IsRasterizerDiscarded = rs.rasterizerDiscard;

    return Result::SUCCESS;
}

Result PipelineD3D11::Create(const ComputePipelineDesc& pipelineDesc)
{
    HRESULT hr;

    // shaders

    if (pipelineDesc.computeShader.bytecode)
    {
        hr = m_VersionedDevice->CreateComputeShader(pipelineDesc.computeShader.bytecode, pipelineDesc.computeShader.size, nullptr, &m_ComputeShader);

        RETURN_ON_BAD_HRESULT(m_Device.GetLog(), hr, "ID3D11Device::CreateComputeShader() - FAILED!");
    }

    // resources

    m_PipelineLayout = (const PipelineLayoutD3D11*)pipelineDesc.pipelineLayout;

    return Result::SUCCESS;
}

void PipelineD3D11::ChangeSamplePositions(const VersionedContext& context, const SamplePositionsState& samplePositionState, DynamicState mode)
{
    if (IsCompute())
        return;

    size_t i = 0;
    for (; i < m_RasterizerStates.size(); i++)
    {
        if (m_RasterizerStates[i].samplePositionHash == samplePositionState.positionHash)
            break;
    }

    if (i == m_RasterizerStates.size())
    {
        RasterizerState newState = {};
        newState.samplePositionHash = samplePositionState.positionHash;

        m_RasterizerStateExDesc->InterleavedSamplingEnable = samplePositionState.positionNum > m_RasterizerStateExDesc->SampleCount;
        for (uint32_t i = 0; i < samplePositionState.positionNum; i++)
        {
            m_RasterizerStateExDesc->SamplePositionsX[i] = samplePositionState.positions[i].x + 8;
            m_RasterizerStateExDesc->SamplePositionsY[i] = samplePositionState.positions[i].y + 8;
        }

        if (context.ext->isAvailableNVAPI)
        {
            NvAPI_Status result = NvAPI_D3D11_CreateRasterizerState(m_VersionedDevice.ptr, m_RasterizerStateExDesc, (ID3D11RasterizerState**)&newState.ptr);
            if (result != NVAPI_OK)
                REPORT_ERROR(m_Device.GetLog(), "NvAPI_D3D11_CreateRasterizerState() - FAILED!");
        }
        else
            REPORT_ERROR(m_Device.GetLog(), "Programmable Sample Locations feature is only supported on NVIDIA GPUs on DX11! Ignoring...");

        if (!newState.ptr)
            newState.ptr = m_RasterizerState;

        m_RasterizerStates.push_back(newState);
    }

    ID3D11RasterizerState2* newState = m_RasterizerStates[i].ptr;
    if (mode == DynamicState::BIND_AND_SET && m_RasterizerState != newState)
        context->RSSetState(newState);

    m_RasterizerState = newState;
}

void PipelineD3D11::ChangeStencilReference(const VersionedContext& context, uint8_t stencilRef, DynamicState mode)
{
    if (mode == DynamicState::BIND_AND_SET && m_StencilRef != stencilRef)
        context->OMSetDepthStencilState(m_DepthStencilState, stencilRef);

    m_StencilRef = stencilRef;
}

void PipelineD3D11::Bind(const VersionedContext& context, const PipelineD3D11* currentPipeline) const
{
    if (this == currentPipeline)
        return;

    if (IsCompute())
    {
        if (m_ComputeShader != currentPipeline->m_ComputeShader)
            context->CSSetShader(m_ComputeShader, nullptr, 0);
    }
    else
    {
        if (m_Topology != currentPipeline->m_Topology)
            context->IASetPrimitiveTopology(m_Topology);

        if (m_InputLayout != currentPipeline->m_InputLayout)
            context->IASetInputLayout(m_InputLayout);

        if (m_RasterizerState != currentPipeline->m_RasterizerState)
            context->RSSetState(m_RasterizerState);

        if (m_DepthStencilState != currentPipeline->m_DepthStencilState || m_StencilRef != currentPipeline->m_StencilRef)
            context->OMSetDepthStencilState(m_DepthStencilState, m_StencilRef);

        if (m_BlendState != currentPipeline->m_BlendState || m_SampleMask != currentPipeline->m_SampleMask || memcmp(&m_BlendFactor.r, &currentPipeline->m_BlendFactor.r, sizeof(m_BlendFactor)))
            context->OMSetBlendState(m_BlendState, &m_BlendFactor.r, m_SampleMask);

        if (m_VertexShader != currentPipeline->m_VertexShader)
            context->VSSetShader(m_VertexShader, nullptr, 0);

        if (m_TessControlShader != currentPipeline->m_TessControlShader)
            context->HSSetShader(m_TessControlShader, nullptr, 0);

        if (m_TessEvaluationShader != currentPipeline->m_TessEvaluationShader)
            context->DSSetShader(m_TessEvaluationShader, nullptr, 0);

        if (m_GeometryShader != currentPipeline->m_GeometryShader)
            context->GSSetShader(m_GeometryShader, nullptr, 0);

        if (m_FragmentShader != currentPipeline->m_FragmentShader)
            context->PSSetShader(m_FragmentShader, nullptr, 0);

        if (m_IsRasterizerDiscarded)
        {
            // no RASTERIZER_DISCARD support in DX11, below is the simplest emulation
            D3D11_RECT rect = { -1, -1, -1, -1 };
            context->RSSetScissorRects(1, &rect);
        }
    }
}

void PipelineD3D11::SetDebugName(const char* name)
{
    SetName(m_VertexShader, name);
    SetName(m_TessControlShader, name);
    SetName(m_TessEvaluationShader, name);
    SetName(m_GeometryShader, name);
    SetName(m_FragmentShader, name);
    SetName(m_ComputeShader, name);
    SetName(m_InputLayout, name);
    SetName(m_DepthStencilState, name);
    SetName(m_BlendState, name);

    for (size_t i = 0; i < m_RasterizerStates.size(); i++)
        SetName(m_RasterizerStates[i].ptr, name);
}

#include "PipelineD3D11.hpp"
