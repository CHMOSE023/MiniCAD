#pragma once 
#include "Camera.h"
#include "Cursor.h"
#include "Grid.h"
#include "Axis.h"
#include "Gizmo.h"
#include "Viewport/ViewState.h"
#include "Core/Math/PackedTypes.hpp"
#include "Render/IRenderTarget.h"
#include "Render/IRenderer.h"
#include <memory>

namespace MiniCAD
{ 
    // 选择范围框
    struct SelectionGeometry
    {
        std::vector<Vertex_P3_C4> fill;
        std::vector<Vertex_P3_C4> border; 
    };

    struct GripGeometry
    {
        std::vector<Vertex_P3_C4> fill;
        std::vector<Vertex_P3_C4> border; 
    };

    struct SnapGeometry
    {
        std::vector<Vertex_P3_C4> fill;
        std::vector<Vertex_P3_C4> border;
    };
    class Viewport
    {
    public:
        // 库内不创建 RenderTarget，应用层负责创建并在 Render() 时传入
        Viewport(float width, float height);

        // 应用层每帧调用：传入自己的 renderer + renderTarget，Viewport 负责提交所有几何
        void Render(IRenderer& renderer, IRenderTarget& target, const ViewState& viewState);

        void Resize(float width, float height);

        Camera&        GetCamera();
        const  Camera& GetCamera() const;

        void  Pan (float dx, float dy);
        void  Zoom(float delta, float mouseX, float mouseY);
        float GetWidth() const { return m_width; }
        float GetHeight() const { return m_height; }

        // 缓存层（网格 / 坐标轴 / 场景）重画的次数：鼠标移动等只改动态内容的帧不会增加（测试与统计用）
        uint64_t GetLayerRedraws() const { return m_layerRedraws; }
        bool     IsLayerCached()   const { return m_sceneLayer != nullptr; }

        void ShowAxis       (bool show) { m_showAxis = show; }
        void ShowGrid       (bool show) { m_showGrid = show; }
        void ShowGizmo      (bool show) { m_showGizmo = show; }
        void ShowGizmoToggle()          { m_showGizmo = !m_showGizmo; }
        void ShowGridToggle ()          { m_showGrid = !m_showGrid; }
        void ShowAxisToggle ()          { m_showAxis = !m_showAxis; }
        bool IsGizmoShown   () const    { return m_showGizmo; }
        bool IsGridShown    () const    { return m_showGrid; }
        bool IsAxisShown    () const    { return m_showAxis; }

    private:
        void              AddDashedLine(std::vector<Vertex_P3_C4>& out, Math::Float3& a, Math::Float3& b, Math::Float4& color, float dashLen = 6.0f, float gapLen = 4.0f);
        SelectionGeometry BuildSelectionGeometry(const ViewState& viewState);
        GripGeometry      BuildGripGeometry(const ViewState& vs);
        SnapGeometry      BuildSnapGeometry(const ViewState& vs);

        // 相机/尺寸/开关变化时重建网格、坐标轴、Gizmo 顶点并递增视图版本；
        // 未变化时直接复用缓存（CPU 不重建，GPU 缓冲不重新上传）
        void RefreshViewGeometry(const ViewState& vs);

    private:
        float  m_width;
        float  m_height;
        Camera m_camera;
        Cursor m_cursor;
        Grid   m_grid;
        Axis   m_axis;
        Gizmo  m_gizmo;
        ViewportDesc m_viewportDesc;

        std::vector<Vertex_P3_C4> m_vertices;

        // 视图几何缓存：仅在相机/尺寸/显示开关变化时重建
        std::vector<Vertex_P3_C4> m_gridVerts;
        std::vector<Vertex_P3_C4> m_axisVerts;
        std::vector<Vertex_P3_C4> m_gizmoVerts;
        uint64_t m_viewVersion = 0;   // 供 SubmitCached 判断 GPU 缓冲是否需重传
        uint64_t m_viewKey     = 0;   // 相机状态+尺寸+开关的指纹

        // 离屏缓存层：网格、坐标轴、Gizmo、场景（填充 / 线 / 文字）。只有场景版本、相机、尺寸、
        // 显示开关变化时才重画；后端不支持时为空，退回每帧全量绘制
        std::unique_ptr<IRenderTarget> m_sceneLayer;
        bool     m_layerTried   = false;
        uint64_t m_layerKey     = 0;
        bool     m_layerValid   = false;
        uint64_t m_layerRedraws = 0;

        bool m_showGizmo = true;
        bool m_showGrid  = false;
        bool m_showAxis  = false;
    };
	
}
