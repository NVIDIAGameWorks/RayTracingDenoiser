/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRI.h"
#include "Helper.h"

#include <map>
#include <algorithm>
#include <unordered_map>

struct MemoryAllocator
{
    MemoryAllocator(const nri::CoreInterface& NRI, nri::Device& device, std::vector<nri::Memory*>& memoryAllocations);

    nri::Result AllocateAndBindMemory(nri::MemoryLocation memoryLocation, nri::Buffer* const* buffers, uint32_t bufferNum,
        nri::Texture* const* textures, uint32_t textureNum);

private:
    struct MemoryTypeGroup;

    nri::Result ProcessMemoryTypeGroup(nri::MemoryType memoryType, MemoryTypeGroup& group);
    nri::Result ProcessDedicatedResources(nri::MemoryLocation memoryLocation);
    void GroupByMemoryType(nri::MemoryLocation memoryLocation, nri::Buffer* const* buffers, uint32_t bufferNum);
    void GroupByMemoryType(nri::MemoryLocation memoryLocation, nri::Texture* const* textures, uint32_t textureNum);
    void FillMemoryBindingDescs(nri::Buffer* const* buffers, const uint64_t* bufferOffsets, uint32_t bufferNum);
    void FillMemoryBindingDescs(nri::Texture* const* texture, const uint64_t* textureOffsets, uint32_t textureNum);
    nri::Result AllocateMemory(nri::MemoryType memoryType, uint64_t size);

    struct MemoryTypeGroup
    {
        std::vector<nri::Buffer*> buffers;
        std::vector<uint64_t> bufferOffsets;
        std::vector<nri::Texture*> textures;
        std::vector<uint64_t> textureOffsets;
        uint64_t memoryOffset;
    };

    const nri::CoreInterface& m_NRI;
    nri::Device& m_Device;
    std::vector<nri::Memory*>& m_MemoryAllocations;
    std::map<nri::MemoryType, MemoryTypeGroup> m_Map;
    std::vector<nri::Buffer*> m_DedicatedBuffers;
    std::vector<nri::Texture*> m_DedicatedTextures;
    std::vector<nri::BufferMemoryBindingDesc> m_BufferBindingDescs;
    std::vector<nri::TextureMemoryBindingDesc> m_TextureBindingDescs;
};

MemoryAllocator::MemoryAllocator(const nri::CoreInterface& NRI, nri::Device& device, std::vector<nri::Memory*>& memoryAllocations) :
    m_NRI(NRI),
    m_Device(device),
    m_MemoryAllocations(memoryAllocations)
{
}

nri::Result MemoryAllocator::AllocateAndBindMemory(nri::MemoryLocation memoryLocation, nri::Buffer* const* buffers, uint32_t bufferNum,
    nri::Texture* const* textures, uint32_t textureNum)
{
    GroupByMemoryType(memoryLocation, buffers, bufferNum);
    GroupByMemoryType(memoryLocation, textures, textureNum);

    nri::Result result;

    for (auto it = m_Map.begin(); it != m_Map.end(); ++it)
        ProcessMemoryTypeGroup(it->first, it->second);

    ProcessDedicatedResources(memoryLocation);

    result = m_NRI.BindBufferMemory(m_Device, m_BufferBindingDescs.data(), (uint32_t)m_BufferBindingDescs.size());
    if (result != nri::Result::SUCCESS)
        return result;

    result = m_NRI.BindTextureMemory(m_Device, m_TextureBindingDescs.data(), (uint32_t)m_TextureBindingDescs.size());
    if (result != nri::Result::SUCCESS)
        return result;

    m_Map.clear();
    m_BufferBindingDescs.clear();
    m_TextureBindingDescs.clear();

    return result;
}

nri::Result MemoryAllocator::ProcessMemoryTypeGroup(nri::MemoryType memoryType, MemoryTypeGroup& group)
{
    const uint64_t allocationSize = group.memoryOffset;

    const nri::Result result = AllocateMemory(memoryType, allocationSize);
    if (result != nri::Result::SUCCESS)
        return result;

    FillMemoryBindingDescs(group.buffers.data(), group.bufferOffsets.data(), (uint32_t)group.buffers.size());
    FillMemoryBindingDescs(group.textures.data(), group.textureOffsets.data(), (uint32_t)group.textures.size());

    return nri::Result::SUCCESS;
}

