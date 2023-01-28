/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRD.h"
#include "DenoiserImpl.h"
#include "../Resources/Version.h"

#include <array>

static_assert(VERSION_MAJOR == NRD_VERSION_MAJOR, "VERSION_MAJOR & NRD_VERSION_MAJOR don't match!");
static_assert(VERSION_MINOR == NRD_VERSION_MINOR, "VERSION_MINOR & NRD_VERSION_MINOR don't match!");
static_assert(VERSION_BUILD == NRD_VERSION_BUILD, "VERSION_BUILD & NRD_VERSION_BUILD don't match!");

constexpr std::array<nrd::Method, (size_t)nrd::Method::MAX_NUM> g_NrdSupportedMethods =
{
    nrd::Method::REBLUR_DIFFUSE,
    nrd::Method::REBLUR_DIFFUSE_OCCLUSION,
    nrd::Method::REBLUR_DIFFUSE_SH,
    nrd::Method::REBLUR_SPECULAR,
    nrd::Method::REBLUR_SPECULAR_OCCLUSION,
    nrd::Method::REBLUR_SPECULAR_SH,
    nrd::Method::REBLUR_DIFFUSE_SPECULAR,
    nrd::Method::REBLUR_DIFFUSE_SPECULAR_OCCLUSION,
    nrd::Method::REBLUR_DIFFUSE_SPECULAR_SH,
    nrd::Method::REBLUR_DIFFUSE_DIRECTIONAL_OCCLUSION,
    nrd::Method::SIGMA_SHADOW,
    nrd::Method::SIGMA_SHADOW_TRANSLUCENCY,
    nrd::Method::RELAX_DIFFUSE,
    nrd::Method::RELAX_SPECULAR,
    nrd::Method::RELAX_DIFFUSE_SPECULAR,
    nrd::Method::REFERENCE,
    nrd::Method::SPECULAR_REFLECTION_MV,
    nrd::Method::SPECULAR_DELTA_MV
};

constexpr nrd::LibraryDesc g_NrdLibraryDesc =
{
    // IMPORTANT: these should match "VK_{S/T/B/U}_SHIFT" in "ShaderCompilation.cmake"!
    { 100, 200, 300, 400 },
    g_NrdSupportedMethods.data(),
    (uint32_t)g_NrdSupportedMethods.size(),
    VERSION_MAJOR,
    VERSION_MINOR,
    VERSION_BUILD,
    (nrd::NormalEncoding)NRD_NORMAL_ENCODING,
    (nrd::RoughnessEncoding)NRD_ROUGHNESS_ENCODING
};

const char* g_NrdResourceTypeNames[] =
{
    "IN_MV",
    "IN_NORMAL_ROUGHNESS",
    "IN_VIEWZ",
    "IN_DIFF_RADIANCE_HITDIST",
    "IN_SPEC_RADIANCE_HITDIST",
    "IN_DIFF_HITDIST",
    "IN_SPEC_HITDIST",
    "IN_DIFF_DIRECTION_HITDIST",
    "IN_DIFF_SH0",
    "IN_DIFF_SH1",
    "IN_SPEC_SH0",
    "IN_SPEC_SH1",
    "IN_DIFF_CONFIDENCE",
    "IN_SPEC_CONFIDENCE",
    "IN_DISOCCLUSION_THRESHOLD_MIX",
    "IN_BASECOLOR_METALNESS",
    "IN_SHADOWDATA",
    "IN_SHADOW_TRANSLUCENCY",
    "IN_RADIANCE",
    "IN_DELTA_PRIMARY_POS",
    "IN_DELTA_SECONDARY_POS",

    "OUT_DIFF_RADIANCE_HITDIST",
    "OUT_SPEC_RADIANCE_HITDIST",
    "OUT_DIFF_SH0",
    "OUT_DIFF_SH1",
    "OUT_SPEC_SH0",
    "OUT_SPEC_SH1",
    "OUT_DIFF_HITDIST",
    "OUT_SPEC_HITDIST",
    "OUT_DIFF_DIRECTION_HITDIST",
    "OUT_SHADOW_TRANSLUCENCY",
    "OUT_RADIANCE",
    "OUT_REFLECTION_MV",
    "OUT_DELTA_MV",
    "OUT_VALIDATION",

    "TRANSIENT_POOL",
    "PERMANENT_POOL",
};
static_assert( GetCountOf(g_NrdResourceTypeNames) == (uint32_t)nrd::ResourceType::MAX_NUM );

