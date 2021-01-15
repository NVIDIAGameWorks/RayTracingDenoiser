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
    struct MemoryVal;

    struct BufferVal final : public DeviceObjectVal<Buffer>
    {
        BufferVal(DeviceVal& device, Buffer& buffer, const BufferDesc& bufferDesc);
        BufferVal(DeviceVal& device, Buffer& buffer, const BufferD3D11Desc& bufferD3D11Desc);
        BufferVal(DeviceVal& device, Buffer& buffer, const BufferD3D12Desc& bufferD3D12Desc);
        BufferVal(DeviceVal& device, Buffer& buffer, const BufferVulkanDesc& bufferVulkanDesc);
        ~BufferVal();

        void SetBoundToMemory();
        void SetBoundToMemory(MemoryVal& memory);
        bool IsBoundToMemory() const;
        BufferUsageBits GetUsageMask() const;

        const BufferDesc& GetDesc() const;

        void SetDebugName(const char* name);
        void GetMemoryInfo(MemoryLocation memoryLocation, MemoryDesc& memoryDesc) const;
        void* Map(uint64_t offset, uint64_t size);
        void Unmap();
        ID3D11Resource* GetBufferD3D11() const;
        ID3D12Resource* GetBufferD3D12() const;

    private:
        MemoryVal* m_Memory = nullptr;
        BufferDesc m_BufferDesc = {};
        bool m_IsBoundToMemory = false;
        bool m_IsMapped = false;
    };

    inline void BufferVal::SetBoundToMemory()
    {
        m_IsBoundToMemory = true;
    }

    inline void BufferVal::SetBoundToMemory(MemoryVal& memory)
    {
        m_Memory = &memory;
        m_IsBoundToMemory = true;
    }

    inline bool BufferVal::IsBoundToMemory() const
    {
        return m_IsBoundToMemory;
    }

    inline BufferUsageBits BufferVal::GetUsageMask() const
    {
        return m_BufferDesc.usageMask;
    }

    inline const BufferDesc& BufferVal::GetDesc() const
    {
        return m_BufferDesc;
    }
}
