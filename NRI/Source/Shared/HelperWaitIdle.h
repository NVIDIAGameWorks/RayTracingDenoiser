#pragma once

struct HelperWaitIdle
{
    HelperWaitIdle(const nri::CoreInterface& NRI, nri::Device& device, nri::CommandQueue& commandQueue);
    ~HelperWaitIdle();

    nri::Result WaitIdle();

private:
    const nri::CoreInterface& NRI;
    nri::Device& m_Device;
    nri::CommandQueue& m_CommandQueue;
    nri::DeviceSemaphore* m_DeviceSemaphore = nullptr;
};