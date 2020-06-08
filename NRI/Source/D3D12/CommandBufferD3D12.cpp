/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedD3D12.h"
#include "CommandBufferD3D12.h"
#include "DeviceD3D12.h"
#include "CommandQueueD3D12.h"
#include "CommandAllocatorD3D12.h"
#include "BufferD3D12.h"
#include "TextureD3D12.h"
#include "DescriptorD3D12.h"
#include "DescriptorSetD3D12.h"
#include "FrameBufferD3D12.h"
#include "PipelineD3D12.h"
#include "PipelineLayoutD3D12.h"
#include "QueryPoolD3D12.h"
#include "AccelerationStructureD3D12.h"
#include <pix.h>

using namespace nri;

extern D3D12_RESOURCE_STATES GetResourceStates(nri::AccessBits accessMask);
extern void ConvertRects(D3D12_RECT* rectsD3D12, const Rect* rects, uint32_t rectNum);
extern DXGI_FORMAT GetFormat(Format format);
#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
extern D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS GetAccelerationStructureBuildFlags(AccelerationStructureBuildBits accelerationStructureBuildFlags);
extern void ConvertGeometryDescs(D3D12_RAYTRACING_GEOMETRY_DESC* geometryDescs, const GeometryObject* geometryObjects, uint32_t geometryObjectNum);
extern D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE GetCopyMode(CopyMode copyMode);
#endif

Result CommandBufferD3D12::Create(D3D12_COMMAND_LIST_TYPE commandListType, ID3D12CommandAllocator* commandAllocator)
{
    ComPtr<ID3D12GraphicsCommandList> graphicsCommandList;
    HRESULT hr = ((ID3D12Device*)m_Device)->CreateCommandList(NRI_TEMP_NODE_MASK, commandListType, commandAllocator, nullptr, IID_PPV_ARGS(&graphicsCommandList));
    if (FAILED(hr))
    {
        REPORT_ERROR(m_Device.GetLog(), "ID3D12Device::CreateCommandList() failed, error code: 0x%X.", hr);
        return Result::FAILURE;
    }

    m_CommandAllocator = commandAllocator;
    m_GraphicsCommandList = graphicsCommandList;
    m_GraphicsCommandList->QueryInterface(IID_PPV_ARGS(&m_GraphicsCommandList1));

#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
    m_GraphicsCommandList->QueryInterface(IID_PPV_ARGS(&m_GraphicsCommandList4));
#endif

    hr = m_GraphicsCommandList->Close();
    if (FAILED(hr))
    {
        REPORT_ERROR(m_Device.GetLog(), "ID3D12GraphicsCommandList::Close() failed, error code: 0x%X.", hr);
        return Result::FAILURE;
    }

    return Result::SUCCESS;
}

Result CommandBufferD3D12::Create(const CommandBufferD3D12Desc& commandBufferDesc)
{
    m_CommandAllocator = (ID3D12CommandAllocator*)commandBufferDesc.d3d12CommandAllocator;
    m_GraphicsCommandList = (ID3D12GraphicsCommandList*)commandBufferDesc.d3d12CommandList;
    m_GraphicsCommandList->QueryInterface(IID_PPV_ARGS(&m_GraphicsCommandList1));

#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
    m_GraphicsCommandList->QueryInterface(IID_PPV_ARGS(&m_GraphicsCommandList4));
#endif

    return Result::SUCCESS;
}

inline void CommandBufferD3D12::SetDebugName(const char* name)
{
    SET_D3D_DEBUG_OBJECT_NAME(m_GraphicsCommandList, name);
}

inline Result CommandBufferD3D12::Begin(const DescriptorPool* descriptorPool)
{
    HRESULT hr = m_GraphicsCommandList->Reset(m_CommandAllocator, nullptr);
    if (FAILED(hr))
    {
        REPORT_ERROR(m_Device.GetLog(), "ID3D12GraphicsCommandList::Reset() failed, return code %d.", hr);
        return Result::FAILURE;
    }

    if (descriptorPool)
        SetDescriptorPool(*descriptorPool);

    m_PipelineLayout = nullptr;
    m_IsGraphicsPipelineLayout = false;
    m_Pipeline = nullptr;
    m_FrameBuffer = nullptr;
    m_PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

    return Result::SUCCESS;
}

inline Result CommandBufferD3D12::End()
{
    if (FAILED(m_GraphicsCommandList->Close()))
        return Result::FAILURE;

    return Result::SUCCESS;
}

