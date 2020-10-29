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
    struct DeviceBase
    {
        DeviceBase(const Log& log, const StdAllocator<uint8_t>& stdAllocator);

        virtual ~DeviceBase() {}
        virtual void Destroy() = 0;
        virtual Result FillFunctionTable(CoreInterface& table) const { table = {}; return Result::UNSUPPORTED; }
        virtual Result FillFunctionTable(SwapChainInterface& table) const { table = {}; return Result::UNSUPPORTED; }
        virtual Result FillFunctionTable(WrapperD3D11Interface& table) const { table = {}; return Result::UNSUPPORTED; }
        virtual Result FillFunctionTable(WrapperD3D12Interface& table) const { table = {}; return Result::UNSUPPORTED; }
        virtual Result FillFunctionTable(WrapperVKInterface& table) const { table = {}; return Result::UNSUPPORTED; }
        virtual Result FillFunctionTable(RayTracingInterface& table) const { table = {}; return Result::UNSUPPORTED; }
        virtual Result FillFunctionTable(MeshShaderInterface& table) const { table = {}; return Result::UNSUPPORTED; }

        const Log& GetLog() const;
        StdAllocator<uint8_t>& GetStdAllocator();

    protected:
        Log m_Log;
        StdAllocator<uint8_t> m_StdAllocator;
    };

    inline DeviceBase::DeviceBase(const Log& log, const StdAllocator<uint8_t>& stdAllocator) :
        m_Log(log),
        m_StdAllocator(stdAllocator)
    {
    }

    inline const Log& DeviceBase::GetLog() const
    {
        return m_Log;
    }

    inline StdAllocator<uint8_t>& DeviceBase::GetStdAllocator()
    {
        return m_StdAllocator;
    }
}