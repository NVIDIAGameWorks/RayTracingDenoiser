/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedD3D12.h"
#include "PipelineD3D12.h"
#include "DeviceD3D12.h"
#include "DescriptorSetD3D12.h"
#include "PipelineLayoutD3D12.h"

using namespace nri;

extern const uint16_t ROOT_PARAMETER_UNUSED;

extern D3D12_PRIMITIVE_TOPOLOGY_TYPE GetPrimitiveTopologyType(Topology topology);
extern D3D_PRIMITIVE_TOPOLOGY GetPrimitiveTopology(Topology topology, uint8_t tessControlPointNum);
extern D3D12_FILL_MODE GetFillMode(FillMode fillMode);
extern D3D12_CULL_MODE GetCullMode(CullMode cullMode);
extern D3D12_COMPARISON_FUNC GetComparisonFunc(CompareFunc compareFunc);
extern D3D12_STENCIL_OP GetStencilOp(StencilFunc stencilFunc);
extern UINT8 GetRenderTargetWriteMask(ColorWriteBits colorWriteMask);
extern D3D12_LOGIC_OP GetLogicOp(LogicFunc logicFunc);
extern D3D12_BLEND GetBlend(BlendFactor blendFactor);
extern D3D12_BLEND_OP GetBlendOp(BlendFunc blendFunc);
extern DXGI_FORMAT GetFormat(Format format);

Result PipelineD3D12::Create(const GraphicsPipelineDesc& graphicsPipelineDesc)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipleineStateDesc = {};
    graphicsPipleineStateDesc.NodeMask = NRI_TEMP_NODE_MASK;

    m_PipelineLayout = (const PipelineLayoutD3D12*)graphicsPipelineDesc.pipelineLayout;

    graphicsPipleineStateDesc.pRootSignature = *m_PipelineLayout;

    graphicsPipleineStateDesc.PrimitiveTopologyType = GetPrimitiveTopologyType(graphicsPipelineDesc.inputAssembly->topology);
    m_PrimitiveTopology = ::GetPrimitiveTopology(graphicsPipelineDesc.inputAssembly->topology, graphicsPipelineDesc.inputAssembly->tessControlPointNum);
    graphicsPipleineStateDesc.InputLayout.pInputElementDescs = STACK_ALLOC(D3D12_INPUT_ELEMENT_DESC, graphicsPipelineDesc.inputAssembly->attributeNum);

    static_assert( (uint32_t)PrimitiveRestart::DISABLED == D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED, "Enum mismatch." );
    static_assert( (uint32_t)PrimitiveRestart::INDICES_UINT16 == D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF, "Enum mismatch." );
    static_assert( (uint32_t)PrimitiveRestart::INDICES_UINT32 == D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF, "Enum mismatch." );
    graphicsPipleineStateDesc.IBStripCutValue = (D3D12_INDEX_BUFFER_STRIP_CUT_VALUE)graphicsPipelineDesc.inputAssembly->primitiveRestart;

    FillInputLayout(graphicsPipleineStateDesc.InputLayout, graphicsPipelineDesc);

    for (uint32_t i = 0; i < graphicsPipelineDesc.shaderStageNum; i++)
    {
        const ShaderDesc& shader = graphicsPipelineDesc.shaderStages[i];
        if (shader.stage == ShaderStage::VERTEX)
            FillShaderBytecode(graphicsPipleineStateDesc.VS, shader);
        else if (shader.stage == ShaderStage::TESS_CONTROL)
            FillShaderBytecode(graphicsPipleineStateDesc.HS, shader);
        else if (shader.stage == ShaderStage::TESS_EVALUATION)
            FillShaderBytecode(graphicsPipleineStateDesc.DS, shader);
        else if (shader.stage == ShaderStage::GEOMETRY)
            FillShaderBytecode(graphicsPipleineStateDesc.GS, shader);
        else if (shader.stage == ShaderStage::FRAGMENT)
            FillShaderBytecode(graphicsPipleineStateDesc.PS, shader);
    }

    FillRasterizerState(graphicsPipleineStateDesc, graphicsPipelineDesc);

    FillOutputMergerState(graphicsPipleineStateDesc, graphicsPipelineDesc);

    HRESULT hr = ((ID3D12Device*)m_Device)->CreateGraphicsPipelineState(&graphicsPipleineStateDesc, IID_PPV_ARGS(&m_PipelineState));
    if (FAILED(hr))
    {
        REPORT_ERROR(m_Device.GetLog(), "ID3D12Device()::CreateGraphicsPipelineState failed, error code: 0x%X.", hr);
        return Result::FAILURE;
    }

    m_IsGraphicsPipeline = true;

    return Result::SUCCESS;
}

