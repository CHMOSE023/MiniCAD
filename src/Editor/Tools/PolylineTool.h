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
                    printf("[PolylineTool] Line Segment "  "End=(%.3f %.3f)\n", pt.x, pt.y);
                } 
                else  // arc segment
                {
                    double bulge = ComputeArcBulge(m_points.back(), pt, m_cursor); 
                    m_points.push_back(pt); 
                    m_bulges.push_back(bulge); 
                    printf("[PolylineTool] Arc Segment  End=(%.3f %.3f) Bulge=%.6f\n", pt.x, pt.y, bulge);
                }

                RefreshOverlay();

                return true;
            }

            // ================================================================
            // right click
            // ================================================================

            if (e.IsRightClick())
            {
                TryCommit(); 
                Reset(); 
                if (OnFinished)
                    OnFinished();

                return true;
            }

            // ================================================================
            // ESC
            // ================================================================

            if (e.IsCancel())
            {
                Reset();

                if (OnFinished)
                    OnFinished();

                return true;
            }

            return false;
        }

    public:

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

        // ====================================================================
        // current input point
        // ====================================================================

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap)
                return e.SnapWorld;

            return
                m_viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        // ====================================================================
        // Compute Bulge
        //
        // A = previous point
        // B = clicked endpoint
        // M = current mouse
        //
        // Bulge determined by which side M lies relative to chord A→B.
        //
        // ====================================================================

        static double ComputeArcBulge(const Math::Point3& A, const Math::Point3& B, const Math::Point3& M)
        {
            double abx = B.x - A.x;
            double aby = B.y - A.y;

            double amx = M.x - A.x;
            double amy = M.y - A.y;

            double chord = std::sqrt(abx * abx + aby * aby);

            if (chord < Math::LengthEPS)
                return 0.0;

            // ------------------------------------------------------------
            // signed side test
            // >0 : left
            // <0 : right
            // ------------------------------------------------------------

            double cross = abx * amy - aby * amx;

            // ------------------------------------------------------------
            // perpendicular distance from M to chord
            // ------------------------------------------------------------

            double sagitta = std::abs(cross) / chord;

            // ------------------------------------------------------------
            // convert sagitta -> bulge
            //
            // bulge = 2s / chord
            //
            // stable small-angle approximation
            // ------------------------------------------------------------

            double bulge = (2.0 * sagitta) / chord;

            // clamp extreme values
            bulge = std::clamp(bulge, 0.0, 10.0);

            // sign:
            // left  -> CCW -> positive
            // right -> CW  -> negative

            if (cross < 0.0)
                bulge = -bulge;

            return bulge;
        }

    private:

        void RefreshOverlay()
        {
            m_overlay.Clear();

            if (m_points.empty())
                return;

            const auto& layer = m_scene.GetLayerManager().GetActiveLayer();

            Math::Color4 layerColor  = layer.GetColor(); 
            Math::Color4 rubberColor = { 0.6,  0.6,   0.6,  0.5 };

            // ------------------------------------------------------------
            // committed polyline
            // ------------------------------------------------------------ 
            Polyline pl(m_points, m_bulges); 
            m_overlay.AddPolyline(pl, layerColor);
             
            // ------------------------------------------------------------
            // rubber preview
            // ------------------------------------------------------------

            if (!m_points.empty())
            {
                if (m_mode == DrawMode::Line)
                {
                    m_overlay.AddLine(m_points.back(), m_cursor, rubberColor);
                }
                else
                {
                    // preview arc 
                    std::vector<Math::Point3> tmpPts =
                    {
                        m_points.back(),
                        m_cursor
                    };

                    std::vector<double> tmpBulges =
                    {
                        ComputeArcBulge(m_points.back(),  m_cursor,  m_cursor)
                    };

                    Polyline preview(std::move(tmpPts), std::move(tmpBulges)); 
                    m_overlay.AddPolyline(preview, rubberColor);
                }
            }

           
        }

        // ====================================================================
        // commit
        // ====================================================================

        void TryCommit()
        {
            if (m_points.size() < 2)
            {
                printf("[PolylineTool] Not enough points\n");

                return;
            }

            Commit();
        }

        void Commit()
        {
            auto id =  m_scene.NextObjectID();

            auto entity = std::make_unique<PolylineEntity>(id, m_points, m_bulges);

            auto cmd = std::make_unique<AddEntityCommand>(std::move(entity));

            m_cmdStack.Execute(std::move(cmd), m_scene);

            Polyline pl(m_points, m_bulges);

            printf("[PolylineTool] Commit Segments=%d  Length=%.3f\n", pl.SegCount(), pl.Length());
        }

        // ====================================================================
        // reset
        // ====================================================================

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
