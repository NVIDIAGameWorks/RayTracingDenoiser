#include "SharedExternal.h"

using namespace nri;

HelperWaitIdle::HelperWaitIdle(const CoreInterface& NRI, Device& device, CommandQueue& commandQueue) :
    NRI(NRI),
    m_Device(device),
    m_CommandQueue(commandQueue)
{
    NRI.CreateDeviceSemaphore(device, false, m_DeviceSemaphore);
}

HelperWaitIdle::~HelperWaitIdle()
{
    if (m_DeviceSemaphore != nullptr)
        NRI.DestroyDeviceSemaphore(*m_DeviceSemaphore);
    m_DeviceSemaphore = nullptr;
}

Result HelperWaitIdle::WaitIdle()
{
    if (m_DeviceSemaphore == nullptr)
        return Result::FAILURE;

    const uint32_t physicalDeviceNum = NRI.GetDeviceDesc(m_Device).phyiscalDeviceGroupSize;

    for (uint32_t i = 0; i < physicalDeviceNum; i++)
    {
        WorkSubmissionDesc workSubmissionDesc = {};
        workSubmissionDesc.physicalDeviceIndex = i;
        NRI.SubmitQueueWork(m_CommandQueue, workSubmissionDesc, m_DeviceSemaphore);
        NRI.WaitForSemaphore(m_CommandQueue, *m_DeviceSemaphore);
    }

    return Result::SUCCESS;
}
