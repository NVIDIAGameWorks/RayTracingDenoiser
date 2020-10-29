/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include <cstdint>
#include <array>

// TODO: Key scan codes from dinput.h. Are they same on Linux?
enum class Key : uint8_t
{
    ESCAPE          = 0x01,
    _1              = 0x02,
    _2              = 0x03,
    _3              = 0x04,
    _4              = 0x05,
    _5              = 0x06,
    _6              = 0x07,
    _7              = 0x08,
    _8              = 0x09,
    _9              = 0x0A,
    _0              = 0x0B,
    Minus           = 0x0C,   // - on main keyboard
    Equals          = 0x0D,
    Back            = 0x0E,   // backspace
    Tab             = 0x0F,
    Q               = 0x10,
    W               = 0x11,
    E               = 0x12,
    R               = 0x13,
    T               = 0x14,
    Y               = 0x15,
    U               = 0x16,
    I               = 0x17,
    O               = 0x18,
    P               = 0x19,
    LBracket        = 0x1A,
    RBracket        = 0x1B,
    Return          = 0x1C,   // Enter on main keyboard
    LControl        = 0x1D,
    A               = 0x1E,
    S               = 0x1F,
    D               = 0x20,
    F               = 0x21,
    G               = 0x22,
    H               = 0x23,
    J               = 0x24,
    K               = 0x25,
    L               = 0x26,
    Semicolon       = 0x27,
    Apostrophe      = 0x28,
    Grave           = 0x29,   // accent grave
    LShift          = 0x2A,
    BackSlash       = 0x2B,
    Z               = 0x2C,
    X               = 0x2D,
    C               = 0x2E,
    V               = 0x2F,
    B               = 0x30,
    N               = 0x31,
    M               = 0x32,
    Comma           = 0x33,
    Period          = 0x34,   // . on main keyboard
    Slash           = 0x35,   // / on main keyboard
    RShift          = 0x36,
    Multiply        = 0x37,   // * on numeric keypad
    LMenu           = 0x38,   // left Alt
    Space           = 0x39,
    Capital         = 0x3A,
    F1              = 0x3B,
    F2              = 0x3C,
    F3              = 0x3D,
    F4              = 0x3E,
    F5              = 0x3F,
    F6              = 0x40,
    F7              = 0x41,
    F8              = 0x42,
    F9              = 0x43,
    F10             = 0x44,
    NumLock         = 0x45,
    Scroll          = 0x46,    // Scroll Lock
    Num7            = 0x47,
    Num8            = 0x48,
    Num9            = 0x49,
    Subtract        = 0x4A,    // - on numeric keypad
    Num4            = 0x4B,
    Num5            = 0x4C,
    Num6            = 0x4D,
    Add             = 0x4E,    // + on numeric keypad
    Num1            = 0x4F,
    Num2            = 0x50,
    Num3            = 0x51,
    Num0            = 0x52,
    Decimal         = 0x53,    // . on numeric keypad
    Oem102          = 0x56,    // <> or \| on RT 102-key keyboard (Non-U.S.)
    F11             = 0x57,
    F12             = 0x58,
    F13             = 0x64,    //                     (NEC PC98)
    F14             = 0x65,    //                     (NEC PC98)
    F15             = 0x66,    //                     (NEC PC98)
    Kana            = 0x70,    // (Japanese keyboard)
    AbntC1          = 0x73,    // /? on Brazilian keyboard
    Convert         = 0x79,    // (Japanese keyboard)
    NoConvert       = 0x7B,    // (Japanese keyboard)
    Yen             = 0x7D,    // (Japanese keyboard)
    AbntC2          = 0x7E,    // Numpad . on Brazilian keyboard
    NumEquals       = 0x8D,    // = on numeric keypad (NEC PC98)
    PrevTrack       = 0x90,    // Previous Track (CIRCUMFLEX on Japanese keyboard)
    At              = 0x91,    //                     (NEC PC98)
    Colon           = 0x92,    //                     (NEC PC98)
    Underline       = 0x93,    //                     (NEC PC98)
    Kanji           = 0x94,    // (Japanese keyboard)
    Stop            = 0x95,    //                     (NEC PC98)
    Ax              = 0x96,    //                     (Japan AX)
    Unlabeled       = 0x97,    //                        (J3100)
    NextTrack       = 0x99,    // Next Track
    NumEnter        = 0x9C,    // Enter on numeric keypad
    RControl        = 0x9D,
    Mute            = 0xA0,    // Mute
    Calculator      = 0xA1,    // Calculator
    PlayPause       = 0xA2,    // Play / Pause
    MediaStop       = 0xA4,    // Media Stop
    VolumeDown      = 0xAE,    // Volume -
    VolumeUp        = 0xB0,    // Volume +
    WebHome         = 0xB2,    // Web home
    NumComma        = 0xB3,    // , on numeric keypad (NEC PC98)
    Divide          = 0xB5,    // / on numeric keypad
    SysRq           = 0xB7,
    RMenu           = 0xB8,    // right Alt
    Pause           = 0xC5,    // Pause
    Home            = 0xC7,    // Home on arrow keypad
    Up              = 0xC8,    // UpArrow on arrow keypad
    Prior           = 0xC9,    // PgUp on arrow keypad
    Left            = 0xCB,    // LeftArrow on arrow keypad
    Right           = 0xCD,    // RightArrow on arrow keypad
    End             = 0xCF,    // End on arrow keypad
    Down            = 0xD0,    // DownArrow on arrow keypad
    Next            = 0xD1,    // PgDn on arrow keypad
    Insert          = 0xD2,    // Insert on arrow keypad
    Del             = 0xD3,    // Delete on arrow keypad
    Lwin            = 0xDB,    // Left Windows key
    Rwin            = 0xDC,    // Right Windows key
    Apps            = 0xDD,    // AppMenu key
    Power           = 0xDE,    // System Power
    Sleep           = 0xDF,    // System Sleep
    Wake            = 0xE3,    // System Wake
    WebSearch       = 0xE5,    // Web Search
    WebFavorites    = 0xE6,    // Web Favorites
    WebRefresh      = 0xE7,    // Web Refresh
    WebStop         = 0xE8,    // Web Stop
    WebForward      = 0xE9,    // Web Forward
    WebBack         = 0xEA,    // Web Back
    MyComputer      = 0xEB,    // My Computer
    Mail            = 0xEC,    // Mail
    MediaSelect     = 0xED,    // Media Select