nri::Result MemoryAllocator::ProcessDedicatedResources(nri::MemoryLocation memoryLocation)
{
    constexpr uint64_t zeroOffset = 0;
    nri::MemoryDesc memoryDesc = {};

    for (size_t i = 0; i < m_DedicatedBuffers.size(); i++)
    {
        m_NRI.GetBufferMemoryInfo(*m_DedicatedBuffers[i], memoryLocation, memoryDesc);

        const nri::Result result = AllocateMemory(memoryDesc.type, memoryDesc.size);
        if (result != nri::Result::SUCCESS)
            return result;

        FillMemoryBindingDescs(m_DedicatedBuffers.data() + i, &zeroOffset, 1);
    }

    for (size_t i = 0; i < m_DedicatedTextures.size(); i++)
    {
        m_NRI.GetTextureMemoryInfo(*m_DedicatedTextures[i], memoryLocation, memoryDesc);

        const nri::Result result = AllocateMemory(memoryDesc.type, memoryDesc.size);
        if (result != nri::Result::SUCCESS)
            return result;

        FillMemoryBindingDescs(m_DedicatedTextures.data() + i, &zeroOffset, 1);
    }

    return nri::Result::SUCCESS;
}

void MemoryAllocator::GroupByMemoryType(nri::MemoryLocation memoryLocation, nri::Buffer* const* buffers, uint32_t bufferNum)
{
    nri::MemoryDesc memoryDesc = {};

    for (uint32_t i = 0; i < bufferNum; i++)
    {
        nri::Buffer* buffer = buffers[i];
        m_NRI.GetBufferMemoryInfo(*buffer, memoryLocation, memoryDesc);

        if (memoryDesc.mustBeDedicated)
            m_DedicatedBuffers.push_back(buffer);
        else
        {
            MemoryTypeGroup& group = m_Map.try_emplace(memoryDesc.type, MemoryTypeGroup{}).first->second;

            const uint64_t offset = helper::GetAlignedSize(group.memoryOffset, memoryDesc.alignment);

            group.buffers.push_back(buffer);
            group.bufferOffsets.push_back(offset);
            group.memoryOffset = offset + memoryDesc.size;
        }
    }
}

void MemoryAllocator::GroupByMemoryType(nri::MemoryLocation memoryLocation, nri::Texture* const* textures, uint32_t textureNum)
{
    const nri::DeviceDesc& deviceDesc = m_NRI.GetDeviceDesc(m_Device);

    nri::MemoryDesc memoryDesc = {};

    for (uint32_t i = 0; i < textureNum; i++)
    {
        nri::Texture* texture = textures[i];
        m_NRI.GetTextureMemoryInfo(*texture, memoryLocation, memoryDesc);

        if (memoryDesc.mustBeDedicated)
            m_DedicatedTextures.push_back(texture);
        else
        {
            MemoryTypeGroup& group = m_Map.try_emplace(memoryDesc.type, MemoryTypeGroup{}).first->second;

            if (group.textures.empty() && group.memoryOffset > 0)
                group.memoryOffset = helper::GetAlignedSize(group.memoryOffset, deviceDesc.bufferTextureGranularity);

            const uint64_t offset = helper::GetAlignedSize(group.memoryOffset, memoryDesc.alignment);

            group.textures.push_back(texture);
            group.textureOffsets.push_back(offset);
            group.memoryOffset = offset + memoryDesc.size;
        }
    }
}

void MemoryAllocator::FillMemoryBindingDescs(nri::Buffer* const* buffers, const uint64_t* bufferOffsets, uint32_t bufferNum)
{
    for (uint32_t i = 0; i < bufferNum; i++)
    {
        nri::BufferMemoryBindingDesc desc = {};
        desc.memory = m_MemoryAllocations.back();
        desc.buffer = buffers[i];
        desc.offset = bufferOffsets[i];
        desc.physicalDeviceMask = nri::WHOLE_DEVICE_GROUP;

        m_BufferBindingDescs.push_back(desc);
    }
}

void MemoryAllocator::FillMemoryBindingDescs(nri::Texture* const* textures, const uint64_t* textureOffsets, uint32_t textureNum)
{
    for (uint32_t i = 0; i < textureNum; i++)
    {
        nri::TextureMemoryBindingDesc desc = {};
        desc.memory = m_MemoryAllocations.back();
        desc.texture = textures[i];
        desc.offset = textureOffsets[i];
        desc.physicalDeviceMask = nri::WHOLE_DEVICE_GROUP;

        m_TextureBindingDescs.push_back(desc);
    }
}

