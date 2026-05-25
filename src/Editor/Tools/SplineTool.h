#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/GeomKernel/Spline.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include <cstdio>
#include <vector>
#include <cmath>

namespace MiniCAD
{
    class SplineTool : public ITool
    {
    public:

        SplineTool()
        {
            printf("[SplineTool] 左键追加拟合点 | 右键提交 | C=闭合 | Z=撤回上一点 | ESC取消\n");
        }

        ~SplineTool()
        {
            printf("[SplineTool] 退出\n");
        }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            const auto& e = ctx.event;

            if (e.IsKeyPressed(KeyCode::C))
            {
                ToggleClosed();
                return true;
            }

            if (e.IsKeyPressed(KeyCode::Z))
            {
                UndoLastPoint();
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
                Math::Point3 pt = GetPoint(e);

                if (!m_fitPoints.empty() && IsDoubleClick(pt, m_fitPoints.back()))
                {
                    TryCommit();
                    Reset();
                    return true;
                }

                m_fitPoints.push_back(pt);
                printf("[SplineTool] 拟合点 #%zu (%.3f, %.3f)\n",
                       m_fitPoints.size(), pt.x, pt.y);
                RefreshOverlay();
                return true;
            }

            if (e.IsRightClick())
            {
                TryCommit();
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

        bool HasAnchor() const override { return !m_fitPoints.empty(); }

        Math::Point3 GetAnchor() const override
        {
            return m_fitPoints.empty() ? Math::Point3{} : m_fitPoints.back();
        }

        std::string GetPrompt() const override
        {
            return m_fitPoints.empty() ? "指定起点:"
                                       : "指定下一点 [右键提交/C 闭合/Z 撤回/ESC 取消]:";
        }

        void OnSceneChanged() override { Reset(); }

    private:

        void ToggleClosed()
        {
            if (m_fitPoints.size() < 3)
            {
                printf("[SplineTool] 闭合需要至少 3 个拟合点\n");
                return;
            }
            m_closed = !m_closed;
            printf("[SplineTool] 样条 %s\n", m_closed ? "已闭合" : "已开放");
            RefreshOverlay();
        }

        void UndoLastPoint()
        {
            if (m_fitPoints.empty()) return;
            m_fitPoints.pop_back();
            if (m_fitPoints.size() < 3) m_closed = false;
            printf("[SplineTool] 撤回拟合点，剩余 %zu 个\n", m_fitPoints.size());
            RefreshOverlay();
        }

        void RefreshOverlay()
        {
            if (!m_ctx) return;
            m_ctx->overlay.Clear();
            if (m_fitPoints.empty()) return;

            const auto& layer = m_ctx->scene.GetLayerManager().GetActiveLayer();
            const Math::Color4 layerColor   = layer.GetColor();
            const Math::Color4 rubberColor  = { 0.6f, 0.6f, 0.6f, 0.5f  };
            const Math::Color4 cpColor      = { 0.4f, 0.8f, 1.0f, 0.7f  };
            const Math::Color4 cpLineColor  = { 0.4f, 0.6f, 0.8f, 0.25f };
            const Math::Color4 closedColor  = { 0.2f, 1.0f, 0.5f, 0.6f  };

            for (size_t i = 0; i < m_fitPoints.size(); ++i)
            {
                m_ctx->overlay.AddPoint(m_fitPoints[i], cpColor);
                if (i + 1 < m_fitPoints.size())
                    m_ctx->overlay.AddLine(m_fitPoints[i], m_fitPoints[i+1], cpLineColor);
            }

            std::vector<Math::Point3> previewPts = m_fitPoints;
            previewPts.push_back(m_cursor);

            SplineBoundary boundary = m_closed ? SplineBoundary::Closed
                                               : SplineBoundary::Natural;
            if (previewPts.size() >= 2)
            {
                Spline preview(previewPts, boundary);
                if (preview.IsValid())
                {
                    auto pts = preview.Tessellate(24);
                    const Math::Color4& drawColor = m_closed ? closedColor : layerColor;
                    for (size_t i = 0; i + 1 < pts.size(); ++i)
                        m_ctx->overlay.AddLine(pts[i], pts[i+1], drawColor);
                }
                else if (previewPts.size() == 2)
                {
                    m_ctx->overlay.AddLine(previewPts[0], previewPts[1], rubberColor);
                }
            }

            if (m_closed && m_fitPoints.size() >= 2)
                m_ctx->overlay.AddLine(m_fitPoints.back(), m_fitPoints.front(), cpLineColor);

            m_ctx->overlay.AddLine(m_fitPoints.back(), m_cursor, rubberColor);
        }

        void TryCommit()
        {
            if (m_fitPoints.size() < 2)
            {
                printf("[SplineTool] 拟合点不足（至少需要 2 个），放弃提交\n");
                return;
            }

            SplineBoundary boundary = m_closed ? SplineBoundary::Closed
                                               : SplineBoundary::Natural;

            if (m_closed && m_fitPoints.size() < 3)
            {
                boundary = SplineBoundary::Natural;
                printf("[SplineTool] 点数不足，自动降级为开放样条\n");
            }

            auto id     = m_ctx->scene.NextObjectID();
            auto entity = std::make_unique<SplineEntity>(id, m_fitPoints, boundary);
            auto cmd    = std::make_unique<AddEntityCommand>(std::move(entity));
            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);

            printf("[SplineTool] 提交 Id=%d  拟合点=%zu  %s  总长≈%.3f\n",
                   static_cast<int>(id),
                   m_fitPoints.size(),
                   m_closed ? "闭合" : "开放",
                   Spline(m_fitPoints, boundary).IsValid()
                       ? Spline(m_fitPoints, boundary).Tessellate(32).size() > 1
                           ? [&]{ auto& s = *new Spline(m_fitPoints,boundary);
                                  auto t = s.Tessellate(32); double l=0;
                                  for(size_t i=0;i+1<t.size();++i){
                                      auto dx=t[i+1].x-t[i].x, dy=t[i+1].y-t[i].y;
                                      l+=std::sqrt(dx*dx+dy*dy);} return l; }()
                           : 0.0
                       : 0.0);
        }

        void Reset()
        {
            m_fitPoints.clear();
            m_closed = false;
            m_cursor = {};
            if (m_ctx) m_ctx->overlay.Clear();
        }

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            auto p = m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
            return { p.x, p.y, 0.0 };
        }

        bool IsDoubleClick(const Math::Point3& a, const Math::Point3& b) const
        {
            auto sa = m_ctx->viewport.GetCamera().WorldToScreen(a);
            auto sb = m_ctx->viewport.GetCamera().WorldToScreen(b);
            double dx = sa.x-sb.x, dy = sa.y-sb.y;
            return (dx*dx + dy*dy) < (kDoubleClickPx * kDoubleClickPx);
        }

    private:
        static constexpr double kDoubleClickPx = 6.0;

        const EditorContext* m_ctx = nullptr;

        std::vector<Math::Point3> m_fitPoints;
        bool                      m_closed = false;
        Math::Point3              m_cursor{};
    };
}
