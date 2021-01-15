/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SharedD3D12.h"
#include "FrameBufferD3D12.h"
#include "DeviceD3D12.h"
#include "DescriptorD3D12.h"

using namespace nri;

extern void ConvertRects(D3D12_RECT* rectsD3D12, const Rect* rects, uint32_t rectNum);

FrameBufferD3D12::FrameBufferD3D12(DeviceD3D12& device)
    : m_Device(device)
    , m_RenderTargets(device.GetStdAllocator())
    , m_ClearDescs(device.GetStdAllocator())
{}

Result FrameBufferD3D12::Create(const FrameBufferDesc& frameBufferDesc)
{
    if (frameBufferDesc.colorAttachmentNum)
    {
        m_RenderTargets.resize(frameBufferDesc.colorAttachmentNum);

        for (uint32_t i = 0; i < frameBufferDesc.colorAttachmentNum; i++)
        {
            m_RenderTargets[i] = { ((DescriptorD3D12*)frameBufferDesc.colorAttachments[i])->GetPointerCPU() };

            if (frameBufferDesc.colorClearValues)
            {
                ClearDesc clearDesc = { frameBufferDesc.colorClearValues[i], AttachmentContentType::COLOR, i };
                m_ClearDescs.push_back(clearDesc);
            }
        }
    }

    if (frameBufferDesc.depthStencilAttachment)
    {
        m_DepthStencilTarget = { ((DescriptorD3D12*)frameBufferDesc.depthStencilAttachment)->GetPointerCPU() };

        if (frameBufferDesc.depthStencilClearValue)
        {
            ClearDesc clearDesc = { *frameBufferDesc.depthStencilClearValue, AttachmentContentType::DEPTH_STENCIL, 0 };
            m_ClearDescs.push_back(clearDesc);
        }
    }

    return Result::SUCCESS;
}

void FrameBufferD3D12::Bind(ID3D12GraphicsCommandList* graphicsCommandList, RenderPassBeginFlag renderPassBeginFlag)
{
    graphicsCommandList->OMSetRenderTargets(
        (UINT)m_RenderTargets.size(),
        &m_RenderTargets[0],
        FALSE,
        m_DepthStencilTarget.ptr != 0 ? &m_DepthStencilTarget : nullptr
    );

    if (renderPassBeginFlag == RenderPassBeginFlag::SKIP_FRAME_BUFFER_CLEAR || m_ClearDescs.empty())
        return;

    Clear(graphicsCommandList, &m_ClearDescs[0], (uint32_t)m_ClearDescs.size(), nullptr, 0);
}

void FrameBufferD3D12::Clear(ID3D12GraphicsCommandList* graphicsCommandList, const ClearDesc* clearDescs, uint32_t clearDescNum, const Rect* rects, uint32_t rectNum)
{
    D3D12_RECT* rectsD3D12 = STACK_ALLOC(D3D12_RECT, rectNum);
    ConvertRects(rectsD3D12, rects, rectNum);

    for (uint32_t i = 0; i < clearDescNum; i++)
    {
        if (AttachmentContentType::COLOR == clearDescs[i].attachmentContentType)
        {
            if (clearDescs[i].colorAttachmentIndex < m_RenderTargets.size())
                graphicsCommandList->ClearRenderTargetView(m_RenderTargets[clearDescs[i].colorAttachmentIndex], &clearDescs[i].value.rgba32f.r, rectNum, rectsD3D12);
        }
        else if (m_DepthStencilTarget.ptr)
        {
            D3D12_CLEAR_FLAGS clearFlags = (D3D12_CLEAR_FLAGS)0;
            switch (clearDescs[i].attachmentContentType)
            {
            case AttachmentContentType::DEPTH:
                clearFlags = D3D12_CLEAR_FLAG_DEPTH;
                break;
            case AttachmentContentType::STENCIL:
                clearFlags = D3D12_CLEAR_FLAG_STENCIL;
                break;
            case AttachmentContentType::DEPTH_STENCIL:
                clearFlags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
                break;
            }

            if (clearFlags)
                graphicsCommandList->ClearDepthStencilView(m_DepthStencilTarget, clearFlags, clearDescs[i].value.depthStencil.depth, clearDescs[i].value.depthStencil.stencil, rectNum, rectsD3D12);
        }
    }
}

void FrameBufferD3D12::SetDebugName(const char* name)
{}