Result PipelineD3D12::Create(const ComputePipelineDesc& computePipelineDesc)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC computePipleineStateDesc = {};
    computePipleineStateDesc.NodeMask = NRI_TEMP_NODE_MASK;

    m_PipelineLayout = (const PipelineLayoutD3D12*)computePipelineDesc.pipelineLayout;

    computePipleineStateDesc.pRootSignature = *m_PipelineLayout;

    FillShaderBytecode(computePipleineStateDesc.CS, computePipelineDesc.computeShader);

    HRESULT hr = ((ID3D12Device*)m_Device)->CreateComputePipelineState(&computePipleineStateDesc, IID_PPV_ARGS(&m_PipelineState));
    if (FAILED(hr))
    {
        REPORT_ERROR(m_Device.GetLog(), "ID3D12Device()::CreateComputePipelineState failed, error code: 0x%X.", hr);
        return Result::FAILURE;
    }

    return Result::SUCCESS;
}

Result PipelineD3D12::Create(const RayTracingPipelineDesc& rayTracingPipelineDesc)
{
#ifdef __ID3D12Device5_INTERFACE_DEFINED__
    ID3D12Device5* device5 = m_Device;
    if (!device5)
        return Result::FAILURE;

    m_PipelineLayout = (const PipelineLayoutD3D12*)rayTracingPipelineDesc.pipelineLayout;

    ID3D12RootSignature* rootSignature = *m_PipelineLayout;

    uint32_t stateSubobjectNum = 0;
    uint32_t shaderNum = rayTracingPipelineDesc.shaderLibrary ? rayTracingPipelineDesc.shaderLibrary->shaderNum : 0;
    Vector<D3D12_STATE_SUBOBJECT> stateSubobjects(1 /*pipeline config*/ + 1 /*shader config*/ + 1 /*node mask*/ + shaderNum /*DXIL libraries*/ +
        rayTracingPipelineDesc.shaderGroupDescNum + (rootSignature ? 1u : 0u),
        m_Device.GetStdAllocator());

    D3D12_RAYTRACING_PIPELINE_CONFIG rayTracingPipelineConfig = {};
    {
        rayTracingPipelineConfig.MaxTraceRecursionDepth = rayTracingPipelineDesc.recursionDepthMax;

        stateSubobjects[stateSubobjectNum].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
        stateSubobjects[stateSubobjectNum].pDesc = &rayTracingPipelineConfig;
        stateSubobjectNum++;
    }

    D3D12_RAYTRACING_SHADER_CONFIG rayTracingShaderConfig = {};
    {
        rayTracingShaderConfig.MaxPayloadSizeInBytes = rayTracingPipelineDesc.payloadAttributeSizeMax;
        rayTracingShaderConfig.MaxAttributeSizeInBytes = rayTracingPipelineDesc.intersectionAttributeSizeMax;

        stateSubobjects[stateSubobjectNum].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        stateSubobjects[stateSubobjectNum].pDesc = &rayTracingShaderConfig;
        stateSubobjectNum++;
    }

    D3D12_NODE_MASK nodeMask = {};
    {
        nodeMask.NodeMask = NRI_TEMP_NODE_MASK;

        stateSubobjects[stateSubobjectNum].Type = D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK;
        stateSubobjects[stateSubobjectNum].pDesc = &nodeMask;
        stateSubobjectNum++;
    }

    D3D12_GLOBAL_ROOT_SIGNATURE globalRootSignature = {};
    if (rootSignature)
    {
        globalRootSignature.pGlobalRootSignature = rootSignature;

        stateSubobjects[stateSubobjectNum].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
        stateSubobjects[stateSubobjectNum].pDesc = &globalRootSignature;
        stateSubobjectNum++;
    }

    Vector<D3D12_DXIL_LIBRARY_DESC> libraryDescs(rayTracingPipelineDesc.shaderLibrary->shaderNum, m_Device.GetStdAllocator());
    for (uint32_t i = 0; i < rayTracingPipelineDesc.shaderLibrary->shaderNum; i++)
    {
        libraryDescs[i].DXILLibrary.pShaderBytecode = rayTracingPipelineDesc.shaderLibrary->shaderDescs[i].bytecode;
        libraryDescs[i].DXILLibrary.BytecodeLength = rayTracingPipelineDesc.shaderLibrary->shaderDescs[i].size;
        libraryDescs[i].NumExports = 0;
        libraryDescs[i].pExports = nullptr;

        stateSubobjects[stateSubobjectNum].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        stateSubobjects[stateSubobjectNum].pDesc = &libraryDescs[i];
        stateSubobjectNum++;
    }

    Vector<std::wstring> wEntryPointNames(rayTracingPipelineDesc.shaderLibrary->shaderNum, m_Device.GetStdAllocator());
    for (uint32_t i = 0; i < rayTracingPipelineDesc.shaderLibrary->shaderNum; i++)
    {
        const ShaderDesc& shader = rayTracingPipelineDesc.shaderLibrary->shaderDescs[i];
        const size_t entryPointNameLength = shader.entryPointName != nullptr ? strlen(shader.entryPointName) + 1 : 0;
        wEntryPointNames[i].resize(entryPointNameLength);
        ConvertCharToWchar(shader.entryPointName, (wchar_t*)wEntryPointNames[i].data(), entryPointNameLength);
    }

    uint32_t hitGroupNum = 0;
    Vector<D3D12_HIT_GROUP_DESC> hitGroups(rayTracingPipelineDesc.shaderGroupDescNum, m_Device.GetStdAllocator());
    m_ShaderGroupNames.reserve(rayTracingPipelineDesc.shaderGroupDescNum);
    for (uint32_t i = 0; i < rayTracingPipelineDesc.shaderGroupDescNum; i++)
    {
        bool isHitGroup = true;
        bool hasIntersectionShader = false;
        std::wstring shaderIndentifierName;
        for (uint32_t j = 0; j < GetCountOf(rayTracingPipelineDesc.shaderGroupDescs[i].shaderIndices); j++)
        {
            const uint32_t& shaderIndex = rayTracingPipelineDesc.shaderGroupDescs[i].shaderIndices[j];
            if (shaderIndex)
            {
                uint32_t lookupIndex = shaderIndex - 1;
                const ShaderDesc& shader = rayTracingPipelineDesc.shaderLibrary->shaderDescs[lookupIndex];
                const std::wstring& entryPointName = wEntryPointNames[lookupIndex];
                if (shader.stage == ShaderStage::RAYGEN || shader.stage == ShaderStage::MISS || shader.stage == ShaderStage::CALLABLE)
                {
                    shaderIndentifierName = entryPointName;
                    isHitGroup = false;
                    break;
                }

                switch (shader.stage)
                {
                case ShaderStage::INTERSECTION:
                    hitGroups[hitGroupNum].IntersectionShaderImport = entryPointName.c_str();
                    hasIntersectionShader = true;
                    break;
                case ShaderStage::CLOSEST_HIT:
                    hitGroups[hitGroupNum].ClosestHitShaderImport = entryPointName.c_str();
                    break;
                case ShaderStage::ANY_HIT:
                    hitGroups[hitGroupNum].AnyHitShaderImport = entryPointName.c_str();
                    break;
                }

                shaderIndentifierName = i;
            }
        }

        m_ShaderGroupNames.push_back(shaderIndentifierName);

        if (isHitGroup)
        {
            hitGroups[hitGroupNum].HitGroupExport = m_ShaderGroupNames[i].c_str();
            hitGroups[hitGroupNum].Type = hasIntersectionShader ? D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE : D3D12_HIT_GROUP_TYPE_TRIANGLES;

            stateSubobjects[stateSubobjectNum].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
            stateSubobjects[stateSubobjectNum].pDesc = &hitGroups[hitGroupNum++];
            stateSubobjectNum++;
        }
    }

    D3D12_STATE_OBJECT_DESC stateObjectDesc = {};
    stateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    stateObjectDesc.NumSubobjects = stateSubobjectNum;
    stateObjectDesc.pSubobjects = stateSubobjectNum ? &stateSubobjects[0] : nullptr;

    HRESULT hr = device5->CreateStateObject(&stateObjectDesc, IID_PPV_ARGS(&m_StateObject));
    if (FAILED(hr))
    {
        REPORT_ERROR(m_Device.GetLog(), "ID3D12Device5()::CreateStateObject failed, error code: 0x%X.", hr);
        return Result::FAILURE;
    }

    m_StateObject->QueryInterface(&m_StateObjectProperties);

    return Result::SUCCESS;
#else
    return Result::FAILURE;
#endif
}

