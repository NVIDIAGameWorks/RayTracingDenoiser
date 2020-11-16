/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "DeviceBase.h"
#include "SharedVal.h"
#include "DeviceVal.h"

#include "BufferVal.h"
#include "CommandQueueVal.h"
#include "CommandAllocatorVal.h"
#include "CommandBufferVal.h"
#include "DescriptorVal.h"
#include "DescriptorPoolVal.h"
#include "DeviceSemaphoreVal.h"
#include "FrameBufferVal.h"
#include "MemoryVal.h"
#include "PipelineLayoutVal.h"
#include "PipelineVal.h"
#include "QueryPoolVal.h"
#include "QueueSemaphoreVal.h"
#include "SwapChainVal.h"
#include "TextureVal.h"
#include "AccelerationStructureVal.h"

using namespace nri;

void ConvertGeometryObjectsVal(GeometryObject* destObjects, const GeometryObject* sourceObjects, uint32_t objectNum);
QueryType GetQueryTypeVK(uint32_t queryTypeVK);

DeviceVal::DeviceVal(const Log& log, const StdAllocator<uint8_t>& stdAllocator, DeviceBase& device, uint32_t physicalDeviceNum) :
    DeviceBase(log, stdAllocator),
    m_Device(*(Device*)&device),
    m_Name(GetStdAllocator()),
    m_MemoryTypeMap(GetStdAllocator()),
    m_PhysicalDeviceNum(physicalDeviceNum),
    m_PhysicalDeviceMask((1 << (physicalDeviceNum + 1)) - 1)
{
}

DeviceVal::~DeviceVal()
{
    for (size_t i = 0; i < m_CommandQueues.size(); i++)
    {
        if (m_CommandQueues[i])
            Deallocate(GetStdAllocator(), m_CommandQueues[i]);
    }
    ((DeviceBase*)&m_Device)->Destroy();
}

bool DeviceVal::Create()
{
    const DeviceBase& deviceBase = (DeviceBase&)m_Device;

    if (deviceBase.FillFunctionTable(m_CoreAPI) != Result::SUCCESS)
    {
        REPORT_ERROR(GetLog(), "Failed to get 'CoreInterface' interface.");
        return false;
    }

    m_IsSwapChainSupported = deviceBase.FillFunctionTable(m_SwapChainAPI) == Result::SUCCESS;
    m_IsWrapperD3D11Supported = deviceBase.FillFunctionTable(m_WrapperD3D11API) == Result::SUCCESS;
    m_IsWrapperD3D12Supported = deviceBase.FillFunctionTable(m_WrapperD3D12API) == Result::SUCCESS;
    m_IsWrapperVKSupported = deviceBase.FillFunctionTable(m_WrapperVKAPI) == Result::SUCCESS;
    m_IsRayTracingSupported = deviceBase.FillFunctionTable(m_RayTracingAPI) == Result::SUCCESS;
    m_IsMeshShaderExtSupported = deviceBase.FillFunctionTable(m_MeshShaderAPI) == Result::SUCCESS;
    deviceBase.FillFunctionTable(m_HelperAPI);

    return true;
}

void DeviceVal::RegisterMemoryType(MemoryType memoryType, MemoryLocation memoryLocation)
{
    ExclusiveScope lockScope(m_Lock);
    m_MemoryTypeMap[memoryType] = memoryLocation;
}

Result DeviceVal::CreateSwapChain(const SwapChainDesc& swapChainDesc, SwapChain*& swapChain)
{
    RETURN_ON_FAILURE(GetLog(), swapChainDesc.commandQueue != nullptr, Result::INVALID_ARGUMENT,
        "Can't create SwapChain: 'swapChainDesc.commandQueue' is invalid.");

    RETURN_ON_FAILURE(GetLog(), swapChainDesc.windowHandle != nullptr, Result::INVALID_ARGUMENT,
        "Can't create SwapChain: 'swapChainDesc.windowHandle' is invalid.");

    RETURN_ON_FAILURE(GetLog(), swapChainDesc.width != 0, Result::INVALID_ARGUMENT,
        "Can't create SwapChain: 'swapChainDesc.width' is 0.");

    RETURN_ON_FAILURE(GetLog(), swapChainDesc.height != 0, Result::INVALID_ARGUMENT,
        "Can't create SwapChain: 'swapChainDesc.height' is 0.");

    RETURN_ON_FAILURE(GetLog(), swapChainDesc.textureNum > 0, Result::INVALID_ARGUMENT,
        "Can't create SwapChain: 'swapChainDesc.textureNum' is invalid.");

    RETURN_ON_FAILURE(GetLog(), swapChainDesc.format < SwapChainFormat::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create SwapChain: 'swapChainDesc.format' is invalid.");

    RETURN_ON_FAILURE(GetLog(), swapChainDesc.physicalDeviceIndex < m_PhysicalDeviceNum, Result::INVALID_ARGUMENT,
        "Can't create SwapChain: 'swapChainDesc.physicalDeviceIndex' is invalid.");

    auto swapChainDescImpl = swapChainDesc;
    swapChainDescImpl.commandQueue = NRI_GET_IMPL(CommandQueue, swapChainDesc.commandQueue);

    SwapChain* swapChainImpl;
    const Result result = m_SwapChainAPI.CreateSwapChain(m_Device, swapChainDescImpl, swapChainImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), swapChainImpl != nullptr, Result::FAILURE, "Unexpected error: 'swapChainImpl' is NULL.");
        swapChain = (SwapChain*)Allocate<SwapChainVal>(GetStdAllocator(), *this, *swapChainImpl, swapChainDesc);
    }

    return result;
}

void DeviceVal::DestroySwapChain(SwapChain& swapChain)
{
    m_SwapChainAPI.DestroySwapChain(*NRI_GET_IMPL(SwapChain, &swapChain));
    Deallocate(GetStdAllocator(), (SwapChainVal*)&swapChain);
}

void DeviceVal::SetDebugName(const char* name)
{
    m_Name = name;
    m_CoreAPI.SetDeviceDebugName(m_Device, name);
}

const DeviceDesc& DeviceVal::GetDesc() const
{
    return m_CoreAPI.GetDeviceDesc(m_Device);
}

Result DeviceVal::GetCommandQueue(CommandQueueType commandQueueType, CommandQueue*& commandQueue)
{
    RETURN_ON_FAILURE(GetLog(), commandQueueType < CommandQueueType::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't get CommandQueue: 'commandQueueType' is invalid.");

    CommandQueue* commandQueueImpl;
    const Result result = m_CoreAPI.GetCommandQueue(m_Device, commandQueueType, commandQueueImpl);

    if (result == Result::SUCCESS)
    {
        const uint32_t index = (uint32_t)commandQueueType;
        if (!m_CommandQueues[index])
            m_CommandQueues[index] = Allocate<CommandQueueVal>(GetStdAllocator(), *this, *commandQueueImpl);

        commandQueue = (CommandQueue*)m_CommandQueues[index];
    }

    return result;
}

Result DeviceVal::CreateCommandAllocator(const CommandQueue& commandQueue, uint32_t physicalDeviceMask, CommandAllocator*& commandAllocator)
{
    RETURN_ON_FAILURE(GetLog(), IsPhysicalDeviceMaskValid(physicalDeviceMask), Result::INVALID_ARGUMENT,
        "Can't create CommandAllocator: 'physicalDeviceMask' is invalid.");

    auto commandQueueImpl = NRI_GET_IMPL(CommandQueue, &commandQueue);

    CommandAllocator* commandAllocatorImpl = nullptr;
    const Result result = m_CoreAPI.CreateCommandAllocator(*commandQueueImpl, physicalDeviceMask, commandAllocatorImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), commandAllocatorImpl != nullptr, Result::FAILURE, "Unexpected error: 'commandAllocatorImpl' is NULL.");
        commandAllocator = (CommandAllocator*)Allocate<CommandAllocatorVal>(GetStdAllocator(), *this, *commandAllocatorImpl);
    }

    return result;
}

Result DeviceVal::CreateDescriptorPool(const DescriptorPoolDesc& descriptorPoolDesc, DescriptorPool*& descriptorPool)
{
    RETURN_ON_FAILURE(GetLog(), IsPhysicalDeviceMaskValid(descriptorPoolDesc.physicalDeviceMask), Result::INVALID_ARGUMENT,
        "Can't create DescriptorPool: 'descriptorPoolDesc.physicalDeviceMask' is invalid.");

    DescriptorPool* descriptorPoolImpl = nullptr;
    const Result result = m_CoreAPI.CreateDescriptorPool(m_Device, descriptorPoolDesc, descriptorPoolImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), descriptorPoolImpl != nullptr, Result::FAILURE, "Unexpected error: 'descriptorPoolImpl' is NULL.");
        descriptorPool = (DescriptorPool*)Allocate<DescriptorPoolVal>(GetStdAllocator(), *this, *descriptorPoolImpl, descriptorPoolDesc);
    }

    return result;
}

Result DeviceVal::CreateBuffer(const BufferDesc& bufferDesc, Buffer*& buffer)
{
    RETURN_ON_FAILURE(GetLog(), IsPhysicalDeviceMaskValid(bufferDesc.physicalDeviceMask), Result::INVALID_ARGUMENT,
        "Can't create Buffer: 'bufferDesc.physicalDeviceMask' is invalid.");

    RETURN_ON_FAILURE(GetLog(), bufferDesc.size > 0, Result::INVALID_ARGUMENT,
        "Can't create Buffer: 'bufferDesc.size' is 0.");

    Buffer* bufferImpl = nullptr;
    const Result result = m_CoreAPI.CreateBuffer(m_Device, bufferDesc, bufferImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), bufferImpl != nullptr, Result::FAILURE, "Unexpected error: 'bufferImpl' is NULL.");
        buffer = (Buffer*)Allocate<BufferVal>(GetStdAllocator(), *this, *bufferImpl, bufferDesc);
    }

    return result;
}

Result DeviceVal::CreateTexture(const TextureDesc& textureDesc, Texture*& texture)
{
    RETURN_ON_FAILURE(GetLog(), IsPhysicalDeviceMaskValid(textureDesc.physicalDeviceMask), Result::INVALID_ARGUMENT,
        "Can't create Texture: 'textureDesc.physicalDeviceMask' is invalid.");

    RETURN_ON_FAILURE(GetLog(), textureDesc.format > Format::UNKNOWN && textureDesc.format < Format::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Texture: 'textureDesc.format' is invalid.");

    RETURN_ON_FAILURE(GetLog(), textureDesc.sampleNum > 0, Result::INVALID_ARGUMENT,
        "Can't create Texture: 'textureDesc.sampleNum' is invalid.");

    Texture* textureImpl = nullptr;
    const Result result = m_CoreAPI.CreateTexture(m_Device, textureDesc, textureImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), textureImpl != nullptr, Result::FAILURE, "Unexpected error: 'textureImpl' is NULL.");
        texture = (Texture*)Allocate<TextureVal>(GetStdAllocator(), *this, *textureImpl, textureDesc);
    }

    return result;
}

