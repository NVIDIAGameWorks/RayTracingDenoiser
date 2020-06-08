/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "SharedVal.h"
#include "BufferVal.h"

using namespace nri;

void ConvertGeometryObjectsVal(GeometryObject* destObjects, const GeometryObject* sourceObjects, uint32_t objectNum)
{
    for (uint32_t i = 0; i < objectNum; i++)
    {
        const GeometryObject& geometrySrc = sourceObjects[i];
        GeometryObject& geometryDst = destObjects[i];

        geometryDst = geometrySrc;

        if (geometrySrc.type == GeometryType::TRIANGLES)
        {
            geometryDst.triangles.vertexBuffer = NRI_GET_IMPL(Buffer, geometrySrc.triangles.vertexBuffer);
            geometryDst.triangles.indexBuffer = NRI_GET_IMPL(Buffer, geometrySrc.triangles.indexBuffer);
            geometryDst.triangles.transformBuffer = NRI_GET_IMPL(Buffer, geometrySrc.triangles.transformBuffer);
        }
        else
        {
            geometryDst.boxes.buffer = NRI_GET_IMPL(Buffer, geometrySrc.boxes.buffer);
        }
    }
}