inline void CommandBufferD3D12::SetViewports(const Viewport* viewports, uint32_t viewportNum)
{
    static_assert(offsetof(Viewport, offset) == 0, "Unsupported viewport data layout.");
    static_assert(offsetof(Viewport, size) == 8, "Unsupported viewport data layout.");
    static_assert(offsetof(Viewport, depthRangeMin) == 16, "Unsupported viewport data layout.");
    static_assert(offsetof(Viewport, depthRangeMax) == 20, "Unsupported viewport data layout.");

    m_GraphicsCommandList->RSSetViewports(viewportNum, (D3D12_VIEWPORT*)viewports);
}

inline void CommandBufferD3D12::SetScissors(const Rect* rects, uint32_t rectNum)
{
    D3D12_RECT* rectsD3D12 = STACK_ALLOC(D3D12_RECT, rectNum);
    ConvertRects(rectsD3D12, rects, rectNum);

    m_GraphicsCommandList->RSSetScissorRects(rectNum, rectsD3D12);
}

inline void CommandBufferD3D12::SetDepthBounds(float boundsMin, float boundsMax)
{
    if (m_GraphicsCommandList1)
        m_GraphicsCommandList1->OMSetDepthBounds(boundsMin, boundsMax);
}

inline void CommandBufferD3D12::SetStencilReference(uint8_t reference)
{
    m_GraphicsCommandList->OMSetStencilRef(reference);
}

inline void CommandBufferD3D12::SetSamplePositions(const SamplePosition* positions, uint32_t positionNum)
{
    if (m_GraphicsCommandList1)
    {
        uint8_t sampleNum = m_Pipeline->GetSampleNum();
        uint32_t pixelNum = positionNum / sampleNum;

        m_GraphicsCommandList1->SetSamplePositions(sampleNum, pixelNum, (D3D12_SAMPLE_POSITION*)positions);
    }
}

inline void CommandBufferD3D12::ClearAttachments(const ClearDesc* clearDescs, uint32_t clearDescNum, const Rect* rects, uint32_t rectNum)
{
    m_FrameBuffer->Clear(m_GraphicsCommandList, clearDescs, clearDescNum, rects, rectNum);
}

inline void CommandBufferD3D12::ClearStorageBuffer(const ClearStorageBufferDesc& clearDesc)
{
    DescriptorSetD3D12* descriptorSet = m_DescriptorSets[clearDesc.setIndex];
    DescriptorD3D12* resourceView = (DescriptorD3D12*)clearDesc.storageBuffer;
    const UINT clearValues[4] = { clearDesc.value, clearDesc.value , clearDesc.value, clearDesc.value };

    m_GraphicsCommandList->ClearUnorderedAccessViewUint(
    { descriptorSet->GetPointerGPU(clearDesc.rangeIndex, clearDesc.offsetInRange) },
    { resourceView->GetPointerCPU() },
        *resourceView,
        clearValues,
        0,
        nullptr);
}

inline void CommandBufferD3D12::ClearStorageTexture(const ClearStorageTextureDesc& clearDesc)
{
    DescriptorSetD3D12* descriptorSet = m_DescriptorSets[clearDesc.setIndex];
    DescriptorD3D12* resourceView = (DescriptorD3D12*)clearDesc.storageTexture;

    if (resourceView->IsFloatingPointUAV())
    {
        m_GraphicsCommandList->ClearUnorderedAccessViewFloat(
        { descriptorSet->GetPointerGPU(clearDesc.rangeIndex, clearDesc.offsetInRange) },
        { resourceView->GetPointerCPU() },
            *resourceView,
            &clearDesc.value.rgba32f.r,
            0,
            nullptr);
    }
    else
    {
        m_GraphicsCommandList->ClearUnorderedAccessViewUint(
        { descriptorSet->GetPointerGPU(clearDesc.rangeIndex, clearDesc.offsetInRange) },
        { resourceView->GetPointerCPU() },
            *resourceView,
            &clearDesc.value.rgba32ui.r,
            0,
            nullptr);
    }
}

inline void CommandBufferD3D12::BeginRenderPass(const FrameBuffer& frameBuffer, FramebufferBindFlag bindFlag)
{
    m_FrameBuffer = (FrameBufferD3D12*)&frameBuffer;
    m_FrameBuffer->Bind(m_GraphicsCommandList, bindFlag);
}

inline void CommandBufferD3D12::EndRenderPass()
{
    m_FrameBuffer = nullptr;
}