const char* g_NrdMethodNames[] =
{
    "REBLUR_DIFFUSE",
    "REBLUR_DIFFUSE_OCCLUSION",
    "REBLUR_DIFFUSE_SH",
    "REBLUR_SPECULAR",
    "REBLUR_SPECULAR_OCCLUSION",
    "REBLUR_SPECULAR_SH",
    "REBLUR_DIFFUSE_SPECULAR",
    "REBLUR_DIFFUSE_SPECULAR_OCCLUSION",
    "REBLUR_DIFFUSE_SPECULAR_SH",
    "REBLUR_DIFFUSE_DIRECTIONAL_OCCLUSION",

    "SIGMA_SHADOW",
    "SIGMA_SHADOW_TRANSLUCENCY",

    "RELAX_DIFFUSE",
    "RELAX_SPECULAR",
    "RELAX_DIFFUSE_SPECULAR",

    "REFERENCE",

    "SPECULAR_REFLECTION_MV",
    "SPECULAR_DELTA_MV",
};
static_assert( GetCountOf(g_NrdMethodNames) == (uint32_t)nrd::Method::MAX_NUM );

NRD_API const nrd::LibraryDesc& NRD_CALL nrd::GetLibraryDesc()
{
    return g_NrdLibraryDesc;
}

NRD_API nrd::Result NRD_CALL nrd::CreateDenoiser(const DenoiserCreationDesc& denoiserCreationDesc, Denoiser*& denoiser)
{
#if 0
    // REBLUR shader source files generator
    static std::array<const char*, 3> typeNames             = {"Diffuse", "Specular", "DiffuseSpecular"};
    static std::array<const char*, 3> typeMacros            = {"#define REBLUR_DIFFUSE\n", "#define REBLUR_SPECULAR\n", "#define REBLUR_DIFFUSE\n#define REBLUR_SPECULAR\n"};

    static std::array<const char*, 4> permutationNames      = {"", "Occlusion", "Sh", "DirectionalOcclusion"};
    static std::array<const char*, 4> permutationMacros     = {"", "#define REBLUR_OCCLUSION\n", "#define REBLUR_SH\n", "#define REBLUR_DIRECTIONAL_OCCLUSION\n"};

    static std::array<const char*, 9> passNames             = {"HitDistReconstruction", "PrePass", "TemporalAccumulation", "HistoryFix", "Blur", "PostBlur", "CopyStabilizedHistory", "TemporalStabilization", "SplitScreen"};
    static std::array<size_t, 9> passPermutationNums        = {2, 1, 1, 1, 1, 2, 1, 1, 1};
    static std::array<const char*, 9> passPermutationNames  = {"_5x5", "", "", "", "", "_NoTemporalStabilization", "", "", ""};
    static std::array<const char*, 9> passPermutationMacros = {"#define REBLUR_HITDIST_RECONSTRUCTION_5X5\n", "", "", "", "", "#define REBLUR_NO_TEMPORAL_STABILIZATION\n", "", "", ""};

    if( !_wmkdir(L"_Temp") )
    {
        for (size_t type = 0; type < typeNames.size(); type++)
        {
            for (size_t permutation = 0; permutation < permutationNames.size(); permutation++)
            {
                for (size_t pass = 0; pass < passNames.size(); pass++)
                {
                    for (size_t passPermutation = 0; passPermutation < passPermutationNums[pass]; passPermutation++)
                    {
                        for (uint32_t perf = 0; perf < 2; perf++)
                        {
                            // Skip "PostBlur" for "Occlusion" denoisers
                            if (permutation == 1 && pass == 5 && passPermutation == 0)
                                continue;

                            // Skip "TemporalStabilization" for "Occlusion" denoisers
                            if (permutation == 1 && pass == 7)
                                continue;

                            // Skip "CopyStabilizedHistory" for "Occlusion" & "DirectionalOcclusion" denoisers
                            if ((permutation == 1 || permutation == 3) && pass == 6)
                                continue;

                            // Skip "CopyStabilizedHistory" for performance mode
                            if (pass == 6 && perf == 1)
                                continue;

                            // Skip "HitDistReconstruction" for "Sh" & "DirectionalOcclusion" denoisers
                            if (permutation > 1 && pass == 0)
                                continue;

                            // Skip non-diffuse "DirectionalOcclusion" denoisers
                            if (type != 0 && permutation == 3)
                                continue;

                            // Skip "SplitScreen" for "Occlusion" & "DirectionalOcclusion" denoisers
                            if ((permutation == 1 || permutation == 3) && pass == 8)
                                continue;

                            // Skip "SplitScreen" for performance mode
                            if (pass == 8 && perf == 1)
                                continue;

                            char filename[256];
                            snprintf(filename, sizeof(filename) - 1, "./_temp/REBLUR_%s%s%s_%s%s.cs.hlsl",
                                perf == 0 ? "" : "Perf_",
                                typeNames[type],
                                permutationNames[permutation],
                                passNames[pass],
                                passPermutation == 0 ? "" : passPermutationNames[pass]
                            );

                            FILE* fp = fopen(filename, "w");
                            if (fp)
                            {
                                fprintf(fp,
                                    "/*\n"
                                    "Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.\n"
                                    "\n"
                                    "NVIDIA CORPORATION and its licensors retain all intellectual property\n"
                                    "and proprietary rights in and to this software, related documentation\n"
                                    "and any modifications thereto. Any use, reproduction, disclosure or\n"
                                    "distribution of this software and related documentation without an express\n"
                                    "license agreement from NVIDIA CORPORATION is strictly prohibited.\n"
                                    "*/\n"
                                    "\n"
                                    "#include \"../Include/NRD.hlsli\"\n"
                                    "#include \"STL.hlsli\"\n"
                                    "\n"
                                    "%s"
                                    "%s"
                                    "%s"
                                    "%s"
                                    "\n"
                                    "#include \"../Include/REBLUR/REBLUR_Config.hlsli\"\n"
                                    "#include \"../Resources/REBLUR_DiffuseSpecular_%s.resources.hlsli\"\n"
                                    "\n"
                                    "#include \"../Include/Common.hlsli\"\n"
                                    "%s"
                                    "#include \"../Include/REBLUR/REBLUR_DiffuseSpecular_%s.hlsli\"\n",
                                    perf == 0 ? "" : "#define REBLUR_PERFORMANCE_MODE\n",
                                    typeMacros[type],
                                    permutationMacros[permutation],
                                    passPermutation == 0 ? "" : passPermutationMacros[pass],
                                    passNames[pass],
                                    pass == 6 ? "" : "#include \"../Include/REBLUR/REBLUR_Common.hlsli\"\n",
                                    passNames[pass]
                                );
                                fclose(fp);
                            }
                        }
                    }
                }
            }
        }
    }

    __debugbreak();
#endif

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

NRD_API const nrd::DenoiserDesc& NRD_CALL nrd::GetDenoiserDesc(const Denoiser& denoiser)
{
    return ((const DenoiserImpl&)denoiser).GetDesc();
}

NRD_API nrd::Result NRD_CALL nrd::SetMethodSettings(Denoiser& denoiser, Method method, const void* methodSettings)
{
    return ((DenoiserImpl&)denoiser).SetMethodSettings(method, methodSettings);
}

NRD_API void NRD_CALL nrd::GetComputeDispatches(Denoiser& denoiser, const CommonSettings& commonSettings, const DispatchDesc*& dispatchDescs, uint32_t& dispatchDescNum)
{
    ((DenoiserImpl&)denoiser).GetComputeDispatches(commonSettings, dispatchDescs, dispatchDescNum);
}

NRD_API void NRD_CALL nrd::DestroyDenoiser(Denoiser& denoiser)
{
    StdAllocator<uint8_t> memoryAllocator = ((DenoiserImpl&)denoiser).GetStdAllocator();
    Deallocate(memoryAllocator, (DenoiserImpl*)&denoiser);
}

NRD_API const char* NRD_CALL nrd::GetResourceTypeString(ResourceType resourceType)
{
    uint32_t i = (uint32_t)resourceType;
    return i < (uint32_t)ResourceType::MAX_NUM ? g_NrdResourceTypeNames[i] : nullptr;
}

NRD_API const char* NRD_CALL nrd::GetMethodString(Method method)
{
    uint32_t i = (uint32_t)method;
    return i < (uint32_t)Method::MAX_NUM ? g_NrdMethodNames[i] : nullptr;
}
