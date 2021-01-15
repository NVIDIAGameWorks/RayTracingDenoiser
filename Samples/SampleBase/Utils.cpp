/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRI.h"
#include "Utils.h"

#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>
#undef OPAQUE
#undef TRANSPARENT

#include <io.h>
#include <direct.h>

#include <array>
#include <map>
#include <algorithm>
#include <fstream>
#include <functional>

#include "assimp/scene.h"
#include "assimp/cimport.h"
#include "assimp/postprocess.h"

#define NRI_DATA_DIR "_Data"

static const std::array<aiTextureType, 5> gSupportedTextureTypes =
{
    aiTextureType_DIFFUSE,      // OBJ - map_Kd
    aiTextureType_SPECULAR,     // OBJ - map_Ks
    aiTextureType_NORMALS,      // OBJ - map_Kn
    aiTextureType_EMISSIVE,     // OBJ - map_Ke
    aiTextureType_SHININESS     // OBJ - map_Ns (smoothness)
};

static const std::array<const char*, 13> gShaderExts =
{
    "",
    ".vs.",
    ".tcs.",
    ".tes.",
    ".gs.",
    ".fs.",
    ".cs.",
    ".rgen.",
    ".rmiss.",
    "<noimpl>",
    ".rchit.",
    ".rahit.",
    "<noimpl>"
};

static inline bool IsExist(const std::string& path)
{ return _access(path.c_str(), 0) == 0; }


static inline bool EndsWithNoCase(std::string const &value, std::string const &ending)
{
    auto it = ending.begin();
    return value.size() >= ending.size() &&
        std::all_of(std::next(value.begin(), value.size() - ending.size()), value.end(), [&it](const char & c) {
            return ::tolower(c) == ::tolower(*(it++));
        });
}

static void GenerateTangents(const aiMesh& mesh, std::vector<float4>& tangents)
{
    const aiVector3D zeroUv(0.0f, 0.0f, 0.0f);
    float3* tan1 = (float3*)&tangents[0];
    float3* tan2 = tan1 + mesh.mNumVertices;
    const bool hasTexCoord0 = mesh.HasTextureCoords(0);

    std::memset(tan1, 0, sizeof(float3) * mesh.mNumVertices * 2);

    for (uint32_t i = 0; i < mesh.mNumFaces; i++)
    {
        uint32_t i1 = mesh.mFaces[i].mIndices[0];
        uint32_t i2 = mesh.mFaces[i].mIndices[1];
        uint32_t i3 = mesh.mFaces[i].mIndices[2];

        const aiVector3D& v1 = mesh.mVertices[i1];
        const aiVector3D& v2 = mesh.mVertices[i2];
        const aiVector3D& v3 = mesh.mVertices[i3];

        const aiVector3D& w1 = hasTexCoord0 ? mesh.mTextureCoords[0][i1] : zeroUv;
        const aiVector3D& w2 = hasTexCoord0 ? mesh.mTextureCoords[0][i2] : zeroUv;
        const aiVector3D& w3 = hasTexCoord0 ? mesh.mTextureCoords[0][i3] : zeroUv;

        float s1 = w2.x - w1.x;
        float s2 = w3.x - w1.x;
        float t1 = w2.y - w1.y;
        float t2 = w3.y - w1.y;
        float r = s1 * t2 - s2 * t1;

        float3 sdir;
        float3 tdir;

        if (Abs(r) < 1e-9f)
        {
            float3 N( &mesh.mNormals[i1].x );
            N.z += 1e-6f;
            sdir = GetPerpendicularVector(N);
            tdir = Cross(N, sdir);
        }
        else
        {
            float invr = 1.0f / r;

            float3 a = float3(v2.x - v1.x, v2.y - v1.y, v2.z - v1.z) * invr;
            float3 b = float3(v3.x - v1.x, v3.y - v1.y, v3.z - v1.z) * invr;

            sdir = a * t2 - b * t1;
            tdir = b * s1 - a * s2;
        }

        tan1[i1] += sdir;
        tan1[i2] += sdir;
        tan1[i3] += sdir;

        tan2[i1] += tdir;
        tan2[i2] += tdir;
        tan2[i3] += tdir;
    }

    for (uint32_t i = 0; i < mesh.mNumVertices; i++)
    {
        float3 n(&mesh.mNormals[i].x);

        float3 t = tan1[i];
        if (t.IsZero())
           t = Cross(tan2[i], n);

        // Gram-Schmidt orthogonalize
        t -= n * Dot33(n, t);
        float len = Length(t);
        t /= Max(len, 1e-9f);

        // Calculate handedness
        float handedness = Sign( Dot33( Cross(n, t), tan2[i] ) );

        tangents[i] = float4(t.x, t.y, t.z, handedness);
    }
}

static inline uint64_t ComputeHash(const void* key, uint32_t len)
{
    const uint8_t* p = (uint8_t*)key;
    uint64_t result = 14695981039346656037ull;
    while( len-- )
        result = (result ^ (*p++)) * 1099511628211ull;

    return result;
}

static inline const char* GetShaderExt(nri::GraphicsAPI graphicsAPI)
{
    if (graphicsAPI == nri::GraphicsAPI::D3D11)
        return ".dxbc";
    else if (graphicsAPI == nri::GraphicsAPI::D3D12)
        return ".dxil";

    return ".spirv";
}

struct NodeData
{
    aiMatrix4x4 transform;
    aiNode* node;
};

static void BindNodesToMeshID(aiNode* node, aiMatrix4x4 parentTransform, std::vector<std::vector<NodeData>>& vector)
{
    aiMatrix4x4 transform = parentTransform * node->mTransformation;

    for (uint32_t j = 0; j < node->mNumMeshes; j++)
    {
        NodeData tmpNodeData = {};
        tmpNodeData.node = node;
        tmpNodeData.transform = transform;
        vector[node->mMeshes[j]].push_back(tmpNodeData);
    }

    for (uint32_t i = 0; i < node->mNumChildren; i++)
        BindNodesToMeshID(node->mChildren[i], transform, vector);
}

