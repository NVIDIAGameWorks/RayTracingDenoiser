/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include "Detex/detex.h"

#define NDC_DONT_CARE
#include "MathLib/MathLib.h"

#include "NRIDescs.h"
#include "Extensions/NRIHelper.h"

#include "Helper.h"

#include <string>
#include <vector>

#define NRI_ABORT_ON_FAILURE(result) \
    if ((result) != nri::Result::SUCCESS) \
        exit(1);

#define NRI_ABORT_ON_FALSE(result) \
    if (!(result)) \
        exit(1);

namespace utils
{
    struct Texture;
    struct Scene;
    typedef std::vector<std::vector<uint8_t>> ShaderCodeStorage;

    enum StaticTexture : uint32_t
    {
        Invalid,
        Black,
        FlatNormal,
        ScramblingRanking1spp,
        ScramblingRanking32spp,
        SobolSequence
    };

    enum class AlphaMode
    {
        OPAQUE,
        PREMULTIPLIED,
        TRANSPARENT,
        OFF // alpha is 0 everywhere
    };

    enum class DataFolder
    {
        ROOT,
        SHADERS,
        TEXTURES,
        SCENES
    };

    std::string GetFullPath(const std::string& localPath, DataFolder dataFolder);
    bool LoadFile(const std::string& path, std::vector<uint8_t>& data);
    nri::ShaderDesc LoadShader(nri::GraphicsAPI graphicsAPI, const std::string& path, ShaderCodeStorage& storage, const char* entryPointName = nullptr);
    bool LoadTexture(const std::string& path, Texture& texture, bool computeAvgColorAndAlphaMode = false);
    void LoadTextureFromMemory(nri::Format format, uint32_t width, uint32_t height, const uint8_t *pixels, Texture &texture);
    bool LoadScene(const std::string& path, Scene& scene, bool simpleOIT = false, const std::vector<float3>& instanceData = {float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f) });

    struct Texture
    {
        detexTexture **texture = nullptr;
        std::string name;
        uint64_t hash = 0;
        uint32_t averageColor = 0;
        AlphaMode alphaMode = AlphaMode::OPAQUE;
        nri::Format format = nri::Format::UNKNOWN;
        uint16_t width = 0;
        uint16_t height = 0;
        uint16_t depth = 0;
        uint16_t mipNum = 0;
        uint16_t arraySize = 0;

        inline ~Texture()
        {
            detexFreeTexture(texture, mipNum);
            texture = nullptr;
        }

        inline void OverrideFormat(nri::Format fmt)
        { this->format = fmt; }

        inline bool IsBlockCompressed() const
        { return detexFormatIsCompressed(texture[0]->format); }

        inline uint16_t GetArraySize() const
        { return arraySize; }

        inline uint16_t GetMipNum() const
        { return mipNum; }

        inline uint16_t GetWidth() const
        { return helper::GetAlignedSize(width, IsBlockCompressed() ? 4 : 1); }

        inline uint16_t GetHeight() const
        { return helper::GetAlignedSize(height, IsBlockCompressed() ? 4 : 1); }

        inline uint16_t GetDepth() const
        { return depth; }

        inline nri::Format GetFormat() const
        { return format; }

        void GetSubresource(nri::TextureSubresourceUploadDesc& subresource, uint32_t mipIndex, uint32_t arrayIndex = 0) const
        {
            // TODO: 3D images are not supported, "subresource.slices" needs to be allocated to store pointers to all slices of current mipmap
            assert(GetDepth() == 1);
            PLATFORM_UNUSED(arrayIndex);

            subresource.slices = texture[mipIndex]->data;
            subresource.sliceNum = 1;
            int rowPitch, slicePitch;
            detexComputePitch(texture[mipIndex]->format, texture[mipIndex]->width, texture[mipIndex]->height, &rowPitch, &slicePitch);
            subresource.rowPitch = (uint32_t)rowPitch;
            subresource.slicePitch = (uint32_t)slicePitch;
        }
    };

    struct MaterialGroup
    {
        uint32_t materialOffset;
        uint32_t materialNum;
    };

    struct Material
    {
        uint32_t instanceOffset;
        uint32_t instanceNum;
        uint32_t diffuseMapIndex;
        uint32_t specularMapIndex;
        uint32_t normalMapIndex;
        uint32_t emissiveMapIndex;
        uint32_t averageBaseColor;
        AlphaMode alphaMode;

        inline bool IsOpaque() const
        { return alphaMode == AlphaMode::OPAQUE; }

        inline bool IsAlphaOpaque() const
        { return alphaMode == AlphaMode::PREMULTIPLIED; }

        inline bool IsTransparent() const
        { return alphaMode == AlphaMode::TRANSPARENT; }

        inline bool IsOff() const
        { return alphaMode == AlphaMode::OFF; }

        inline bool IsEmissive() const
        { return (averageBaseColor >> 24) != 0; }
    };

    struct Instance
    {
        float4x4 rotation;
        float4x4 rotationPrev;
        double3 position;
        double3 positionPrev;
        uint32_t meshIndex;
        uint32_t materialIndex;
    };

    struct Mesh
    {
        cBoxf aabb;
        uint32_t vertexOffset;
        uint32_t indexOffset;
        uint32_t indexNum;
        uint32_t vertexNum;
    };

    struct Vertex
    {
        float position[3];
        uint32_t uv; // half float
        uint32_t normal; // 10 10 10 2 unorm
        uint32_t tangent; // 10 10 10 2 unorm
    };

    struct Primitive
    {
        float worldToUvUnits;
        uint32_t normal; // 10 10 10 2 unorm
    };

    struct AnimationNode
    {
        std::vector<double3> positionValues;
        std::vector<float4> rotationValues;
        std::vector<float3> scaleValues;
        std::vector<float> positionKeys;
        std::vector<float> rotationKeys;
        std::vector<float> scaleKeys;
        float4x4 mTransform = float4x4::identity;

        void Animate(float time);
    };

    struct NodeTree
    {
        std::vector<NodeTree> mChildren;
        std::vector<uint32_t> mInstances;
        float4x4 mTransform = float4x4::identity;
        uint64_t mHash = 0;
        int32_t animationNodeID = -1;

        void Animate(utils::Scene& scene, std::vector<AnimationNode>& animationNodes, const float4x4& parentTransform, float4x4* outTransform = nullptr);
    };

    struct Animation
    {
        std::vector<AnimationNode> animationNodes;
        NodeTree rootNode;
        NodeTree cameraNode;
        std::string animationName;
        float durationMs = 0.0f;
        float animationProgress;
        float sign = 1.0f;
        float normalizedTime;
        bool hasCameraAnimation;
    };

    typedef uint16_t Index;

    struct Scene
    {
        ~Scene()
        {
            for (size_t i = 0; i < textures.size(); i++)
                delete textures[i];
        }

        // 0 - opaque
        // 1 - two-sided, alpha opaque
        // 2 - transparent (back faces)
        // 3 - transparent (front faces)
        std::vector<MaterialGroup> materialsGroups;
        std::vector<utils::Texture*> textures;
        std::vector<Material> materials;
        std::vector<Instance> instances;
        std::vector<Mesh> meshes;
        std::vector<Primitive> primitives;
        std::vector<Vertex> vertices;
        std::vector<Index> indices;
        std::vector<Animation> animations;
        float4x4 mSceneToWorld = float4x4::identity;
        cBoxf aabb;

        void Animate(float animationSpeed, float elapsedTime, float& animationProgress, uint32_t animationID, float4x4* outCameraTransform = nullptr);

        inline void UnloadResources()
        {
            for (size_t i = 0; i < textures.size(); i++)
                delete textures[i];

            textures.clear();
            textures.shrink_to_fit();

            vertices.clear();
            vertices.shrink_to_fit();

            indices.clear();
            indices.shrink_to_fit();

            primitives.clear();
            primitives.shrink_to_fit();
        }
    };
}
