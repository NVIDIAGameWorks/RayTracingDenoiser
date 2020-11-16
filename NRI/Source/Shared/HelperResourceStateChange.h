#pragma once

struct HelperResourceStateChange
{
    HelperResourceStateChange(const nri::CoreInterface& NRI, nri::Device& device, nri::CommandQueue& commandQueue);
    ~HelperResourceStateChange();

    nri::Result ChangeStates(const nri::TransitionBarrierDesc& transitionBarriers);

private:
    const nri::CoreInterface& NRI;
    nri::Device& m_Device;
    nri::CommandQueue& m_CommandQueue;
    nri::CommandAllocator* m_CommandAllocator = nullptr;
    nri::CommandBuffer* m_CommandBuffer = nullptr;
    HelperWaitIdle m_HelperWaitIdle;
};