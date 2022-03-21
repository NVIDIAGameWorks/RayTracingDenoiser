/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

// IMPORTANT: these files must be included beforehand
//    NRD.h
//    NRIDescs.hpp
//    Extensions/NRIHelper.h

#include <array>
#include <vector>
#include <map>

#define NRD_INTEGRATION 1
#define NRD_INTEGRATION_MAJOR 2
#define NRD_INTEGRATION_MINOR 0
#define NRD_INTEGRATION_DATE "21 March 2022"

// Settings
#ifndef NRD_INTEGRATION_ASSERT
    #include <assert.h>
    #define NRD_INTEGRATION_ASSERT(expr, msg) (assert(expr && msg))
#endif

#define NRD_INTEGRATION_DEBUG_LOGGING 0
#define NRD_INTEGRATION_ABORT_ON_FAILURE(result) if ((result) != nri::Result::SUCCESS) NRD_INTEGRATION_ASSERT(false, "Abort on failure!")

// User inputs / outputs are not mipmapped, thus only 1 entry is needed.
// "TextureTransitionBarrierDesc::texture" is used to store the resource.
// "TextureTransitionBarrierDesc::next..." are used to represent current state of the subresource.
struct NrdIntegrationTexture
{
    nri::TextureTransitionBarrierDesc* subresourceStates;
    nri::Format format;
};

// Ensure to fill all slots even unused ones because the order is predefined
constexpr uint32_t NRD_USER_POOL_SIZE = (uint32_t)nrd::ResourceType::MAX_NUM - 2;
typedef std::array<NrdIntegrationTexture, NRD_USER_POOL_SIZE> NrdUserPool;

inline void NrdIntegration_SetResource(NrdUserPool& pool, nrd::ResourceType slot, const NrdIntegrationTexture& texture)
{
    NRD_INTEGRATION_ASSERT( texture.subresourceStates->texture != nullptr, "Invalid texture!" );
    NRD_INTEGRATION_ASSERT( texture.format != nri::Format::UNKNOWN, "Invalid format!" );

    pool[(size_t)slot] = texture;
}

class NrdIntegration
{
public:
    // The application must provide number of buffered frames, it's needed to guarantee that
    // constants data and descriptor sets are not overwritten while being executed on the GPU.
    // Usually it's 2-3 frames.
    NrdIntegration(uint32_t bufferedFrameMaxNum) :
        m_BufferedFrameMaxNum(bufferedFrameMaxNum)
    {}

    ~NrdIntegration()
    { NRD_INTEGRATION_ASSERT( m_NRI == nullptr, "m_NRI must be NULL at this point!" ); }

    // There is no "Resize" functionallity, because NRD full recreation costs nothing.
    // The main cost comes from render targets resizing, which needs to be done in any case
    // (call Destroy beforehand)
    bool Initialize(nri::Device& nriDevice, const nri::CoreInterface& nriCore,
        const nri::HelperInterface& nriHelper, const nrd::DenoiserCreationDesc& denoiserCreationDesc);

    // Just calls "nrd::SetMethodSettings"
    bool SetMethodSettings(nrd::Method method, const void* methodSettings);

    // User pool must contain valid entries only for resources which required for the set of
    // requested resources
    void Denoise(uint32_t consecutiveFrameIndex, nri::CommandBuffer& commandBuffer,
        const nrd::CommonSettings& commonSettings, const NrdUserPool& userPool);

    // This function assums that the device is in the IDLE state, i.e. there is no work in flight
    void Destroy();

    // Should not be called explicitly, unless you want to reload pipelines
    void CreatePipelines();

private:
    NrdIntegration(const NrdIntegration&) = delete;

    void CreateResources();
    void AllocateAndBindMemory();
    void Dispatch(nri::CommandBuffer& commandBuffer, nri::DescriptorPool& descriptorPool,
        const nrd::DispatchDesc& dispatchDesc, const NrdUserPool& userPool);

private:
    std::vector<NrdIntegrationTexture> m_TexturePool;
    std::map<uint64_t, nri::Descriptor*> m_Descriptors;
    std::vector<nri::TextureTransitionBarrierDesc> m_ResourceState;
    std::vector<nri::PipelineLayout*> m_PipelineLayouts;
    std::vector<nri::Pipeline*> m_Pipelines;
    std::vector<nri::Memory*> m_MemoryAllocations;
    std::array<nri::DescriptorPool*, 16> m_DescriptorPools = {};
    const nri::CoreInterface* m_NRI = nullptr;
    const nri::HelperInterface* m_NRIHelper = nullptr;
    nri::Device* m_Device = nullptr;
    nri::Buffer* m_ConstantBuffer = nullptr;
    nri::Descriptor* m_ConstantBufferView = nullptr;
    nrd::Denoiser* m_Denoiser = nullptr;
    uint64_t m_ConstantBufferSize = 0;
    uint32_t m_ConstantBufferViewSize = 0;
    uint32_t m_ConstantBufferOffset = 0;
    uint32_t m_BufferedFrameMaxNum = 0;
    bool m_IsShadersReloadRequested = false;
};