inline void CommandBufferD3D12::SetVertexBuffers(uint32_t baseSlot, uint32_t bufferNum, const Buffer* const* buffers, const uint64_t* offsets)
{
    D3D12_VERTEX_BUFFER_VIEW* vertexBufferViews = STACK_ALLOC(D3D12_VERTEX_BUFFER_VIEW, bufferNum);

    for (uint32_t i = 0; i < bufferNum; i++)
    {
        if (buffers[i] != nullptr)
        {
            const BufferD3D12* buffer = (BufferD3D12*)buffers[i];
            uint64_t offset = offsets ? offsets[i] : 0;
            vertexBufferViews[i].BufferLocation = buffer->GetPointerGPU() + offset;
            vertexBufferViews[i].SizeInBytes = (UINT)(buffer->GetByteSize() - offset);
            vertexBufferViews[i].StrideInBytes = m_Pipeline->GetIAStreamStride(baseSlot + i);
        }
        else
        {
            vertexBufferViews[i].BufferLocation = 0;
            vertexBufferViews[i].SizeInBytes = 0;
            vertexBufferViews[i].StrideInBytes = 0;
        }
    }

    m_GraphicsCommandList->IASetVertexBuffers(baseSlot, bufferNum, vertexBufferViews);
}

inline void CommandBufferD3D12::SetIndexBuffer(const Buffer& buffer, uint64_t offset, IndexType indexType)
{
    const BufferD3D12& bufferD3D12 = (BufferD3D12&)buffer;

    D3D12_INDEX_BUFFER_VIEW indexBufferView;
    indexBufferView.BufferLocation = bufferD3D12.GetPointerGPU() + offset;
    indexBufferView.SizeInBytes = (UINT)(bufferD3D12.GetByteSize() - offset);
    indexBufferView.Format = indexType == IndexType::UINT16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

    m_GraphicsCommandList->IASetIndexBuffer(&indexBufferView);
}

inline void CommandBufferD3D12::SetPipelineLayout(const PipelineLayout& pipelineLayout)
{
    const PipelineLayoutD3D12& pipelineLayoutD3D12 = (const PipelineLayoutD3D12&)pipelineLayout;

    m_PipelineLayout = &pipelineLayoutD3D12;
    m_IsGraphicsPipelineLayout = pipelineLayoutD3D12.IsGraphicsPipelineLayout();

    if (m_IsGraphicsPipelineLayout)
        m_GraphicsCommandList->SetGraphicsRootSignature(pipelineLayoutD3D12);
    else
        m_GraphicsCommandList->SetComputeRootSignature(pipelineLayoutD3D12);
}

inline void CommandBufferD3D12::SetPipeline(const Pipeline& pipeline)
{
    PipelineD3D12* pipelineD3D12 = (PipelineD3D12*)&pipeline;
    //if (m_Pipeline == pipelineD3D12)
        //return;

    pipelineD3D12->Bind(m_GraphicsCommandList, m_PrimitiveTopology);

    m_Pipeline = pipelineD3D12;
}

inline void CommandBufferD3D12::SetDescriptorPool(const DescriptorPool& descriptorPool)
{
    ((DescriptorPoolD3D12&)descriptorPool).Bind(m_GraphicsCommandList);
}

inline void CommandBufferD3D12::SetDescriptorSets(uint32_t baseIndex, uint32_t setNum, const DescriptorSet* const* descriptorSets, const uint32_t* offsets)
{
    m_PipelineLayout->SetDescriptorSets(*m_GraphicsCommandList, m_IsGraphicsPipelineLayout, baseIndex, setNum, descriptorSets, offsets);

    for (uint32_t i = 0; i < setNum; i++)
        m_DescriptorSets[baseIndex + i] = (DescriptorSetD3D12*)descriptorSets[i];
}

inline void CommandBufferD3D12::SetConstants(uint32_t pushConstantRangeIndex, const void* data, uint32_t size)
{
    uint32_t rootParameterIndex = m_PipelineLayout->GetPushConstantsRootOffset(pushConstantRangeIndex);
    uint32_t constantNum = size / 4;

    if (m_IsGraphicsPipelineLayout)
        m_GraphicsCommandList->SetGraphicsRoot32BitConstants(rootParameterIndex, constantNum, data, 0);
    else
        m_GraphicsCommandList->SetComputeRoot32BitConstants(rootParameterIndex, constantNum, data, 0);
}

inline void CommandBufferD3D12::Draw(uint32_t vertexNum, uint32_t instanceNum, uint32_t baseVertex, uint32_t baseInstance)
{
    m_GraphicsCommandList->DrawInstanced(vertexNum, instanceNum, baseVertex, baseInstance);
}

inline void CommandBufferD3D12::DrawIndexed(uint32_t indexNum, uint32_t instanceNum, uint32_t baseIndex, uint32_t baseVertex, uint32_t baseInstance)
{
    m_GraphicsCommandList->DrawIndexedInstanced(indexNum, instanceNum, baseIndex, baseVertex, baseInstance);
}

