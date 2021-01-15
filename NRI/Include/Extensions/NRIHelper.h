/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

namespace nri
{
    struct TextureSubresourceUploadDesc
    {
        const void* slices;
        uint32_t sliceNum;
        uint32_t rowPitch;
        uint32_t slicePitch;
    };

    struct TextureUploadDesc
    {
        const TextureSubresourceUploadDesc* subresources;
        Texture* texture;
        AccessBits nextAccess;
        TextureLayout nextLayout;
        uint16_t mipNum;
        uint16_t arraySize;
    };

    struct BufferUploadDesc
    {
        const void* data;
        uint64_t dataSize;
        Buffer* buffer;
        uint64_t bufferOffset;
        AccessBits prevAccess;
        AccessBits nextAccess;
    };

    struct ResourceGroupDesc
    {
        MemoryLocation memoryLocation;
        Texture* const* textures;
        uint32_t textureNum;
        Buffer* const* buffers;
        uint32_t bufferNum;
    };

    struct HelperInterface
    {
        uint32_t (NRI_CALL *CalculateAllocationNumber)(Device& device, const ResourceGroupDesc& resourceGroupDesc);
        Result (NRI_CALL *AllocateAndBindMemory)(Device& device, const ResourceGroupDesc& resourceGroupDesc, Memory** allocations);
        Result (NRI_CALL *ChangeResourceStates)(CommandQueue& commandQueue, const TransitionBarrierDesc& transitionBarriers);
        Result (NRI_CALL *UploadData)(CommandQueue& commandQueue, const TextureUploadDesc* textureUploadDescs, uint32_t textureUploadDescNum,
            const BufferUploadDesc* bufferUploadDescs, uint32_t bufferUploadDescNum);
        Result (NRI_CALL *WaitForIdle)(CommandQueue& commandQueue);
    };
}
