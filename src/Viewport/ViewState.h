#pragma once 
#include <span>
#include <vector> 
#include "Core/Math/Point2.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Color4.hpp"
#include "Render/VertexTypes.hpp"
#include "Render/GlyphInstancing.hpp"
#include "Render/ImageDraw.hpp"
#include "Core/GeomKernel/AABB.hpp"
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
        // 点图元流（POINTLIST，一个顶点一个点，渲染成恒定 1 屏幕像素）：
        // 图案填充的 dash=0 点笔划、POINT 的 $PDMODE=0。与线段分开提交。
        std::span<const Vertex_P3_C4>    ScenePoints;
        std::span<const Vertex_P3_C4>    Overlay;     // 预览线段
        std::span<const Vertex_P3_C4_UV> TextScene;   // 场景文字四边形
        std::span<const ImageDraw>       Images;      // 场景光栅图像（画在填充、线、文字之下）

        // 场景顶点版本号：每次重建自增。渲染后端据此判断 GPU 缓冲是否需要
        // 重新上传（版本未变直接复用，避免每帧全量上传）。
        uint64_t                         SceneVersion = 0;
        // 场景静态流的追加段：拼接流交给渲染后端释放之后新增的实体，CPU 端只有它们的顶点（Scene / SceneFill /
        // ScenePoints 为空）。后端把这三段接到 (slot, SceneAppendBase) 缓冲的末尾并升级为 SceneVersion
        //（IRenderer::AppendCached）；接不上时按 SceneAppendBase 画旧内容，应用层再请编辑器重新拼接
        bool                             SceneAppendActive = false;
        uint64_t                         SceneAppendBase   = 0;
        std::span<const Vertex_P3_C4>    SceneAppend;
        std::span<const Vertex_P3_C4>    SceneAppendFill;
        std::span<const Vertex_P3_C4>    SceneAppendPoints;
        // 原地改写段（与追加段同一帧、同一基础版本）：修改 / 删除 / 拖动中的实体在静态流里的那一段改成新顶点或透明的
        // 退化图元（IRenderer::WriteCached），按顺序先写再追加。Stream：0 线、1 填充、2 点；顶点在 ScenePatchLines /
        // ScenePatchFills / ScenePatchPoints 的 [First, First + Count)，写到缓冲的 Offset 处
        struct ScenePatch
        {
            uint32_t Stream = 0;
            size_t   Offset = 0;
            size_t   First  = 0;
            size_t   Count  = 0;
        };
        std::span<const ScenePatch>      ScenePatches;
        std::span<const Vertex_P3_C4>    ScenePatchLines;
        std::span<const Vertex_P3_C4>    ScenePatchFills;
        std::span<const Vertex_P3_C4>    ScenePatchPoints;
        // 动态场景流：随视图变化的线与填充（按像素等宽的线宽、按像素画的点样式；虚线在世界坐标里，进静态流），缩放时只重建这一路，画在静态流之后
        std::span<const Vertex_P3_C4>    SceneDyn;
        std::span<const Vertex_P3_C4>    SceneDynFill;
        uint64_t                         SceneDynVersion = 0;
        // 三维实体随视图变化的部分（M6）：着色面（已按视线方向算好明暗的三角形，画在填充之前）
        // 与轮廓线（画在场景线之后）；视线方向、视觉样式或场景变化时重建，共用一个版本号
        std::span<const Vertex_P3_C4>    Shaded;
        std::span<const Vertex_P3_C4>    Silhouettes;
        uint64_t                         ShadedVersion = 0;
        // 场景范围（可见实体的包围盒）：三维视图按它设置深度范围；为空时按相机缩放估计
        AABB                             SceneBounds = AABB::Empty();
        // 渲染原点：上面与下面各路世界坐标的顶点（场景、选中、预览、着色面、图像、字形实例）存的都是"世界坐标 − 它"
        //（见 ToRenderFloat3），Viewport 把它合进视图投影矩阵（双精度）。夹点、捕捉标记、框选框是屏幕坐标，与它无关
        Math::Point3                     RenderOrigin;

        // 场景文字的字形实例（按网格排序）与字形网格库、纹理文字顶点（TextScene）的版本：
        // 只有文字变化时才自增，新画一条线不必把几十万个字形实例重新上传
        std::span<const GlyphInstance>   Glyphs;
        std::span<const GlyphBatch>      GlyphBatches;
        GlyphMeshView                    GlyphMeshes;
        uint64_t                         SceneGlyphVersion = 0;

        // 选中流：选中/拖动中实体按选中态绘制，叠加在场景之上。
        // 仅在选择集变化或拖动时重建，选中不再触发全场景顶点重建。
        std::span<const Vertex_P3_C4>    SelScene;     // 选中线段
        std::span<const Vertex_P3_C4>    SelFill;      // 选中填充三角形
        std::span<const Vertex_P3_C4_UV> SelText;      // 选中文字四边形
        uint64_t                         SelVersion = 0;
        // 选中流里随视图变化的部分（线宽 / 点样式实体、点图元的小十字、长虚线段、拖动中的实体）：
        // 缩放时只重建这一路，静态部分（从逐实体缓存复制来的）不动、不重新上传
        std::span<const Vertex_P3_C4>    SelDyn;
        std::span<const Vertex_P3_C4>    SelDynFill;
        std::span<const Vertex_P3_C4_UV> SelDynText;
        uint64_t                         SelDynVersion = 0;
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
        bool ShowCrosshair  = true;    // 十字光标（平移工具改用系统的手形光标）

        // ===== 最近点 ===== 
        SnapDraw Snap    = {};
    };
}