static void ExtractNodeTree(const aiNode* node, std::map<const aiNode*, std::vector<uint32_t>>& nodeToInstanceMap, utils::NodeTree& animationIstance)
{
    float4x4 transform = float4x4(
        node->mTransformation.a1, node->mTransformation.a2, node->mTransformation.a3, node->mTransformation.a4,
        node->mTransformation.b1, node->mTransformation.b2, node->mTransformation.b3, node->mTransformation.b4,
        node->mTransformation.c1, node->mTransformation.c2, node->mTransformation.c3, node->mTransformation.c4,
        0.0f, 0.0f, 0.0f, 1.0f);

    utils::NodeTree parentInstance = {};
    parentInstance.mHash = ComputeHash((uint8_t*)node->mName.C_Str(), node->mName.length);
    parentInstance.mTransform = transform;

    std::map<const aiNode*, std::vector<uint32_t>>::iterator it = nodeToInstanceMap.find((aiNode*)node);
    if (it != nodeToInstanceMap.end())
        parentInstance.mInstances = it->second;

    parentInstance.mChildren.resize(node->mNumChildren);
    for (uint32_t i = 0; i < node->mNumChildren; i++)
        ExtractNodeTree(node->mChildren[i], nodeToInstanceMap, parentInstance.mChildren[i]);

    animationIstance = parentInstance;
}

static inline float SafeLinearstep(float a, float b, float x)
{
    return a == b ? 0.0f : Linearstep(a, b, x);
}

static inline uint32_t FindCurrentIndex(const std::vector<float>& keys, float time)
{
    for (uint32_t i = helper::GetCountOf(keys) - 1; i >= 1; i--)
    {
        if (time >= keys[i])
            return i;
    }

    return 0u;
};

std::string utils::GetFullPath(const std::string& localPath, DataFolder dataFolder)
{
    static std::string s_dataPath;

    if (s_dataPath.empty())
    {
        char path[2048];
        GetModuleFileNameA(nullptr, path, helper::GetCountOf(path));

        char baseDir[2048];
        char drive[128];
        _splitpath_s(path, drive, helper::GetCountOf(drive), baseDir, helper::GetCountOf(baseDir), 0, 0, 0, 0);

        std::string base = drive;
        base += baseDir;

        const uint32_t N = 10;
        uint32_t i = 0;
        for (; i < N; i++)
        {
            std::string temp = base + NRI_DATA_DIR + "\\";
            if ( IsExist(temp) )
            {
                base = temp;
                break;
            }
            base += "..\\";
        }

        s_dataPath = base;

        if (i == N)
            MessageBoxA(nullptr, "Can't locate '" NRI_DATA_DIR "' directory!", "ERROR", MB_OK);
    }

    std::string path = s_dataPath;
    if (dataFolder == DataFolder::SHADERS)
        path += "Shaders\\";
    else if (dataFolder == DataFolder::TEXTURES)
        path += "Textures\\";
    else if (dataFolder == DataFolder::SCENES)
        path += "Scenes\\";

    return path + localPath;
}

bool utils::LoadFile(const std::string& path, std::vector<uint8_t>& data)
{
    FILE* file = nullptr;
    fopen_s(&file, path.c_str(), "rb");
    if (file)
    {
        uint32_t bytes = _filelength(_fileno(file));
        data.resize(bytes);
        fread(&data[0], sizeof(uint8_t), bytes, file);
        fclose(file);
    }
    else
    {
        char msg[2048];
        sprintf_s(msg, "File '%s' is not present in '" NRI_DATA_DIR "' folder!", path.c_str());
        MessageBoxA(nullptr, msg, "ERROR", MB_OK);

        data.clear();
    }

    return !data.empty();
}

nri::ShaderDesc utils::LoadShader(nri::GraphicsAPI graphicsAPI, const std::string& shaderName, ShaderCodeStorage& storage, const char* entryPointName)
{
    const char* ext = GetShaderExt(graphicsAPI);
    std::string path = GetFullPath(shaderName + ext, DataFolder::SHADERS);
    nri::ShaderDesc shaderDesc = {};

    uint32_t i = 1;
    for (; i < (uint32_t)nri::ShaderStage::MAX_NUM; i++)
    {
        if (path.rfind(gShaderExts[i]) != std::string::npos)
        {
            storage.push_back( std::vector<uint8_t>() );
            std::vector<uint8_t>& code = storage.back();

            if (LoadFile(path, code))
            {
                shaderDesc.stage = (nri::ShaderStage)i;
                shaderDesc.bytecode = code.data();
                shaderDesc.size = code.size();
                shaderDesc.entryPointName = entryPointName;
            }

            break;
        }
    }

    if (i == (uint32_t)nri::ShaderStage::MAX_NUM)
    {
        char msg[2048];
        sprintf_s(msg, "Shader '%s' has invalid shader extension!", shaderName.c_str());
        MessageBoxA(nullptr, msg, "ERROR", MB_OK);
        NRI_ABORT_ON_FALSE(false);
    };

    return shaderDesc;
}

static inline const char* GetFileName(const std::string& path)
{
    const size_t slashPos = path.find_last_of("\\/");
    if (slashPos != std::string::npos)
        return path.c_str() + slashPos + 1;

    return "";
}

