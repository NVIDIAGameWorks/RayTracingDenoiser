/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

namespace nri
{
    struct BufferVal;
    struct TextureVal;
    struct AccelerationStructureVal;

    struct MemoryVal : public DeviceObjectVal<Memory>
    {
        MemoryVal(DeviceVal& device, Memory& memory, uint64_t size, MemoryLocation memoryLocation);
        MemoryVal(DeviceVal& device, Memory& memory, const MemoryD3D12Desc& memoryD3D12Desc);

        uint64_t GetSize() const;
        MemoryLocation GetMemoryLocation() const;
        bool HasBoundResources();
        void ReportBoundResources();

        void BindBuffer(BufferVal& buffer);
        void BindTexture(TextureVal& texture);
        void BindAccelerationStructure(AccelerationStructureVal& accelerationStructure);
        void UnbindBuffer(BufferVal& buffer);
        void UnbindTexture(TextureVal& texture);
        void UnbindAccelerationStructure(AccelerationStructureVal& accelerationStructure);

        void SetDebugName(const char* name);

    private:
        std::vector<BufferVal*> m_Buffers;
        std::vector<TextureVal*> m_Textures;
        std::vector<AccelerationStructureVal*> m_AccelerationStructures;
        Lock m_Lock;

        uint64_t m_Size = 0;
        MemoryLocation m_MemoryLocation = MemoryLocation::MAX_NUM;
    };

    inline uint64_t MemoryVal::GetSize() const
    {
        return m_Size;
    }

    inline MemoryLocation MemoryVal::GetMemoryLocation() const
    {
        return m_MemoryLocation;
    }
}