nri::Result MemoryAllocator::AllocateMemory(nri::MemoryType memoryType, uint64_t size)
{
    nri::Memory* memory = nullptr;
    const nri::Result result = m_NRI.AllocateMemory(m_Device, nri::WHOLE_DEVICE_GROUP, memoryType, size, memory);

    if (result == nri::Result::SUCCESS)
        m_MemoryAllocations.push_back(memory);

    return nri::Result::SUCCESS;
}

nri::Result helper::BindMemory(const nri::CoreInterface& NRI, nri::Device& device, nri::MemoryLocation memoryLocation, nri::Texture* const* textures, uint32_t textureNum,
    nri::Buffer* const* buffers, uint32_t bufferNum, std::vector<nri::Memory*>& outMemoryBlobs)
{
    MemoryAllocator memoryAllocator(NRI, device, outMemoryBlobs);

    return memoryAllocator.AllocateAndBindMemory(memoryLocation, buffers, bufferNum, textures, textureNum);
}

struct Uploader
{
    Uploader(const nri::CoreInterface& NRI, nri::Device& device, nri::CommandQueue& commandQueue, uint64_t uploadBufferSize);
    ~Uploader();

    nri::Result Create();
    nri::Result UploadTextures(const helper::TextureDataDesc* textureDataDescs, uint32_t textureDataDescNum);
    nri::Result UploadBuffers(const helper::BufferDataDesc* bufferDataDescs, uint32_t bufferDataDescNum);

private:
    nri::Result BeginCommandBuffers();
    nri::Result EndCommandBuffersAndSubmit();
    bool CopyTextureContent(const helper::TextureDataDesc& textureDataDesc, uint16_t& arrayOffset, uint16_t& mipOffset, bool& isCapacityInsufficient);
    void CopyTextureSubresourceContent(const helper::TextureSubresource& subresource, uint64_t alignedRowPitch, uint64_t alignedSlicePitch);
    bool CopyBufferContent(const helper::BufferDataDesc& bufferDataDesc, uint64_t& bufferContentOffset);
    template<bool isInitialTransition>
    nri::Result DoTransition(const helper::TextureDataDesc* textureDataDescs, uint32_t textureDataDescNum);
    template<bool isInitialTransition>
    nri::Result DoTransition(const helper::BufferDataDesc* bufferDataDescs, uint32_t bufferDataDescNum);

    const nri::CoreInterface& NRI;
    const nri::DeviceDesc& m_DeviceDesc;
    nri::Device& m_Device;
    nri::CommandQueue& m_CommandQueue;
    nri::DeviceSemaphore* m_DeviceSemaphore = nullptr;
    std::vector<nri::CommandAllocator*> m_CommandAllocators;
    std::vector<nri::CommandBuffer*> m_CommandBuffers;
    nri::Buffer* m_UploadBuffer = nullptr;
    nri::Memory* m_UploadBufferMemory = nullptr;
    uint8_t* m_MappedMemory = nullptr;
    uint64_t m_UploadBufferSize = 0;
    uint64_t m_UploadBufferOffset = 0;

    static constexpr uint64_t COPY_ALIGMENT = 16;
};

Uploader::Uploader(const nri::CoreInterface& NRI, nri::Device& device, nri::CommandQueue& commandQueue, uint64_t uploadBufferSize) :
    NRI(NRI),
    m_DeviceDesc(NRI.GetDeviceDesc(device)),
    m_Device(device),
    m_CommandQueue(commandQueue),
    m_UploadBufferSize(helper::GetAlignedSize(uploadBufferSize, COPY_ALIGMENT))
{
}

Uploader::~Uploader()
{
    for (uint32_t i = 0; i < m_CommandBuffers.size(); i++)
    {
        NRI.DestroyCommandBuffer(*m_CommandBuffers[i]);
        NRI.DestroyCommandAllocator(*m_CommandAllocators[i]);
    }
    NRI.DestroyDeviceSemaphore(*m_DeviceSemaphore);

    NRI.DestroyBuffer(*m_UploadBuffer);
    NRI.FreeMemory(*m_UploadBufferMemory);
}

