/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedVK.h"
#include "BufferVK.h"

using namespace nri;

void nri::ConvertGeometryObjectsVK(VkGeometryNV* destObjects, const GeometryObject* sourceObjects, uint32_t objectNum, uint32_t physicalDeviceIndex)
{
    for (uint32_t i = 0; i < objectNum; i++)
    {
        const GeometryObject& geometrySrc = sourceObjects[i];
        VkGeometryNV& geometryDst = destObjects[i];

        geometryDst.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV;
        geometryDst.pNext = nullptr;
        geometryDst.flags = GetGeometryFlags(geometrySrc.flags);
        geometryDst.geometryType = GetGeometryType(geometrySrc.type);
        geometryDst.geometry.aabbs.sType = VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV;
        geometryDst.geometry.aabbs.pNext = nullptr;
        geometryDst.geometry.aabbs.aabbData = GetVulkanHandle<VkBuffer, BufferVK>(geometrySrc.boxes.buffer, physicalDeviceIndex);
        geometryDst.geometry.aabbs.numAABBs = geometrySrc.boxes.boxNum;
        geometryDst.geometry.aabbs.stride = geometrySrc.boxes.stride;
        geometryDst.geometry.aabbs.offset = geometrySrc.boxes.offset;
        geometryDst.geometry.triangles.sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV;
        geometryDst.geometry.triangles.pNext = nullptr;
        geometryDst.geometry.triangles.vertexData = GetVulkanHandle<VkBuffer, BufferVK>(geometrySrc.triangles.vertexBuffer, physicalDeviceIndex);
        geometryDst.geometry.triangles.vertexOffset = geometrySrc.triangles.vertexOffset;
        geometryDst.geometry.triangles.vertexCount = geometrySrc.triangles.vertexNum;
        geometryDst.geometry.triangles.vertexStride = geometrySrc.triangles.vertexStride;
        geometryDst.geometry.triangles.vertexFormat = GetVkFormat(geometrySrc.triangles.vertexFormat);
        geometryDst.geometry.triangles.indexData = GetVulkanHandle<VkBuffer, BufferVK>(geometrySrc.triangles.indexBuffer, physicalDeviceIndex);
        geometryDst.geometry.triangles.indexOffset = geometrySrc.triangles.indexOffset;
        geometryDst.geometry.triangles.indexCount = geometrySrc.triangles.indexNum;
        geometryDst.geometry.triangles.indexType = GetIndexType(geometrySrc.triangles.indexType);
        geometryDst.geometry.triangles.transformData = GetVulkanHandle<VkBuffer, BufferVK>(geometrySrc.triangles.transformBuffer, physicalDeviceIndex);
        geometryDst.geometry.triangles.transformOffset = geometrySrc.triangles.transformOffset;
    }
}

TextureType GetTextureTypeVK(uint32_t vkImageType)
{
    return GetTextureType((VkImageType)vkImageType);
}