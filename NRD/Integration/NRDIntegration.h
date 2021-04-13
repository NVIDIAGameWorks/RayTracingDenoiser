/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include "../Include/NRD.h"

#include <assert.h>
#include <array>
#include <vector>
#include <map>

struct NrdTexture
{
    nri::Texture* texture;
    nri::TextureTransitionBarrierDesc* states;
    nri::Format format;
};

constexpr uint32_t NRD_USER_POOL_SIZE = (uint32_t)nrd::ResourceType::MAX_NUM - 2;
typedef std::array<NrdTexture, NRD_USER_POOL_SIZE> NrdUserPool;

#define NRD_DEBUG_LOGGING 0
#define NRD_ASSERT(expr) (assert(expr))
#define NRD_ABORT_ON_FAILURE(result) if ((result) != nri::Result::SUCCESS) NRD_ASSERT(false)

class Nrd
{
public:
    Nrd(uint32_t bufferedFrameMaxNum) :
        m_BufferedFrameMaxNum(bufferedFrameMaxNum)
    {}

    ~Nrd()
    { NRD_ASSERT( m_NRI == nullptr ); }

    // There is not "Resize" functionallity, because NRD full recreation costs nothing. The main cost comes from render targets resizing which needs to be done in any case
    bool Initialize(nri::Device& nriDevice, const nri::CoreInterface& nriCoreInterface, const nri::HelperInterface& nriHelperInterface, const nrd::DenoiserCreationDesc& denoiserCreationDesc);
    void Destroy();

    void SetMethodSettings(nrd::Method method, const void* methodSettings);
    void Denoise(uint32_t consecutiveFrameIndex, nri::CommandBuffer& commandBuffer, const nrd::CommonSettings& commonSettings, const NrdUserPool& userPool);

    // Should not be called explicitly, unless you want to reload pipelines
    void CreatePipelines();

private:
    Nrd(const Nrd&) = delete;

    void CreateResources();
    void AllocateAndBindMemory();
    void Dispatch(nri::CommandBuffer& commandBuffer, nri::DescriptorPool& descriptorPool, const nrd::DispatchDesc& dispatchDesc, const NrdUserPool& userPool);

private:
    std::map<uint64_t, nri::Descriptor*> m_Descriptors;
    std::vector<NrdTexture> m_TexturePool;
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
