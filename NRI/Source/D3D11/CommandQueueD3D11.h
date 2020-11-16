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
    struct DeviceD3D11;

    struct CommandQueueD3D11
    {
        CommandQueueD3D11(DeviceD3D11& device, const VersionedContext& immediateContext, CommandQueueType type);
        ~CommandQueueD3D11();

        inline DeviceD3D11& GetDevice() const
        { return m_Device; }

        //======================================================================================================================
        // NRI
        //======================================================================================================================
        void SetDebugName(const char* name);
        void Submit(const WorkSubmissionDesc& workSubmissions, DeviceSemaphore* deviceSemaphore);
        void Wait(DeviceSemaphore& deviceSemaphore);

        Result ChangeResourceStates(const TransitionBarrierDesc& transitionBarriers);
        Result UploadData(const TextureUploadDesc* textureUploadDescs, uint32_t textureUploadDescNum, 
            const BufferUploadDesc* bufferUploadDescs, uint32_t bufferUploadDescNum);
        Result WaitForIdle();

    private:
        const VersionedContext& m_ImmediateContext;
        CommandQueueType m_Type = CommandQueueType::GRAPHICS;
        DeviceD3D11& m_Device;
    };
}