    // Alternate names for keys, to facilitate transition from DOS.
    Backspace       = Back,            // backspace
    Numstar         = Multiply,        // * on numeric keypad
    LAlt            = LMenu,           // left Alt
    Capslock        = Capital,         // CapsLock
    Numminus        = Subtract,        // - on numeric keypad
    Numplus         = Add,             // + on numeric keypad
    Numperiod       = Decimal,         // . on numeric keypad
    Numslash        = Divide,          // / on numeric keypad
    RAlt            = RMenu,           // right Alt
    Uparrow         = Up,              // UpArrow on arrow keypad
    Pgup            = Prior,           // PgUp on arrow keypad
    Leftarrow       = Left,            // LeftArrow on arrow keypad
    Rightarrow      = Right,           // RightArrow on arrow keypad
    Downarrow       = Down,            // DownArrow on arrow keypad
    Pgdn            = Next,            // PgDn on arrow keypad

    // Alternate names for keys originally not used on US keyboards.
    Circumflex      = PrevTrack       // Japanese keyboard
};

enum class Button : uint8_t
{
    Left,
    Right,
    Middle,
    Mouse4,
    Mouse5
};

class Input
{
public:
    bool Inititialize(void* hwnd);
    void Process(void* data);
    void Prepare();

    inline bool IsKeyToggled(Key key)
    {
        bool state = m_KeyToggled[(uint8_t)key];
        m_KeyToggled[(uint8_t)key] = false;

        return state;
    }

    inline bool IsKeyPressed(Key key) const
    { return m_KeyState[(uint8_t)key]; }

    inline bool IsButtonPressed(Button button) const
    { return m_ButtonState[(uint8_t)button]; }

    inline float GetMouseDx() const
    { return m_MouseDx; }

    inline float GetMouseDy() const
    { return m_MouseDy; }

    inline float GetMouseWheel() const
    { return m_MouseWheel; }

private:
    std::array<bool, 256> m_KeyState = {};
    std::array<bool, 256> m_KeyToggled = {};
    std::array<bool, 256> m_ButtonState = {};
    float m_MouseDx = 0.0f;
    float m_MouseDy = 0.0f;
    float m_MouseWheel = 0.0f;
};
