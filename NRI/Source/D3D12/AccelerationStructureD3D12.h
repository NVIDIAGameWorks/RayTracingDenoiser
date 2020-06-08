/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#ifdef __ID3D12GraphicsCommandList4_INTERFACE_DEFINED__
namespace nri
{
    struct DeviceD3D12;
    struct BufferD3D12;

    struct AccelerationStructureD3D12
    {
        AccelerationStructureD3D12(DeviceD3D12& device);
        ~AccelerationStructureD3D12();

        DeviceD3D12& GetDevice() const;

        Result Create(const AccelerationStructureDesc& accelerationStructureDesc);
        void GetMemoryInfo(MemoryDesc& memoryDesc) const;
        uint64_t GetUpdateScratchBufferSize() const;
        uint64_t GetBuildScratchBufferSize() const;
        Result BindMemory(Memory* memory, uint64_t offset);
        Result CreateDescriptor(Descriptor*& descriptor) const;
        uint64_t GetHandle() const;

        //================================================================================================================
        // NRI
        //================================================================================================================
        void SetDebugName(const char* name);

    private:
        DeviceD3D12& m_Device;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS m_AccelerationStructureInputs = {};
        D3D12_RAYTRACING_GEOMETRY_DESC* m_GeometryDescs = nullptr;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO m_PrebuildInfo;
        BufferD3D12* m_Buffer = nullptr;
    };

    inline AccelerationStructureD3D12::AccelerationStructureD3D12(DeviceD3D12& device)
        : m_Device(device)
    {}

    inline DeviceD3D12& AccelerationStructureD3D12::GetDevice() const
    {
        return m_Device;
    }
}
#endif
