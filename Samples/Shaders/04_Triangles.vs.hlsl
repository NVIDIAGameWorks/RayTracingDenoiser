/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

struct OutputVS
{
    float4 position : SV_Position;
};

OutputVS main( float3 inPos : POSITION0 )
{
    OutputVS output;
    output.position = float4( inPos, 1.0 );

    return output;
}