Result DeviceVal::CreateDescriptor(const BufferViewDesc& bufferViewDesc, Descriptor*& bufferView)
{
    RETURN_ON_FAILURE(GetLog(), IsPhysicalDeviceMaskValid(bufferViewDesc.physicalDeviceMask), Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'bufferViewDesc.physicalDeviceMask' is invalid.");

    RETURN_ON_FAILURE(GetLog(), bufferViewDesc.buffer != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'bufferViewDesc.buffer' is invalid.");

    RETURN_ON_FAILURE(GetLog(), bufferViewDesc.format < Format::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'bufferViewDesc.format' is invalid.");

    RETURN_ON_FAILURE(GetLog(), bufferViewDesc.viewType < BufferViewType::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'bufferViewDesc.viewType' is invalid");

    const BufferDesc& bufferDesc = ((BufferVal*)bufferViewDesc.buffer)->GetDesc();

    RETURN_ON_FAILURE(GetLog(), bufferViewDesc.offset < bufferDesc.size, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'bufferViewDesc.offset' is invalid. (bufferViewDesc.offset=%llu, bufferDesc.size=%llu)",
        bufferViewDesc.offset, bufferDesc.size);

    RETURN_ON_FAILURE(GetLog(), bufferViewDesc.offset + bufferViewDesc.size <= bufferDesc.size, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'bufferViewDesc.size' is invalid. (bufferViewDesc.offset=%llu, bufferViewDesc.size=%llu, bufferDesc.size=%llu)",
        bufferViewDesc.offset, bufferViewDesc.size, bufferDesc.size);

    auto bufferViewDescImpl = bufferViewDesc;
    bufferViewDescImpl.buffer = NRI_GET_IMPL(Buffer, bufferViewDesc.buffer);

    Descriptor* descriptorImpl = nullptr;
    const Result result = m_CoreAPI.CreateBufferView(bufferViewDescImpl, descriptorImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), descriptorImpl != nullptr, Result::FAILURE, "Unexpected error: 'descriptorImpl' is NULL.");
        bufferView = (Descriptor*)Allocate<DescriptorVal>(GetStdAllocator(), *this, *descriptorImpl, bufferViewDesc);
    }

    return result;
}

Result DeviceVal::CreateDescriptor(const Texture1DViewDesc& textureViewDesc, Descriptor*& textureView)
{
    RETURN_ON_FAILURE(GetLog(), IsPhysicalDeviceMaskValid(textureViewDesc.physicalDeviceMask), Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.physicalDeviceMask' is invalid.");

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.texture != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.texture' is invalid.");

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.format > Format::UNKNOWN && textureViewDesc.format < Format::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.format' is invalid.");

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.viewType < Texture1DViewType::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.viewType' is invalid.");

    const TextureDesc& textureDesc = ((TextureVal*)textureViewDesc.texture)->GetDesc();

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.mipOffset < textureDesc.mipNum, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.mipOffset' is invalid. (textureViewDesc.mipOffset=%hu, textureDesc.mipNum=%hu)",
        textureViewDesc.mipOffset, textureDesc.mipNum);

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.mipOffset + textureViewDesc.mipNum <= textureDesc.mipNum, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.mipNum' is invalid. "
        "(textureViewDesc.mipOffset=%hu, textureViewDesc.mipNum=%hu, textureDesc.mipNum=%hu)",
        textureViewDesc.mipOffset, textureViewDesc.mipNum, textureDesc.mipNum);

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.arrayOffset < textureDesc.arraySize, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.arrayOffset' is invalid. "
        "(textureViewDesc.arrayOffset=%hu, textureDesc.arraySize=%hu)",
        textureViewDesc.arrayOffset, textureDesc.arraySize);

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.arrayOffset + textureViewDesc.arraySize <= textureDesc.arraySize, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.arraySize' is invalid. "
        "(textureViewDesc.arrayOffset=%hu, textureViewDesc.arraySize=%hu, textureDesc.arraySize=%hu)",
        textureViewDesc.arrayOffset, textureViewDesc.arraySize, textureDesc.arraySize);

    auto textureViewDescImpl = textureViewDesc;
    textureViewDescImpl.texture = NRI_GET_IMPL(Texture, textureViewDesc.texture);

    Descriptor* descriptorImpl = nullptr;
    const Result result = m_CoreAPI.CreateTexture1DView(textureViewDescImpl, descriptorImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), descriptorImpl != nullptr, Result::FAILURE, "Unexpected error: 'descriptorImpl' is NULL.");
        textureView = (Descriptor*)Allocate<DescriptorVal>(GetStdAllocator(), *this, *descriptorImpl, textureViewDesc);
    }

    return result;
}

Result DeviceVal::CreateDescriptor(const Texture2DViewDesc& textureViewDesc, Descriptor*& textureView)
{
    RETURN_ON_FAILURE(GetLog(), IsPhysicalDeviceMaskValid(textureViewDesc.physicalDeviceMask), Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.physicalDeviceMask' is invalid.");

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.texture != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.texture' is invalid.");

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.format > Format::UNKNOWN && textureViewDesc.format < Format::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.format' is invalid.");

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.viewType < Texture2DViewType::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.viewType' is invalid.");

    const TextureDesc& textureDesc = ((TextureVal*)textureViewDesc.texture)->GetDesc();

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.mipOffset < textureDesc.mipNum, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.mipOffset' is invalid. "
        "(textureViewDesc.mipOffset=%hu, textureDesc.mipNum=%hu)",
        textureViewDesc.mipOffset, textureDesc.mipNum);

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.mipOffset + textureViewDesc.mipNum <= textureDesc.mipNum, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.mipNum' is invalid. "
        "(textureViewDesc.mipOffset=%hu, textureViewDesc.mipNum=%hu, textureDesc.mipNum=%hu)",
        textureViewDesc.mipOffset, textureViewDesc.mipNum, textureDesc.mipNum);

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.arrayOffset < textureDesc.arraySize, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.arrayOffset' is invalid. "
        "(textureViewDesc.arrayOffset=%hu, textureDesc.arraySize=%hu)",
        textureViewDesc.arrayOffset, textureDesc.arraySize);

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.arrayOffset + textureViewDesc.arraySize <= textureDesc.arraySize, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.arraySize' is invalid. "
        "(textureViewDesc.arrayOffset=%hu, textureViewDesc.arraySize=%hu, textureDesc.arraySize=%hu)",
        textureViewDesc.arrayOffset, textureViewDesc.arraySize, textureDesc.arraySize);

    auto textureViewDescImpl = textureViewDesc;
    textureViewDescImpl.texture = NRI_GET_IMPL(Texture, textureViewDesc.texture);

    Descriptor* descriptorImpl = nullptr;
    const Result result = m_CoreAPI.CreateTexture2DView(textureViewDescImpl, descriptorImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), descriptorImpl != nullptr, Result::FAILURE, "Unexpected error: 'descriptorImpl' is NULL.");
        textureView = (Descriptor*)Allocate<DescriptorVal>(GetStdAllocator(), *this, *descriptorImpl, textureViewDesc);
    }

    return result;
}

Result DeviceVal::CreateDescriptor(const Texture3DViewDesc& textureViewDesc, Descriptor*& textureView)
{
    RETURN_ON_FAILURE(GetLog(), IsPhysicalDeviceMaskValid(textureViewDesc.physicalDeviceMask), Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.physicalDeviceMask' is invalid.");

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.texture != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.texture' is invalid.");

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.format > Format::UNKNOWN && textureViewDesc.format < Format::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.format' is invalid.");

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.viewType < Texture3DViewType::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.viewType' is invalid.");

    const TextureDesc& textureDesc = ((TextureVal*)textureViewDesc.texture)->GetDesc();

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.mipOffset < textureDesc.mipNum, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.mipOffset' is invalid. "
        "(textureViewDesc.mipOffset=%hu, textureViewDesc.mipOffset=%hu)",
        textureViewDesc.mipOffset, textureDesc.mipNum);

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.mipOffset + textureViewDesc.mipNum <= textureDesc.mipNum, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.mipNum' is invalid. "
        "(textureViewDesc.mipOffset=%hu, textureViewDesc.mipNum=%hu, textureDesc.mipNum=%hu)",
        textureViewDesc.mipOffset, textureViewDesc.mipNum, textureDesc.mipNum);

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.sliceOffset < textureDesc.size[2], Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.arrayOffset' is invalid. "
        "(textureViewDesc.sliceOffset=%hu, textureDesc.size[2]=%hu)",
        textureViewDesc.sliceOffset, textureDesc.size[2]);

    RETURN_ON_FAILURE(GetLog(), textureViewDesc.sliceOffset + textureViewDesc.sliceNum <= textureDesc.size[2], Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'textureViewDesc.arraySize' is invalid. "
        "(textureViewDesc.sliceOffset=%hu, textureViewDesc.sliceNum=%hu, textureDesc.size[2]=%hu)",
        textureViewDesc.sliceOffset, textureViewDesc.sliceNum, textureDesc.size[2]);

    auto textureViewDescImpl = textureViewDesc;
    textureViewDescImpl.texture = NRI_GET_IMPL(Texture, textureViewDesc.texture);

    Descriptor* descriptorImpl = nullptr;
    const Result result = m_CoreAPI.CreateTexture3DView(textureViewDescImpl, descriptorImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), descriptorImpl != nullptr, Result::FAILURE, "Unexpected error: 'descriptorImpl' is NULL.");
        textureView = (Descriptor*)Allocate<DescriptorVal>(GetStdAllocator(), *this, *descriptorImpl, textureViewDesc);
    }

    return result;
}