static struct FormatMapping
{
    uint32_t detexFormat;
    nri::Format nriFormat;
} formatTable[] = {
    // Uncompressed formats.
    { DETEX_PIXEL_FORMAT_RGB8, nri::Format::UNKNOWN },
    { DETEX_PIXEL_FORMAT_RGBA8, nri::Format::RGBA8_UNORM },
    { DETEX_PIXEL_FORMAT_R8, nri::Format::R8_UNORM },
    { DETEX_PIXEL_FORMAT_SIGNED_R8, nri::Format::R8_SNORM },
    { DETEX_PIXEL_FORMAT_RG8, nri::Format::RG8_UNORM },
    { DETEX_PIXEL_FORMAT_SIGNED_RG8, nri::Format::RG8_SNORM },
    { DETEX_PIXEL_FORMAT_R16, nri::Format::R16_UNORM },
    { DETEX_PIXEL_FORMAT_SIGNED_R16, nri::Format::R16_SNORM },
    { DETEX_PIXEL_FORMAT_RG16, nri::Format::RG16_UNORM },
    { DETEX_PIXEL_FORMAT_SIGNED_RG16, nri::Format::RG16_SNORM },
    { DETEX_PIXEL_FORMAT_RGB16, nri::Format::UNKNOWN },
    { DETEX_PIXEL_FORMAT_RGBA16, nri::Format::RGBA16_UNORM },
    { DETEX_PIXEL_FORMAT_FLOAT_R16, nri::Format::R16_SFLOAT },
    { DETEX_PIXEL_FORMAT_FLOAT_RG16, nri::Format::RG16_SFLOAT },
    { DETEX_PIXEL_FORMAT_FLOAT_RGB16, nri::Format::UNKNOWN },
    { DETEX_PIXEL_FORMAT_FLOAT_RGBA16, nri::Format::RGBA16_SFLOAT },
    { DETEX_PIXEL_FORMAT_FLOAT_R32, nri::Format::R32_SFLOAT },
    { DETEX_PIXEL_FORMAT_FLOAT_RG32, nri::Format::RG32_SFLOAT },
    { DETEX_PIXEL_FORMAT_FLOAT_RGB32, nri::Format::RGB32_SFLOAT },
    { DETEX_PIXEL_FORMAT_FLOAT_RGBA32, nri::Format::RGBA32_SFLOAT },
    { DETEX_PIXEL_FORMAT_A8, nri::Format::UNKNOWN },
    // Compressed formats.
    { DETEX_TEXTURE_FORMAT_BC1, nri::Format::BC1_RGBA_UNORM },
    { DETEX_TEXTURE_FORMAT_BC1A, nri::Format::UNKNOWN },
    { DETEX_TEXTURE_FORMAT_BC2, nri::Format::BC2_RGBA_UNORM },
    { DETEX_TEXTURE_FORMAT_BC3, nri::Format::BC3_RGBA_UNORM },
    { DETEX_TEXTURE_FORMAT_RGTC1, nri::Format::BC4_R_UNORM },
    { DETEX_TEXTURE_FORMAT_SIGNED_RGTC1, nri::Format::BC4_R_SNORM },
    { DETEX_TEXTURE_FORMAT_RGTC2, nri::Format::BC5_RG_UNORM },
    { DETEX_TEXTURE_FORMAT_SIGNED_RGTC2, nri::Format::BC5_RG_SNORM },
    { DETEX_TEXTURE_FORMAT_BPTC_FLOAT, nri::Format::BC6H_RGB_UFLOAT },
    { DETEX_TEXTURE_FORMAT_BPTC_SIGNED_FLOAT, nri::Format::BC6H_RGB_SFLOAT },
    { DETEX_TEXTURE_FORMAT_BPTC, nri::Format::BC7_RGBA_UNORM },
    { DETEX_TEXTURE_FORMAT_ETC1, nri::Format::UNKNOWN },
    { DETEX_TEXTURE_FORMAT_ETC2, nri::Format::UNKNOWN },
    { DETEX_TEXTURE_FORMAT_ETC2_PUNCHTHROUGH, nri::Format::UNKNOWN },
    { DETEX_TEXTURE_FORMAT_ETC2_EAC, nri::Format::UNKNOWN },
    { DETEX_TEXTURE_FORMAT_EAC_R11, nri::Format::UNKNOWN },
    { DETEX_TEXTURE_FORMAT_EAC_SIGNED_R11, nri::Format::UNKNOWN },
    { DETEX_TEXTURE_FORMAT_EAC_RG11, nri::Format::UNKNOWN },
    { DETEX_TEXTURE_FORMAT_EAC_SIGNED_RG11, nri::Format::UNKNOWN }
};

static nri::Format GetFormatNRI(uint32_t detexFormat)
{
    for (auto& entry : formatTable)
    {
        if (entry.detexFormat == detexFormat)
        {
            return entry.nriFormat;
        }
    }

    return nri::Format::UNKNOWN;
}

static nri::Format MakeSRGBFormat(nri::Format format)
{
    switch (format)
    {
    case nri::Format::RGBA8_UNORM:
        return nri::Format::RGBA8_SRGB;

    case nri::Format::BC1_RGBA_UNORM:
        return nri::Format::BC1_RGBA_SRGB;

    case nri::Format::BC2_RGBA_UNORM:
        return nri::Format::BC2_RGBA_SRGB;

    case nri::Format::BC3_RGBA_UNORM:
        return nri::Format::BC3_RGBA_SRGB;

    case nri::Format::BC7_RGBA_UNORM:
        return nri::Format::BC7_RGBA_SRGB;

    default:
        return format;
    }
}

