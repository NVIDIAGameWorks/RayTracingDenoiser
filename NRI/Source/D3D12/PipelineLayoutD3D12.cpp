/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedD3D12.h"
#include "PipelineLayoutD3D12.h"
#include "DeviceD3D12.h"

using namespace nri;

extern D3D12_SHADER_VISIBILITY GetShaderVisibility(ShaderStage shaderStage);
extern D3D12_DESCRIPTOR_RANGE_TYPE GetDescriptorRangesType(DescriptorType descriptorType);
extern D3D12_COMPARISON_FUNC GetComparisonFunc(CompareFunc compareFunc);
extern D3D12_FILTER GetFilterIsotropic(Filter mip, Filter magnification, Filter minification, FilterExt filterExt, bool useComparison);
extern D3D12_FILTER GetFilterAnisotropic(FilterExt filterExt, bool useComparison);
extern D3D12_TEXTURE_ADDRESS_MODE GetAddressMode(AddressMode addressMode);

constexpr D3D12_ROOT_SIGNATURE_FLAGS GetRootSignatureStageFlags(const PipelineLayoutDesc& pipelineLayoutDesc)
{
    D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    if (pipelineLayoutDesc.stageMask & PipelineLayoutShaderStageBits::VERTEX)
        flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    else
        flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;

    if (!(pipelineLayoutDesc.stageMask & PipelineLayoutShaderStageBits::TESS_CONTROL))
        flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;
    if (!(pipelineLayoutDesc.stageMask & PipelineLayoutShaderStageBits::TESS_EVALUATION))
        flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS;
    if (!(pipelineLayoutDesc.stageMask & PipelineLayoutShaderStageBits::GEOMETRY))
        flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
    if (!(pipelineLayoutDesc.stageMask & PipelineLayoutShaderStageBits::FRAGMENT))
        flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

    return flags;
}

PipelineLayoutD3D12::PipelineLayoutD3D12(DeviceD3D12& device) :
    m_DescriptorSetMappings(device.GetStdAllocator()),
    m_DescriptorSetRootMappings(device.GetStdAllocator()),
    m_DynamicConstantBufferMappings(device.GetStdAllocator()),
    m_Device(device)
{
}