inline void CommandBufferD3D12::DrawIndirect(const Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride)
{
    m_GraphicsCommandList->ExecuteIndirect(m_Device.GetDrawCommandSignature(stride), drawNum, (BufferD3D12&)buffer, offset, nullptr, 0);
}

inline void CommandBufferD3D12::DrawIndexedIndirect(const Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride)
{
    m_GraphicsCommandList->ExecuteIndirect(m_Device.GetDrawIndexedCommandSignature(stride), drawNum, (BufferD3D12&)buffer, offset, nullptr, 0);
}

inline void CommandBufferD3D12::CopyBuffer(Buffer& dstBuffer, uint64_t dstOffset, const Buffer& srcBuffer, uint64_t srcOffset, uint64_t size)
{
    if (size == WHOLE_SIZE)
        size = ((BufferD3D12&)srcBuffer).GetByteSize();

    m_GraphicsCommandList->CopyBufferRegion((BufferD3D12&)dstBuffer, dstOffset, (BufferD3D12&)srcBuffer, srcOffset, size);
}

inline void CommandBufferD3D12::CopyTexture(Texture& dstTexture, const TextureRegionDesc* dstRegion, const Texture& srcTexture, const TextureRegionDesc* srcRegion)
{
    TextureD3D12& dstTextureD3D12 = (TextureD3D12&)dstTexture;
    TextureD3D12& srcTextureD3D12 = (TextureD3D12&)srcTexture;

    if (!dstRegion || !srcRegion)
    {
        m_GraphicsCommandList->CopyResource(dstTextureD3D12, srcTextureD3D12);
    }
    else
    {
        D3D12_TEXTURE_COPY_LOCATION dstTextureCopyLocation = {
            dstTextureD3D12, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, dstTextureD3D12.GetSubresourceIndex(dstRegion->arrayOffset, dstRegion->mipOffset) };

        D3D12_TEXTURE_COPY_LOCATION srcTextureCopyLocation = {
            srcTextureD3D12, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, srcTextureD3D12.GetSubresourceIndex(srcRegion->arrayOffset, srcRegion->mipOffset) };

        const uint16_t(&offset)[3] = srcRegion->offset;
        const uint16_t size[3] = {
            srcRegion->size[0] == WHOLE_SIZE ? srcTextureD3D12.GetSize(0, srcRegion->mipOffset) : srcRegion->size[0],
            srcRegion->size[1] == WHOLE_SIZE ? srcTextureD3D12.GetSize(1, srcRegion->mipOffset) : srcRegion->size[1],
            srcRegion->size[2] == WHOLE_SIZE ? srcTextureD3D12.GetSize(2, srcRegion->mipOffset) : srcRegion->size[2]
        };
        D3D12_BOX box = { offset[0], offset[1], offset[2], uint16_t(offset[0] + size[0]), uint16_t(offset[1] + size[1]), uint16_t(offset[2] + size[2]) };

        const uint16_t(&dstOffset)[3] = dstRegion->offset;

        m_GraphicsCommandList->CopyTextureRegion(&dstTextureCopyLocation, dstOffset[0], dstOffset[1], dstOffset[2],
            &srcTextureCopyLocation, &box);
    }
}

inline void CommandBufferD3D12::UploadBufferToTexture(Texture& dstTexture, const TextureRegionDesc& dstRegionDesc, const Buffer& srcBuffer, const TextureDataLayoutDesc& srcDataLayoutDesc)
{
    TextureD3D12& dstTextureD3D12 = (TextureD3D12&)dstTexture;
    const D3D12_RESOURCE_DESC& textureDesc = dstTextureD3D12.GetTextureDesc();
    D3D12_TEXTURE_COPY_LOCATION dstTextureCopyLocation = {
        dstTextureD3D12, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, dstTextureD3D12.GetSubresourceIndex(dstRegionDesc.arrayOffset, dstRegionDesc.mipOffset) };

    const uint16_t size[3] = {
        dstRegionDesc.size[0] == WHOLE_SIZE ? dstTextureD3D12.GetSize(0, dstRegionDesc.mipOffset) : dstRegionDesc.size[0],
        dstRegionDesc.size[1] == WHOLE_SIZE ? dstTextureD3D12.GetSize(1, dstRegionDesc.mipOffset) : dstRegionDesc.size[1],
        dstRegionDesc.size[2] == WHOLE_SIZE ? dstTextureD3D12.GetSize(2, dstRegionDesc.mipOffset) : dstRegionDesc.size[2]
    };

    D3D12_TEXTURE_COPY_LOCATION srcTextureCopyLocation;
    srcTextureCopyLocation.pResource = (BufferD3D12&)srcBuffer;
    srcTextureCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcTextureCopyLocation.PlacedFootprint.Offset = srcDataLayoutDesc.offset;
    srcTextureCopyLocation.PlacedFootprint.Footprint.Format = textureDesc.Format;
    srcTextureCopyLocation.PlacedFootprint.Footprint.Width = size[0];
    srcTextureCopyLocation.PlacedFootprint.Footprint.Height = size[1];
    srcTextureCopyLocation.PlacedFootprint.Footprint.Depth = size[2];
    srcTextureCopyLocation.PlacedFootprint.Footprint.RowPitch = srcDataLayoutDesc.rowPitch;

    const uint16_t(&offset)[3] = dstRegionDesc.offset;
    D3D12_BOX box = { offset[0], offset[1], offset[2], uint16_t(offset[0] + size[0]), uint16_t(offset[1] + size[1]), uint16_t(offset[2] + size[2]) };

    m_GraphicsCommandList->CopyTextureRegion(&dstTextureCopyLocation, offset[0], offset[1], offset[2], &srcTextureCopyLocation, &box);
}