Result DeviceVal::CreateDescriptor(const SamplerDesc& samplerDesc, Descriptor*& sampler)
{
    RETURN_ON_FAILURE(GetLog(), samplerDesc.magnification < Filter::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'samplerDesc.magnification' is invalid.");

    RETURN_ON_FAILURE(GetLog(), samplerDesc.minification < Filter::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'samplerDesc.magnification' is invalid.");

    RETURN_ON_FAILURE(GetLog(), samplerDesc.mip < Filter::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'samplerDesc.mip' is invalid.");

    RETURN_ON_FAILURE(GetLog(), samplerDesc.filterExt < FilterExt::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'samplerDesc.filterExt' is invalid.");

    RETURN_ON_FAILURE(GetLog(), samplerDesc.addressModes.u < AddressMode::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'samplerDesc.addressModes.u' is invalid.");

    RETURN_ON_FAILURE(GetLog(), samplerDesc.addressModes.v < AddressMode::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'samplerDesc.addressModes.v' is invalid.");

    RETURN_ON_FAILURE(GetLog(), samplerDesc.addressModes.w < AddressMode::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'samplerDesc.addressModes.w' is invalid.");

    RETURN_ON_FAILURE(GetLog(), samplerDesc.compareFunc < CompareFunc::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'samplerDesc.compareFunc' is invalid.");

    RETURN_ON_FAILURE(GetLog(), samplerDesc.borderColor < BorderColor::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create Descriptor: 'samplerDesc.borderColor' is invalid.");

    Descriptor* samplerImpl = nullptr;
    const Result result = m_CoreAPI.CreateSampler(m_Device, samplerDesc, samplerImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), samplerImpl != nullptr, Result::FAILURE, "Unexpected error: 'samplerImpl' is NULL.");
        sampler = (Descriptor*)Allocate<DescriptorVal>(GetStdAllocator(), *this, *samplerImpl, samplerDesc);
    }

    return result;
}

Result DeviceVal::CreatePipelineLayout(const PipelineLayoutDesc& pipelineLayoutDesc, PipelineLayout*& pipelineLayout)
{
    const bool isGraphics = pipelineLayoutDesc.stageMask & PipelineLayoutShaderStageBits::ALL_GRAPHICS;
    const bool isCompute = pipelineLayoutDesc.stageMask & PipelineLayoutShaderStageBits::COMPUTE;
    const bool isRayTracing = pipelineLayoutDesc.stageMask & PipelineLayoutShaderStageBits::ALL_RAY_TRACING;
    const uint32_t supportedTypes = (uint32_t)isGraphics + (uint32_t)isCompute + (uint32_t)isRayTracing;

    RETURN_ON_FAILURE(GetLog(), supportedTypes > 0, Result::INVALID_ARGUMENT,
        "Can't create pipeline layout: 'pipelineLayoutDesc.stageMask' is 0.");
    RETURN_ON_FAILURE(GetLog(), supportedTypes == 1, Result::INVALID_ARGUMENT,
        "Can't create pipeline layout: 'pipelineLayoutDesc.stageMask' is invalid, it can't be compatible with more than one type of pipeline.");

    for (uint32_t i = 0; i < pipelineLayoutDesc.descriptorSetNum; i++)
    {
        const DescriptorSetDesc& descriptorSetDesc = pipelineLayoutDesc.descriptorSets[i];

        for (uint32_t j = 0; j < descriptorSetDesc.rangeNum; j++)
        {
            const DescriptorRangeDesc& range = descriptorSetDesc.ranges[j];

            RETURN_ON_FAILURE(GetLog(), !range.isDescriptorNumVariable || range.isArray, Result::INVALID_ARGUMENT,
                "Can't create pipeline layout: 'pipelineLayoutDesc.descriptorSets[%u].ranges[%u]' is invalid, "
                "'isArray' can't be false if 'isDescriptorNumVariable' is true.",
                i, j);

            RETURN_ON_FAILURE(GetLog(), range.descriptorNum > 0, Result::INVALID_ARGUMENT,
                "Can't create pipeline layout: 'pipelineLayoutDesc.descriptorSets[%u].ranges[%u].descriptorNum' can't be 0.",
                i, j);

            RETURN_ON_FAILURE(GetLog(), range.visibility < ShaderStage::MAX_NUM, Result::INVALID_ARGUMENT,
                "Can't create pipeline layout: 'pipelineLayoutDesc.descriptorSets[%u].ranges[%u].visibility' is invalid.",
                i, j);

            RETURN_ON_FAILURE(GetLog(), range.descriptorType < DescriptorType::MAX_NUM, Result::INVALID_ARGUMENT,
                "Can't create pipeline layout: 'pipelineLayoutDesc.descriptorSets[%u].ranges[%u].descriptorType' is invalid.",
                i, j);

            if (range.visibility != ShaderStage::ALL)
            {
                const PipelineLayoutShaderStageBits visibilityMask = PipelineLayoutShaderStageBits(1 << (uint32_t)range.visibility);
                const uint32_t filteredVisibilityMask = visibilityMask & pipelineLayoutDesc.stageMask;

                RETURN_ON_FAILURE(GetLog(), (uint32_t)visibilityMask == filteredVisibilityMask, Result::INVALID_ARGUMENT,
                    "Can't create pipeline layout: 'pipelineLayoutDesc.descriptorSets[%u].ranges[%u].visibility' is not "
                    "compatible with 'pipelineLayoutDesc.stageMask'.", i, j);
            }
        }
    }

    PipelineLayout* pipelineLayoutImpl = nullptr;
    const Result result = m_CoreAPI.CreatePipelineLayout(m_Device, pipelineLayoutDesc, pipelineLayoutImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), pipelineLayoutImpl != nullptr, Result::FAILURE, "Unexpected error: 'pipelineLayoutImpl' is NULL.");
        pipelineLayout = (PipelineLayout*)Allocate<PipelineLayoutVal>(GetStdAllocator(), *this, *pipelineLayoutImpl, pipelineLayoutDesc);
    }

    return result;
}

Result DeviceVal::CreatePipeline(const GraphicsPipelineDesc& graphicsPipelineDesc, Pipeline*& pipeline)
{
    RETURN_ON_FAILURE(GetLog(), graphicsPipelineDesc.pipelineLayout != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Pipeline: 'graphicsPipelineDesc.pipelineLayout' is invalid.");

    RETURN_ON_FAILURE(GetLog(), graphicsPipelineDesc.outputMerger != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Pipeline: 'graphicsPipelineDesc.outputMerger' is invalid.");

    RETURN_ON_FAILURE(GetLog(), graphicsPipelineDesc.rasterization != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Pipeline: 'graphicsPipelineDesc.rasterization' is invalid.");

    RETURN_ON_FAILURE(GetLog(), graphicsPipelineDesc.shaderStages != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Pipeline: 'graphicsPipelineDesc.shaderStages' is invalid.");

    RETURN_ON_FAILURE(GetLog(), graphicsPipelineDesc.shaderStageNum > 0, Result::INVALID_ARGUMENT,
        "Can't create Pipeline: 'graphicsPipelineDesc.shaderStageNum' is 0.");

    const ShaderDesc* vertexShader = nullptr;
    for (uint32_t i = 0; i < graphicsPipelineDesc.shaderStageNum; i++)
    {
        const ShaderDesc* shaderDesc = graphicsPipelineDesc.shaderStages + i;

        if (shaderDesc->stage == ShaderStage::VERTEX)
            vertexShader = shaderDesc;

        RETURN_ON_FAILURE(GetLog(), shaderDesc->bytecode != nullptr, Result::INVALID_ARGUMENT,
            "Can't create Pipeline: 'graphicsPipelineDesc.shaderStages[%u].bytecode' is invalid.", i);

        RETURN_ON_FAILURE(GetLog(), shaderDesc->size != 0, Result::INVALID_ARGUMENT,
            "Can't create Pipeline: 'graphicsPipelineDesc.shaderStages[%u].size' is 0.", i);

        RETURN_ON_FAILURE(GetLog(), shaderDesc->stage > ShaderStage::ALL && shaderDesc->stage < ShaderStage::COMPUTE, Result::INVALID_ARGUMENT,
            "Can't create Pipeline: 'graphicsPipelineDesc.shaderStages[%u].stage' is invalid.", i);
    }

    if (graphicsPipelineDesc.inputAssembly != nullptr)
    {
        RETURN_ON_FAILURE(GetLog(), !graphicsPipelineDesc.inputAssembly->attributes || vertexShader, Result::INVALID_ARGUMENT,
            "Can't create Pipeline: vertex shader is not specified, but input assembly attributes provided.");

        const PipelineLayoutVal& pipelineLayout = *(PipelineLayoutVal*)graphicsPipelineDesc.pipelineLayout;
        const PipelineLayoutShaderStageBits stageMask = pipelineLayout.GetPipelineLayoutDesc().stageMask;

        RETURN_ON_FAILURE(GetLog(), (stageMask & PipelineLayoutShaderStageBits::VERTEX) != 0, Result::INVALID_ARGUMENT,
            "Can't create Pipeline: vertex stage is not enabled in the pipeline layout.");
    }

    auto graphicsPipelineDescImpl = graphicsPipelineDesc;
    graphicsPipelineDescImpl.pipelineLayout = NRI_GET_IMPL(PipelineLayout, graphicsPipelineDesc.pipelineLayout);

    Pipeline* pipelineImpl = nullptr;
    const Result result = m_CoreAPI.CreateGraphicsPipeline(m_Device, graphicsPipelineDescImpl, pipelineImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), pipelineImpl != nullptr, Result::FAILURE, "Unexpected error: 'pipelineImpl' is NULL.");
        pipeline = (Pipeline*)Allocate<PipelineVal>(GetStdAllocator(), *this, *pipelineImpl, graphicsPipelineDesc);
    }

    return result;
}

Result DeviceVal::CreatePipeline(const ComputePipelineDesc& computePipelineDesc, Pipeline*& pipeline)
{
    RETURN_ON_FAILURE(GetLog(), computePipelineDesc.pipelineLayout != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Pipeline: 'computePipelineDesc.pipelineLayout' is invalid.");

    RETURN_ON_FAILURE(GetLog(), computePipelineDesc.computeShader.bytecode != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Pipeline: 'computePipelineDesc.computeShader.bytecode' is invalid.");

    RETURN_ON_FAILURE(GetLog(), computePipelineDesc.computeShader.size != 0, Result::INVALID_ARGUMENT,
        "Can't create Pipeline: 'computePipelineDesc.computeShader.size' is 0.");

    RETURN_ON_FAILURE(GetLog(), computePipelineDesc.computeShader.stage == ShaderStage::COMPUTE, Result::INVALID_ARGUMENT,
        "Can't create Pipeline: 'computePipelineDesc.computeShader.stage' must be ShaderStage::COMPUTE.");

    auto computePipelineDescImpl = computePipelineDesc;
    computePipelineDescImpl.pipelineLayout = NRI_GET_IMPL(PipelineLayout, computePipelineDesc.pipelineLayout);

    Pipeline* pipelineImpl = nullptr;
    const Result result = m_CoreAPI.CreateComputePipeline(m_Device, computePipelineDescImpl, pipelineImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), pipelineImpl != nullptr, Result::FAILURE, "Unexpected error: 'pipelineImpl' is NULL.");
        pipeline = (Pipeline*)Allocate<PipelineVal>(GetStdAllocator(), *this, *pipelineImpl, computePipelineDesc);
    }

    return result;
}

Result DeviceVal::CreateFrameBuffer(const FrameBufferDesc& frameBufferDesc, FrameBuffer*& frameBuffer)
{
    RETURN_ON_FAILURE(GetLog(), IsPhysicalDeviceMaskValid(frameBufferDesc.physicalDeviceMask), Result::INVALID_ARGUMENT,
        "Can't create FrameBuffer: 'frameBufferDesc.physicalDeviceMask' is invalid.");

    if (frameBufferDesc.colorAttachmentNum > 0)
    {
        RETURN_ON_FAILURE(GetLog(), frameBufferDesc.colorAttachments != nullptr, Result::INVALID_ARGUMENT,
            "Can't create FrameBuffer: 'frameBufferDesc.colorAttachments' is invalid.");

        for (uint32_t i = 0; i < frameBufferDesc.colorAttachmentNum; i++)
        {
            DescriptorVal* descriptorVal = (DescriptorVal*)frameBufferDesc.colorAttachments[i];

            RETURN_ON_FAILURE(GetLog(), descriptorVal->IsColorAttachment(), Result::INVALID_ARGUMENT,
                "Can't create FrameBuffer: 'frameBufferDesc.colorAttachments[%u]' is not a color attachment descriptor.", i);
        }
    }

    if (frameBufferDesc.depthStencilAttachment != nullptr)
    {
        DescriptorVal* descriptorVal = (DescriptorVal*)frameBufferDesc.depthStencilAttachment;
        RETURN_ON_FAILURE(GetLog(), descriptorVal->IsDepthStencilAttachment(), Result::INVALID_ARGUMENT,
            "Can't create FrameBuffer: 'frameBufferDesc.depthStencilAttachment' is not a depth stencil attachment descriptor.");
    }

    auto frameBufferDescImpl = frameBufferDesc;
    if (frameBufferDesc.depthStencilAttachment)
        frameBufferDescImpl.depthStencilAttachment = NRI_GET_IMPL(Descriptor, frameBufferDesc.depthStencilAttachment);
    if (frameBufferDesc.colorAttachmentNum)
    {
        frameBufferDescImpl.colorAttachments = STACK_ALLOC(Descriptor*, frameBufferDesc.colorAttachmentNum);
        for (uint32_t i = 0; i < frameBufferDesc.colorAttachmentNum; i++)
            ((Descriptor**)frameBufferDescImpl.colorAttachments)[i] = NRI_GET_IMPL(Descriptor, frameBufferDesc.colorAttachments[i]);
    }

    FrameBuffer* frameBufferImpl = nullptr;
    const Result result = m_CoreAPI.CreateFrameBuffer(m_Device, frameBufferDescImpl, frameBufferImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), frameBufferImpl != nullptr, Result::FAILURE, "Unexpected error: 'frameBufferImpl' is NULL!");
        frameBuffer = (FrameBuffer*)Allocate<FrameBufferVal>(GetStdAllocator(), *this, *frameBufferImpl);
    }

    return result;
}

Result DeviceVal::CreateQueryPool(const QueryPoolDesc& queryPoolDesc, QueryPool*& queryPool)
{
    RETURN_ON_FAILURE(GetLog(), IsPhysicalDeviceMaskValid(queryPoolDesc.physicalDeviceMask), Result::INVALID_ARGUMENT,
        "Can't create QueryPool: 'queryPoolDesc.physicalDeviceMask' is invalid.");

    RETURN_ON_FAILURE(GetLog(), queryPoolDesc.queryType < QueryType::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create QueryPool: 'queryPoolDesc.queryType' is invalid.");

    RETURN_ON_FAILURE(GetLog(), queryPoolDesc.capacity > 0, Result::INVALID_ARGUMENT,
        "Can't create QueryPool: 'queryPoolDesc.capacity' is 0.");

    QueryPool* queryPoolImpl = nullptr;
    const Result result = m_CoreAPI.CreateQueryPool(m_Device, queryPoolDesc, queryPoolImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), queryPoolImpl != nullptr, Result::FAILURE, "Unexpected error: 'queryPoolImpl' is NULL!");
        queryPool = (QueryPool*)Allocate<QueryPoolVal>(GetStdAllocator(), *this, *queryPoolImpl, queryPoolDesc.queryType);
    }

    return result;
}

Result DeviceVal::CreateQueueSemaphore(QueueSemaphore*& queueSemaphore)
{
    QueueSemaphore* queueSemaphoreImpl;
    const Result result = m_CoreAPI.CreateQueueSemaphore(m_Device, queueSemaphoreImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), queueSemaphoreImpl != nullptr, Result::FAILURE, "Unexpected error: 'queueSemaphoreImpl' is NULL!");
        queueSemaphore = (QueueSemaphore*)Allocate<QueueSemaphoreVal>(GetStdAllocator(), *this, *queueSemaphoreImpl);
    }

    return result;
}

Result DeviceVal::CreateDeviceSemaphore(bool signaled, DeviceSemaphore*& deviceSemaphore)
{
    DeviceSemaphore* deviceSemaphoreImpl;
    const Result result = m_CoreAPI.CreateDeviceSemaphore(m_Device, signaled, deviceSemaphoreImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), deviceSemaphoreImpl != nullptr, Result::FAILURE, "Unexpected error: 'queueSemaphoreImpl' is NULL!");
        DeviceSemaphoreVal* deviceSemaphoreVal = Allocate<DeviceSemaphoreVal>(GetStdAllocator(), *this, *deviceSemaphoreImpl);
        deviceSemaphoreVal->Create(signaled);
        deviceSemaphore = (DeviceSemaphore*)deviceSemaphoreVal;
    }

    return result;
}

