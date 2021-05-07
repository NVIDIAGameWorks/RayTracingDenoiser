/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

/*
CREDITS:
    Developed by:
        Library, REBLUR and SIGMA denoisers:
            Dmitry Zhdan (dzhdan@nvidia.com)

        ReLAX denoiser:
            Tim Cheblokov (ttcheblokov@nvidia.com)
            Pawel Kozlowski (pkozlowski@nvidia.com) 

    Special thanks:
        Evgeny Makarov (NVIDIA) - denoising ideas
        Ivan Fedorov (NVIDIA) - interface
        Ivan Povarov (NVIDIA) - QA, integrations and feedback
        Oles Shyshkovtsov (4A GAMES) - initial idea of recurrent blurring
*/

#pragma once

#include <cstdint>
#include <cstddef>

#define NRD_VERSION_MAJOR 2
#define NRD_VERSION_MINOR 1
#define NRD_VERSION_BUILD 1
#define NRD_VERSION_DATE "5 May 2021"

#if defined(_MSC_VER)
    #define NRD_CALL __fastcall
#elif !defined(__x86_64) && (defined(__GNUC__)  || defined (__clang__))
    #define NRD_CALL __attribute__((fastcall))
#else
    #define NRD_CALL 
#endif

#ifndef NRD_API
    #define NRD_API extern "C"
#endif

#include "NRDDescs.h"
#include "NRDSettings.h"

namespace nrd
{
    NRD_API const LibraryDesc& NRD_CALL GetLibraryDesc();
    NRD_API Result NRD_CALL CreateDenoiser(const DenoiserCreationDesc& denoiserCreationDesc, Denoiser*& denoiser);
    NRD_API const DenoiserDesc& NRD_CALL GetDenoiserDesc(const Denoiser& denoiser);
    NRD_API Result NRD_CALL SetMethodSettings(Denoiser& denoiser, Method method, const void* methodSettings);
    NRD_API Result NRD_CALL GetComputeDispatches(Denoiser& denoiser, const CommonSettings& commonSettings, const DispatchDesc*& dispatchDescs, uint32_t& dispatchDescNum);
    NRD_API void NRD_CALL DestroyDenoiser(Denoiser& denoiser);
}
