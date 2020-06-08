/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include "NRI.h"

namespace nri
{
    inline Format GetSupportedDepthFormat(const nri::CoreInterface& coreInterface, const Device& device, uint32_t minBits, bool stencil)
    {
        if (stencil)
        {
            if (minBits <= 24)
            {
                if (coreInterface.GetFormatSupport(device, Format::D24_UNORM_S8_UINT) & FormatSupportBits::DEPTH_STENCIL_ATTACHMENT)
                    return Format::D24_UNORM_S8_UINT;
            }
        }
        else
        {
            if (minBits <= 16)
            {
                if (coreInterface.GetFormatSupport(device, Format::D16_UNORM) & FormatSupportBits::DEPTH_STENCIL_ATTACHMENT)
                    return Format::D16_UNORM;
            }
            else if (minBits <= 24)
            {
                if (coreInterface.GetFormatSupport(device, Format::D24_UNORM_S8_UINT) & FormatSupportBits::DEPTH_STENCIL_ATTACHMENT)
                    return Format::D24_UNORM_S8_UINT;
            }

            if (coreInterface.GetFormatSupport(device, Format::D32_SFLOAT) & FormatSupportBits::DEPTH_STENCIL_ATTACHMENT)
                return Format::D32_SFLOAT;
        }

        if (coreInterface.GetFormatSupport(device, Format::D32_SFLOAT_S8_UINT_X24) & FormatSupportBits::DEPTH_STENCIL_ATTACHMENT)
            return Format::D32_SFLOAT_S8_UINT_X24;

        return Format::UNKNOWN;
    }

    struct CTextureDesc : TextureDesc
    {
        CTextureDesc() = default;
        CTextureDesc(const TextureDesc& textureDesc) : TextureDesc(textureDesc)
        {}

        static constexpr TextureUsageBits DEFAULT_USAGE_MASK = TextureUsageBits::SHADER_RESOURCE;

        static CTextureDesc Texture1D(Format format, uint16_t width, uint16_t mipNum = 1, uint16_t arraySize = 1, TextureUsageBits usageMask = DEFAULT_USAGE_MASK);
        static CTextureDesc Texture2D(Format format, uint16_t width, uint16_t height, uint16_t mipNum = 1, uint16_t arraySize = 1, TextureUsageBits usageMask = DEFAULT_USAGE_MASK, uint8_t sampleNum = 1);
        static CTextureDesc Texture3D(Format format, uint16_t width, uint16_t height, uint16_t depth, uint16_t mipNum = 1, TextureUsageBits usageMask = DEFAULT_USAGE_MASK);
    };

    static TextureTransitionBarrierDesc TextureTransition(Texture* texture, AccessBits prevAccess, AccessBits nextAccess, TextureLayout prevLayout, TextureLayout nextLayout,
            uint16_t mipOffset = 0, uint16_t mipNum = nri::REMAINING_MIP_LEVELS, uint16_t arrayOffset = 0, uint16_t arraySize = nri::REMAINING_ARRAY_LAYERS);

    static TextureTransitionBarrierDesc TextureTransition(Texture* texture, AccessBits nextAccess, TextureLayout nextLayout,
            uint16_t mipOffset = 0, uint16_t mipNum = nri::REMAINING_MIP_LEVELS, uint16_t arrayOffset = 0, uint16_t arraySize = nri::REMAINING_ARRAY_LAYERS);

    static const TextureTransitionBarrierDesc& TextureTransition(TextureTransitionBarrierDesc& prevState, AccessBits nextAccess, TextureLayout nextLayout, uint16_t mipOffset = 0, uint16_t mipNum = nri::REMAINING_MIP_LEVELS);

    // Implementation

    inline CTextureDesc CTextureDesc::Texture1D(Format format, uint16_t width, uint16_t mipNum, uint16_t arraySize, TextureUsageBits usageMask)
    {
        TextureDesc textureDesc = {};
        textureDesc.type = TextureType::TEXTURE_1D;
        textureDesc.format = format;
        textureDesc.usageMask = usageMask;
        textureDesc.size[0] = width;
        textureDesc.size[1] = 1;
        textureDesc.size[2] = 1;
        textureDesc.mipNum = mipNum;
        textureDesc.arraySize = arraySize;
        textureDesc.sampleNum = 1;

        return CTextureDesc(textureDesc);
    }

