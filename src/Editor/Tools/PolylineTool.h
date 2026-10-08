#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Color4.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Log.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace MiniCAD
{
    class PolylineTool : public ITool
    {
    public:

        enum class DrawMode
        {
            Line,
            Arc
        };

    public:

        PolylineTool()
        {
            LOG_DEBUG("[PolylineTool] L=直线 A=圆弧 | 左键追加顶点 | 右键提交 | ESC取消");
        }

        ~PolylineTool()
        {
            LOG_DEBUG("[PolylineTool] 退出");
        }

    public:

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsKeyPressed(KeyCode::L))
            {
                SwitchToLine();
                return true;
            }
            if (e.IsKeyPressed(KeyCode::A))
            {
                SwitchToArc();
                return true;
            }

            if (e.Type == InputEventType::MouseMove)
            {
                m_cursor = GetPoint(e);
                RefreshOverlay();
                return false;
            }

            if (e.IsLeftClick())
            {
                OnLeftClick(GetPoint(e));
                return true;
            }

            if (e.IsRightClick())
            {
                Commit();
                Reset();
                if (OnFinished) OnFinished();
                return true;
            }

            if (e.IsCancel())
            {
                Reset();
                if (OnFinished) OnFinished();
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

        std::string GetPrompt() const override
        {
            if (m_points.empty())
                return "指定起点 [L 直线/A 圆弧]:";
            if (m_mode == DrawMode::Arc)
                return m_hasArcMid ? "指定圆弧终点:" : "指定圆弧上一点:";
            return "指定下一点 [L 直线/A 圆弧/右键提交/ESC]:";
        }

        void OnSceneChanged() override
        {
            Reset();
        }

    private:

        void SwitchToLine()
        {
            m_mode      = DrawMode::Line;
            m_hasArcMid = false;
            m_arcMid    = {};
            LOG_INFO("[PolylineTool] 模式 = 直线");
            RefreshOverlay();
        }

        void SwitchToArc()
        {
            m_mode      = DrawMode::Arc;
            m_hasArcMid = false;
            m_arcMid    = {};
            LOG_INFO("[PolylineTool] 模式 = 圆弧（三点：弧上点 → 终点）");
            RefreshOverlay();
        }

        void OnLeftClick(const Math::Point3& pt)
        {
            if (m_points.empty())
            {
                m_points.push_back(pt);
                LOG_INFO("[PolylineTool] 起点 (%.3f, %.3f)", pt.x, pt.y);
                RefreshOverlay();
                return;
            }

            if (m_mode == DrawMode::Line)
            {
                m_bulges.push_back(0.0);
                m_points.push_back(pt);
                LOG_INFO("[PolylineTool] 直线顶点 #%zu (%.3f, %.3f)",  m_points.size(), pt.x, pt.y);
            }
            else
            {
                if (!m_hasArcMid)
                {
                    m_arcMid    = pt;
                    m_hasArcMid = true;
                    LOG_INFO("[PolylineTool] 弧上点 (%.3f, %.3f)，请继续点击弧终点",  pt.x, pt.y);
                }
                else
                {
                    double bulge = ComputeBulgeFromThreePoints(m_points.back(), m_arcMid, pt);

                    m_bulges.push_back(bulge);
                    m_points.push_back(pt);

                    LOG_DEBUG("[PolylineTool] 弧终点 (%.3f, %.3f)  Bulge=%.6f",  pt.x, pt.y, bulge);

                    m_hasArcMid = false;
                    m_arcMid    = {};
                }
            }

            RefreshOverlay();
        }

        static double ComputeBulgeFromThreePoints(
            const Math::Point3& A,
            const Math::Point3& M,
            const Math::Point3& B)
        {
            double ax = A.x, ay = A.y;
            double mx = M.x, my = M.y;
            double bx = B.x, by = B.y;

            double D = 2.0 * (ax * (my - by) + mx * (by - ay) + bx * (ay - my));
            if (std::abs(D) < 1e-10)
                return 0.0;

            double a2 = ax * ax + ay * ay;
            double m2 = mx * mx + my * my;
            double b2 = bx * bx + by * by;

            double cx = (a2 * (my - by) + m2 * (by - ay) + b2 * (ay - my)) / D;
            double cy = (a2 * (bx - mx) + m2 * (ax - bx) + b2 * (mx - ax)) / D;

            double sa = std::atan2(ay - cy, ax - cx);
            double ea = std::atan2(by - cy, bx - cx);

            double cross = (mx - ax) * (by - ay) - (my - ay) * (bx - ax);

            double sweep;
            if (cross >= 0.0)
            {
                sweep = ea - sa;
                if (sweep <= 0.0) sweep += Math::TwoPI;
                return -std::tan(sweep * 0.25);
            }
            else
            {
                sweep = sa - ea;
                if (sweep <= 0.0) sweep += Math::TwoPI;
                return std::tan(sweep * 0.25);
            }
        }

        void RefreshOverlay()
        {
            if (!m_ctx) return;
            m_ctx->overlay.Clear();
            if (m_points.empty()) return;

            const auto& layer = m_ctx->scene.GetLayerManager().GetActiveLayer();
            const Math::Color4 layerColor  = layer.GetColor();
            const Math::Color4 rubberColor = { 0.6f, 0.6f, 0.6f, 0.5f };
            const Math::Color4 helperColor = { 0.5f, 0.5f, 0.5f, 0.25f };
            const Math::Color4 midColor    = { 1.0f, 0.8f, 0.2f, 0.8f };

            if (m_points.size() >= 2)
            {
                Polyline pl(m_points, m_bulges);
                m_ctx->overlay.AddPolyline(pl, layerColor);
            }

            for (const auto& pt : m_points)
                m_ctx->overlay.AddPoint(pt, midColor);

            const Math::Point3& last = m_points.back();

            if (m_mode == DrawMode::Line)
            {
                m_ctx->overlay.AddLine(last, m_cursor, rubberColor);
            }
            else
            {
                if (!m_hasArcMid)
                {
                    m_ctx->overlay.AddLine(last, m_cursor, rubberColor);
                }
                else
                {
                    m_ctx->overlay.AddPoint(m_arcMid, midColor);

                    double bulgePreview =
                        ComputeBulgeFromThreePoints(last, m_arcMid, m_cursor);

                    if (std::abs(bulgePreview) > 1e-12)
                    {
                        ArcGeom arc = Polyline::ComputeArc(last, m_cursor, bulgePreview);
                        if (arc.Radius > Math::LengthEPS)
                        {
                            m_ctx->overlay.AddArcGeom(arc, rubberColor);
                        }
                        else
                        {
                            m_ctx->overlay.AddLine(last, m_cursor, rubberColor);
                        }
                    }
                    else
                    {
                        m_ctx->overlay.AddLine(last, m_cursor, rubberColor);
                    }

                    m_ctx->overlay.AddLine(last,     m_arcMid, helperColor);
                    m_ctx->overlay.AddLine(m_arcMid, m_cursor, helperColor);
                }
            }
        }

        void Commit()
        {
            if (m_points.size() < 2)
            {
                LOG_WARN("[PolylineTool] 顶点不足，放弃提交");
                return;
            }

            int arcCount = 0, lineCount = 0;
            for (double b : m_bulges)
                (std::abs(b) > 1e-12 ? arcCount : lineCount)++;

            auto id     = m_ctx->scene.NextObjectID();
            auto entity = std::make_unique<PolylineEntity>(id, m_points, m_bulges);
            m_ctx->ApplyCurrentAttr(*entity);
            auto cmd    = std::make_unique<AddEntityCommand>(std::move(entity));
            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);

            LOG_DEBUG("[PolylineTool] 提交 Id=%d  顶点=%zu  直线段=%d  弧段=%d", static_cast<int>(id), m_points.size(), lineCount, arcCount);
        }

        void Reset()
        {
            m_points.clear();
            m_bulges.clear();
            m_hasArcMid = false;
            m_arcMid    = {};
            m_cursor    = {};
            if (m_overlay) m_overlay->Clear();
        }

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            auto p = m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
            return { p.x, p.y, 0.0 };
        }

    private:

        const EditorContext* m_ctx = nullptr;

        Overlay*              m_overlay = nullptr;

        DrawMode m_mode = DrawMode::Line;

        std::vector<Math::Point3> m_points;
        std::vector<double>       m_bulges;

        bool         m_hasArcMid = false;
        Math::Point3 m_arcMid{};

        Math::Point3 m_cursor{};
    };
}