bool utils::LoadTexture(const std::string& path, Texture& texture, bool computeAvgColorAndAlphaMode)
{
    detexTexture** dTexture = nullptr;
    int mipNum = 0;

    if (!detexLoadTextureFileWithMipmaps(path.c_str(), 32, &dTexture, &mipNum)) {
        char s[1024];
        sprintf_s(s, "ERROR: Can't load texture '%s'\n", path.c_str());
        OutputDebugStringA(s);

        return false;
    }

    texture.texture = dTexture;
    texture.name = path;
    texture.hash = ComputeHash(path.c_str(), (uint32_t)path.length());
    texture.format = GetFormatNRI(dTexture[0]->format);
    texture.width = (uint16_t)dTexture[0]->width;
    texture.height = (uint16_t)dTexture[0]->height;
    texture.mipNum = (uint16_t)mipNum;

    // TODO: detex doesn't support cubemaps and 3D textures
    texture.arraySize = 1;
    texture.depth = 1;

    texture.alphaMode = AlphaMode::OPAQUE;
    if (computeAvgColorAndAlphaMode)
    {
        // Alpha mode
        if (texture.format == nri::Format::BC1_RGBA_UNORM || texture.format == nri::Format::BC1_RGBA_SRGB)
        {
            bool hasTransparency = false;
            for (int i = mipNum - 1; i >= 0 && !hasTransparency; i--) {
                const size_t size = detexTextureSize(dTexture[i]->width_in_blocks, dTexture[i]->height_in_blocks, dTexture[i]->format);
                const uint8_t* bc1 = dTexture[i]->data;

                for (size_t j = 0; j < size && !hasTransparency; j += 8)
                {
                    const uint16_t* c = (uint16_t*)bc1;
                    if (c[0] <= c[1])
                    {
                        const uint32_t bits = *(uint32_t*)(bc1 + 4);
                        for (uint32_t k = 0; k < 32 && !hasTransparency; k += 2)
                            hasTransparency = ((bits >> k) & 0x3) == 0x3;
                    }
                    bc1 += 8;
                }
            }

            if (hasTransparency)
                texture.alphaMode = AlphaMode::PREMULTIPLIED;
        }

        // Decompress last mip
        std::vector<uint8_t> image;
        detexTexture *lastMip = dTexture[mipNum - 1];
        uint8_t *rgba8 = lastMip->data;
        if (lastMip->format != DETEX_PIXEL_FORMAT_RGBA8)
        {
            image.resize(lastMip->width * lastMip->height * detexGetPixelSize(DETEX_PIXEL_FORMAT_RGBA8));
            // Converts to RGBA8 if the texture is not compressed
            detexDecompressTextureLinear(lastMip, &image[0], DETEX_PIXEL_FORMAT_RGBA8);
            rgba8 = &image[0];
        }

        // Average color
        double4 avgColor = double4(0.0f);
        const size_t pixelNum = lastMip->width * lastMip->height;
        for (size_t i = 0; i < pixelNum; i++)
            avgColor += ToDouble( Packed::uint_to_uf4<8, 8, 8, 8>(*(uint32_t*)(rgba8 + i * 4)) );
        avgColor /= float(pixelNum);
        texture.averageColor = Packed::uf4_to_uint<8, 8, 8, 8>( ToFloat( avgColor ) );

        if (texture.alphaMode != AlphaMode::PREMULTIPLIED && avgColor.w < 254.5f / 255.0f)
            texture.alphaMode = avgColor.w == 0.0f ? AlphaMode::OFF : AlphaMode::TRANSPARENT;

        // Useful to find a texture which is TRANSPARENT but needs to be OPAQUE or PREMULTIPLIED
        /*if (texture.alphaMode == AlphaMode::TRANSPARENT || texture.alphaMode == AlphaMode::OFF)
        {
            char s[1024];
            sprintf_s(s, "%s: %s\n", texture.alphaMode == AlphaMode::OFF ? "OFF" : "TRANSPARENT", path.c_str());
            OutputDebugStringA(s);
        }*/
    }

    return true;
}

void utils::LoadTextureFromMemory(nri::Format format, uint32_t width, uint32_t height, const uint8_t *pixels, Texture &texture)
{
    detexTexture **dTexture;
    detexLoadTextureFromMemory(DETEX_PIXEL_FORMAT_RGBA8, width, height, pixels, &dTexture);

    texture.mipNum = 1;
    texture.arraySize = 1;
    texture.depth = 1;
    texture.format = format;
    texture.alphaMode = AlphaMode::OPAQUE;
    texture.texture = dTexture;
}