Result PipelineD3D12::WriteShaderGroupIdentifiers(uint32_t baseShaderGroupIndex, uint32_t shaderGroupNum, void* buffer) const
{
#ifdef __ID3D12Device5_INTERFACE_DEFINED__
    uint8_t* byteBuffer = (uint8_t*)buffer;
    for (uint32_t i = 0; i < shaderGroupNum; i++)
    {
        memcpy(byteBuffer + i * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, m_StateObjectProperties->GetShaderIdentifier(m_ShaderGroupNames[baseShaderGroupIndex + i].c_str()),
            m_Device.GetDesc().rayTracingShaderGroupIdentifierSize);
    }

    return Result::SUCCESS;
#else
    return Result::FAILURE;
#endif
}

void PipelineD3D12::Bind(ID3D12GraphicsCommandList* graphicsCommandList, D3D12_PRIMITIVE_TOPOLOGY& primitiveTopology) const
{
#ifdef __ID3D12Device5_INTERFACE_DEFINED__
    if (m_StateObject)
        ((ID3D12GraphicsCommandList4*)graphicsCommandList)->SetPipelineState1(m_StateObject);
    else
#endif
    graphicsCommandList->SetPipelineState(m_PipelineState);

    if (m_IsGraphicsPipeline)
    {
        //if (primitiveTopology != m_PrimitiveTopology)
        {
            primitiveTopology = m_PrimitiveTopology;
            graphicsCommandList->IASetPrimitiveTopology(m_PrimitiveTopology);
        }

        if (m_BlendEnabled)
            graphicsCommandList->OMSetBlendFactor(&m_BlendFactor.r);
    }
}

