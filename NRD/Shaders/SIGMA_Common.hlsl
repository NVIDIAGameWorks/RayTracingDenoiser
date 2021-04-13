/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

// Misc

#define PackShadow( s )                                 STL::Math::Sqrt01( s )
#define UnpackShadow( s )                               ( s * s )

// TODO: shadow unpacking is less trivial
// 2.0 - closer to reference (dictated by encoding)
// 2.0 - s.x - looks better
#if 0
    #define UnpackShadowSpecial( s )                    STL::Math::Pow01( s, 2.0 - s.x * ( 1 - SIGMA_REFERENCE ) )
#else
    #define UnpackShadowSpecial                         UnpackShadow
#endif