void DeviceVal::DestroyCommandAllocator(CommandAllocator& commandAllocator)
{
    m_CoreAPI.DestroyCommandAllocator(*NRI_GET_IMPL(CommandAllocator, &commandAllocator));
    Deallocate(GetStdAllocator(), (CommandAllocatorVal*)&commandAllocator);
}

void DeviceVal::DestroyDescriptorPool(DescriptorPool& descriptorPool)
{
    m_CoreAPI.DestroyDescriptorPool(*NRI_GET_IMPL(DescriptorPool, &descriptorPool));
    Deallocate(GetStdAllocator(), (DescriptorPoolVal*)&descriptorPool);
}

void DeviceVal::DestroyBuffer(Buffer& buffer)
{
    m_CoreAPI.DestroyBuffer(*NRI_GET_IMPL(Buffer, &buffer));
    Deallocate(GetStdAllocator(), (BufferVal*)&buffer);
}

void DeviceVal::DestroyTexture(Texture& texture)
{
    m_CoreAPI.DestroyTexture(*NRI_GET_IMPL(Texture, &texture));
    Deallocate(GetStdAllocator(), (TextureVal*)&texture);
}

void DeviceVal::DestroyDescriptor(Descriptor& descriptor)
{
    m_CoreAPI.DestroyDescriptor(*NRI_GET_IMPL(Descriptor, &descriptor));
    Deallocate(GetStdAllocator(), (DescriptorVal*)&descriptor);
}

void DeviceVal::DestroyPipelineLayout(PipelineLayout& pipelineLayout)
{
    m_CoreAPI.DestroyPipelineLayout(*NRI_GET_IMPL(PipelineLayout, &pipelineLayout));
    Deallocate(GetStdAllocator(), (PipelineLayoutVal*)&pipelineLayout);
}

void DeviceVal::DestroyPipeline(Pipeline& pipeline)
{
    m_CoreAPI.DestroyPipeline(*NRI_GET_IMPL(Pipeline, &pipeline));
    Deallocate(GetStdAllocator(), (PipelineVal*)&pipeline);
}

void DeviceVal::DestroyFrameBuffer(FrameBuffer& frameBuffer)
{
    m_CoreAPI.DestroyFrameBuffer(*NRI_GET_IMPL(FrameBuffer, &frameBuffer));
    Deallocate(GetStdAllocator(), (FrameBufferVal*)&frameBuffer);
}

void DeviceVal::DestroyQueryPool(QueryPool& queryPool)
{
    m_CoreAPI.DestroyQueryPool(*NRI_GET_IMPL(QueryPool, &queryPool));
    Deallocate(GetStdAllocator(), (QueryPoolVal*)&queryPool);
}

void DeviceVal::DestroyQueueSemaphore(QueueSemaphore& queueSemaphore)
{
    m_CoreAPI.DestroyQueueSemaphore(*NRI_GET_IMPL(QueueSemaphore, &queueSemaphore));
    Deallocate(GetStdAllocator(), (QueueSemaphoreVal*)&queueSemaphore);
}

void DeviceVal::DestroyDeviceSemaphore(DeviceSemaphore& deviceSemaphore)
{
    m_CoreAPI.DestroyDeviceSemaphore(*NRI_GET_IMPL(DeviceSemaphore, &deviceSemaphore));
    Deallocate(GetStdAllocator(), (DeviceSemaphoreVal*)&deviceSemaphore);
}

Result DeviceVal::AllocateMemory(uint32_t physicalDeviceMask, MemoryType memoryType, uint64_t size, Memory*& memory)
{
    RETURN_ON_FAILURE(GetLog(), IsPhysicalDeviceMaskValid(physicalDeviceMask), Result::INVALID_ARGUMENT,
        "Can't allocate Memory: 'physicalDeviceMask' is invalid.");

    RETURN_ON_FAILURE(GetLog(), size > 0, Result::INVALID_ARGUMENT,
        "Can't allocate Memory: 'size' is 0.");

    std::unordered_map<MemoryType, MemoryLocation>::iterator it;
    std::unordered_map<MemoryType, MemoryLocation>::iterator end;
    {
        SharedScope lockScope(m_Lock);
        it = m_MemoryTypeMap.find(memoryType);
        end = m_MemoryTypeMap.end();
    }

    RETURN_ON_FAILURE(GetLog(), it != end, Result::FAILURE,
        "Can't allocate Memory: 'memoryType' is invalid.");

    Memory* memoryImpl;
    const Result result = m_CoreAPI.AllocateMemory(m_Device, physicalDeviceMask, memoryType, size, memoryImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), memoryImpl != nullptr, Result::FAILURE, "Unexpected error: 'memoryImpl' is NULL!");
        memory = (Memory*)Allocate<MemoryVal>(GetStdAllocator(), *this, *memoryImpl, size, it->second);
    }

    return result;
}

