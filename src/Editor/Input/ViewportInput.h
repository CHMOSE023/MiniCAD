#pragma once
#include "KeyCode.h"
#include <cstdint>
#include <imgui.h>
#include <array>
#include <DirectXMath.h>
using namespace DirectX;
namespace MiniCAD
{
    struct ButtonState
    {
        bool Down     = false;   // 当前是否按住
        bool Pressed  = false;   // 本帧按下
        bool Released = false;   // 本帧释放
    };

    struct ViewportInput
    {
        bool     Valid      = false;
        bool     Hovered    = false;
        bool     Active     = false;
        bool     Focused    = false;

        XMFLOAT2 Size       = { 0.f, 0.f };
        XMFLOAT2 ScreenMin  = { 0.f, 0.f };
        XMFLOAT2 ScreenMax  = { 0.f, 0.f };

        XMFLOAT2 MouseLocal = { 0.f, 0.f };
        XMFLOAT2 MouseDelta = { 0.f, 0.f };

        float    Wheel = 0.f;

        std::array<ButtonState, 3> MouseButtons;

        std::array<ButtonState, (size_t)KeyCode::COUNT> Keys;

        uint8_t Modifiers = 0;
    };
}