nri::Result Uploader::Create()
{
    nri::BufferDesc bufferDesc = {};
    bufferDesc.size = m_UploadBufferSize;
    nri::Result result = NRI.CreateBuffer(m_Device, bufferDesc, m_UploadBuffer);
    if (result != nri::Result::SUCCESS)
        return result;

    nri::MemoryDesc memoryDesc = {};
    NRI.GetBufferMemoryInfo(*m_UploadBuffer, nri::MemoryLocation::HOST_UPLOAD, memoryDesc);

    result = NRI.AllocateMemory(m_Device, nri::WHOLE_DEVICE_GROUP, memoryDesc.type, memoryDesc.size, m_UploadBufferMemory);
    if (result != nri::Result::SUCCESS)
        return result;

    const nri::BufferMemoryBindingDesc bufferMemoryBindingDesc = { m_UploadBufferMemory, m_UploadBuffer, 0 };
    result = NRI.BindBufferMemory(m_Device, &bufferMemoryBindingDesc, 1);
    if (result != nri::Result::SUCCESS)
        return result;

    result = NRI.CreateDeviceSemaphore(m_Device, false, m_DeviceSemaphore);
    if (result != nri::Result::SUCCESS)
        return result;

    m_CommandAllocators.resize(m_DeviceDesc.phyiscalDeviceGroupSize);
    m_CommandBuffers.resize(m_DeviceDesc.phyiscalDeviceGroupSize);

    for (uint32_t i = 0; i < m_CommandBuffers.size(); i++)
    {
        result = NRI.CreateCommandAllocator(m_CommandQueue, nri::WHOLE_DEVICE_GROUP, m_CommandAllocators[i]);
        if (result != nri::Result::SUCCESS)
            return result;

        result = NRI.CreateCommandBuffer(*m_CommandAllocators[i], m_CommandBuffers[i]);
        if (result != nri::Result::SUCCESS)
            return result;
    }

    return result;
}

nri::Result Uploader::UploadTextures(const helper::TextureDataDesc* textureDataDescs, uint32_t textureDataDescNum)
{
    nri::Result result = DoTransition<true>(textureDataDescs, textureDataDescNum);
    if (result != nri::Result::SUCCESS)
        return result;

    uint32_t i = 0;
    uint16_t arrayOffset = 0;
    uint16_t mipOffset = 0;

    while (i < textureDataDescNum)
    {
        result = BeginCommandBuffers();
        if (result != nri::Result::SUCCESS)
            return result;

        m_UploadBufferOffset = 0;
        bool isCapacityInsufficient = false;

        for (; i < textureDataDescNum && CopyTextureContent(textureDataDescs[i], arrayOffset, mipOffset, isCapacityInsufficient); i++)
            ;

        if (isCapacityInsufficient)
            return nri::Result::OUT_OF_MEMORY;

        result = EndCommandBuffersAndSubmit();
        if (result != nri::Result::SUCCESS)
            return result;
    }

    return DoTransition<false>(textureDataDescs, textureDataDescNum);
}

nri::Result Uploader::UploadBuffers(const helper::BufferDataDesc* bufferDataDescs, uint32_t bufferDataDescNum)
{
    nri::Result result = DoTransition<true>(bufferDataDescs, bufferDataDescNum);
    if (result != nri::Result::SUCCESS)
        return result;

    uint32_t i = 0;
    uint64_t bufferContentOffset = 0;

    while (i < bufferDataDescNum)
    {
        result = BeginCommandBuffers();
        if (result != nri::Result::SUCCESS)
            return result;

        m_UploadBufferOffset = 0;
        m_MappedMemory = (uint8_t*)NRI.MapBuffer(*m_UploadBuffer, 0, m_UploadBufferSize);

        for (; i < bufferDataDescNum && CopyBufferContent(bufferDataDescs[i], bufferContentOffset); i++)
            ;

        NRI.UnmapBuffer(*m_UploadBuffer);

        result = EndCommandBuffersAndSubmit();
        if (result != nri::Result::SUCCESS)
            return result;
    }

    return DoTransition<false>(bufferDataDescs, bufferDataDescNum);
}