Result DeviceVal::BindBufferMemory(const BufferMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum)
{
    if (memoryBindingDescNum == 0)
        return Result::SUCCESS;

    RETURN_ON_FAILURE(GetLog(), memoryBindingDescs != nullptr, Result::INVALID_ARGUMENT,
        "Can't bind memory to buffers: 'memoryBindingDescs' is invalid.");

    BufferMemoryBindingDesc* memoryBindingDescsImpl = STACK_ALLOC(BufferMemoryBindingDesc, memoryBindingDescNum);

    for (uint32_t i = 0; i < memoryBindingDescNum; i++)
    {
        BufferMemoryBindingDesc& destDesc = memoryBindingDescsImpl[i];
        const BufferMemoryBindingDesc& srcDesc = memoryBindingDescs[i];

        RETURN_ON_FAILURE(GetLog(), srcDesc.buffer != nullptr, Result::INVALID_ARGUMENT,
            "Can't bind memory to buffers: 'memoryBindingDescs[%u].buffer' is invalid.", i);
        RETURN_ON_FAILURE(GetLog(), srcDesc.memory != nullptr, Result::INVALID_ARGUMENT,
            "Can't bind memory to buffers: 'memoryBindingDescs[%u].memory' is invalid.", i);

        MemoryVal& memory = (MemoryVal&)*srcDesc.memory;
        BufferVal& buffer = (BufferVal&)*srcDesc.buffer;

        RETURN_ON_FAILURE(GetLog(), !buffer.IsBoundToMemory(), Result::INVALID_ARGUMENT,
            "Can't bind memory to buffers: 'memoryBindingDescs[%u].buffer' is already bound to memory.", i);

        // Skip validation if memory has been created from GAPI object using a wrapper extension
        if (memory.GetMemoryLocation() == MemoryLocation::MAX_NUM)
            continue;

        MemoryDesc memoryDesc = {};
        buffer.GetMemoryInfo(memory.GetMemoryLocation(), memoryDesc);

        RETURN_ON_FAILURE(GetLog(), !memoryDesc.mustBeDedicated || srcDesc.offset == 0, Result::INVALID_ARGUMENT,
            "Can't bind memory to buffers: 'memoryBindingDescs[%u].offset' must be zero for dedicated allocation.", i);

        RETURN_ON_FAILURE(GetLog(), memoryDesc.alignment != 0, Result::INVALID_ARGUMENT,
            "Can't bind memory to buffers: 'memoryBindingDescs[%u].alignment' can't be zero.", i);

        RETURN_ON_FAILURE(GetLog(), srcDesc.offset % memoryDesc.alignment == 0, Result::INVALID_ARGUMENT,
            "Can't bind memory to buffers: 'memoryBindingDescs[%u].offset' is misaligned.", i);

        const uint64_t rangeMax = srcDesc.offset + memoryDesc.size;
        const bool memorySizeIsUnknown = memory.GetSize() == 0;

        RETURN_ON_FAILURE(GetLog(), memorySizeIsUnknown || rangeMax <= memory.GetSize(), Result::INVALID_ARGUMENT,
            "Can't bind memory to buffers: 'memoryBindingDescs[%u].offset' is invalid.", i);

        destDesc = srcDesc;
        destDesc.memory = &memory.GetImpl();
        destDesc.buffer = &buffer.GetImpl();
    }

    const Result result = m_CoreAPI.BindBufferMemory(m_Device, memoryBindingDescsImpl, memoryBindingDescNum);

    if (result == Result::SUCCESS)
    {
        for (uint32_t i = 0; i < memoryBindingDescNum; i++)
        {
            MemoryVal& memory = *(MemoryVal*)memoryBindingDescs[i].memory;
            memory.BindBuffer(*(BufferVal*)memoryBindingDescs[i].buffer);
        }
    }

    return result;
}

Result DeviceVal::BindTextureMemory(const TextureMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum)
{
    RETURN_ON_FAILURE(GetLog(), memoryBindingDescs != nullptr || memoryBindingDescNum == 0, Result::INVALID_ARGUMENT,
        "Can't bind memory to textures: 'memoryBindingDescs' is a NULL.");

    TextureMemoryBindingDesc* memoryBindingDescsImpl = STACK_ALLOC(TextureMemoryBindingDesc, memoryBindingDescNum);

    for (uint32_t i = 0; i < memoryBindingDescNum; i++)
    {
        TextureMemoryBindingDesc& destDesc = memoryBindingDescsImpl[i];
        const TextureMemoryBindingDesc& srcDesc = memoryBindingDescs[i];

        RETURN_ON_FAILURE(GetLog(), srcDesc.texture != nullptr, Result::INVALID_ARGUMENT,
            "Can't bind memory to textures: 'memoryBindingDescs[%u].texture' is invalid.", i);
        RETURN_ON_FAILURE(GetLog(), srcDesc.memory != nullptr, Result::INVALID_ARGUMENT,
            "Can't bind memory to textures: 'memoryBindingDescs[%u].memory' is invalid.", i);

        MemoryVal& memory = (MemoryVal&)*srcDesc.memory;
        TextureVal& texture = (TextureVal&)*srcDesc.texture;

        RETURN_ON_FAILURE(GetLog(), !texture.IsBoundToMemory(), Result::INVALID_ARGUMENT,
            "Can't bind memory to textures: 'memoryBindingDescs[%u].texture' is already bound to memory.", i);

        // Skip validation if memory has been created from GAPI object using a wrapper extension
        if (memory.GetMemoryLocation() == MemoryLocation::MAX_NUM)
            continue;

        MemoryDesc memoryDesc = {};
        texture.GetMemoryInfo(memory.GetMemoryLocation(), memoryDesc);

        RETURN_ON_FAILURE(GetLog(), !memoryDesc.mustBeDedicated || srcDesc.offset == 0, Result::INVALID_ARGUMENT,
            "Can't bind memory to textures: 'memoryBindingDescs[%u].offset' must be zero for dedicated allocation.", i);

        RETURN_ON_FAILURE(GetLog(), memoryDesc.alignment != 0, Result::INVALID_ARGUMENT,
            "Can't bind memory to textures: 'memoryBindingDescs[%u].alignment' can't be zero.", i);

        RETURN_ON_FAILURE(GetLog(), srcDesc.offset % memoryDesc.alignment == 0, Result::INVALID_ARGUMENT,
            "Can't bind memory to textures: 'memoryBindingDescs[%u].offset' is misaligned.", i);

        const uint64_t rangeMax = srcDesc.offset + memoryDesc.size;
        const bool memorySizeIsUnknown = memory.GetSize() == 0;

        RETURN_ON_FAILURE(GetLog(), memorySizeIsUnknown || rangeMax <= memory.GetSize(), Result::INVALID_ARGUMENT,
            "Can't bind memory to textures: 'memoryBindingDescs[%u].offset' is invalid.", i);

        destDesc = srcDesc;
        destDesc.memory = &memory.GetImpl();
        destDesc.texture = &texture.GetImpl();
    }

    const Result result = m_CoreAPI.BindTextureMemory(m_Device, memoryBindingDescsImpl, memoryBindingDescNum);

    if (result == Result::SUCCESS)
    {
        for (uint32_t i = 0; i < memoryBindingDescNum; i++)
        {
            MemoryVal& memory = *(MemoryVal*)memoryBindingDescs[i].memory;
            memory.BindTexture(*(TextureVal*)memoryBindingDescs[i].texture);
        }
    }

    return result;
}

void DeviceVal::FreeMemory(Memory& memory)
{
    MemoryVal& memoryVal = (MemoryVal&)memory;

    if (memoryVal.HasBoundResources())
    {
        memoryVal.ReportBoundResources();
        REPORT_ERROR(GetLog(), "Can't free Memory: some resources are still bound to the memory.");
        return;
    }

    m_CoreAPI.FreeMemory(*NRI_GET_IMPL(Memory, &memory));
    Deallocate(GetStdAllocator(), (MemoryVal*)&memory);
}

FormatSupportBits DeviceVal::GetFormatSupport(Format format) const
{
    return m_CoreAPI.GetFormatSupport(m_Device, format);
}

VkDevice DeviceVal::GetDeviceVK() const
{
    return m_WrapperVKAPI.GetDeviceVK(m_Device);
}

VkPhysicalDevice DeviceVal::GetPhysicalDeviceVK() const
{
    return m_WrapperVKAPI.GetPhysicalDeviceVK(m_Device);
}

VkInstance DeviceVal::GetInstanceVK() const
{
    return m_WrapperVKAPI.GetInstanceVK(m_Device);
}

Result DeviceVal::CreateCommandQueueVK(const CommandQueueVulkanDesc& commandQueueVulkanDesc, CommandQueue*& commandQueue)
{
    RETURN_ON_FAILURE(GetLog(), commandQueueVulkanDesc.vkQueue != nullptr, Result::INVALID_ARGUMENT,
        "Can't create CommandQueue: 'commandQueueVulkanDesc.vkQueue' is invalid.");

    RETURN_ON_FAILURE(GetLog(), commandQueueVulkanDesc.commandQueueType < CommandQueueType::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create CommandQueue: 'commandQueueVulkanDesc.commandQueueType' is invalid.");

    CommandQueue* commandQueueImpl = nullptr;
    const Result result = m_WrapperVKAPI.CreateCommandQueueVK(m_Device, commandQueueVulkanDesc, commandQueueImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), commandQueueImpl != nullptr, Result::FAILURE,
            "Can't create CommandQueue: unexpected error.");

        commandQueue = (CommandQueue*)Allocate<CommandQueueVal>(GetStdAllocator(), *this, *commandQueueImpl);
    }

    return result;
}

Result DeviceVal::CreateCommandAllocatorVK(const CommandAllocatorVulkanDesc& commandAllocatorVulkanDesc, CommandAllocator*& commandAllocator)
{
    RETURN_ON_FAILURE(GetLog(), commandAllocatorVulkanDesc.vkCommandPool != nullptr, Result::INVALID_ARGUMENT,
        "Can't create CommandAllocator: 'commandAllocatorVulkanDesc.vkCommandPool' is invalid.");

    RETURN_ON_FAILURE(GetLog(), commandAllocatorVulkanDesc.commandQueueType < CommandQueueType::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create CommandAllocator: 'commandAllocatorVulkanDesc.commandQueueType' is invalid.");

    CommandAllocator* commandAllocatorImpl = nullptr;
    const Result result = m_WrapperVKAPI.CreateCommandAllocatorVK(m_Device, commandAllocatorVulkanDesc, commandAllocatorImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), commandAllocatorImpl != nullptr, Result::FAILURE,
            "Can't create CommandAllocator: unexpected error.");

        commandAllocator = (CommandAllocator*)Allocate<CommandAllocatorVal>(GetStdAllocator(), *this, *commandAllocatorImpl);
    }

    return result;
}