uint32_t PipelineD3D12::GetIAStreamStride(uint32_t streamSlot) const
{
    return m_IAStreamStride[streamSlot];
}

uint8_t PipelineD3D12::GetSampleNum() const
{
    return m_SampleNum;
}

void PipelineD3D12::FillInputLayout(D3D12_INPUT_LAYOUT_DESC& inputLayoutDesc, const GraphicsPipelineDesc& graphicsPipelineDesc)
{
    uint8_t attributeNum = graphicsPipelineDesc.inputAssembly->attributeNum;
    if (!attributeNum)
        return;

    for (uint32_t i = 0; i < graphicsPipelineDesc.inputAssembly->streamNum; i++)
        m_IAStreamStride[graphicsPipelineDesc.inputAssembly->streams[i].bindingSlot] = graphicsPipelineDesc.inputAssembly->streams[i].stride;

    inputLayoutDesc.NumElements = attributeNum;
    D3D12_INPUT_ELEMENT_DESC* inputElementsDescs = (D3D12_INPUT_ELEMENT_DESC*)inputLayoutDesc.pInputElementDescs;

    for (uint32_t i = 0; i < attributeNum; i++)
    {
        const VertexAttributeDesc& vertexAttributeDesc = graphicsPipelineDesc.inputAssembly->attributes[i];
        const VertexStreamDesc& vertexStreamDesc = graphicsPipelineDesc.inputAssembly->streams[vertexAttributeDesc.streamIndex];
        bool isPerVertexData = vertexStreamDesc.stepRate == VertexStreamStepRate::PER_VERTEX;

        inputElementsDescs[i].SemanticName = vertexAttributeDesc.d3d.semanticName;
        inputElementsDescs[i].SemanticIndex = vertexAttributeDesc.d3d.semanticIndex;
        inputElementsDescs[i].Format = GetFormat(vertexAttributeDesc.format);
        inputElementsDescs[i].InputSlot = vertexStreamDesc.bindingSlot;
        inputElementsDescs[i].AlignedByteOffset = vertexAttributeDesc.offset;
        inputElementsDescs[i].InputSlotClass = isPerVertexData ? D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA : D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
        inputElementsDescs[i].InstanceDataStepRate = isPerVertexData ? 0 : 1;
    }
}