nri::Result Uploader::BeginCommandBuffers()
{
    nri::Result result = nri::Result::SUCCESS;

    for (uint32_t i = 0; i < m_CommandBuffers.size() && result == nri::Result::SUCCESS; i++)
        result = NRI.BeginCommandBuffer(*m_CommandBuffers[i], nullptr, i);

    return result;
}

nri::Result Uploader::EndCommandBuffersAndSubmit()
{
    for (uint32_t i = 0; i < m_CommandBuffers.size(); i++)
    {
        const nri::Result result = NRI.EndCommandBuffer(*m_CommandBuffers[i]);
        if (result != nri::Result::SUCCESS)
            return result;
    }

    nri::WorkSubmissionDesc workSubmissionDesc = {};
    workSubmissionDesc.commandBufferNum = 1;

    for (uint32_t i = 0; i < m_CommandBuffers.size(); i++)
    {
        workSubmissionDesc.commandBuffers = &m_CommandBuffers[i];
        workSubmissionDesc.physicalDeviceIndex = i;
        NRI.SubmitQueueWork(m_CommandQueue, workSubmissionDesc, m_DeviceSemaphore);
        NRI.WaitForSemaphore(m_CommandQueue, *m_DeviceSemaphore);
    }

    for (uint32_t i = 0; i < m_CommandAllocators.size(); i++)
        NRI.ResetCommandAllocator(*m_CommandAllocators[i]);

    return nri::Result::SUCCESS;
}

bool Uploader::CopyTextureContent(const helper::TextureDataDesc& textureDataDesc, uint16_t& arrayOffset, uint16_t& mipOffset, bool& isCapacityInsufficient)
{
    if (textureDataDesc.subresources == nullptr)
        return true;

    for (; arrayOffset < textureDataDesc.arraySize; arrayOffset++)
    {
        for (; mipOffset < textureDataDesc.mipNum; mipOffset++)
        {
            const auto& subresource = textureDataDesc.subresources[arrayOffset * textureDataDesc.mipNum + mipOffset];

            const uint32_t sliceRowNum = subresource.slicePitch / subresource.rowPitch;
            const uint32_t alignedRowPitch = helper::GetAlignedSize(subresource.rowPitch, m_DeviceDesc.uploadBufferTextureRowAlignment);
            const uint32_t alignedSlicePitch = helper::GetAlignedSize(sliceRowNum * alignedRowPitch, m_DeviceDesc.uploadBufferTextureSliceAlignment);
            const uint64_t mipLevelContentSize = uint64_t(alignedSlicePitch) * subresource.sliceNum;
            const uint64_t freeSpace = m_UploadBufferSize - m_UploadBufferOffset;

            if (mipLevelContentSize > freeSpace)
            {
                isCapacityInsufficient = mipLevelContentSize > m_UploadBufferSize;
                return false;
            }

            CopyTextureSubresourceContent(subresource, alignedRowPitch, alignedSlicePitch);

            nri::TextureDataLayoutDesc srcDataLayout = {};
            srcDataLayout.offset = m_UploadBufferOffset;
            srcDataLayout.rowPitch = alignedRowPitch;
            srcDataLayout.slicePitch = alignedSlicePitch;

            nri::TextureRegionDesc dstRegion = {};
            dstRegion.arrayOffset = arrayOffset;
            dstRegion.mipOffset = mipOffset;

            for (uint32_t k = 0; k < m_CommandBuffers.size(); k++)
                NRI.CmdUploadBufferToTexture(*m_CommandBuffers[k], *textureDataDesc.texture, dstRegion, *m_UploadBuffer, srcDataLayout);

            m_UploadBufferOffset = helper::GetAlignedSize(m_UploadBufferOffset + mipLevelContentSize, COPY_ALIGMENT);
        }
        mipOffset = 0;
    }
    arrayOffset = 0;

    m_UploadBufferOffset = helper::GetAlignedSize(m_UploadBufferOffset, COPY_ALIGMENT);

    return true;
}