Result PipelineLayoutD3D12::Create(const PipelineLayoutDesc& pipelineLayoutDesc)
{
    m_IsGraphicsPipelineLayout = pipelineLayoutDesc.stageMask & PipelineLayoutShaderStageBits::ALL_GRAPHICS;

    uint32_t rangeMax = 0;
    for (uint32_t i = 0; i < pipelineLayoutDesc.descriptorSetNum; i++)
        rangeMax += pipelineLayoutDesc.descriptorSets[i].rangeNum;

    StdAllocator<uint8_t>& allocator = m_Device.GetStdAllocator();

    uint32_t totalRangeNum = 0;
    Vector<D3D12_ROOT_PARAMETER1> rootParameters(allocator);
    Vector<D3D12_DESCRIPTOR_RANGE1> descriptorRanges(rangeMax, allocator);
    Vector<D3D12_STATIC_SAMPLER_DESC> staticSamplerDescs(allocator);

    m_DescriptorSetMappings.resize(pipelineLayoutDesc.descriptorSetNum, DescriptorSetMapping(allocator));
    m_DescriptorSetRootMappings.resize(pipelineLayoutDesc.descriptorSetNum, DescriptorSetRootMapping(allocator));
    m_DynamicConstantBufferMappings.resize(pipelineLayoutDesc.descriptorSetNum);

    for (uint32_t i = 0; i < pipelineLayoutDesc.descriptorSetNum; i++)
    {
        const DescriptorSetDesc& descriptorSetDesc = pipelineLayoutDesc.descriptorSets[i];
        DescriptorSetD3D12::BuildDescriptorSetMapping(descriptorSetDesc, m_DescriptorSetMappings[i]);
        m_DescriptorSetRootMappings[i].rootOffsets.resize(descriptorSetDesc.rangeNum);

        uint32_t heapIndex = 0;
        D3D12_ROOT_PARAMETER1 rootParameter = {};
        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;

        uint32_t groupedRangeNum = 0;
        D3D12_DESCRIPTOR_RANGE_TYPE groupedRangeType = {};
        for (uint32_t j = 0; j < descriptorSetDesc.rangeNum; j++)
        {
            auto& descriptorRangeMapping = m_DescriptorSetMappings[i].descriptorRangeMappings[j];

            D3D12_SHADER_VISIBILITY shaderVisibility = GetShaderVisibility(descriptorSetDesc.ranges[j].visibility);
            D3D12_DESCRIPTOR_RANGE_TYPE rangeType = GetDescriptorRangesType(descriptorSetDesc.ranges[j].descriptorType);

            if (groupedRangeNum &&
                (   rootParameter.ShaderVisibility != shaderVisibility ||
                    groupedRangeType != rangeType ||
                    descriptorRangeMapping.descriptorHeapType != heapIndex)
                )
            {
                rootParameter.DescriptorTable.NumDescriptorRanges = groupedRangeNum;
                rootParameters.push_back(rootParameter);

                totalRangeNum += groupedRangeNum;
                groupedRangeNum = 0;
            }

            groupedRangeType = rangeType;
            heapIndex = (uint32_t)descriptorRangeMapping.descriptorHeapType;
            m_DescriptorSetRootMappings[i].rootOffsets[j] = groupedRangeNum ? ROOT_PARAMETER_UNUSED : (uint16_t)rootParameters.size();

            rootParameter.ShaderVisibility = shaderVisibility;
            rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRanges[totalRangeNum];

            D3D12_DESCRIPTOR_RANGE1& descriptorRange = descriptorRanges[totalRangeNum + groupedRangeNum];
            descriptorRange.RangeType = rangeType;
            descriptorRange.NumDescriptors = descriptorSetDesc.ranges[j].descriptorNum;
            descriptorRange.BaseShaderRegister = descriptorSetDesc.ranges[j].baseRegisterIndex;
            descriptorRange.RegisterSpace = i;
            descriptorRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE; // TODO:
            descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            groupedRangeNum++;
        }

        if (groupedRangeNum)
        {
            rootParameter.DescriptorTable.NumDescriptorRanges = groupedRangeNum;
            rootParameters.push_back(rootParameter);
            totalRangeNum += groupedRangeNum;
        }

        if (descriptorSetDesc.dynamicConstantBufferNum)
        {
            D3D12_ROOT_PARAMETER1 rootParameter = {};
            rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            rootParameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
            m_DynamicConstantBufferMappings[i].constantNum = (uint16_t)descriptorSetDesc.dynamicConstantBufferNum;
            m_DynamicConstantBufferMappings[i].rootOffset = (uint16_t)rootParameters.size();

            for (uint32_t j = 0; j < descriptorSetDesc.dynamicConstantBufferNum; j++)
            {
                rootParameter.Descriptor.ShaderRegister = descriptorSetDesc.dynamicConstantBuffers[j].registerIndex;
                rootParameter.Descriptor.RegisterSpace = i;
                rootParameter.ShaderVisibility = GetShaderVisibility(descriptorSetDesc.dynamicConstantBuffers[j].visibility);
                rootParameters.push_back(rootParameter);
            }
        }
        else
        {
            m_DynamicConstantBufferMappings[i].constantNum = 0;
            m_DynamicConstantBufferMappings[i].rootOffset = 0;
        }

        if (descriptorSetDesc.staticSamplerNum)
        {
            for (uint32_t j = 0; j < descriptorSetDesc.staticSamplerNum; j++)
            {
                const SamplerDesc& samplerDesc = descriptorSetDesc.staticSamplers[j].samplerDesc;
                bool useAnisotropy = samplerDesc.anisotropy > 1 ? true : false;
                bool useComparison = samplerDesc.compareFunc != CompareFunc::NONE;

                D3D12_STATIC_SAMPLER_DESC desc = {};
                desc.Filter = useAnisotropy ?
                    GetFilterAnisotropic(samplerDesc.filterExt, useComparison) :
                    GetFilterIsotropic(samplerDesc.mip, samplerDesc.magnification, samplerDesc.minification, samplerDesc.filterExt, useComparison);
                desc.AddressU = GetAddressMode(samplerDesc.addressModes.u);
                desc.AddressV = GetAddressMode(samplerDesc.addressModes.v);
                desc.AddressW = GetAddressMode(samplerDesc.addressModes.w);
                desc.MipLODBias = samplerDesc.mipBias;
                desc.MaxAnisotropy = samplerDesc.anisotropy;
                desc.ComparisonFunc = GetComparisonFunc(samplerDesc.compareFunc);
                desc.MinLOD = samplerDesc.mipMin;
                desc.MaxLOD = samplerDesc.mipMax;
                desc.ShaderRegister = descriptorSetDesc.staticSamplers[j].registerIndex;
                desc.RegisterSpace = i;
                desc.ShaderVisibility = GetShaderVisibility(descriptorSetDesc.staticSamplers[j].visibility);

                if (samplerDesc.borderColor == BorderColor::FLOAT_TRANSPARENT_BLACK || samplerDesc.borderColor == BorderColor::INT_TRANSPARENT_BLACK)
                    desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
                else if (samplerDesc.borderColor == BorderColor::FLOAT_OPAQUE_BLACK || samplerDesc.borderColor == BorderColor::INT_OPAQUE_BLACK)
                    desc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
                else if (samplerDesc.borderColor == BorderColor::FLOAT_OPAQUE_WHITE || samplerDesc.borderColor == BorderColor::INT_OPAQUE_WHITE)
                    desc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;

                staticSamplerDescs.push_back(desc);
            }
        }
    }

    if (pipelineLayoutDesc.pushConstantNum)
    {
        m_PushConstantsBaseIndex = (uint32_t)rootParameters.size();

        D3D12_ROOT_PARAMETER1 rootParameter = {};
        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        for (uint32_t i = 0; i < pipelineLayoutDesc.pushConstantNum; i++)
        {
            rootParameter.ShaderVisibility = GetShaderVisibility(pipelineLayoutDesc.pushConstants[i].visibility);
            rootParameter.Constants.ShaderRegister = pipelineLayoutDesc.pushConstants[i].registerIndex;
            rootParameter.Constants.RegisterSpace = 0;
            rootParameter.Constants.Num32BitValues = pipelineLayoutDesc.pushConstants[i].size / 4;
            rootParameters.push_back(rootParameter);
        }
    }

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSignatureDesc.Desc_1_1.NumParameters = (UINT)rootParameters.size();
    rootSignatureDesc.Desc_1_1.pParameters = rootParameters.empty() ? nullptr : &rootParameters[0];
    rootSignatureDesc.Desc_1_1.NumStaticSamplers = (UINT)staticSamplerDescs.size();
    rootSignatureDesc.Desc_1_1.pStaticSamplers = staticSamplerDescs.empty() ? nullptr : &staticSamplerDescs[0];
    rootSignatureDesc.Desc_1_1.Flags = GetRootSignatureStageFlags(pipelineLayoutDesc);

    ComPtr<ID3DBlob> rootSignatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &rootSignatureBlob, &errorBlob);
    if (FAILED(hr))
    {
        REPORT_ERROR(m_Device.GetLog(), "D3D12SerializeVersionedRootSignature() failed, error code: 0x%X.", hr);
        return Result::FAILURE;
    }

    hr = ((ID3D12Device*)m_Device)->CreateRootSignature(NRI_TEMP_NODE_MASK, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_RootSignature));
    if (FAILED(hr))
    {
        REPORT_ERROR(m_Device.GetLog(), "ID3D12Device::CreateRootSignature() failed, error code: 0x%X.", hr);
        return Result::FAILURE;
    }

    return Result::SUCCESS;
}

void PipelineLayoutD3D12::SetDebugName(const char* name)
{
    SET_D3D_DEBUG_OBJECT_NAME(m_RootSignature, name);
}

#include "PipelineLayoutD3D12.hpp"