void PipelineD3D12::FillShaderBytecode(D3D12_SHADER_BYTECODE& shaderBytecode, const ShaderDesc& shaderDesc) const
{
    shaderBytecode.pShaderBytecode = shaderDesc.bytecode;
    shaderBytecode.BytecodeLength = shaderDesc.size;
}

void PipelineD3D12::FillRasterizerState(D3D12_GRAPHICS_PIPELINE_STATE_DESC& graphicsPipleineStateDesc, const GraphicsPipelineDesc& graphicsPipelineDesc)
{
    if (!graphicsPipelineDesc.rasterization)
        return;

    D3D12_RASTERIZER_DESC& rasterizerDesc = graphicsPipleineStateDesc.RasterizerState;
    const RasterizationDesc& rasterizationDesc = *graphicsPipelineDesc.rasterization;

    bool useMultisampling = rasterizationDesc.sampleNum > 1 ? true : false;
    rasterizerDesc.FillMode = GetFillMode(rasterizationDesc.fillMode);
    rasterizerDesc.CullMode = GetCullMode(rasterizationDesc.cullMode);
    rasterizerDesc.FrontCounterClockwise = (BOOL)rasterizationDesc.frontCounterClockwise;
    rasterizerDesc.DepthBias = rasterizationDesc.depthBiasConstantFactor;
    rasterizerDesc.DepthBiasClamp = rasterizationDesc.depthBiasClamp;
    rasterizerDesc.SlopeScaledDepthBias = rasterizationDesc.depthBiasSlopeFactor;
    rasterizerDesc.DepthClipEnable = (BOOL)rasterizationDesc.depthClamp;
    rasterizerDesc.MultisampleEnable = useMultisampling;
    rasterizerDesc.AntialiasedLineEnable = (BOOL)rasterizationDesc.antialiasedLines;
    rasterizerDesc.ForcedSampleCount = useMultisampling ? rasterizationDesc.sampleNum : 0;
    rasterizerDesc.ConservativeRaster = rasterizationDesc.conservativeRasterization ? D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON : D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    graphicsPipleineStateDesc.SampleMask = graphicsPipelineDesc.rasterization->sampleMask;
    graphicsPipleineStateDesc.SampleDesc.Count = rasterizationDesc.sampleNum;
    graphicsPipleineStateDesc.SampleDesc.Quality = 0;

    m_SampleNum = rasterizationDesc.sampleNum;
}

void PipelineD3D12::FillDepthStencilState(D3D12_DEPTH_STENCIL_DESC& depthStencilDesc, const OutputMergerDesc& outputMergerDesc) const
{
    depthStencilDesc.DepthEnable = outputMergerDesc.depth.compareFunc == CompareFunc::NONE ? FALSE : TRUE;
    depthStencilDesc.DepthWriteMask = outputMergerDesc.depth.write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencilDesc.DepthFunc = GetComparisonFunc(outputMergerDesc.depth.compareFunc);
    depthStencilDesc.StencilEnable = (outputMergerDesc.stencil.front.compareFunc == CompareFunc::NONE && outputMergerDesc.stencil.back.compareFunc == CompareFunc::NONE) ? FALSE : TRUE;
    depthStencilDesc.StencilReadMask = (UINT8)outputMergerDesc.stencil.compareMask;
    depthStencilDesc.StencilWriteMask = (UINT8)outputMergerDesc.stencil.writeMask;
    depthStencilDesc.FrontFace.StencilFailOp = GetStencilOp(outputMergerDesc.stencil.front.fail);
    depthStencilDesc.FrontFace.StencilDepthFailOp = GetStencilOp(outputMergerDesc.stencil.front.depthFail);
    depthStencilDesc.FrontFace.StencilPassOp = GetStencilOp(outputMergerDesc.stencil.front.pass);
    depthStencilDesc.FrontFace.StencilFunc = GetComparisonFunc(outputMergerDesc.stencil.front.compareFunc);
    depthStencilDesc.BackFace.StencilFailOp = GetStencilOp(outputMergerDesc.stencil.back.fail);
    depthStencilDesc.BackFace.StencilDepthFailOp = GetStencilOp(outputMergerDesc.stencil.back.depthFail);
    depthStencilDesc.BackFace.StencilPassOp = GetStencilOp(outputMergerDesc.stencil.back.pass);
    depthStencilDesc.BackFace.StencilFunc = GetComparisonFunc(outputMergerDesc.stencil.back.compareFunc);
}