inline void CommandBufferD3D12::ReadbackTextureToBuffer(Buffer& dstBuffer, TextureDataLayoutDesc& dstDataLayoutDesc, const Texture& srcTexture, const TextureRegionDesc& srcRegionDesc)
{
    TextureD3D12& srcTextureD3D12 = (TextureD3D12&)srcTexture;
    const D3D12_RESOURCE_DESC& textureDesc = srcTextureD3D12.GetTextureDesc();
    D3D12_TEXTURE_COPY_LOCATION srcTextureCopyLocation = {
        srcTextureD3D12, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, srcTextureD3D12.GetSubresourceIndex(srcRegionDesc.arrayOffset, srcRegionDesc.mipOffset) };

    D3D12_TEXTURE_COPY_LOCATION dstTextureCopyLocation;
    dstTextureCopyLocation.pResource = (BufferD3D12&)dstBuffer;
    dstTextureCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstTextureCopyLocation.PlacedFootprint.Offset = dstDataLayoutDesc.offset;
    dstTextureCopyLocation.PlacedFootprint.Footprint.Format = textureDesc.Format;
    dstTextureCopyLocation.PlacedFootprint.Footprint.Width = srcRegionDesc.size[0];
    dstTextureCopyLocation.PlacedFootprint.Footprint.Height = srcRegionDesc.size[1];
    dstTextureCopyLocation.PlacedFootprint.Footprint.Depth = srcRegionDesc.size[2];
    dstTextureCopyLocation.PlacedFootprint.Footprint.RowPitch = dstDataLayoutDesc.rowPitch;

    const uint16_t(&offset)[3] = srcRegionDesc.offset;
    const uint16_t size[3] = {
        srcRegionDesc.size[0] == WHOLE_SIZE ? srcTextureD3D12.GetSize(0, srcRegionDesc.mipOffset) : srcRegionDesc.size[0],
        srcRegionDesc.size[1] == WHOLE_SIZE ? srcTextureD3D12.GetSize(1, srcRegionDesc.mipOffset) : srcRegionDesc.size[1],
        srcRegionDesc.size[2] == WHOLE_SIZE ? srcTextureD3D12.GetSize(2, srcRegionDesc.mipOffset) : srcRegionDesc.size[2]
    };
    D3D12_BOX box = { offset[0], offset[1], offset[2], uint16_t(offset[0] + size[0]), uint16_t(offset[1] + size[1]), uint16_t(offset[2] + size[2]) };

    m_GraphicsCommandList->CopyTextureRegion(&dstTextureCopyLocation, 0, 0, 0, &srcTextureCopyLocation, &box);
}

inline void CommandBufferD3D12::Dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    m_GraphicsCommandList->Dispatch(x, y, z);
}

inline void CommandBufferD3D12::DispatchIndirect(const Buffer& buffer, uint64_t offset)
{
    m_GraphicsCommandList->ExecuteIndirect(m_Device.GetDispatchCommandSignature(), 1, (BufferD3D12&)buffer, offset, nullptr, 0);
}

