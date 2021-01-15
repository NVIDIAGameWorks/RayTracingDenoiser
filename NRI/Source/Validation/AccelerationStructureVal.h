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

    struct AccelerationStructureVal final : public DeviceObjectVal<AccelerationStructure>
    {
        AccelerationStructureVal(DeviceVal& device, AccelerationStructure& accelerationStructure);
        ~AccelerationStructureVal();

        void SetBoundToMemory(MemoryVal& memory);
        bool IsBoundToMemory() const;

        void GetMemoryInfo(MemoryDesc& memoryDesc) const;
        uint64_t GetUpdateScratchBufferSize() const;
        uint64_t GetBuildScratchBufferSize() const;
        uint64_t GetHandle(uint32_t physicalDeviceIndex) const;
        Result CreateDescriptor(uint32_t physicalDeviceIndex, Descriptor*& descriptor);
        void SetDebugName(const char* name);

    private:
        const RayTracingInterface& m_RayTracingAPI;
        MemoryVal* m_Memory = nullptr;
    };

    inline void AccelerationStructureVal::SetBoundToMemory(MemoryVal& memory)
    {
        m_Memory = &memory;
    }

    inline bool AccelerationStructureVal::IsBoundToMemory() const
    {
        return m_Memory != nullptr;
    }
}
