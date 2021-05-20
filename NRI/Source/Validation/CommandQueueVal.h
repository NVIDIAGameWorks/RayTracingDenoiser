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
    struct CommandBufferVal;

    struct CommandQueueVal : public DeviceObjectVal<CommandQueue>
    {
        CommandQueueVal(DeviceVal& device, CommandQueue& commandQueue);

        void SetDebugName(const char* name);
        void Submit(const WorkSubmissionDesc& workSubmissions, DeviceSemaphore* deviceSemaphore);
        void Wait(DeviceSemaphore& deviceSemaphore);

        Result ChangeResourceStates(const TransitionBarrierDesc& transitionBarriers);
        Result UploadData(const TextureUploadDesc* textureUploadDescs, uint32_t textureUploadDescNum, 
            const BufferUploadDesc* bufferUploadDescs, uint32_t bufferUploadDescNum);
        Result WaitForIdle();

    private:
        void ProcessValidationCommands(const CommandBufferVal* const* commandBuffers, uint32_t commandBufferNum);
        void ProcessValidationCommandBeginQuery(const uint8_t*& begin, const uint8_t* end);
        void ProcessValidationCommandEndQuery(const uint8_t*& begin, const uint8_t* end);
        void ProcessValidationCommandResetQuery(const uint8_t*& begin, const uint8_t* end);

        const HelperInterface& m_HelperAPI;
    };
}
