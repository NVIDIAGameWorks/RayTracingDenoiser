/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "Input.h"

#include <assert.h>
#include <windows.h>

bool Input::Inititialize(void* hwnd)
{
    RAWINPUTDEVICE Rid[2] = {};

    Rid[0].usUsagePage  = 0x01;
    Rid[0].usUsage      = 0x02;
    Rid[0].hwndTarget   = (HWND)hwnd;

    Rid[1].usUsagePage  = 0x01;
    Rid[1].usUsage      = 0x06;
    Rid[1].hwndTarget   = (HWND)hwnd;

    BOOL res = RegisterRawInputDevices(Rid, 2, sizeof(RAWINPUTDEVICE));

    return res == TRUE;
}

void Input::Process(void* data)
{
    char buffer[1024];
    const RAWINPUT* rawInput = (RAWINPUT*)buffer;
    const RAWMOUSE* rawMouse = &rawInput->data.mouse;
    const RAWKEYBOARD* rawKeyboard = &rawInput->data.keyboard;

    uint32_t bytes = 0;
    GetRawInputData((HRAWINPUT)data, RID_INPUT, nullptr, &bytes, sizeof(RAWINPUTHEADER));
    assert( bytes <= sizeof(buffer) );
    GetRawInputData((HRAWINPUT)data, RID_INPUT, buffer, &bytes, sizeof(RAWINPUTHEADER));

    if (rawInput->header.dwType == RIM_TYPEMOUSE)
    {
        for (uint32_t i = 0; i < 5; i++)
        {
            const uint32_t downBit = 1 << (i << 1);
            const uint32_t upBit = downBit << 1;

            if ( rawMouse->usButtonFlags & downBit )
                m_ButtonState[i] = true;
            if ( rawMouse->usButtonFlags & upBit )
                m_ButtonState[i] = false;
        }

        if (rawMouse->usButtonFlags & RI_MOUSE_WHEEL)
            m_MouseWheel = float((short)rawMouse->usButtonData);

        // RDP is not supported!
        if (!(rawMouse->usFlags & MOUSE_MOVE_ABSOLUTE))
        {
            m_MouseDx = float(rawMouse->lLastX);
            m_MouseDy = float(rawMouse->lLastY);
        }
    }
    else if (rawInput->header.dwType == RIM_TYPEKEYBOARD)
    {
        uint8_t code = rawKeyboard->MakeCode & 0x7F;
        if (rawKeyboard->Flags & RI_KEY_E0)
            code |= 0x80;

        if (code != 0xAA)
            m_KeyState[code] = !(rawKeyboard->Flags & RI_KEY_BREAK);
    }
}

void Input::Prepare()
{
    m_MouseDx = 0.0f;
    m_MouseDy = 0.0f;
    m_MouseWheel = 0.0f;
}