Result DeviceVal::CreateCommandBufferVK(const CommandBufferVulkanDesc& commandBufferVulkanDesc, CommandBuffer*& commandBuffer)
{
    RETURN_ON_FAILURE(GetLog(), commandBufferVulkanDesc.vkCommandBuffer != nullptr, Result::INVALID_ARGUMENT,
        "Can't create CommandBuffer: 'commandBufferVulkanDesc.vkCommandBuffer' is invalid.");

    RETURN_ON_FAILURE(GetLog(), commandBufferVulkanDesc.commandQueueType < CommandQueueType::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't create CommandBuffer: 'commandBufferVulkanDesc.commandQueueType' is invalid.");

    CommandBuffer* commandBufferImpl = nullptr;
    const Result result = m_WrapperVKAPI.CreateCommandBufferVK(m_Device, commandBufferVulkanDesc, commandBufferImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), commandBufferImpl != nullptr, Result::FAILURE,
            "Can't create CommandBuffer: unexpected error.");

        commandBuffer = (CommandBuffer*)Allocate<CommandBufferVal>(GetStdAllocator(), *this, *commandBufferImpl);
    }

    return result;
}

Result DeviceVal::CreateDescriptorPoolVK(VkDescriptorPool vkDescriptorPool, DescriptorPool*& descriptorPool)
{
    RETURN_ON_FAILURE(GetLog(), vkDescriptorPool != nullptr, Result::INVALID_ARGUMENT,
        "Can't create DescriptorPool: 'vkDescriptorPool' is invalid.");

    DescriptorPool* descriptorPoolImpl = nullptr;
    const Result result = m_WrapperVKAPI.CreateDescriptorPoolVK(m_Device, vkDescriptorPool, descriptorPoolImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), descriptorPoolImpl != nullptr, Result::FAILURE,
            "Can't create DescriptorPool: unexpected error.");

        descriptorPool = (DescriptorPool*)Allocate<DescriptorPoolVal>(GetStdAllocator(), *this, *descriptorPoolImpl);
    }

    return result;
}

Result DeviceVal::CreateBufferVK(const BufferVulkanDesc& bufferDesc, Buffer*& buffer)
{
    RETURN_ON_FAILURE(GetLog(), IsPhysicalDeviceMaskValid(bufferDesc.physicalDeviceMask), Result::INVALID_ARGUMENT,
        "Can't create Buffer: 'bufferDesc.physicalDeviceMask' is invalid.");

    RETURN_ON_FAILURE(GetLog(), bufferDesc.vkBuffer != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Buffer: 'bufferDesc.vkBuffer' is invalid.");

    RETURN_ON_FAILURE(GetLog(), bufferDesc.memory != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Buffer: 'bufferDesc.memory' is invalid.");

    RETURN_ON_FAILURE(GetLog(), bufferDesc.bufferSize > 0, Result::INVALID_ARGUMENT,
        "Can't create Buffer: 'bufferDesc.bufferSize' is 0.");

    Buffer* bufferImpl = nullptr;
    const Result result = m_WrapperVKAPI.CreateBufferVK(m_Device, bufferDesc, bufferImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), bufferImpl != nullptr, Result::FAILURE,
            "Can't create Buffer: unexpected error.");

        buffer = (Buffer*)Allocate<BufferVal>(GetStdAllocator(), *this, *bufferImpl, bufferDesc);
    }

    return result;
}

Result DeviceVal::CreateTextureVK(const TextureVulkanDesc& textureVulkanDesc, Texture*& texture)
{
    RETURN_ON_FAILURE(GetLog(), IsPhysicalDeviceMaskValid(textureVulkanDesc.physicalDeviceMask), Result::INVALID_ARGUMENT,
        "Can't create Texture: 'textureVulkanDesc.physicalDeviceMask' is invalid.");

    RETURN_ON_FAILURE(GetLog(), textureVulkanDesc.vkImage != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Texture: 'textureVulkanDesc.vkImage' is invalid.");

    RETURN_ON_FAILURE(GetLog(), GetFormatVK(textureVulkanDesc.vkFormat) != Format::UNKNOWN, Result::INVALID_ARGUMENT,
        "Can't create Texture: 'textureVulkanDesc.sampleNum' is 0.");

    RETURN_ON_FAILURE(GetLog(), textureVulkanDesc.sampleNum > 0, Result::INVALID_ARGUMENT,
        "Can't create Texture: 'textureVulkanDesc.sampleNum' is 0.");

    RETURN_ON_FAILURE(GetLog(), textureVulkanDesc.arraySize > 0, Result::INVALID_ARGUMENT,
        "Can't create Texture: 'textureVulkanDesc.arraySize' is 0.");

    RETURN_ON_FAILURE(GetLog(), textureVulkanDesc.mipNum > 0, Result::INVALID_ARGUMENT,
        "Can't create Texture: 'textureVulkanDesc.mipNum' is 0.");

    Texture* textureImpl = nullptr;
    const Result result = m_WrapperVKAPI.CreateTextureVK(m_Device, textureVulkanDesc, textureImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), textureImpl != nullptr, Result::FAILURE,
            "Can't create Texture: unexpected error.");

        texture = (Texture*)Allocate<TextureVal>(GetStdAllocator(), *this, *textureImpl, textureVulkanDesc);
    }

    return result;
}

Result DeviceVal::CreateMemoryVK(const MemoryVulkanDesc& memoryVulkanDesc, Memory*& memory)
{
    RETURN_ON_FAILURE(GetLog(), IsPhysicalDeviceMaskValid(memoryVulkanDesc.physicalDeviceMask), Result::INVALID_ARGUMENT,
        "Can't create Memory: 'memoryVulkanDesc.physicalDeviceMask' is invalid.");

    RETURN_ON_FAILURE(GetLog(), memoryVulkanDesc.vkDeviceMemory != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Memory: 'memoryVulkanDesc.vkDeviceMemory' is invalid.");

    RETURN_ON_FAILURE(GetLog(), memoryVulkanDesc.size > 0, Result::INVALID_ARGUMENT,
        "Can't create Memory: 'memoryVulkanDesc.size' is 0.");

    Memory* memoryImpl = nullptr;
    const Result result = m_WrapperVKAPI.CreateMemoryVK(m_Device, memoryVulkanDesc, memoryImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), memoryImpl != nullptr, Result::FAILURE,
            "Can't create Memory: unexpected error.");

        memory = (Memory*)Allocate<MemoryVal>(GetStdAllocator(), *this, *memoryImpl, memoryVulkanDesc.size, MemoryLocation::MAX_NUM);
    }

    return result;
}

Result DeviceVal::CreateGraphicsPipelineVK(VkPipeline vkPipeline, Pipeline*& pipeline)
{
    RETURN_ON_FAILURE(GetLog(), vkPipeline != 0, Result::INVALID_ARGUMENT,
        "Can't create graphics Pipeline: 'vkPipeline' is invalid.");

    Pipeline* pipelineImpl = nullptr;
    const Result result = m_WrapperVKAPI.CreateGraphicsPipelineVK(m_Device, vkPipeline, pipelineImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), pipelineImpl != nullptr, Result::FAILURE,
            "Can't create Pipeline: unexpected error.");

        pipeline = (Pipeline*)Allocate<PipelineVal>(GetStdAllocator(), *this, *pipelineImpl);
    }

    return result;
}

Result DeviceVal::CreateComputePipelineVK(VkPipeline vkPipeline, Pipeline*& pipeline)
{
    RETURN_ON_FAILURE(GetLog(), vkPipeline != 0, Result::INVALID_ARGUMENT,
        "Can't create compute Pipeline: 'vkPipeline' is invalid.");

    Pipeline* pipelineImpl = nullptr;
    const Result result = m_WrapperVKAPI.CreateComputePipelineVK(m_Device, vkPipeline, pipelineImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), pipelineImpl != nullptr, Result::FAILURE,
            "Can't create Pipeline: unexpected error.");

        pipeline = (Pipeline*)Allocate<PipelineVal>(GetStdAllocator(), *this, *pipelineImpl);
    }

    return result;
}

Result DeviceVal::CreateQueryPoolVK(const QueryPoolVulkanDesc& queryPoolVulkanDesc, QueryPool*& queryPool)
{
    RETURN_ON_FAILURE(GetLog(), IsPhysicalDeviceMaskValid(queryPoolVulkanDesc.physicalDeviceMask), Result::INVALID_ARGUMENT,
        "Can't create QueryPool: 'queryPoolVulkanDesc.physicalDeviceMask' is invalid.");

    RETURN_ON_FAILURE(GetLog(), queryPoolVulkanDesc.vkQueryPool != nullptr, Result::INVALID_ARGUMENT,
        "Can't create QueryPool: 'queryPoolVulkanDesc.vkQueryPool' is invalid.");

    QueryPool* queryPoolImpl = nullptr;
    const Result result = m_WrapperVKAPI.CreateQueryPoolVK(m_Device, queryPoolVulkanDesc, queryPoolImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), queryPoolImpl != nullptr, Result::FAILURE,
            "Can't create QueryPool: unexpected error.");

        const QueryType queryType = GetQueryTypeVK(queryPoolVulkanDesc.vkQueryType);

        queryPool = (QueryPool*)Allocate<QueryPoolVal>(GetStdAllocator(), *this, *queryPoolImpl, queryType);
    }

    return result;
}

Result DeviceVal::CreateQueueSemaphoreVK(VkSemaphore vkSemaphore, QueueSemaphore*& queueSemaphore)
{
    RETURN_ON_FAILURE(GetLog(), vkSemaphore != nullptr, Result::INVALID_ARGUMENT,
        "Can't create QueueSemaphore: 'vkSemaphore' is invalid.");

    QueueSemaphore* queueSemaphoreImpl = nullptr;
    const Result result = m_WrapperVKAPI.CreateQueueSemaphoreVK(m_Device, vkSemaphore, queueSemaphoreImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), queueSemaphoreImpl != nullptr, Result::FAILURE,
            "Can't create QueueSemaphore: unexpected error.");

        queueSemaphore = (QueueSemaphore*)Allocate<QueueSemaphoreVal>(GetStdAllocator(), *this, *queueSemaphoreImpl);
    }

    return result;
}

Result DeviceVal::CreateDeviceSemaphoreVK(VkFence vkFence, DeviceSemaphore*& deviceSemaphore)
{
    RETURN_ON_FAILURE(GetLog(), vkFence != nullptr, Result::INVALID_ARGUMENT,
        "Can't create DeviceSemaphore: 'vkFence' is invalid.");

    DeviceSemaphore* deviceSemaphoreImpl = nullptr;
    const Result result = m_WrapperVKAPI.CreateDeviceSemaphoreVK(m_Device, vkFence, deviceSemaphoreImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), deviceSemaphoreImpl != nullptr, Result::FAILURE,
            "Can't create DeviceSemaphore: unexpected error.");

        deviceSemaphore = (DeviceSemaphore*)Allocate<DeviceSemaphoreVal>(GetStdAllocator(), *this, *deviceSemaphoreImpl);
    }

    return result;
}