bool utils::LoadScene(const std::string& path, Scene& scene, bool simpleOIT, const std::vector<float3>& instanceData)
{
    static bool isLibraryLoaded = false;
    if (!isLibraryLoaded)
    {
        std::string assimpDllPath = GetFullPath("assimp-vc141-mt.dll", DataFolder::ROOT);
        HMODULE handle = LoadLibraryA(assimpDllPath.c_str());
        isLibraryLoaded = (handle != nullptr);
    }

    const uint32_t globalInstanceCount = ((uint32_t)instanceData.size() / 2);
    constexpr uint32_t MAX_INDEX = 65535;

    // Taken from Falcor
    uint32_t aiFlags = aiProcessPreset_TargetRealtime_MaxQuality;
    aiFlags |= aiProcess_FlipUVs;
    aiFlags &= ~aiProcess_CalcTangentSpace; // Use Mikktspace instead
    aiFlags &= ~aiProcess_FindDegenerates; // Avoid converting degenerated triangles to lines
    aiFlags &= ~aiProcess_OptimizeGraph; // Never use as it doesn't handle transforms with negative determinants
    aiFlags &= ~aiProcess_OptimizeMeshes; // Avoid merging original meshes

    char s[1024];
    _splitpath_s(path.c_str(), 0, 0, s, helper::GetCountOf(s), 0, 0, 0, 0);
    std::string baseDir(s);

    aiPropertyStore* props = aiCreatePropertyStore();
    aiSetImportPropertyInteger(props, AI_CONFIG_PP_SLM_VERTEX_LIMIT, MAX_INDEX);
    aiSetImportPropertyInteger(props, AI_CONFIG_PP_RVC_FLAGS, aiComponent_COLORS);
    aiSetImportPropertyInteger(props, AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT);
    const aiScene* result = aiImportFileExWithProperties(path.c_str(), aiFlags, nullptr, props);
    aiReleasePropertyStore(props);
    if (!result)
        return false;
    const aiScene& aiScene = *result;
    const aiNode* rootNode = aiScene.mRootNode;

    uint32_t instanceNum = aiScene.mNumMeshes;
    std::map<const aiNode*, std::vector<uint32_t>> nodeToInstanceMap;
    std::vector<std::vector<NodeData>> nodesByMeshID;
    const bool loadFromNodes = EndsWithNoCase(path.c_str(), ".fbx") != 0;
    if (loadFromNodes)
    {
        nodesByMeshID.resize(aiScene.mNumMeshes);
        for (uint32_t j = 0; j < rootNode->mNumChildren; j++)
            BindNodesToMeshID(rootNode->mChildren[j], rootNode->mTransformation, nodesByMeshID);

        instanceNum = 0;
        for (uint32_t j = 0; j < helper::GetCountOf(nodesByMeshID); j++)
            instanceNum += helper::GetCountOf(nodesByMeshID[j]);

        instanceNum *= globalInstanceCount;

        scene.mSceneToWorld.SetupByRotationX( Pi(0.5f) );
    }

    // Group meshes by material
    std::vector<std::pair<uint32_t, uint32_t>> sortedMaterials(aiScene.mNumMeshes);
    {
        for (uint32_t i = 0; i < aiScene.mNumMeshes; i++)
            sortedMaterials[i] = { i, aiScene.mMeshes[i]->mMaterialIndex };

        const auto sortPred = [](const std::pair<uint32_t, uint32_t>& a, const std::pair<uint32_t, uint32_t>& b)
        { return a.second < b.second; };

        std::sort(sortedMaterials.begin(), sortedMaterials.end(), sortPred);
    }

    const uint32_t materialOffset = (uint32_t)scene.materials.size();
    scene.materials.resize(materialOffset + aiScene.mNumMaterials);

    // Meshes and instances
    const uint32_t meshOffset = (uint32_t)scene.meshes.size();
    scene.meshes.resize(meshOffset + aiScene.mNumMeshes);

    const uint32_t instanceOffset = (uint32_t)scene.instances.size();
    scene.instances.resize(instanceOffset + instanceNum);

    uint32_t totalIndices = (uint32_t)scene.indices.size();
    uint32_t totalVertices = (uint32_t)scene.vertices.size();
    uint32_t indexOffset = totalIndices;
    uint32_t vertexOffset = totalVertices;
    const Material* prevMaterial = &scene.materials[materialOffset];

    uint32_t nodeInstanceOffset = instanceOffset;
    for (uint32_t i = 0; i < aiScene.mNumMeshes; i++)
    {
        const uint32_t sortedMeshIndex = sortedMaterials[i].first;
        const aiMesh& aiMesh = *aiScene.mMeshes[sortedMeshIndex];
        const uint32_t meshIndex = meshOffset + i;

        Mesh& mesh = scene.meshes[meshIndex];
        mesh.indexOffset = totalIndices;
        mesh.vertexOffset = totalVertices;
        mesh.indexNum = aiMesh.mNumFaces * 3;
        mesh.vertexNum = aiMesh.mNumVertices;

        totalIndices += mesh.indexNum;
        totalVertices += mesh.vertexNum;

        const uint32_t materialIndex = materialOffset + sortedMaterials[i].second;
        Material* material = &scene.materials[materialIndex];
        if (loadFromNodes)
        {
            std::vector<NodeData>& relatedNodes = nodesByMeshID[sortedMeshIndex];

            for (uint32_t z = 0; z < helper::GetCountOf(relatedNodes); z++)
            {
                const aiMatrix4x4& aiTransform = relatedNodes[z].transform;

                float4x4 transform
                (
                    aiTransform.a1, aiTransform.a2, aiTransform.a3, aiTransform.a4,
                    aiTransform.b1, aiTransform.b2, aiTransform.b3, aiTransform.b4,
                    aiTransform.c1, aiTransform.c2, aiTransform.c3, aiTransform.c4,
                    0.0f, 0.0f, 0.0f, 1.0f
                );
                transform = scene.mSceneToWorld * transform;

                double3 position = ToDouble( transform.GetCol3().To3d() );

                transform.SetTranslation( float3::Zero() );

                for (uint32_t j = 0; j < globalInstanceCount; j++)
                {
                    float3 instanceRotation = DegToRad(instanceData[j * 2 + 1]);

                    float4x4 mInstanceRotation;
                    mInstanceRotation.SetupByRotationYPR(instanceRotation.x, instanceRotation.y, instanceRotation.z);

                    Instance& instance = scene.instances[nodeInstanceOffset];
                    instance.meshIndex = meshIndex;
                    instance.position = position + ToDouble(instanceData[j * 2]);
                    instance.rotationPrev.SetIdentity();
                    instance.rotation = mInstanceRotation * transform;

                    std::map<const aiNode*, std::vector<uint32_t>>::iterator nodeToInstIt = nodeToInstanceMap.find(relatedNodes[z].node);
                    if (nodeToInstIt != nodeToInstanceMap.end())
                        nodeToInstIt->second.push_back(nodeInstanceOffset);
                    else
                    {
                        std::vector<uint32_t> tmpVector = { nodeInstanceOffset };
                        nodeToInstanceMap.insert( std::make_pair(relatedNodes[z].node, tmpVector) );
                    }

                    nodeInstanceOffset++;
                }
            }

            uint32_t instancesCreated = helper::GetCountOf(relatedNodes) * globalInstanceCount;
            material->instanceNum += instancesCreated;
        }
        else
        {
            material->instanceNum++;
            Instance& instance = scene.instances[instanceOffset + i];
            instance.meshIndex = meshIndex;
            instance.rotation.SetIdentity();
            instance.rotationPrev.SetIdentity();
        }

        if (material != prevMaterial)
            material->instanceOffset = prevMaterial->instanceOffset + prevMaterial->instanceNum;

        prevMaterial = material;
    }

    // Animation
    if (aiScene.HasAnimations())
    {
        const aiAnimation* aiAnimation = aiScene.mAnimations[0];
        scene.animations.push_back(Animation());
        Animation& animation = scene.animations.back();
        animation.animationName = GetFileName(path);

        ExtractNodeTree(rootNode, nodeToInstanceMap, animation.rootNode);

        float animationTotalMs = float(1000.0 * aiAnimation->mDuration / aiAnimation->mTicksPerSecond);
        animation.durationMs = animationTotalMs;
        animation.animationNodes.resize(aiAnimation->mNumChannels);

        for(uint32_t i = 0; i < aiAnimation->mNumChannels; i++)
        {
            const aiNodeAnim* animChannel = aiAnimation->mChannels[i];
            const aiNode* affectedNode = rootNode->FindNode(animChannel->mNodeName);

            // Camera
            bool isCamera = false;
            if (aiScene.mNumCameras > 0 && strstr(animChannel->mNodeName.C_Str(), aiScene.mCameras[0]->mName.C_Str()))
            {
                NodeTree* nextNode = &animation.cameraNode;
                while (!nextNode->mChildren.empty())
                    nextNode = &nextNode->mChildren[0];

                nextNode->animationNodeID = (int32_t)i;
                nextNode->mChildren.push_back( NodeTree() );

                isCamera = true;
            }

            // Objects
            const uint64_t hash = ComputeHash(affectedNode->mName.C_Str(), affectedNode->mName.length);
            AnimationNode& animationNode = animation.animationNodes[i];

            for (uint32_t j = 0; j < animChannel->mNumPositionKeys; j++)
            {
                const aiVectorKey& positionKey = animChannel->mPositionKeys[j];
                const float time = float( positionKey.mTime / aiAnimation->mDuration );
                const double3 value = double3(positionKey.mValue.x, positionKey.mValue.y, positionKey.mValue.z);
                animationNode.positionKeys.push_back(time);
                animationNode.positionValues.push_back(value);
            }

            for (uint32_t j = 0; j < animChannel->mNumRotationKeys; j++)
            {
                const aiQuatKey& rotationKey = animChannel->mRotationKeys[j];
                const float time = float( rotationKey.mTime / aiAnimation->mDuration );
                const float4 value = float4(rotationKey.mValue.x, rotationKey.mValue.y, rotationKey.mValue.z, isCamera ? rotationKey.mValue.w : -rotationKey.mValue.w); // +/- is correct but WTF?
                animationNode.rotationKeys.push_back(time);
                animationNode.rotationValues.push_back(value);
            }

            for (uint32_t j = 0; j < animChannel->mNumScalingKeys; j++)
            {
                const aiVectorKey& scalingKey = animChannel->mScalingKeys[j];
                const float time = float( scalingKey.mTime / aiAnimation->mDuration );
                const float3 value = float3(scalingKey.mValue.x, scalingKey.mValue.y, scalingKey.mValue.z);
                animationNode.scaleKeys.push_back(time);
                animationNode.scaleValues.push_back(value);
            }

            std::function<void(const uint64_t&, NodeTree&)> findNode = [&](const uint64_t& hash, NodeTree& sceneNode)
            {
                if (hash == sceneNode.mHash)
                {
                    //sceneNode.pAnimationNode = &animationNode;
                    sceneNode.animationNodeID = (int32_t)i;
                    return;
                }

                for (NodeTree& child : sceneNode.mChildren)
                    findNode(hash, child);
            };

            findNode(hash, animation.rootNode);
        }
    }

    // Geometry
    std::vector<float4> tangents(totalVertices * 2, float4(0.0f, 0.0f, 0.0f, 0.0f));
    scene.indices.resize(totalIndices);
    scene.primitives.resize(totalIndices / 3);
    scene.vertices.resize(totalVertices);

    for (uint32_t i = 0; i < aiScene.mNumMeshes; i++)
    {
        const uint32_t sortedMeshIndex = sortedMaterials[i].first;
        const aiMesh& aiMesh = *aiScene.mMeshes[sortedMeshIndex];

        Mesh& mesh = scene.meshes[meshOffset + i];
        mesh.aabb.Clear();

        // Generate tangent basis
        GenerateTangents(aiMesh, tangents);

        // Indices
        for (uint32_t j = 0; j < aiMesh.mNumFaces; j++)
        {
            const aiFace& aiFace = aiMesh.mFaces[j];
            for (uint32_t k = 0; k < aiFace.mNumIndices; k++)
            {
                uint32_t index = aiFace.mIndices[k];
                scene.indices[indexOffset++] = (uint16_t)index;
            }
        }

        // Vertices
        for (uint32_t j = 0; j < aiMesh.mNumVertices; j++)
        {
            Vertex& vertex = scene.vertices[vertexOffset++];

            float3 position = float3(&aiMesh.mVertices[j].x);
            vertex.position[0] = position.x;
            vertex.position[1] = position.y;
            vertex.position[2] = position.z;
            mesh.aabb.Add(position);

            float3 normal = (-float3(&aiMesh.mNormals[j].x)) * 0.5f + 0.5f;
            vertex.normal = Packed::uf4_to_uint<10, 10, 10, 2>(normal);

            float4 tangent = tangents[j] * 0.5f + 0.5f;
            vertex.tangent = Packed::uf4_to_uint<10, 10, 10, 2>(tangent);

            if (aiMesh.HasTextureCoords(0))
            {
                vertex.uv = Packed::sf2_to_h2
                (
                    Min( aiMesh.mTextureCoords[0][j].x, 65504.0f ),
                    Min( aiMesh.mTextureCoords[0][j].y, 65504.0f )
                );
            }
            else
                vertex.uv = 0;
        }

        // Primitive data
        uint32_t triangleNum = mesh.indexNum / 3;
        for (uint32_t j = 0; j < triangleNum; j++)
        {
            uint32_t primitiveIndex = mesh.indexOffset / 3 + j;
            const Vertex& v0 = scene.vertices[ mesh.vertexOffset + scene.indices[primitiveIndex * 3] ];
            const Vertex& v1 = scene.vertices[ mesh.vertexOffset + scene.indices[primitiveIndex * 3 + 1] ];
            const Vertex& v2 = scene.vertices[ mesh.vertexOffset + scene.indices[primitiveIndex * 3 + 2] ];

            float2 uv0 = Packed::h2_to_sf2(v0.uv);
            float2 uv1 = Packed::h2_to_sf2(v1.uv);
            float2 uv2 = Packed::h2_to_sf2(v2.uv);

            float3 p0(v0.position);
            float3 p1(v1.position);
            float3 p2(v2.position);

            float3 d0 = p2 - p0;
            float3 d1 = p1 - p0;
            float3 triangleNormal = Cross(d0, d1);
            float worldArea = Max( Length(triangleNormal), 1e-9f );
            triangleNormal /= worldArea;

            d0 = float3(uv2.x, uv2.y, 0.0f) - float3(uv0.x, uv0.y, 0.0f);
            d1 = float3(uv1.x, uv1.y, 0.0f) - float3(uv0.x, uv0.y, 0.0f);
            float uvArea = Length( Cross(d0, d1) );

            Primitive& primitive = scene.primitives[primitiveIndex];
            primitive.worldToUvUnits = uvArea == 0 ? 1.0f : Sqrt( uvArea / worldArea );
            primitive.normal = Packed::uf4_to_uint<10, 10, 10, 2>(triangleNormal * 0.5f + 0.5f);
        }

        // Scene AABB
        if (loadFromNodes)
        {
            for (auto instance : scene.instances)
            {
                if (instance.meshIndex == meshOffset + i)
                {
                    float4x4 transform = instance.rotation;
                    transform.AddTranslation( ToFloat(instance.position) );

                    cBoxf aabb;
                    TransformAabb(transform, mesh.aabb, aabb);

                    scene.aabb.Add(aabb);
                }
            }
        }
        else
            scene.aabb.Add(mesh.aabb);
    }

    // Count textures
    aiString str;
    uint32_t textureNum = 0;
    for (uint32_t i = 0; i < aiScene.mNumMaterials; i++)
    {
        const aiMaterial* assimpMaterial = aiScene.mMaterials[i];
        for (size_t j = 0; j < gSupportedTextureTypes.size(); j++)
        {
            if (assimpMaterial->GetTexture(gSupportedTextureTypes[j], 0, &str) == AI_SUCCESS)
                textureNum++;
        }
    }

    size_t newCapacity = scene.textures.size() + textureNum;
    scene.textures.reserve(newCapacity);

    // StaticTexture::Invalid
    {
        Texture* texture = new Texture;
        const std::string& texPath = GetFullPath("checkerboard0.dds", DataFolder::TEXTURES);
        NRI_ABORT_ON_FALSE( LoadTexture(texPath, *texture, true) );
        scene.textures.push_back(texture);
    }

    // StaticTexture::Black
    {
        Texture* texture = new Texture;
        const std::string& texPath = GetFullPath("black.png", DataFolder::TEXTURES);
        NRI_ABORT_ON_FALSE( LoadTexture(texPath, *texture) );
        scene.textures.push_back(texture);
    }

    // StaticTexture::FlatNormal
    {
        Texture* texture = new Texture;
        const std::string& texPath = GetFullPath("flatnormal.png", DataFolder::TEXTURES);
        NRI_ABORT_ON_FALSE( LoadTexture(texPath, *texture) );
        scene.textures.push_back(texture);
    }

    // StaticTexture::ScramblingRanking1spp
    {
        Texture* texture = new Texture;
        const std::string& texPath = GetFullPath("scrambling_ranking_128x128_2d_1spp.png", DataFolder::TEXTURES);
        NRI_ABORT_ON_FALSE( LoadTexture(texPath, *texture) );
        texture->OverrideFormat(nri::Format::RGBA8_UINT);
        scene.textures.push_back(texture);
    }

    // StaticTexture::ScramblingRanking32spp
    {
        Texture* texture = new Texture;
        const std::string& texPath = GetFullPath("scrambling_ranking_128x128_2d_32spp.png", DataFolder::TEXTURES);
        NRI_ABORT_ON_FALSE( LoadTexture(texPath, *texture) );
        texture->OverrideFormat(nri::Format::RGBA8_UINT);
        scene.textures.push_back(texture);
    }

    // StaticTexture::SobolSequence
    {
        Texture* texture = new Texture;
        const std::string& texPath = GetFullPath("sobol_256_4d.png", DataFolder::TEXTURES);
        NRI_ABORT_ON_FALSE( LoadTexture(texPath, *texture) );
        texture->OverrideFormat(nri::Format::RGBA8_UINT);
        scene.textures.push_back(texture);
    }

    // Load only unique textures
    for (uint32_t i = 0; i < aiScene.mNumMaterials; i++)
    {
        Material& material = scene.materials[materialOffset + i];
        material.instanceOffset += instanceOffset;

        const aiMaterial* assimpMaterial = aiScene.mMaterials[i];
        uint32_t* textureIndices = &material.diffuseMapIndex;
        for (size_t j = 0; j < gSupportedTextureTypes.size(); j++)
        {
            const aiTextureType type = gSupportedTextureTypes[j];

            textureIndices[j] = StaticTexture::Black;
            if (type == aiTextureType_NORMALS)
                textureIndices[j] = StaticTexture::FlatNormal;
            // OPTIONAL - useful for debug:
            //else if (type == aiTextureType_DIFFUSE)
            //    textureIndices[j] = StaticTexture::Invalid;

            if (assimpMaterial->GetTexture(type, 0, &str) == AI_SUCCESS)
            {
                std::string texPath = baseDir + str.data;
                const uint64_t hash = ComputeHash(texPath.c_str(), (uint32_t)texPath.length());

                const auto comparePred = [&hash](const Texture* texture)
                { return hash == texture->hash; };

                auto findResult = std::find_if(scene.textures.begin(), scene.textures.end(), comparePred);
                if (findResult == scene.textures.end())
                {
                    const bool computeAverageColor = type == aiTextureType_DIFFUSE || type == aiTextureType_EMISSIVE;

                    Texture* texture = new Texture;
                    bool isLoaded = LoadTexture(texPath, *texture, computeAverageColor);

                    if (isLoaded)
                    {
                        if( type == aiTextureType_DIFFUSE || type == aiTextureType_EMISSIVE || type == aiTextureType_SPECULAR )
                            texture->OverrideFormat(MakeSRGBFormat(texture->format));

                        textureIndices[j] = (uint32_t)scene.textures.size();
                        scene.textures.push_back(texture);
                    }
                    else
                        delete texture;
                }
                else
                    textureIndices[j] = (uint32_t)(findResult - scene.textures.begin());
            }
        }

        const Texture* diffuseTexture = scene.textures[material.diffuseMapIndex];
        const Texture* emissionTexture = scene.textures[material.emissiveMapIndex];

        material.alphaMode = diffuseTexture->alphaMode;
        if (emissionTexture->averageColor)
        {
            float4 baseColor = Packed::uint_to_uf4<8, 8, 8, 8>(diffuseTexture->averageColor);
            float4 emissionColor = Packed::uint_to_uf4<8, 8, 8, 8>(emissionTexture->averageColor);
            baseColor = Pow( baseColor, float4( 2.2f ) );
            emissionColor = Pow( emissionColor, float4( 2.2f ) );
            emissionColor *= (baseColor + 0.01f) / (Max(baseColor.x, Max(baseColor.y, baseColor.z)) + 0.01f);
            emissionColor.w = 1.0f;
            emissionColor = Pow( emissionColor, float4( 1.0f / 2.2f ) );
            material.averageBaseColor = Packed::uf4_to_uint<8, 8, 8, 8>(emissionColor);
        }
        else
            material.averageBaseColor = diffuseTexture->averageColor & 0x00FFFFFF;
    }

    // Sort materials by transparency type
    const auto sortPred = [&scene](const Material& a, const Material& b)
    { return a.alphaMode < b.alphaMode; };

    std::sort(scene.materials.begin(), scene.materials.end(), sortPred);

    // Merge materials into groups
    scene.materialsGroups.clear();
    AlphaMode prevAlphaMode = AlphaMode(-1);
    for (size_t i = 0; i < scene.materials.size(); i++)
    {
        const Material& material = scene.materials[i];

        const AlphaMode alphaMode = material.alphaMode;
        if (alphaMode != prevAlphaMode)
        {
            scene.materialsGroups.push_back( {(uint32_t)i, 0} );
            prevAlphaMode = alphaMode;
        }
        MaterialGroup& materialGroup = scene.materialsGroups.back();
        materialGroup.materialNum++;

        // Bind materials here since they get sorted above
        for (uint32_t j = 0; j < material.instanceNum; j++)
        {
            uint32_t index = material.instanceOffset + j;
            scene.instances[index].materialIndex = (uint32_t)i;
        }
    }

    // Duplicate TRANSPARENT for simple OIT - render back faces, render front faces
    if (simpleOIT && scene.materialsGroups.size() == 3)
        scene.materialsGroups.push_back(scene.materialsGroups.back());

    aiReleaseImport(&aiScene);

    return true;
}

