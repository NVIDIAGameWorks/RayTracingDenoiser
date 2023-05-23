/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

NRD_CONSTANTS_START
    NRD_CONSTANT( float, gDenoisingRange )
NRD_CONSTANTS_END

NRD_INPUT_TEXTURE_START
    NRD_INPUT_TEXTURE( Texture2D<float>, gIn_ViewZ, t, 0 )
NRD_INPUT_TEXTURE_END

NRD_OUTPUT_TEXTURE_START
    NRD_OUTPUT_TEXTURE( RWTexture2D<float>, gOut_Tiles, u, 0 )
NRD_OUTPUT_TEXTURE_END