void Uploader::CopyTextureSubresourceContent(const helper::TextureSubresource& subresource, uint64_t alignedRowPitch, uint64_t alignedSlicePitch)
{
    const uint32_t sliceRowNum = subresource.slicePitch / subresource.rowPitch;

    // TODO: D3D11 does not allow to call CmdUploadBufferToTexture() while the upload buffer is mapped.
    m_MappedMemory = (uint8_t*)NRI.MapBuffer(*m_UploadBuffer, m_UploadBufferOffset, subresource.sliceNum * alignedSlicePitch);

    uint8_t* slices = m_MappedMemory;
    for (uint32_t k = 0; k < subresource.sliceNum; k++)
    {
        for (uint32_t l = 0; l < sliceRowNum; l++)
        {
            uint8_t* dstRow = slices + k * alignedSlicePitch + l * alignedRowPitch;
            uint8_t* srcRow = (uint8_t*)subresource.slices[k] + l * subresource.rowPitch;
            memcpy(dstRow, srcRow, subresource.rowPitch);
        }
    }

    NRI.UnmapBuffer(*m_UploadBuffer);
}

bool Uploader::CopyBufferContent(const helper::BufferDataDesc& bufferDataDesc, uint64_t& bufferContentOffset)
{
    if (bufferDataDesc.dataSize == 0)
        return true;

    const uint64_t freeSpace = m_UploadBufferSize - m_UploadBufferOffset;
    const uint64_t copySize = std::min(bufferDataDesc.dataSize - bufferContentOffset, freeSpace);

    if (freeSpace == 0)
        return false;

    memcpy(m_MappedMemory + m_UploadBufferOffset, (uint8_t*)bufferDataDesc.data + bufferContentOffset, copySize);

    for (uint32_t j = 0; j < m_CommandBuffers.size(); j++)
    {
        NRI.CmdCopyBuffer(*m_CommandBuffers[j], *bufferDataDesc.buffer, j, bufferDataDesc.bufferOffset + bufferContentOffset,
            *m_UploadBuffer, 0, m_UploadBufferOffset, copySize);
    }

    bufferContentOffset += copySize;
    m_UploadBufferOffset += copySize;

    if (bufferContentOffset != bufferDataDesc.dataSize)
        return false;

    bufferContentOffset = 0;
    m_UploadBufferOffset = helper::GetAlignedSize(m_UploadBufferOffset, COPY_ALIGMENT);
    return true;
}

template<bool isInitialTransition>
nri::Result Uploader::DoTransition(const helper::TextureDataDesc* textureDataDescs, uint32_t textureDataDescNum)
{
    const nri::Result result = BeginCommandBuffers();
    if (result != nri::Result::SUCCESS)
        return result;

    constexpr uint32_t TEXTURES_PER_PASS = 1024;
    nri::TextureTransitionBarrierDesc textureTransitions[TEXTURES_PER_PASS];

    for (uint32_t i = 0; i < textureDataDescNum;)
    {
        const uint32_t passBegin = i;
        const uint32_t passEnd = std::min(i + TEXTURES_PER_PASS, textureDataDescNum);

        for ( ; i < passEnd; i++)
        {
            const helper::TextureDataDesc& textureDesc = textureDataDescs[i];
            nri::TextureTransitionBarrierDesc& transition = textureTransitions[i - passBegin];

            transition = {};
            transition.texture = textureDesc.texture;
            transition.mipNum = textureDesc.mipNum;
            transition.arraySize = textureDesc.arraySize;

            if (isInitialTransition)
            {
                transition.prevAccess = nri::AccessBits::UNKNOWN;
                transition.nextAccess = nri::AccessBits::COPY_DESTINATION;
                transition.prevLayout = nri::TextureLayout::UNKNOWN;
                transition.nextLayout = nri::TextureLayout::GENERAL;
            }
            else
            {
                transition.prevAccess = nri::AccessBits::COPY_DESTINATION;
                transition.nextAccess = textureDesc.nextAccess;
                transition.prevLayout = nri::TextureLayout::GENERAL;
                transition.nextLayout = textureDesc.nextLayout;
            }
        }

        nri::TransitionBarrierDesc transitions = {};
        transitions.textures = textureTransitions;
        transitions.textureNum = passEnd - passBegin;

        for (uint32_t j = 0; j < m_CommandBuffers.size(); j++)
            NRI.CmdPipelineBarrier(*m_CommandBuffers[j], &transitions, nullptr, nri::BarrierDependency::COPY_STAGE);
    }

    return EndCommandBuffersAndSubmit();
}