void utils::AnimationNode::Animate(float time)
{
    float3 scale = scaleValues.back();
    if (time < scaleKeys.back())
    {
        uint32_t firstID = FindCurrentIndex(scaleKeys, time);
        uint32_t secondID = (firstID + 1) % scaleKeys.size();

        float weight = SafeLinearstep(scaleKeys[firstID], scaleKeys[secondID], time);
        scale = Lerp(scaleValues[firstID], scaleValues[secondID], float3(weight));
    }

    float4 rotation = rotationValues.back();
    if (time < rotationKeys.back())
    {
        uint32_t firstID = FindCurrentIndex(rotationKeys, time);
        uint32_t secondID = (firstID + 1) % rotationKeys.size();

        float weight = SafeLinearstep(rotationKeys[firstID], rotationKeys[secondID], time);
        float4 a = rotationValues[firstID];
        float4 b = rotationValues[secondID];
        float theta = Dot44(a, b);
        a = (theta < 0.0f) ? -a : a;
        rotation = Slerp(a, b, weight);
    }

    double3 position = positionValues.back();
    if (time < positionKeys.back())
    {
        uint32_t firstID = FindCurrentIndex(positionKeys, time);
        uint32_t secondID = (firstID + 1) % positionKeys.size();

        float weight = SafeLinearstep(positionKeys[firstID], positionKeys[secondID], time);
        position = Lerp(positionValues[firstID], positionValues[secondID], double3((double)weight));
    }

    float4x4 mScale;
    mScale.SetupByScale(scale);

    float4x4 mRotation;
    mRotation.SetupByQuaternion(rotation);

    float4x4 mTranslation;
    mTranslation.SetupByTranslation( ToFloat(position) );

    mTransform = mTranslation * (mRotation * mScale);
}

