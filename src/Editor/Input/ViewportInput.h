#pragma once
#include <cstdint>
#include <imgui.h>
#include <DirectXMath.h>
using namespace DirectX;
namespace MiniCAD
{
    struct ViewportInput
    {
        bool      Valid              = false;
        bool      Hovered            = false;
        bool      Active             = false;
        bool      Focused            = false; 
		XMFLOAT2  Size               = { 0.f, 0.f }; 
        XMFLOAT2  ScreenMin          = { 0.f, 0.f };
        XMFLOAT2  ScreenMax          = { 0.f, 0.f };
        XMFLOAT2  MouseLocal         = { 0.f, 0.f };
        XMFLOAT2  MouseDelta         = { 0.f, 0.f }; 
        float     Wheel              = 0.f; 
        bool      MouseClicked [3]   = {};
        bool      MouseReleased[3]   = {};
        bool      MouseDown    [3]   = {}; 
        bool      KeyPressed   [512] = {};
        bool      KeyReleased  [512] = {}; 
        uint8_t   Modifiers          = 0;
    };
}