void PipelineD3D12::FillOutputMergerState(D3D12_GRAPHICS_PIPELINE_STATE_DESC& graphicsPipleineStateDesc, const GraphicsPipelineDesc& graphicsPipelineDesc)
{
    if (!graphicsPipelineDesc.outputMerger)
        return;

    FillDepthStencilState(graphicsPipleineStateDesc.DepthStencilState, *graphicsPipelineDesc.outputMerger);
    graphicsPipleineStateDesc.DSVFormat = GetFormat(graphicsPipelineDesc.outputMerger->depthStencilFormat);

    if (!graphicsPipelineDesc.outputMerger->colorNum)
        return;

    graphicsPipleineStateDesc.NumRenderTargets = graphicsPipelineDesc.outputMerger->colorNum;
    for (uint32_t i = 0; i < graphicsPipelineDesc.outputMerger->colorNum; i++)
        graphicsPipleineStateDesc.RTVFormats[i] = GetFormat(graphicsPipelineDesc.outputMerger->color[i].format);

    D3D12_BLEND_DESC& blendDesc = graphicsPipleineStateDesc.BlendState;
    blendDesc.AlphaToCoverageEnable = graphicsPipelineDesc.rasterization->alphaToCoverage;
    blendDesc.IndependentBlendEnable = TRUE;

    for (uint32_t i = 0; i < graphicsPipelineDesc.outputMerger->colorNum; i++)
    {
        const ColorAttachmentDesc& colorAttachmentDesc = graphicsPipelineDesc.outputMerger->color[i];
        m_BlendEnabled |= colorAttachmentDesc.blendEnabled;

        blendDesc.RenderTarget[i].BlendEnable = colorAttachmentDesc.blendEnabled;
        blendDesc.RenderTarget[i].RenderTargetWriteMask = GetRenderTargetWriteMask(colorAttachmentDesc.colorWriteMask);
        if (colorAttachmentDesc.blendEnabled)
        {
            blendDesc.RenderTarget[i].LogicOp = GetLogicOp(graphicsPipelineDesc.outputMerger->colorLogicFunc);
            blendDesc.RenderTarget[i].LogicOpEnable = blendDesc.RenderTarget[i].LogicOp == D3D12_LOGIC_OP_NOOP ? FALSE : TRUE;
            blendDesc.RenderTarget[i].SrcBlend = GetBlend(colorAttachmentDesc.colorBlend.srcFactor);
            blendDesc.RenderTarget[i].DestBlend = GetBlend(colorAttachmentDesc.colorBlend.dstFactor);
            blendDesc.RenderTarget[i].BlendOp = GetBlendOp(colorAttachmentDesc.colorBlend.func);
            blendDesc.RenderTarget[i].SrcBlendAlpha = GetBlend(colorAttachmentDesc.alphaBlend.srcFactor);
            blendDesc.RenderTarget[i].DestBlendAlpha = GetBlend(colorAttachmentDesc.alphaBlend.dstFactor);
            blendDesc.RenderTarget[i].BlendOpAlpha = GetBlendOp(colorAttachmentDesc.alphaBlend.func);
        }
    }

    m_BlendFactor = graphicsPipelineDesc.outputMerger->blendConsts;
}

void PipelineD3D12::SetDebugName(const char* name)
{
    SET_D3D_DEBUG_OBJECT_NAME(m_PipelineState, name);
}

#include "PipelineD3D12.hpp"
