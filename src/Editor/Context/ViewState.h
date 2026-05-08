#pragma once 
#include "Render/D3D11/Shader.h"
#include <span>
#include <vector>
#include <DirectXMath.h>
namespace MiniCAD
{
    struct DragRect
    {
        bool     Active = false;
        XMFLOAT2 Start  = { .0f,.0f };                // 鼠标按下
        XMFLOAT2 End    = { .0f,.0f };                // 当前鼠标
        XMFLOAT4 Color  = { 0.3f, 0.6f, 1.0f, 0.2f }; // 填充
        XMFLOAT4 Border = { 0.3f, 0.6f, 1.0f, 1.0f }; // 边框
    };

    struct GripDraw
    {
        enum class Type : uint8_t
        {
            Start,
            Mid,
            End,
            Corner,     // 多段线
            Center,     // CAD 圆心
            Tangent     // 曲线控制点
        };

        DirectX::XMFLOAT2 Pos;
        Type              GripType;
        bool              Hovered; // 悬浮上方的
    };

    struct SnapDraw
    {
        enum class Type : uint8_t { None, Endpoint, Midpoint, Nearest, Grid };
        Type              SnapType = Type::None;
        DirectX::XMFLOAT2 Pos = {};
        bool              IsValid() const { return SnapType != Type::None; }
    };

    struct ViewState
    {
        // ===== Geometry =====
        std::span<const Vertex_P3_C4> Scene;   // 屏幕
        std::span<const Vertex_P3_C4> Overlay; // 预览 
        std::vector<GripDraw>         Grips;   // 夹点 

        DragRect  Selection;          // 选择框  
        float MouseX = 0;             // 客户区像素坐标
        float MouseY = 0;
        // ===== Render flags =====
        bool ShowGrid       = true;    // 轴网
        bool ShowGizmo      = true;    //  
        bool ShowAxis       = true;    // 坐标轴
        bool ShowCurrorBox  = true;    // 鼠标中间方框

        // ===== 最近点 ===== 
        SnapDraw Snap    = {};
    };
}