    inline CTextureDesc CTextureDesc::Texture2D(Format format, uint16_t width, uint16_t height, uint16_t mipNum, uint16_t arraySize, TextureUsageBits usageMask, uint8_t sampleNum)
    {
        TextureDesc textureDesc = {};
        textureDesc.type = TextureType::TEXTURE_2D;
        textureDesc.format = format;
        textureDesc.usageMask = usageMask;
        textureDesc.size[0] = width;
        textureDesc.size[1] = height;
        textureDesc.size[2] = 1;
        textureDesc.mipNum = mipNum;
        textureDesc.arraySize = arraySize;
        textureDesc.sampleNum = sampleNum;

        return CTextureDesc(textureDesc);
    }

    inline CTextureDesc CTextureDesc::Texture3D(Format format, uint16_t width, uint16_t height, uint16_t depth, uint16_t mipNum, TextureUsageBits usageMask)
    {
        TextureDesc textureDesc = {};
        textureDesc.type = TextureType::TEXTURE_3D;
        textureDesc.format = format;
        textureDesc.usageMask = usageMask;
        textureDesc.size[0] = width;
        textureDesc.size[1] = height;
        textureDesc.size[2] = depth;
        textureDesc.mipNum = mipNum;
        textureDesc.arraySize = 1;
        textureDesc.sampleNum = 1;

        return CTextureDesc(textureDesc);
    }

    inline TextureTransitionBarrierDesc TextureTransition(Texture* texture, AccessBits prevAccess, AccessBits nextAccess, TextureLayout prevLayout, TextureLayout nextLayout,
        uint16_t mipOffset, uint16_t mipNum, uint16_t arrayOffset, uint16_t arraySize)
    {
        TextureTransitionBarrierDesc textureTransitionBarrierDesc = {};
        textureTransitionBarrierDesc.texture = texture;
        textureTransitionBarrierDesc.prevAccess = prevAccess;
        textureTransitionBarrierDesc.nextAccess = nextAccess;
        textureTransitionBarrierDesc.prevLayout = prevLayout;
        textureTransitionBarrierDesc.nextLayout = nextLayout;
        textureTransitionBarrierDesc.mipOffset = mipOffset;
        textureTransitionBarrierDesc.mipNum = mipNum;
        textureTransitionBarrierDesc.arrayOffset = arrayOffset;
        textureTransitionBarrierDesc.arraySize = arraySize;

        return textureTransitionBarrierDesc;
    }

    inline TextureTransitionBarrierDesc TextureTransition(Texture* texture, AccessBits nextAccess, TextureLayout nextLayout,
        uint16_t mipOffset, uint16_t mipNum, uint16_t arrayOffset, uint16_t arraySize)
    {
        TextureTransitionBarrierDesc textureTransitionBarrierDesc = {};
        textureTransitionBarrierDesc.texture = texture;
        textureTransitionBarrierDesc.prevAccess = nri::AccessBits::UNKNOWN;
        textureTransitionBarrierDesc.nextAccess = nextAccess;
        textureTransitionBarrierDesc.prevLayout = nri::TextureLayout::UNKNOWN;
        textureTransitionBarrierDesc.nextLayout = nextLayout;
        textureTransitionBarrierDesc.mipOffset = mipOffset;
        textureTransitionBarrierDesc.mipNum = mipNum;
        textureTransitionBarrierDesc.arrayOffset = arrayOffset;
        textureTransitionBarrierDesc.arraySize = arraySize;

        return textureTransitionBarrierDesc;
    }

    inline const TextureTransitionBarrierDesc& TextureTransition(TextureTransitionBarrierDesc& prevState, AccessBits nextAccess, TextureLayout nextLayout, uint16_t mipOffset, uint16_t mipNum)
    {
        prevState.mipOffset = mipOffset;
        prevState.mipNum = mipNum;
        prevState.prevAccess = prevState.nextAccess;
        prevState.nextAccess = nextAccess;
        prevState.prevLayout = prevState.nextLayout;
        prevState.nextLayout = nextLayout;

        return prevState;
    }
}