/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "InstanceImpl.h"

// REFERENCE
#ifdef NRD_EMBEDS_DXBC_SHADERS
    #include "REFERENCE_TemporalAccumulation.cs.dxbc.h"
    #include "REFERENCE_SplitScreen.cs.dxbc.h"
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #include "REFERENCE_TemporalAccumulation.cs.dxil.h"
    #include "REFERENCE_SplitScreen.cs.dxil.h"
#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #include "REFERENCE_TemporalAccumulation.cs.spirv.h"
    #include "REFERENCE_SplitScreen.cs.spirv.h"
#endif

#include "Denoisers/Reference.hpp"


// SPECULAR_REFLECTION_MV
#ifdef NRD_EMBEDS_DXBC_SHADERS
    #include "SpecularReflectionMv_Compute.cs.dxbc.h"
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #include "SpecularReflectionMv_Compute.cs.dxil.h"
#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #include "SpecularReflectionMv_Compute.cs.spirv.h"
#endif

#include "Denoisers/SpecularReflectionMv.hpp"


// SPECULAR_DELTA_MV
#ifdef NRD_EMBEDS_DXBC_SHADERS
    #include "SpecularDeltaMv_Compute.cs.dxbc.h"
#endif

#ifdef NRD_EMBEDS_DXIL_SHADERS
    #include "SpecularDeltaMv_Compute.cs.dxil.h"
#endif

#ifdef NRD_EMBEDS_SPIRV_SHADERS
    #include "SpecularDeltaMv_Compute.cs.spirv.h"
#endif

#include "Denoisers/SpecularDeltaMv.hpp"
