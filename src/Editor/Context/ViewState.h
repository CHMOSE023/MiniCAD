#pragma once 
#include "Render/D3D11/Shader.h"
#include <span>
#include <vector> 
#include "Core/Math/Point2.hpp"
#include "Core/Math/Color4.hpp"
#include "Render/VertexTypes.hpp"
namespace MiniCAD
{
    struct DragRect
    {
        bool         Active = false;
        Math::Point2 Start  = { .0,.0 };                // 鼠标按下
        Math::Point2 End    = { .0,.0 };                // 当前鼠标
        Math::Color4 Color  = { 0.3, 0.6, 1.0, 0.2 };   // 填充
        Math::Color4 Border = { 0.3, 0.6, 1.0, 1.0 };   // 边框
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

        Math::Point2 Pos;
        Type         GripType;
        bool         Hovered;  // 悬浮上方的
    };

    struct SnapDraw
    {
		// 与 SnapResult::Type 保持一致
        enum class Type : uint8_t
        {
            None,
            Endpoint,
            Midpoint,
            Nearest,
            Quadrant,   // 象限点
            Grid,
        };
        Type              SnapType = Type::None;
        Math::Point2      Pos = {};
        bool              IsValid() const { return SnapType != Type::None; }
    };

    struct ViewState
    {
        // ===== Geometry =====
        std::span<const Vertex_P3_C4> Scene;   // 屏幕
        std::span<const Vertex_P3_C4> Overlay; // 预览 
        std::vector<GripDraw>         Grips;   // 夹点 

        DragRect  Selection;          // 选择框  
        double    MouseX = 0;             // 客户区像素坐标
        double    MouseY = 0;
        // ===== Render flags =====
        bool ShowGrid       = true;    // 轴网
        bool ShowGizmo      = true;    //  
        bool ShowAxis       = true;    // 坐标轴
        bool ShowCurrorBox  = true;    // 鼠标中间方框

        // ===== 最近点 ===== 
        SnapDraw Snap    = {};
    };
}