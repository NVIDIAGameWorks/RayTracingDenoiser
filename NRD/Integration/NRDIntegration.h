/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include "../Include/NRD.h"

#include <array>
#include <map>

struct NrdTexture
{
    nri::Texture* texture;
    nri::TextureTransitionBarrierDesc* states;
    nri::Format format;
};

constexpr uint32_t NRD_USER_POOL_SIZE = (uint32_t)nrd::ResourceType::MAX_NUM - 2;
typedef std::array<NrdTexture, NRD_USER_POOL_SIZE> NrdUserPool;

class Nrd
{
public:
    Nrd()
    {}

    ~Nrd()
    {}

    bool Initialize(nri::Device& nriDevice, nri::CoreInterface& nriCoreInterface, const nrd::DenoiserCreationDesc& denoiserCreationDesc, bool ignoreNRIProvidedBindingOffsets = true);
    void SetMethodSettings(nrd::Method method, const void* methodSettings);
    void Denoise(nri::CommandBuffer& commandBuffer, const nrd::CommonSettings& commonSettings, const NrdUserPool& userPool);
    void Destroy();

    // Should not be called explicitly, unless you want to reload pipelines
    void CreatePipelines(bool ignoreNRIProvidedBindingOffsets = false);

private:
    void CreateResources();
    void Dispatch(nri::CommandBuffer& commandBuffer, nri::DescriptorPool& descriptorPool, const nrd::DispatchDesc& dispatchDesc, const NrdUserPool& userPool);

private:
    std::vector<NrdTexture> m_TexturePool;
    std::map<uint64_t, nri::Descriptor*> m_Descriptors;
    std::vector<nri::TextureTransitionBarrierDesc> m_ResourceState;
    std::vector<nri::PipelineLayout*> m_PipelineLayouts;
    std::vector<nri::Pipeline*> m_Pipelines;
    std::vector<nri::Memory*> m_Memories;
    std::array<nri::DescriptorPool*, BUFFERED_FRAME_MAX_NUM> m_DescriptorPools;
    nri::CoreInterface* m_NRI = nullptr;
    nri::Device* m_Device = nullptr;
    nri::Buffer* m_ConstantBuffer = nullptr;
    nri::Descriptor* m_ConstantBufferView = nullptr;
    nrd::Denoiser* m_Denoiser = nullptr;
    uint64_t m_ConstantBufferSize = 0;
    uint32_t m_ConstantBufferViewSize = 0;
    uint32_t m_ConstantBufferOffset = 0;
    bool m_IsShadersReloadRequested = false;
};

#define NRD_ASSERT(expr) (assert(expr))
#define NRD_ABORT_ON_FAILURE(result) \
    if ((result) != nri::Result::SUCCESS) \
        NRD_ASSERT(false);
