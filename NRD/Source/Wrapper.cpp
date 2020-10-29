/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.h"
#include "DenoiserImpl.h"
#include "Version.h"

#include <array>

using namespace nrd;

static const std::array<Method, 5> g_NrdSupportedMethods =
{
    Method::NRD_DIFFUSE,
    Method::NRD_SPECULAR,
    Method::NRD_SHADOW,
    Method::NRD_TRANSLUCENT_SHADOW,
    Method::SVGF,
};

static_assert( g_NrdSupportedMethods.size() == (uint32_t)Method::MAX_NUM );
static_assert( VERSION_MAJOR == NRD_VERSION_MAJOR, "VERSION_MAJOR & NRD_VERSION_MAJOR don't match!");
static_assert( VERSION_MINOR == NRD_VERSION_MINOR, "VERSION_MINOR & NRD_VERSION_MINOR don't match!");
static_assert( VERSION_BUILD == NRD_VERSION_BUILD, "VERSION_BUILD & NRD_VERSION_BUILD don't match!");

static const LibraryDesc g_NrdLibraryDesc =
{
    { 100, 200, 300, 400 }, // IMPORTANT: since NRD is compiled via "CompileHLSLToSPIRV" these should match the BAT file!
    g_NrdSupportedMethods.data(),
    (uint32_t)g_NrdSupportedMethods.size(),
    VERSION_MAJOR,
    VERSION_MINOR,
    VERSION_BUILD
};

NRD_API const LibraryDesc& NRD_CALL nrd::GetLibraryDesc()
{
    return g_NrdLibraryDesc;
}

NRD_API Result NRD_CALL nrd::CreateDenoiser(const DenoiserCreationDesc& denoiserCreationDesc, Denoiser*& denoiser)
{
    DenoiserCreationDesc modifiedDenoiserCreationDesc = denoiserCreationDesc;
    CheckAndSetDefaultAllocator(modifiedDenoiserCreationDesc.memoryAllocatorInterface);

    StdAllocator<uint8_t> memoryAllocator(modifiedDenoiserCreationDesc.memoryAllocatorInterface);

    DenoiserImpl* implementation = Allocate<DenoiserImpl>(memoryAllocator, memoryAllocator);
    const Result result = implementation->Create(modifiedDenoiserCreationDesc);

    if (result == Result::SUCCESS)
    {
        denoiser = (Denoiser*)implementation;
        return Result::SUCCESS;
    }

    Deallocate(memoryAllocator, implementation);
    return result;
}

NRD_API const DenoiserDesc& NRD_CALL nrd::GetDenoiserDesc(const Denoiser& denoiser)
{
    return ((const DenoiserImpl&)denoiser).GetDesc();
}

NRD_API Result NRD_CALL nrd::SetMethodSettings(Denoiser& denoiser, Method method, const void* methodSettings)
{
    return ((DenoiserImpl&)denoiser).SetMethodSettings(method, methodSettings);
}

NRD_API Result NRD_CALL nrd::GetComputeDispatches(Denoiser& denoiser, const CommonSettings& commonSettings, const DispatchDesc*& dispatchDescs, uint32_t& dispatchDescNum)
{
    return ((DenoiserImpl&)denoiser).GetComputeDispatches(commonSettings, dispatchDescs, dispatchDescNum);
}

NRD_API void NRD_CALL nrd::DestroyDenoiser(Denoiser& denoiser)
{
    StdAllocator<uint8_t> memoryAllocator = ((DenoiserImpl&)denoiser).GetStdAllocator();
    Deallocate(memoryAllocator, (DenoiserImpl*)&denoiser);
}