void utils::NodeTree::Animate(Scene& scene, std::vector<AnimationNode>& animationNodes, const float4x4& parentTransform, float4x4* outTransform)
{
    const float4x4& transform = animationNodeID != -1 ? animationNodes[animationNodeID].mTransform : mTransform;
    float4x4 combinedTransform = parentTransform * transform;

    for (NodeTree& child : mChildren)
        child.Animate(scene, animationNodes, combinedTransform, outTransform);

    if (outTransform && mChildren.empty())
        *outTransform = combinedTransform;

    if (!mInstances.empty())
    {
        double3 position = ToDouble( combinedTransform.GetCol3().To3d() );
        combinedTransform.SetTranslation( float3::Zero() );

        for (const uint32_t& instanceId : mInstances)
        {
            Instance& sceneInstance = scene.instances[instanceId];
            sceneInstance.rotation = combinedTransform;
            sceneInstance.position = position;
        }
    }
}

void utils::Scene::Animate(float animationSpeed, float elapsedTime, float& animationProgress, uint32_t animationID, float4x4* outCameraTransform)
{
    if (animations.empty())
        return;

    animationID = animationID > helper::GetCountOf(animations) ? helper::GetCountOf(animations) : animationID;
    Animation& selectedAnimation = animations[animationID];

    // Update animation
    const float sceneAnimationDelta = selectedAnimation.durationMs == 0.0f ? 0.0f : animationSpeed / selectedAnimation.durationMs;

    float normalizedTime = animationProgress * 0.01f;
    normalizedTime += elapsedTime * sceneAnimationDelta * selectedAnimation.sign;
    if (normalizedTime >= 1.0f || normalizedTime < 0.0f)
{
        selectedAnimation.sign = -selectedAnimation.sign;
        normalizedTime = Saturate(normalizedTime);
    }
    animationProgress = normalizedTime * 100.0f;

    for (AnimationNode& animationNode : selectedAnimation.animationNodes)
        animationNode.Animate(normalizedTime);

    selectedAnimation.rootNode.Animate(*this, selectedAnimation.animationNodes, mSceneToWorld);

    if (outCameraTransform)
    {
        float4x4 transform;
        selectedAnimation.cameraNode.Animate(*this, selectedAnimation.animationNodes, float4x4::identity, &transform);

        float4x4 m = mSceneToWorld * transform;
        m.Transpose();

        *outCameraTransform = mSceneToWorld * m; // = [mSceneToWorld * transform * (mSceneToWorld)T]T
    }
}
