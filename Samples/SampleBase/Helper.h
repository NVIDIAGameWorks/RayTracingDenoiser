/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include <vector>
#include <array>

namespace helper
{
    constexpr bool VARIABLE_DESCRIPTOR_NUM = true;
    constexpr bool DESCRIPTOR_ARRAY = true;

    template<typename T> constexpr T GetAlignedSize(const T& size, size_t alignment)
    { return T(((size + alignment - 1) / alignment) * alignment); }

    template <typename T, uint32_t N> constexpr uint32_t GetCountOf(T const (&)[N])
    { return N; }

    template <typename T> constexpr uint32_t GetCountOf(const std::vector<T>& v)
    { return (uint32_t)v.size(); }

    template <typename T, uint32_t N> constexpr uint32_t GetCountOf(const std::array<T, N>& v)
    { return (uint32_t)v.size(); }

    template<typename T, typename U> constexpr uint32_t GetOffsetOf(U T::*member)
    { return (uint32_t)((char*)&((T*)nullptr->*member) - (char*)nullptr); }

    template<typename T> constexpr size_t GetByteSizeOf(const std::vector<T>& v)
    { return v.size() * sizeof(decltype(v.back())); }

    struct Annotation
    {
        const nri::CoreInterface& m_NRI;
        nri::CommandBuffer& m_CommandBuffer;

        inline Annotation(const nri::CoreInterface& NRI, nri::CommandBuffer& commandBuffer, const char* name) :
            m_NRI(NRI),
            m_CommandBuffer(commandBuffer)
        { m_NRI.CmdBeginAnnotation(m_CommandBuffer, name); }

        inline ~Annotation()
        { m_NRI.CmdEndAnnotation(m_CommandBuffer); }
    };

    struct TextureSubresource
    {
        const void* const* slices;
        uint32_t sliceNum; // 1 for non-3D textures
        uint32_t rowPitch;
        uint32_t slicePitch;
    };

    struct TextureDataDesc
    {
        const TextureSubresource* subresources;
        nri::Texture* texture;
        nri::AccessBits nextAccess;
        nri::TextureLayout nextLayout;
        uint16_t mipNum;
        uint16_t arraySize;

        const TextureSubresource& GetSubresource(uint32_t mipIndex, uint32_t arrayIndex = 0) const
        { return subresources[arrayIndex * mipNum + mipIndex]; }
    };

    struct BufferDataDesc
    {
        const void* data;
        uint64_t dataSize;
        nri::Buffer* buffer;
        uint64_t bufferOffset;
        nri::AccessBits prevAccess;
        nri::AccessBits nextAccess;
    };

    nri::Result UploadData(const nri::CoreInterface& NRI, nri::Device& device, const TextureDataDesc* textureDataDescs, uint32_t textureDataDescNum, const BufferDataDesc* bufferDataDescs, uint32_t bufferDataDescNum);
    nri::Result BindMemory(const nri::CoreInterface& NRI, nri::Device& device, nri::MemoryLocation memoryLocation, nri::Texture* const* textures, uint32_t textureNum, nri::Buffer* const* buffers, uint32_t bufferNum, std::vector<nri::Memory*>& outMemoryBlobs);
    void WaitIdle(const nri::CoreInterface& NRI, nri::Device& device, nri::CommandQueue& commandQueue);
}
