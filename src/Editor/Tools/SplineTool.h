#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/Viewport/Viewport.h"
#include "Document/CommandStack/CommandStack.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/GeomKernel/Spline.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Editor/Overlay/Overlay.h"
#include <cstdio>
#include <vector>
#include <cmath>

namespace MiniCAD
{
    // =========================================================================
    // SplineTool —— AutoCAD 风格的样条曲线绘制工具
    //
    // 操作说明：
    //   左键          → 追加拟合点（实时预览曲线随光标更新）
    //   右键          → 提交当前样条（≥ 2 点有效）并退出工具
    //   ESC           → 放弃并退出工具
    //   C 键          → 切换闭合 / 开放模式（需已有 ≥ 3 个点）
    //   Z 键          → 撤销最后一个拟合点（Undo 局部）
    //
    // 与 AutoCAD 的对应关系：
    //   本工具 = AutoCAD SPLINE 命令（拟合点方式，Fit 模式）
    //   拟合点 = AutoCAD 中的 Fit Points（曲线严格经过每个点）
    //   C 键   = AutoCAD SPLINE 命令中的 Close 子命令
    // =========================================================================
    class SplineTool : public ITool
    {
    public:

        SplineTool(Scene& scene, CommandStack& cmdStack,
                   Viewport& viewport, Overlay& overlay)
            : m_scene(scene)
            , m_cmdStack(cmdStack)
            , m_viewport(viewport)
            , m_overlay(overlay)
        {
            printf("[SplineTool] 左键追加拟合点 | 右键提交 | C=闭合 | Z=撤回上一点 | ESC取消\n");
        }

        ~SplineTool()
        {
            printf("[SplineTool] 退出\n");
        }

        bool OnInput(const InputEvent& e) override
        {
            // ── 键盘 ─────────────────────────────────────────────────────────
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

            // ── 鼠标移动：实时预览 ───────────────────────────────────────────
            if (e.Type == InputEventType::MouseMove)
            {
                m_cursor = GetPoint(e);
                RefreshOverlay();
                return false;
            }

            // ── 左键：追加拟合点 ─────────────────────────────────────────────
            if (e.IsLeftClick())
            {
                Math::Point3 pt = GetPoint(e);

                // 双击：提交
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

            // ── 右键：提交并退出 ─────────────────────────────────────────────
            if (e.IsRightClick())
            {
                TryCommit();
                Reset();
                if (OnFinished) OnFinished();
                return true;
            }

            // ── ESC：放弃并退出 ──────────────────────────────────────────────
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

        void OnSceneChanged() override { Reset(); }

    private:

        // ── 切换闭合模式 ─────────────────────────────────────────────────────
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

        // ── 撤销最后一个拟合点 ───────────────────────────────────────────────
        void UndoLastPoint()
        {
            if (m_fitPoints.empty()) return;
            m_fitPoints.pop_back();
            // 点数不足 3 时自动解除闭合
            if (m_fitPoints.size() < 3) m_closed = false;
            printf("[SplineTool] 撤回拟合点，剩余 %zu 个\n", m_fitPoints.size());
            RefreshOverlay();
        }

        // ── 刷新 Overlay ─────────────────────────────────────────────────────
        void RefreshOverlay()
        {
            m_overlay.Clear();
            if (m_fitPoints.empty()) return;

            const auto& layer = m_scene.GetLayerManager().GetActiveLayer();
            const Math::Color4 layerColor   = layer.GetColor();
            const Math::Color4 rubberColor  = { 0.6f, 0.6f, 0.6f, 0.5f  };
            const Math::Color4 cpColor      = { 0.4f, 0.8f, 1.0f, 0.7f  };
            const Math::Color4 cpLineColor  = { 0.4f, 0.6f, 0.8f, 0.25f };
            const Math::Color4 closedColor  = { 0.2f, 1.0f, 0.5f, 0.6f  };

            // ── 拟合点标记 + 控制多边形辅助线 ────────────────────────────
            for (size_t i = 0; i < m_fitPoints.size(); ++i)
            {
                m_overlay.AddPoint(m_fitPoints[i], cpColor);
                if (i + 1 < m_fitPoints.size())
                    m_overlay.AddLine(m_fitPoints[i], m_fitPoints[i+1], cpLineColor);
            }

            // ── 预览样条曲线（含光标点）──────────────────────────────────
            // 把光标作为临时末尾点，实时重建样条
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
                        m_overlay.AddLine(pts[i], pts[i+1], drawColor);
                }
                else if (previewPts.size() == 2)
                {
                    // 只有两点时退化为直线预览
                    m_overlay.AddLine(previewPts[0], previewPts[1], rubberColor);
                }
            }

            // ── 闭合辅助线（末尾点→首点）────────────────────────────────
            if (m_closed && m_fitPoints.size() >= 2)
                m_overlay.AddLine(m_fitPoints.back(), m_fitPoints.front(), cpLineColor);

            // ── 橡皮筋（光标→最后拟合点）────────────────────────────────
            m_overlay.AddLine(m_fitPoints.back(), m_cursor, rubberColor);
        }

        // ── 提交 ─────────────────────────────────────────────────────────────
        void TryCommit()
        {
            if (m_fitPoints.size() < 2)
            {
                printf("[SplineTool] 拟合点不足（至少需要 2 个），放弃提交\n");
                return;
            }

            SplineBoundary boundary = m_closed ? SplineBoundary::Closed
                                               : SplineBoundary::Natural;

            // 闭合模式需要至少 3 点
            if (m_closed && m_fitPoints.size() < 3)
            {
                boundary = SplineBoundary::Natural;
                printf("[SplineTool] 点数不足，自动降级为开放样条\n");
            }

            auto id     = m_scene.NextObjectID();
            auto entity = std::make_unique<SplineEntity>(id, m_fitPoints, boundary);
            auto cmd    = std::make_unique<AddEntityCommand>(std::move(entity));
            m_cmdStack.Execute(std::move(cmd), m_scene);

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

        // ── 重置 ─────────────────────────────────────────────────────────────
        void Reset()
        {
            m_fitPoints.clear();
            m_closed = false;
            m_cursor = {};
            m_overlay.Clear();
        }

        // ── 辅助 ─────────────────────────────────────────────────────────────
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            auto p = m_viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
            return { p.x, p.y, 0.0 };
        }

        bool IsDoubleClick(const Math::Point3& a, const Math::Point3& b) const
        {
            auto sa = m_viewport.GetCamera().WorldToScreen(a);
            auto sb = m_viewport.GetCamera().WorldToScreen(b);
            double dx = sa.x-sb.x, dy = sa.y-sb.y;
            return (dx*dx + dy*dy) < (kDoubleClickPx * kDoubleClickPx);
        }

    private:
        static constexpr double kDoubleClickPx = 6.0;

        Scene&        m_scene;
        CommandStack& m_cmdStack;
        Viewport&     m_viewport;
        Overlay&      m_overlay;

        std::vector<Math::Point3> m_fitPoints;   // 已确认的拟合点
        bool                      m_closed = false; // 是否闭合
        Math::Point3              m_cursor{};        // 当前光标（预览用）
    };
}
