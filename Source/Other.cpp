/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "InstanceImpl.h"

#ifdef NRD_USE_PRECOMPILED_SHADERS

    // REFERENCE
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "REFERENCE_TemporalAccumulation.cs.dxbc.h"
        #include "REFERENCE_TemporalAccumulation.cs.dxil.h"
        #include "REFERENCE_SplitScreen.cs.dxbc.h"
        #include "REFERENCE_SplitScreen.cs.dxil.h"
    #endif

    #include "REFERENCE_TemporalAccumulation.cs.spirv.h"
    #include "REFERENCE_SplitScreen.cs.spirv.h"

    // SPECULAR_REFLECTION_MV
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "SpecularReflectionMv_Compute.cs.dxbc.h"
        #include "SpecularReflectionMv_Compute.cs.dxil.h"
    #endif

    #include "SpecularReflectionMv_Compute.cs.spirv.h"

    // SPECULAR_DELTA_MV
    #if !NRD_ONLY_SPIRV_SHADERS_AVAILABLE
        #include "SpecularDeltaMv_Compute.cs.dxbc.h"
        #include "SpecularDeltaMv_Compute.cs.dxil.h"
    #endif

    #include "SpecularDeltaMv_Compute.cs.spirv.h"

#endif

#include "Denoisers/Reference.hpp"
#include "Denoisers/SpecularReflectionMv.hpp"
#include "Denoisers/SpecularDeltaMv.hpp"
