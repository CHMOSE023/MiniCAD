#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/Viewport/Viewport.h"
#include "Document/CommandStack/CommandStack.h"
#include "Document/Command/AddEntityCommand.h"

#include "Core/Math/Point3.hpp"
#include "Core/Math/Color4.hpp"
#include "Core/Entity/PolylineEntity.hpp"

#include "Editor/Overlay/Overlay.h"

#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>

namespace MiniCAD
{
    // =========================================================================
    // PolylineTool
    //
    // Supports:
    //   • line segments
    //   • bulge arc segments
    //
    // Controls:
    //
    //   L              -> line mode
    //   A              -> arc mode
    //
    //   Left Click     -> append vertex
    //   Right Click    -> commit + exit
    //   ESC            -> cancel + exit
    //
    // Arc mode:
    //
    //   bulge is computed dynamically from:
    //
    //       previous point
    //       current mouse position
    //       clicked endpoint
    //
    // =========================================================================

    class PolylineTool : public ITool
    {
    public:

        enum class DrawMode
        {
            Line,
            Arc
        };

    public:

        PolylineTool(Scene& scene, CommandStack& cmdStack, Viewport& viewport, Overlay& overlay)
            : m_scene(scene)
            , m_cmdStack(cmdStack)
            , m_viewport(viewport)
            , m_overlay(overlay)
        {
            printf("[PolylineTool]: L = Line Mode , A = Arc Mode  \n Left Click = Add Vertex  Right Click = Commit ESC = Cancel\n");
        }

        ~PolylineTool()
        {
            printf("[PolylineTool] Exit\n");
        }

    public:

        bool OnInput(const InputEvent& e) override
        {
            // ================================================================
            // mode switch
            // ================================================================

            if (e.IsKeyPressed(KeyCode::L))
            {
                m_mode = DrawMode::Line;
                printf("[PolylineTool] Mode = LINE\n"); 
                RefreshOverlay();
                return true;
            }

            if (e.IsKeyPressed(KeyCode::A))
            {
                m_mode = DrawMode::Arc;
                printf("[PolylineTool] Mode = ARC\n"); 
                RefreshOverlay();
                return true;
            }

            // ================================================================
            // mouse move
            // ================================================================

            if (e.Type == InputEventType::MouseMove)
            {
                m_cursor = GetPoint(e);
                RefreshOverlay();
                return false;
            }

            // ================================================================
            // left click
            // ================================================================

            if (e.IsLeftClick())
            {
                Math::Point3 pt = GetPoint(e);

                // ------------------------------------------------------------
                // first point
                // ------------------------------------------------------------ 

                if (m_points.empty())
                {
                    m_points.push_back(pt);
                    printf("[PolylineTool] First Point " "(%.3f %.3f)\n", pt.x, pt.y); 
                    RefreshOverlay();

                    return true;
                }

                if (m_mode == DrawMode::Line)     // line segment
                {
                    m_points.push_back(pt);
                    m_bulges.push_back(0.0);
                }
                else  // arc segment
                {
                    double bulge = ComputeBulge(m_points.back(), pt, m_cursor);
                    m_points.push_back(pt);
                    m_bulges.push_back(bulge);
                    printf("[PolylineTool] Arc Segment  End=(%.3f %.3f) Bulge=%.6f\n", pt.x, pt.y, bulge);
                }

                RefreshOverlay();
                return true;
            }

            if (e.IsRightClick())
            {
                Commit();
                Reset();
                if (OnFinished) OnFinished();
                return true;
            }

            // ================================================================
            // ESC
            // ================================================================

            if (e.IsCancel())
            {
                Reset();

                return true;
            }

            return false;
        }

        bool HasAnchor() const override
        {
            return !m_points.empty();
        }

        Math::Point3 GetAnchor() const override
        {
            return m_points.empty() ? Math::Point3{} : m_points.back();
        }

        void OnSceneChanged() override
        {
            Reset();
        }

    private:

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap)
                return e.SnapWorld;

            auto p = m_viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
            return { p.x, p.y, 0.0f };
        }

        // ============================================================
        // Stable bulge (CAD-style)
        // bulge = tan(theta / 4)
        // theta from triangle A-M-B
        // ============================================================
        static double ComputeBulge(const Math::Point3& A, const Math::Point3& B, const Math::Point3& M)
        {
            double ax = A.x - M.x;
            double ay = A.y - M.y;
            double bx = B.x - M.x;
            double by = B.y - M.y;

            double la = std::sqrt(ax * ax + ay * ay);
            double lb = std::sqrt(bx * bx + by * by);

            if (la < 1e-9 || lb < 1e-9)
                return 0.0;

            double dot = ax * bx + ay * by;
            double cosv = dot / (la * lb);

            cosv = std::clamp(cosv, -1.0, 1.0);

            double angle = std::acos(cosv);

            double cross = ax * by - ay * bx;

            double bulge = std::tan(angle * 0.25);

            if (cross < 0.0)
                bulge = -bulge;

            return std::clamp(bulge, -10.0, 10.0);
        }

        // ============================================================
        // overlay
        // ============================================================
        void RefreshOverlay()
        {
            m_overlay.Clear();

            if (m_points.empty())
                return;

            const auto& layer = m_scene.GetLayerManager().GetActiveLayer();

            Math::Color4 layerColor = layer.GetColor();
            Math::Color4 rubberColor = { 0.6, 0.6, 0.6, 0.5 };

            Polyline pl(m_points, m_bulges);
            m_overlay.AddPolyline(pl, layerColor);

            if (m_points.size() >= 1)
            {
                if (m_mode == DrawMode::Line)
                {
                    m_overlay.AddLine(m_points.back(), m_cursor, rubberColor);
                }
                else
                {
                    if (!m_points.empty())
                    {
                        Math::Point3 A = m_points.back();
                        Math::Point3 B = m_cursor;

                        std::vector<Math::Point3> pts = { A, B };
                        std::vector<double> bulges = { 0.0 }; // preview用0避免误导

                        Polyline preview(pts, bulges);
                        m_overlay.AddPolyline(preview, rubberColor);
                    }
                }
            }
        }

        void Commit()
        {
            if (m_points.size() < 2)
                return;

            auto id = m_scene.NextObjectID();
            auto entity = std::make_unique<PolylineEntity>(id, m_points, m_bulges);
            auto cmd = std::make_unique<AddEntityCommand>(std::move(entity));

            m_cmdStack.Execute(std::move(cmd), m_scene);

            printf("[PolylineTool] Commit Seg=%zu\n", m_points.size() - 1);
        }

        void Reset()
        {
            m_points.clear();
            m_bulges.clear();
            m_cursor = {};
            m_overlay.Clear();
        }

    private:

        Scene&        m_scene;
        CommandStack& m_cmdStack;
        Viewport&     m_viewport;
        Overlay&      m_overlay; 
        DrawMode      m_mode = DrawMode::Line;

        std::vector<Math::Point3> m_points;
        std::vector<double>       m_bulges;

        Math::Point3 m_cursor{};
    };
}