inline void CommandBufferD3D12::PipelineBarrier(const TransitionBarrierDesc* transitionBarriers, const AliasingBarrierDesc* aliasingBarriers, BarrierDependency dependency)
{
    uint32_t barrierNum = 0;
    if (transitionBarriers)
    {
        barrierNum += transitionBarriers->bufferNum;
        for (uint16_t i = 0; i < transitionBarriers->textureNum; i++)
        {
            const auto& barrierDesc = transitionBarriers->textures[i];
            const TextureD3D12& texture = *(TextureD3D12*)barrierDesc.texture;
            const uint32_t arraySize = barrierDesc.arraySize == REMAINING_ARRAY_LAYERS ? texture.GetTextureDesc().DepthOrArraySize : barrierDesc.arraySize;
            const uint32_t mipNum = barrierDesc.mipNum == REMAINING_MIP_LEVELS ? texture.GetTextureDesc().MipLevels : barrierDesc.mipNum;
            if (barrierDesc.arrayOffset == 0 &&
                barrierDesc.arraySize == REMAINING_ARRAY_LAYERS &&
                barrierDesc.mipOffset == 0 &&
                barrierDesc.mipNum == REMAINING_MIP_LEVELS)
                barrierNum++;
            else
                barrierNum += arraySize * mipNum;
        }
    }
    if (aliasingBarriers)
    {
        barrierNum += aliasingBarriers->bufferNum;
        barrierNum += aliasingBarriers->textureNum;
    }

    if (!barrierNum)
        return;

    D3D12_RESOURCE_BARRIER* resourceBarriers = STACK_ALLOC(D3D12_RESOURCE_BARRIER, barrierNum);
    memset(resourceBarriers, 0, sizeof(D3D12_RESOURCE_BARRIER) * barrierNum);

    D3D12_RESOURCE_BARRIER* ptr = resourceBarriers;
    if (transitionBarriers) // UAV and transitions barriers
    {
        for (uint32_t i = 0; i < transitionBarriers->bufferNum; i++)
        {
            const auto& barrierDesc = transitionBarriers->buffers[i];
            AddResourceBarrier(*((BufferD3D12*)barrierDesc.buffer), barrierDesc.prevAccess, barrierDesc.nextAccess, *ptr++, 0);
        }

        for (uint32_t i = 0; i < transitionBarriers->textureNum; i++)
        {
            const auto& barrierDesc = transitionBarriers->textures[i];
            const TextureD3D12& texture = *(TextureD3D12*)barrierDesc.texture;
            const uint32_t arraySize = barrierDesc.arraySize == REMAINING_ARRAY_LAYERS ? texture.GetTextureDesc().DepthOrArraySize : barrierDesc.arraySize;
            const uint32_t mipNum = barrierDesc.mipNum == REMAINING_MIP_LEVELS ? texture.GetTextureDesc().MipLevels : barrierDesc.mipNum;
            if (barrierDesc.arrayOffset == 0 &&
                barrierDesc.arraySize == REMAINING_ARRAY_LAYERS &&
                barrierDesc.mipOffset == 0 &&
                barrierDesc.mipNum == REMAINING_MIP_LEVELS)
            {
                AddResourceBarrier(texture, barrierDesc.prevAccess, barrierDesc.nextAccess, *ptr++, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
            }
            else
            {
                for (uint16_t arrayOffset = barrierDesc.arrayOffset; arrayOffset < barrierDesc.arrayOffset + arraySize; arrayOffset++)
                {
                    for (uint16_t mipOffset = barrierDesc.mipOffset; mipOffset < barrierDesc.mipOffset + mipNum; mipOffset++)
                    {
                        uint32_t subresource = texture.GetSubresourceIndex(arrayOffset, mipOffset);
                        AddResourceBarrier(texture, barrierDesc.prevAccess, barrierDesc.nextAccess, *ptr++, subresource);
                    }
                }
            }
        }
    }

    if (aliasingBarriers)
    {
        for (uint32_t i = 0; i < aliasingBarriers->bufferNum; i++)
        {
            D3D12_RESOURCE_BARRIER& barrier = *ptr++;
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
            barrier.Aliasing.pResourceBefore = *((BufferD3D12*)aliasingBarriers->buffers[i].before);
            barrier.Aliasing.pResourceAfter = *((BufferD3D12*)aliasingBarriers->buffers[i].after);
        }

        for (uint32_t i = 0; i < aliasingBarriers->textureNum; i++)
        {
            D3D12_RESOURCE_BARRIER& barrier = *ptr++;
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
            barrier.Aliasing.pResourceBefore = *((BufferD3D12*)aliasingBarriers->textures[i].before);
            barrier.Aliasing.pResourceAfter = *((BufferD3D12*)aliasingBarriers->textures[i].after);
        }
    }

    m_GraphicsCommandList->ResourceBarrier(barrierNum, resourceBarriers);
}

inline void CommandBufferD3D12::BeginQuery(const QueryPool& queryPool, uint32_t offset)
{
    const QueryPoolD3D12& queryPoolD3D12 = (QueryPoolD3D12&)queryPool;
    m_GraphicsCommandList->BeginQuery(queryPoolD3D12, queryPoolD3D12.GetType(), offset);
}

inline void CommandBufferD3D12::EndQuery(const QueryPool& queryPool, uint32_t offset)
{
    const QueryPoolD3D12& queryPoolD3D12 = (QueryPoolD3D12&)queryPool;
    m_GraphicsCommandList->EndQuery(queryPoolD3D12, queryPoolD3D12.GetType(), offset);
}

inline void CommandBufferD3D12::CopyQueries(const QueryPool& queryPool, uint32_t offset, uint32_t num, Buffer& buffer, uint64_t alignedBufferOffset)
{
    const QueryPoolD3D12& queryPoolD3D12 = (QueryPoolD3D12&)queryPool;
    const BufferD3D12& bufferD3D12 = (BufferD3D12&)buffer;
    m_GraphicsCommandList->ResolveQueryData(queryPoolD3D12, queryPoolD3D12.GetType(), offset, num, bufferD3D12, alignedBufferOffset);
}

inline void CommandBufferD3D12::BeginAnnotation(const char* name)
{
    size_t len = strlen(name) + 1;
    wchar_t* s = STACK_ALLOC(wchar_t, len);
    ConvertCharToWchar(name, s, len);

    PIXBeginEvent(m_GraphicsCommandList, PIX_COLOR_DEFAULT, s);
}

inline void CommandBufferD3D12::EndAnnotation()
{
    PIXEndEvent(m_GraphicsCommandList);
}

void CommandBufferD3D12::BuildTopLevelAccelerationStructure(uint32_t instanceNum, const Buffer& buffer, uint64_t bufferOffset,
    AccelerationStructureBuildBits flags, AccelerationStructure& dst, Buffer& scratch, uint64_t scratchOffset)
{
#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
    desc.DestAccelerationStructureData = ((AccelerationStructureD3D12&)dst).GetHandle();
    desc.ScratchAccelerationStructureData = ((BufferD3D12&)scratch).GetPointerGPU() + scratchOffset;
    desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    desc.Inputs.Flags = GetAccelerationStructureBuildFlags(flags);
    desc.Inputs.NumDescs = instanceNum;
    desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

    desc.Inputs.InstanceDescs = ((BufferD3D12&)buffer).GetPointerGPU() + bufferOffset;

    m_GraphicsCommandList4->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
#endif
}

void CommandBufferD3D12::BuildBottomLevelAccelerationStructure(uint32_t geometryObjectNum, const GeometryObject* geometryObjects,
    AccelerationStructureBuildBits flags, AccelerationStructure& dst, Buffer& scratch, uint64_t scratchOffset)
{
#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
    desc.DestAccelerationStructureData = ((AccelerationStructureD3D12&)dst).GetHandle();
    desc.ScratchAccelerationStructureData = ((BufferD3D12&)scratch).GetPointerGPU() + scratchOffset;
    desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    desc.Inputs.Flags = GetAccelerationStructureBuildFlags(flags);
    desc.Inputs.NumDescs = geometryObjectNum;
    desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

    Vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs(geometryObjectNum, m_Device.GetStdAllocator());
    ConvertGeometryDescs(&geometryDescs[0], geometryObjects, geometryObjectNum);
    desc.Inputs.pGeometryDescs = &geometryDescs[0];

    m_GraphicsCommandList4->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
#endif
}

void CommandBufferD3D12::UpdateTopLevelAccelerationStructure(uint32_t instanceNum, const Buffer& buffer, uint64_t bufferOffset,
    AccelerationStructureBuildBits flags, AccelerationStructure& dst, AccelerationStructure& src, Buffer& scratch, uint64_t scratchOffset)
{
#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
    desc.DestAccelerationStructureData = ((AccelerationStructureD3D12&)dst).GetHandle();
    desc.SourceAccelerationStructureData = ((AccelerationStructureD3D12&)src).GetHandle();
    desc.ScratchAccelerationStructureData = ((BufferD3D12&)scratch).GetPointerGPU() + scratchOffset;
    desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    desc.Inputs.Flags = GetAccelerationStructureBuildFlags(flags) & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    desc.Inputs.NumDescs = instanceNum;
    desc.Inputs.InstanceDescs = ((BufferD3D12&)buffer).GetPointerGPU() + bufferOffset;

    m_GraphicsCommandList4->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
#endif
}

void CommandBufferD3D12::UpdateBottomLevelAccelerationStructure(uint32_t geometryObjectNum, const GeometryObject* geometryObjects,
    AccelerationStructureBuildBits flags, AccelerationStructure& dst, AccelerationStructure& src, Buffer& scratch, uint64_t scratchOffset)
{
#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
    desc.DestAccelerationStructureData = ((AccelerationStructureD3D12&)dst).GetHandle();
    desc.SourceAccelerationStructureData = ((AccelerationStructureD3D12&)src).GetHandle();
    desc.ScratchAccelerationStructureData = ((BufferD3D12&)scratch).GetPointerGPU() + scratchOffset;
    desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    desc.Inputs.Flags = GetAccelerationStructureBuildFlags(flags) & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    desc.Inputs.NumDescs = geometryObjectNum;
    desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

    Vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs(geometryObjectNum, m_Device.GetStdAllocator());
    ConvertGeometryDescs(&geometryDescs[0], geometryObjects, geometryObjectNum);
    desc.Inputs.pGeometryDescs = &geometryDescs[0];

    m_GraphicsCommandList4->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
#endif
}

void CommandBufferD3D12::CopyAccelerationStructure(AccelerationStructure& dst, AccelerationStructure& src, CopyMode copyMode)
{
#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
    m_GraphicsCommandList4->CopyRaytracingAccelerationStructure(((AccelerationStructureD3D12&)dst).GetHandle(), ((AccelerationStructureD3D12&)src).GetHandle(), GetCopyMode(copyMode));
#endif
}

void CommandBufferD3D12::WriteAccelerationStructureSize(const AccelerationStructure* const* accelerationStructures, uint32_t accelerationStructureNum, QueryPool& queryPool, uint32_t queryOffset)
{
#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
    // TODO:
#endif
}

void CommandBufferD3D12::DispatchRays(const DispatchRaysDesc& dispatchRaysDesc)
{
#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
    D3D12_DISPATCH_RAYS_DESC desc = {};

    desc.RayGenerationShaderRecord.StartAddress = (*(BufferD3D12*)dispatchRaysDesc.raygenShader.buffer).GetPointerGPU() + dispatchRaysDesc.raygenShader.offset;
    desc.RayGenerationShaderRecord.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

    if (dispatchRaysDesc.missShaders.buffer)
    {
        desc.MissShaderTable.StartAddress = (*(BufferD3D12*)dispatchRaysDesc.missShaders.buffer).GetPointerGPU() + dispatchRaysDesc.missShaders.offset;
        desc.MissShaderTable.SizeInBytes = dispatchRaysDesc.missShaders.size;
        desc.MissShaderTable.StrideInBytes = dispatchRaysDesc.missShaders.stride;
    }

    if (dispatchRaysDesc.hitShaderGroups.buffer)
    {
        desc.HitGroupTable.StartAddress = (*(BufferD3D12*)dispatchRaysDesc.hitShaderGroups.buffer).GetPointerGPU() + dispatchRaysDesc.hitShaderGroups.offset;
        desc.HitGroupTable.SizeInBytes = dispatchRaysDesc.hitShaderGroups.size;
        desc.HitGroupTable.StrideInBytes = dispatchRaysDesc.hitShaderGroups.stride;
    }

    if (dispatchRaysDesc.callableShaders.buffer)
    {
        desc.CallableShaderTable.StartAddress = (*(BufferD3D12*)dispatchRaysDesc.callableShaders.buffer).GetPointerGPU() + dispatchRaysDesc.callableShaders.offset;
        desc.CallableShaderTable.SizeInBytes = dispatchRaysDesc.callableShaders.size;
        desc.CallableShaderTable.StrideInBytes = dispatchRaysDesc.callableShaders.stride;
    }

    desc.Width = dispatchRaysDesc.width;
    desc.Height = dispatchRaysDesc.height;
    desc.Depth = dispatchRaysDesc.depth;

    m_GraphicsCommandList4->DispatchRays(&desc);
#endif
}

inline void CommandBufferD3D12::AddResourceBarrier(ID3D12Resource* resource, AccessBits before, AccessBits after, D3D12_RESOURCE_BARRIER& resourceBarrier, uint32_t subresource)
{
    D3D12_RESOURCE_STATES resourceStateBefore = GetResourceStates(before);
    D3D12_RESOURCE_STATES resourceStateAfter = GetResourceStates(after);

    if (resourceStateBefore == resourceStateAfter && resourceStateBefore == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    {
        resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        resourceBarrier.UAV.pResource = resource;
    }
    else
    {
        resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        resourceBarrier.Transition.pResource = resource;
        resourceBarrier.Transition.StateBefore = resourceStateBefore;
        resourceBarrier.Transition.StateAfter = resourceStateAfter;
        resourceBarrier.Transition.Subresource = subresource;
    }
}

#include "CommandBufferD3D12.hpp"
