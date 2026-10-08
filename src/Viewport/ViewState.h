#pragma once 
#include <span>
#include <vector> 
#include "Core/Math/Point2.hpp"
#include "Core/Math/Color4.hpp"
#include "Render/VertexTypes.hpp"
#include "Render/ImageDraw.hpp"
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
            Quadrant,        // 象限点
            Intersection,    // 两曲线交点
            Perpendicular,   // 垂足
            Grid,
        };
        Type              SnapType = Type::None;
        Math::Point2      Pos = {};
        bool              IsValid() const { return SnapType != Type::None; }
    };

    struct ViewState
    {
        // ===== Geometry =====
        std::span<const Vertex_P3_C4>    Scene;       // 场景线段
        std::span<const Vertex_P3_C4>    SceneFill;   // 场景填充三角形（线宽 > 1）
        std::span<const Vertex_P3_C4>    Overlay;     // 预览线段
        std::span<const Vertex_P3_C4_UV> TextScene;   // 场景文字四边形
        std::span<const ImageDraw>       Images;      // 场景光栅图像（画在填充、线、文字之下）

        // 场景顶点版本号：每次重建自增。渲染后端据此判断 GPU 缓冲是否需要
        // 重新上传（版本未变直接复用，避免每帧全量上传）。
        uint64_t                         SceneVersion = 0;

        // 选中流：选中/拖动中实体按选中态绘制，叠加在场景之上。
        // 仅在选择集变化或拖动时重建，选中不再触发全场景顶点重建。
        std::span<const Vertex_P3_C4>    SelScene;     // 选中线段
        std::span<const Vertex_P3_C4>    SelFill;      // 选中填充三角形
        std::span<const Vertex_P3_C4_UV> SelText;      // 选中文字四边形
        uint64_t                         SelVersion = 0;
        void*                            FontTexture = nullptr; // 纹理字形用的字体图集（Web 端；桌面端文字走矢量，不设置）
        std::span<const GripDraw>        Grips;       // 夹点

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
