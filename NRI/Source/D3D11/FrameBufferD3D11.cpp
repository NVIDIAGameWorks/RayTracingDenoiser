/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedExternal.h"
#include "SharedD3D11.h"
#include "FrameBufferD3D11.h"

#include "DescriptorD3D11.h"

using namespace nri;

FrameBufferD3D11::FrameBufferD3D11(DeviceD3D11& device, const FrameBufferDesc& desc) :
    m_Device(device),
    m_ClearDescs(device.GetStdAllocator()),
    m_RenderTargets(device.GetStdAllocator())
{
    uint32_t clearDescNum = 0;
    if (desc.colorAttachments && desc.colorClearValues)
        clearDescNum += desc.colorAttachmentNum;
    if (desc.depthStencilAttachment && desc.depthStencilClearValue)
        clearDescNum++;

    m_ClearDescs.resize(clearDescNum);
    m_RenderTargets.resize(desc.colorAttachmentNum);

    if (desc.colorAttachments)
    {
        for (uint32_t i = 0; i < desc.colorAttachmentNum; i++)
        {
            const DescriptorD3D11& descriptor = *(DescriptorD3D11*)desc.colorAttachments[i];
            m_RenderTargets[i] = descriptor;

            if (desc.colorClearValues)
                m_ClearDescs[i] = {desc.colorClearValues[i], AttachmentContentType::COLOR, i};
        }
    }

    if (desc.depthStencilAttachment)
    {
        const DescriptorD3D11& descriptor = *(DescriptorD3D11*)desc.depthStencilAttachment;
        m_DepthStencil = descriptor;

        if (desc.depthStencilClearValue)
            m_ClearDescs[desc.colorAttachmentNum] = {*desc.depthStencilClearValue, AttachmentContentType::DEPTH_STENCIL, 0};
    }
}

FrameBufferD3D11::~FrameBufferD3D11()
{
}

void FrameBufferD3D11::Bind(VersionedContext& context, FramebufferBindFlag bindFlag) const
{
    if (bindFlag != FramebufferBindFlag::SKIP_CLEAR && !m_ClearDescs.empty())
        ClearAttachments(context, &m_ClearDescs[0], (uint32_t)m_ClearDescs.size(), nullptr, 0);

    context->OMSetRenderTargets((uint32_t)m_RenderTargets.size(), &m_RenderTargets[0], m_DepthStencil);
}

void FrameBufferD3D11::ClearAttachments(VersionedContext& context, const ClearDesc* clearDescs, uint32_t clearDescNum, const Rect* rects, uint32_t rectNum) const
{
    if (!rects || !rectNum)
    {
        for (uint32_t i = 0; i < clearDescNum; i++)
        {
            const ClearDesc& clearDesc = clearDescs[i];

            switch (clearDesc.attachmentContentType)
            {
            case AttachmentContentType::COLOR:
                {
                    context->ClearRenderTargetView(m_RenderTargets[clearDesc.colorAttachmentIndex], &clearDesc.value.rgba32f.r);
                }
                break;
            case AttachmentContentType::DEPTH:
                {
                    const float clearDepth = clearDesc.value.depthStencil.depth;
                    context->ClearDepthStencilView(m_DepthStencil, D3D11_CLEAR_DEPTH, clearDepth, 0);
                }
                break;
            case AttachmentContentType::STENCIL:
                {
                    const uint32_t clearStencil = clearDesc.value.depthStencil.stencil;
                    context->ClearDepthStencilView(m_DepthStencil, D3D11_CLEAR_STENCIL, 0.0f, clearStencil);
                }
                break;
            case AttachmentContentType::DEPTH_STENCIL:
                {
                    const float clearDepth = clearDesc.value.depthStencil.depth;
                    const uint32_t clearStencil = clearDesc.value.depthStencil.stencil;
                    context->ClearDepthStencilView(m_DepthStencil, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, clearDepth, clearStencil);
                }
                break;
            }
        }
    }
    else
    {
        D3D11_RECT* winRect = STACK_ALLOC(D3D11_RECT, rectNum);

        for (uint32_t i = 0; i < rectNum; i++)
        {
            const Rect& rect = rects[i];
            winRect[i] = { rect.left, rect.top, (LONG)(rect.left + rect.width), (LONG)(rect.top + rect.height) };
        }

        if (context.version >= 1)
        {
            for (uint32_t i = 0; i < clearDescNum; i++)
            {
                const ClearDesc& clearDesc = clearDescs[i];
                context->ClearView(m_RenderTargets[clearDesc.colorAttachmentIndex], &clearDesc.value.rgba32f.r, &winRect[0], rectNum);
            }
        }
        else
        {
            CHECK(m_Device.GetLog(), false, "Add 'ClearView' emulation!");
        }
    }
}

void FrameBufferD3D11::SetDebugName(const char* name)
{
}

#include "FrameBufferD3D11.hpp"