Result DeviceVal::CreateCommandBufferD3D11(const CommandBufferD3D11Desc& commandBufferDesc, CommandBuffer*& commandBuffer)
{
    RETURN_ON_FAILURE(GetLog(), commandBufferDesc.d3d11DeviceContext != nullptr, Result::INVALID_ARGUMENT,
        "Can't create CommandBuffer: 'commandBufferDesc.d3d11DeviceContext' is invalid.");

    CommandBuffer* commandBufferImpl = nullptr;
    const Result result = m_WrapperD3D11API.CreateCommandBufferD3D11(m_Device, commandBufferDesc, commandBufferImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), commandBufferImpl != nullptr, Result::FAILURE,
            "Can't create CommandBuffer: unexpected error.");

        commandBuffer = (CommandBuffer*)Allocate<CommandBufferVal>(GetStdAllocator(), *this, *commandBufferImpl);
    }

    return result;
}

Result DeviceVal::CreateBufferD3D11(const BufferD3D11Desc& bufferDesc, Buffer*& buffer)
{
    RETURN_ON_FAILURE(GetLog(), bufferDesc.d3d11Resource != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Buffer: 'bufferDesc.d3d11Resource' is invalid.");

    Buffer* bufferImpl = nullptr;
    const Result result = m_WrapperD3D11API.CreateBufferD3D11(m_Device, bufferDesc, bufferImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), bufferImpl != nullptr, Result::FAILURE,
            "Can't create Buffer: unexpected error.");

        buffer = (Buffer*)Allocate<BufferVal>(GetStdAllocator(), *this, *bufferImpl, bufferDesc);
    }

    return result;
}

Result DeviceVal::CreateTextureD3D11(const TextureD3D11Desc& textureDesc, Texture*& texture)
{
    RETURN_ON_FAILURE(GetLog(), textureDesc.d3d11Resource != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Texture: 'textureDesc.d3d11Resource' is invalid.");

    Texture* textureImpl = nullptr;
    const Result result = m_WrapperD3D11API.CreateTextureD3D11(m_Device, textureDesc, textureImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), textureImpl != nullptr, Result::FAILURE,
            "Can't create Texture: unexpected error.");

        texture = (Texture*)Allocate<TextureVal>(GetStdAllocator(), *this, *textureImpl, textureDesc);
    }

    return result;
}

ID3D11Device* DeviceVal::GetDeviceD3D11()
{
    return m_WrapperD3D11API.GetDeviceD3D11(m_Device);
}

Result DeviceVal::CreateCommandBufferD3D12(const CommandBufferD3D12Desc& commandBufferDesc, CommandBuffer*& commandBuffer)
{
    RETURN_ON_FAILURE(GetLog(), commandBufferDesc.d3d12CommandAllocator != nullptr, Result::INVALID_ARGUMENT,
        "Can't create CommandBuffer: 'commandBufferDesc.d3d12CommandAllocator' is invalid.");

    RETURN_ON_FAILURE(GetLog(), commandBufferDesc.d3d12CommandList != nullptr, Result::INVALID_ARGUMENT,
        "Can't create CommandBuffer: 'commandBufferDesc.d3d12CommandList' is invalid.");

    CommandBuffer* commandBufferImpl = nullptr;
    const Result result = m_WrapperD3D12API.CreateCommandBufferD3D12(m_Device, commandBufferDesc, commandBufferImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), commandBufferImpl != nullptr, Result::FAILURE,
            "Can't create CommandBuffer: unexpected error.");

        commandBuffer = (CommandBuffer*)Allocate<CommandBufferVal>(GetStdAllocator(), *this, *commandBufferImpl);
    }

    return result;
}

Result DeviceVal::CreateBufferD3D12(const BufferD3D12Desc& bufferDesc, Buffer*& buffer)
{
    RETURN_ON_FAILURE(GetLog(), bufferDesc.d3d12Resource != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Buffer: 'bufferDesc.d3d12Resource' is invalid.");

    Buffer* bufferImpl = nullptr;
    const Result result = m_WrapperD3D12API.CreateBufferD3D12(m_Device, bufferDesc, bufferImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), bufferImpl != nullptr, Result::FAILURE,
            "Can't create Buffer: unexpected error.");

        buffer = (Buffer*)Allocate<BufferVal>(GetStdAllocator(), *this, *bufferImpl, bufferDesc);
    }

    return result;
}

Result DeviceVal::CreateTextureD3D12(const TextureD3D12Desc& textureDesc, Texture*& texture)
{
    RETURN_ON_FAILURE(GetLog(), textureDesc.d3d12Resource != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Texture: 'textureDesc.d3d12Resource' is invalid.");

    Texture* textureImpl = nullptr;
    const Result result = m_WrapperD3D12API.CreateTextureD3D12(m_Device, textureDesc, textureImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), textureImpl != nullptr, Result::FAILURE,
            "Can't create Texture: unexpected error.");

        texture = (Texture*)Allocate<TextureVal>(GetStdAllocator(), *this, *textureImpl, textureDesc);
    }

    return result;
}

Result DeviceVal::CreateMemoryD3D12(const MemoryD3D12Desc& memoryDesc, Memory*& memory)
{
    RETURN_ON_FAILURE(GetLog(), memoryDesc.d3d12Heap != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Memory: 'memoryDesc.d3d12Heap' is invalid.");

    Memory* memoryImpl = nullptr;
    const Result result = m_WrapperD3D12API.CreateMemoryD3D12(m_Device, memoryDesc, memoryImpl);

    const uint64_t size = GetMemorySizeD3D12(memoryDesc);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), memoryImpl != nullptr, Result::FAILURE,
            "Can't create Memory: unexpected error.");

        memory = (Memory*)Allocate<MemoryVal>(GetStdAllocator(), *this, *memoryImpl, size, MemoryLocation::MAX_NUM);
    }

    return result;
}

ID3D12Device* DeviceVal::GetDeviceD3D12()
{
    return m_WrapperD3D12API.GetDeviceD3D12(m_Device);
}

uint32_t DeviceVal::CalculateAllocationNumber(const ResourceGroupDesc& resourceGroupDesc) const
{
    RETURN_ON_FAILURE(GetLog(), resourceGroupDesc.memoryLocation < MemoryLocation::MAX_NUM, 0,
        "Can't calculate the number of allocations: 'resourceGroupDesc.memoryLocation' is invalid.");

    RETURN_ON_FAILURE(GetLog(), resourceGroupDesc.bufferNum == 0 || resourceGroupDesc.buffers != nullptr, 0,
        "Can't calculate the number of allocations: 'resourceGroupDesc.buffers' is invalid.");

    RETURN_ON_FAILURE(GetLog(), resourceGroupDesc.textureNum == 0 || resourceGroupDesc.textures != nullptr, 0,
        "Can't calculate the number of allocations: 'resourceGroupDesc.textures' is invalid.");

    Buffer** buffersImpl = STACK_ALLOC(Buffer*, resourceGroupDesc.bufferNum);

    for (uint32_t i = 0; i < resourceGroupDesc.bufferNum; i++)
    {
        RETURN_ON_FAILURE(GetLog(), resourceGroupDesc.buffers[i] != nullptr, 0,
            "Can't calculate the number of allocations: 'resourceGroupDesc.buffers[%u]' is invalid.", i);

        BufferVal& bufferVal = *(BufferVal*)resourceGroupDesc.buffers[i];
        buffersImpl[i] = &(bufferVal.GetImpl());
    }

    Texture** texturesImpl = STACK_ALLOC(Texture*, resourceGroupDesc.textureNum);

    for (uint32_t i = 0; i < resourceGroupDesc.textureNum; i++)
    {
        RETURN_ON_FAILURE(GetLog(), resourceGroupDesc.textures[i] != nullptr, 0,
            "Can't calculate the number of allocations: 'resourceGroupDesc.textures[%u]' is invalid.", i);

        TextureVal& textureVal = *(TextureVal*)resourceGroupDesc.textures[i];
        texturesImpl[i] = &(textureVal.GetImpl());
    }

    ResourceGroupDesc resourceGroupDescImpl = resourceGroupDesc;
    resourceGroupDescImpl.buffers = buffersImpl;
    resourceGroupDescImpl.textures = texturesImpl;

    return m_HelperAPI.CalculateAllocationNumber(m_Device, resourceGroupDescImpl);
}

Result DeviceVal::AllocateAndBindMemory(const ResourceGroupDesc& resourceGroupDesc, Memory** allocations)
{
    RETURN_ON_FAILURE(GetLog(), allocations != nullptr, Result::INVALID_ARGUMENT,
        "Can't allocate and bind memory: 'allocations' is invalid.");

    RETURN_ON_FAILURE(GetLog(), resourceGroupDesc.memoryLocation < MemoryLocation::MAX_NUM, Result::INVALID_ARGUMENT,
        "Can't allocate and bind memory: 'resourceGroupDesc.memoryLocation' is invalid.");

    RETURN_ON_FAILURE(GetLog(), resourceGroupDesc.bufferNum == 0 || resourceGroupDesc.buffers != nullptr, Result::INVALID_ARGUMENT,
        "Can't allocate and bind memory: 'resourceGroupDesc.buffers' is invalid.");

    RETURN_ON_FAILURE(GetLog(), resourceGroupDesc.textureNum == 0 || resourceGroupDesc.textures != nullptr, Result::INVALID_ARGUMENT,
        "Can't allocate and bind memory: 'resourceGroupDesc.textures' is invalid.");

    Buffer** buffersImpl = STACK_ALLOC(Buffer*, resourceGroupDesc.bufferNum);

    for (uint32_t i = 0; i < resourceGroupDesc.bufferNum; i++)
    {
        RETURN_ON_FAILURE(GetLog(), resourceGroupDesc.buffers[i] != nullptr, Result::INVALID_ARGUMENT,
            "Can't allocate and bind memory: 'resourceGroupDesc.buffers[%u]' is invalid.", i);

        BufferVal& bufferVal = *(BufferVal*)resourceGroupDesc.buffers[i];
        buffersImpl[i] = &(bufferVal.GetImpl());
    }

    Texture** texturesImpl = STACK_ALLOC(Texture*, resourceGroupDesc.textureNum);

    for (uint32_t i = 0; i < resourceGroupDesc.textureNum; i++)
    {
        RETURN_ON_FAILURE(GetLog(), resourceGroupDesc.textures[i] != nullptr, Result::INVALID_ARGUMENT,
            "Can't allocate and bind memory: 'resourceGroupDesc.textures[%u]' is invalid.", i);

        TextureVal& textureVal = *(TextureVal*)resourceGroupDesc.textures[i];
        texturesImpl[i] = &(textureVal.GetImpl());
    }

    const size_t allocationNum = CalculateAllocationNumber(resourceGroupDesc);

    ResourceGroupDesc resourceGroupDescImpl = resourceGroupDesc;
    resourceGroupDescImpl.buffers = buffersImpl;
    resourceGroupDescImpl.textures = texturesImpl;

    const Result result = m_HelperAPI.AllocateAndBindMemory(m_Device, resourceGroupDescImpl, allocations);

    if (result == Result::SUCCESS)
    {
        for (uint32_t i = 0; i < resourceGroupDesc.bufferNum; i++)
        {
            BufferVal& bufferVal = *(BufferVal*)resourceGroupDesc.buffers[i];
            bufferVal.SetBoundToMemory();
        }

        for (uint32_t i = 0; i < resourceGroupDesc.textureNum; i++)
        {
            TextureVal& textureVal = *(TextureVal*)resourceGroupDesc.textures[i];
            textureVal.SetBoundToMemory();
        }

        for (uint32_t i = 0; i < allocationNum; i++)
        {
            RETURN_ON_FAILURE(GetLog(), allocations[i] != nullptr, Result::FAILURE, "Unexpected error: 'memoryImpl' is invalid");
            allocations[i] = (Memory*)Allocate<MemoryVal>(GetStdAllocator(), *this, *allocations[i], 0, resourceGroupDesc.memoryLocation);
        }
    }

    return result;
}