template<bool isInitialTransition>
nri::Result Uploader::DoTransition(const helper::BufferDataDesc* bufferDataDescs, uint32_t bufferDataDescNum)
{
    const nri::Result result = BeginCommandBuffers();
    if (result != nri::Result::SUCCESS)
        return result;

    constexpr uint32_t BUFFERS_PER_PASS = 1024;
    nri::BufferTransitionBarrierDesc bufferTransitions[BUFFERS_PER_PASS];

    for (uint32_t i = 0; i < bufferDataDescNum;)
    {
        const uint32_t passBegin = i;
        const uint32_t passEnd = std::min(i + BUFFERS_PER_PASS, bufferDataDescNum);

        for ( ; i < passEnd; i++)
        {
            const helper::BufferDataDesc& bufferDataDesc = bufferDataDescs[i];
            nri::BufferTransitionBarrierDesc& bufferTransition = bufferTransitions[i - passBegin];

            bufferTransition = {};
            bufferTransition.buffer = bufferDataDesc.buffer;

            if (isInitialTransition)
            {
                bufferTransition.prevAccess = bufferDataDesc.prevAccess;
                bufferTransition.nextAccess = nri::AccessBits::COPY_DESTINATION;
            }
            else
            {
                bufferTransition.prevAccess = nri::AccessBits::COPY_DESTINATION;
                bufferTransition.nextAccess = bufferDataDesc.nextAccess;
            }
        }

        nri::TransitionBarrierDesc transitions = {};
        transitions.buffers = bufferTransitions;
        transitions.bufferNum = passEnd - passBegin;

        for (uint32_t j = 0; j < m_CommandBuffers.size(); j++)
            NRI.CmdPipelineBarrier(*m_CommandBuffers[j], &transitions, nullptr, nri::BarrierDependency::COPY_STAGE);
    }

    return EndCommandBuffersAndSubmit();
}

nri::Result helper::UploadData(const nri::CoreInterface& NRI, nri::Device& device, const TextureDataDesc* textureDataDescs,
    uint32_t textureDataDescNum, const BufferDataDesc* bufferDataDescs, uint32_t bufferDataDescNum)
{
    uint64_t uploadBufferSize = 64 * 1024 * 1024; // TODO:

    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(device);

    for (uint32_t i = 0; i < textureDataDescNum; i++)
    {
        if (textureDataDescs[i].subresources == nullptr)
            continue;

        const TextureSubresource& subresource = textureDataDescs[i].subresources[0];

        const uint32_t sliceRowNum = std::max(subresource.slicePitch / subresource.rowPitch, 1u);
        const uint64_t alignedRowPitch = helper::GetAlignedSize(subresource.rowPitch, deviceDesc.uploadBufferTextureRowAlignment);
        const uint64_t alignedSlicePitch = helper::GetAlignedSize(sliceRowNum * alignedRowPitch, deviceDesc.uploadBufferTextureSliceAlignment);
        const uint64_t mipLevelContentSize = alignedSlicePitch * std::max(subresource.sliceNum, 1u);
        uploadBufferSize = std::max(uploadBufferSize, mipLevelContentSize);
    }

    nri::CommandQueue* commandQueue = nullptr;
    NRI.GetCommandQueue(device, nri::CommandQueueType::GRAPHICS, commandQueue);
    Uploader uploader(NRI, device, *commandQueue, uploadBufferSize);

    nri::Result result = uploader.Create();
    if (result != nri::Result::SUCCESS)
        return result;

    result = uploader.UploadTextures(textureDataDescs, textureDataDescNum);
    if (result != nri::Result::SUCCESS)
        return result;

    return uploader.UploadBuffers(bufferDataDescs, bufferDataDescNum);
}

void helper::WaitIdle(const nri::CoreInterface& NRI, nri::Device& device, nri::CommandQueue& commandQueue)
{
    nri::DeviceSemaphore* deviceSemaphore;
    NRI.CreateDeviceSemaphore(device, false, deviceSemaphore);

    const uint32_t physicalDeviceNum = NRI.GetDeviceDesc(device).phyiscalDeviceGroupSize;
    for (uint32_t i = 0; i < physicalDeviceNum; i++)
    {
        nri::WorkSubmissionDesc workSubmissionDesc = {};
        workSubmissionDesc.physicalDeviceIndex = i;
        NRI.SubmitQueueWork(commandQueue, workSubmissionDesc, deviceSemaphore);
        NRI.WaitForSemaphore(commandQueue, *deviceSemaphore);
    }

    NRI.DestroyDeviceSemaphore(*deviceSemaphore);
}