Result DeviceVal::CreateRayTracingPipeline(const RayTracingPipelineDesc& pipelineDesc, Pipeline*& pipeline)
{
    RETURN_ON_FAILURE(GetLog(), pipelineDesc.pipelineLayout != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Pipeline: 'pipelineDesc.pipelineLayout' is invalid.");

    RETURN_ON_FAILURE(GetLog(), pipelineDesc.shaderLibrary != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Pipeline: 'pipelineDesc.shaderLibrary' is invalid.");

    RETURN_ON_FAILURE(GetLog(), pipelineDesc.shaderGroupDescs != nullptr, Result::INVALID_ARGUMENT,
        "Can't create Pipeline: 'pipelineDesc.shaderGroupDescs' is invalid.");

    RETURN_ON_FAILURE(GetLog(), pipelineDesc.shaderGroupDescNum != 0, Result::INVALID_ARGUMENT,
        "Can't create Pipeline: 'pipelineDesc.shaderGroupDescNum' is 0.");

    RETURN_ON_FAILURE(GetLog(), pipelineDesc.recursionDepthMax != 0, Result::INVALID_ARGUMENT,
        "Can't create Pipeline: 'pipelineDesc.recursionDepthMax' is 0.");

    for (uint32_t i = 0; i < pipelineDesc.shaderLibrary->shaderNum; i++)
    {
        const ShaderDesc& shaderDesc = pipelineDesc.shaderLibrary->shaderDescs[i];

        RETURN_ON_FAILURE(GetLog(), shaderDesc.bytecode != nullptr, Result::INVALID_ARGUMENT,
            "Can't create Pipeline: 'pipelineDesc.shaderLibrary->shaderDescs[%u].bytecode' is invalid.", i);

        RETURN_ON_FAILURE(GetLog(), shaderDesc.size != 0, Result::INVALID_ARGUMENT,
            "Can't create Pipeline: 'pipelineDesc.shaderLibrary->shaderDescs[%u].size' is 0.", i);

        RETURN_ON_FAILURE(GetLog(), shaderDesc.stage > ShaderStage::COMPUTE && shaderDesc.stage < ShaderStage::MAX_NUM, Result::INVALID_ARGUMENT,
            "Can't create Pipeline: 'pipelineDesc.shaderLibrary->shaderDescs[%u].stage' is invalid.", i);
    }

    auto pipelineDescImpl = pipelineDesc;
    pipelineDescImpl.pipelineLayout = NRI_GET_IMPL(PipelineLayout, pipelineDesc.pipelineLayout);

    Pipeline* pipelineImpl = nullptr;
    const Result result = m_RayTracingAPI.CreateRayTracingPipeline(m_Device, pipelineDescImpl, pipelineImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), pipelineImpl != nullptr, Result::FAILURE, "Unexpected error: 'pipelineImpl' is NULL.");
        pipeline = (Pipeline*)Allocate<PipelineVal>(GetStdAllocator(), *this, *pipelineImpl);
    }

    return result;
}

Result DeviceVal::CreateAccelerationStructure(const AccelerationStructureDesc& accelerationStructureDesc, AccelerationStructure*& accelerationStructure)
{
    RETURN_ON_FAILURE(GetLog(), IsPhysicalDeviceMaskValid(accelerationStructureDesc.physicalDeviceMask), Result::INVALID_ARGUMENT,
        "Can't create AccelerationStructure: 'accelerationStructureDesc.physicalDeviceMask' is invalid.");

    RETURN_ON_FAILURE(GetLog(), accelerationStructureDesc.instanceOrGeometryObjectNum != 0, Result::INVALID_ARGUMENT,
        "Can't create AccelerationStructure: 'accelerationStructureDesc.instanceOrGeometryObjectNum' is 0.");

    AccelerationStructureDesc accelerationStructureDescImpl = accelerationStructureDesc;

    Vector<GeometryObject> objectImplArray(GetStdAllocator());
    if (accelerationStructureDesc.type == AccelerationStructureType::BOTTOM_LEVEL)
    {
        const uint32_t geometryObjectNum = accelerationStructureDesc.instanceOrGeometryObjectNum;
        objectImplArray.resize(geometryObjectNum);
        ConvertGeometryObjectsVal(objectImplArray.data(), accelerationStructureDesc.geometryObjects, geometryObjectNum);
        accelerationStructureDescImpl.geometryObjects = objectImplArray.data();
    }

    AccelerationStructure* accelerationStructureImpl = nullptr;
    const Result result = m_RayTracingAPI.CreateAccelerationStructure(m_Device, accelerationStructureDescImpl, accelerationStructureImpl);

    if (result == Result::SUCCESS)
    {
        RETURN_ON_FAILURE(GetLog(), accelerationStructureImpl != nullptr, Result::FAILURE, "Unexpected error: 'accelerationStructureImpl' is NULL.");
        accelerationStructure = (AccelerationStructure*)Allocate<AccelerationStructureVal>(GetStdAllocator(), *this, *accelerationStructureImpl);
    }

    return result;
}

Result DeviceVal::BindAccelerationStructureMemory(const AccelerationStructureMemoryBindingDesc* memoryBindingDescs, uint32_t memoryBindingDescNum)
{
    if (memoryBindingDescNum == 0)
        return Result::SUCCESS;

    RETURN_ON_FAILURE(GetLog(), memoryBindingDescs != nullptr, Result::INVALID_ARGUMENT,
        "Can't bind memory to acceleration structures: 'memoryBindingDescs' is invalid.");

    AccelerationStructureMemoryBindingDesc* memoryBindingDescsImpl = STACK_ALLOC(AccelerationStructureMemoryBindingDesc, memoryBindingDescNum);
    for (uint32_t i = 0; i < memoryBindingDescNum; i++)
    {
        AccelerationStructureMemoryBindingDesc& destDesc = memoryBindingDescsImpl[i];
        const AccelerationStructureMemoryBindingDesc& srcDesc = memoryBindingDescs[i];

        MemoryVal& memory = (MemoryVal&)*srcDesc.memory;
        AccelerationStructureVal& accelerationStructure = (AccelerationStructureVal&)*srcDesc.accelerationStructure;

        RETURN_ON_FAILURE(GetLog(), !accelerationStructure.IsBoundToMemory(), Result::INVALID_ARGUMENT,
            "Can't bind memory to acceleration structures: 'memoryBindingDescs[%u].accelerationStructure' is already bound to memory.", i);

        MemoryDesc memoryDesc = {};
        accelerationStructure.GetMemoryInfo(memoryDesc);

        RETURN_ON_FAILURE(GetLog(), !memoryDesc.mustBeDedicated || srcDesc.offset == 0, Result::INVALID_ARGUMENT,
            "Can't bind memory to acceleration structures: 'memoryBindingDescs[%u].offset' must be zero for dedicated allocation.", i);

        RETURN_ON_FAILURE(GetLog(), memoryDesc.alignment != 0, Result::INVALID_ARGUMENT,
            "Can't bind memory to acceleration structures: 'memoryBindingDescs[%u].alignment' can't be zero.", i);

        RETURN_ON_FAILURE(GetLog(), srcDesc.offset % memoryDesc.alignment == 0, Result::INVALID_ARGUMENT,
            "Can't bind memory to acceleration structures: 'memoryBindingDescs[%u].offset' is misaligned.", i);

        const uint64_t rangeMax = srcDesc.offset + memoryDesc.size;
        const bool memorySizeIsUnknown = memory.GetSize() == 0;

        RETURN_ON_FAILURE(GetLog(), memorySizeIsUnknown || rangeMax <= memory.GetSize(), Result::INVALID_ARGUMENT,
            "Can't bind memory to acceleration structures: 'memoryBindingDescs[%u].offset' is invalid.", i);

        destDesc = srcDesc;
        destDesc.memory = &memory.GetImpl();
        destDesc.accelerationStructure = &accelerationStructure.GetImpl();
    }

    const Result result = m_RayTracingAPI.BindAccelerationStructureMemory(m_Device, memoryBindingDescsImpl, memoryBindingDescNum);

    if (result == Result::SUCCESS)
    {
        for (uint32_t i = 0; i < memoryBindingDescNum; i++)
        {
            MemoryVal& memory = *(MemoryVal*)memoryBindingDescs[i].memory;
            memory.BindAccelerationStructure(*(AccelerationStructureVal*)memoryBindingDescs[i].accelerationStructure);
        }
    }

    return result;
}

void DeviceVal::DestroyAccelerationStructure(AccelerationStructure& accelerationStructure)
{
    Deallocate(GetStdAllocator(), (AccelerationStructureVal*)&accelerationStructure);
}

void DeviceVal::Destroy()
{
    Deallocate(GetStdAllocator(), this);
}

DeviceBase* CreateDeviceValidation(const DeviceCreationDesc& deviceCreationDesc, DeviceBase& device)
{
    Log log(deviceCreationDesc.graphicsAPI, deviceCreationDesc.callbackInterface);
    StdAllocator<uint8_t> allocator(deviceCreationDesc.memoryAllocatorInterface);

    uint32_t physicalDeviceNum = 1;
    if (deviceCreationDesc.physicalDeviceGroup != nullptr)
        physicalDeviceNum = deviceCreationDesc.physicalDeviceGroup->physicalDeviceGroupSize;

    DeviceVal* deviceVal = Allocate<DeviceVal>(allocator, log, allocator, device, physicalDeviceNum);

    if (!deviceVal->Create())
    {
        Deallocate(allocator, deviceVal);
        return nullptr;
    }

    return deviceVal;
}

#include "DeviceVal.hpp"
