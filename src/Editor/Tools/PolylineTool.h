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
    // PolylineTool —— 支持直线段 + 圆弧段的混合折线工具
    //
    // 操作说明：
    //   L 键          → 切换为直线模式
    //   A 键          → 切换为圆弧模式
    //   左键（直线）  → 追加顶点，直接形成直线段
    //   左键（圆弧）  → 第1次点击 = 弧上经过点
    //                   第2次点击 = 弧终点（三点唯一确定圆弧）
    //   右键          → 提交折线并退出工具
    //   ESC           → 放弃当前折线并退出工具
    // =========================================================================
    class PolylineTool : public ITool
    {
    public:

        enum class DrawMode
        {
            Line,   // 直线段模式
            Arc     // 圆弧模式（三点定弧：起点 → 弧上点 → 终点）
        };

    public:

        PolylineTool(Scene& scene, CommandStack& cmdStack, Viewport& viewport, Overlay& overlay)
            : m_scene(scene)
            , m_cmdStack(cmdStack)
            , m_viewport(viewport)
            , m_overlay(overlay)
        {
            printf("[PolylineTool] L=直线 A=圆弧 | 左键追加顶点 | 右键提交 | ESC取消\n");
        }

        ~PolylineTool()
        {
            printf("[PolylineTool] 退出\n");
        }

    public:

        bool OnInput(const InputEvent& e) override
        {
            // ── 键盘：模式切换 ───────────────────────────────────────────────
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

            // ── 鼠标移动：更新橡皮筋预览 ────────────────────────────────────
            if (e.Type == InputEventType::MouseMove)
            {
                m_cursor = GetPoint(e);
                RefreshOverlay();
                return false;
            }

            // ── 左键：采集顶点 ───────────────────────────────────────────────
            if (e.IsLeftClick())
            {
                OnLeftClick(GetPoint(e));
                return true;
            }

            // ── 右键：提交并退出 ─────────────────────────────────────────────
            if (e.IsRightClick())
            {
                Commit();
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

        // 最后一个已确认顶点作为捕捉锚点
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

        // ── 切换直线模式 ─────────────────────────────────────────────────────
        void SwitchToLine()
        {
            m_mode      = DrawMode::Line;
            m_hasArcMid = false;
            m_arcMid    = {};
            printf("[PolylineTool] 模式 = 直线\n");
            RefreshOverlay();
        }

        // ── 切换圆弧模式 ─────────────────────────────────────────────────────
        void SwitchToArc()
        {
            m_mode      = DrawMode::Arc;
            m_hasArcMid = false;
            m_arcMid    = {};
            printf("[PolylineTool] 模式 = 圆弧（三点：弧上点 → 终点）\n");
            RefreshOverlay();
        }

        // ── 左键核心逻辑 ─────────────────────────────────────────────────────
        void OnLeftClick(const Math::Point3& pt)
        {
            // 第一个点：折线起点，任何模式下直接记录
            if (m_points.empty())
            {
                m_points.push_back(pt);
                printf("[PolylineTool] 起点 (%.3f, %.3f)\n", pt.x, pt.y);
                RefreshOverlay();
                return;
            }

            if (m_mode == DrawMode::Line)
            {
                // 直线段：Bulge = 0，直接追加
                m_bulges.push_back(0.0);
                m_points.push_back(pt);
                printf("[PolylineTool] 直线顶点 #%zu (%.3f, %.3f)\n",  m_points.size(), pt.x, pt.y);
            }
            else
            {
                // 圆弧模式：两步采集
                if (!m_hasArcMid)
                {
                    // 第1次点击：记录弧上经过点，等待终点
                    m_arcMid    = pt;
                    m_hasArcMid = true;
                    printf("[PolylineTool] 弧上点 (%.3f, %.3f)，请继续点击弧终点\n",  pt.x, pt.y);
                }
                else
                {
                    // 第2次点击：pt 为弧终点，三点计算 Bulge
                    double bulge = ComputeBulgeFromThreePoints(m_points.back(), m_arcMid, pt);

                    m_bulges.push_back(bulge);
                    m_points.push_back(pt);

                    printf("[PolylineTool] 弧终点 (%.3f, %.3f)  Bulge=%.6f\n",  pt.x, pt.y, bulge);

                    // 本段圆弧完成，重置弧上点状态
                    m_hasArcMid = false;
                    m_arcMid    = {};
                }
            }

            RefreshOverlay();
        }

        // ── 三点法计算 Bulge ─────────────────────────────────────────────────
        //
        // 已知：弧起点 A、弧上经过点 M、弧终点 B
        //
        // 步骤：
        //   1. 外接圆公式（行列式法）求圆心 C
        //   2. 叉积 (A→M)×(A→B) 判断 CCW / CW
        //   3. 计算有符号圆心角 sweep
        //   4. bulge = tan(sweep / 4)
        //      正值 = 弧向 A→B 左侧（CW sweep），负值 = 弧向右侧（CCW sweep）
        //      与 Polyline::ComputeArc 的符号约定对齐
        //
        static double ComputeBulgeFromThreePoints(
            const Math::Point3& A,    // 弧起点
            const Math::Point3& M,    // 弧上经过点
            const Math::Point3& B)    // 弧终点
        {
            double ax = A.x, ay = A.y;
            double mx = M.x, my = M.y;
            double bx = B.x, by = B.y;

            // 外接圆行列式分母
            double D = 2.0 * (ax * (my - by) + mx * (by - ay) + bx * (ay - my));
            if (std::abs(D) < 1e-10)
                return 0.0;   // 三点共线，退化为直线段

            double a2 = ax * ax + ay * ay;
            double m2 = mx * mx + my * my;
            double b2 = bx * bx + by * by;

            // 外接圆圆心
            double cx = (a2 * (my - by) + m2 * (by - ay) + b2 * (ay - my)) / D;
            double cy = (a2 * (bx - mx) + m2 * (ax - bx) + b2 * (mx - ax)) / D;

            // 各点到圆心的角度
            double sa = std::atan2(ay - cy, ax - cx);   // 起点角
            double ea = std::atan2(by - cy, bx - cx);   // 终点角

            // 叉积判断 A→M→B 绕向
            double cross = (mx - ax) * (by - ay) - (my - ay) * (bx - ax);

            double sweep;
            if (cross >= 0.0)
            {
                // 逆时针（CCW）：对应 bulge < 0（弧向右侧，ComputeArc 约定）
                sweep = ea - sa;
                if (sweep <= 0.0) sweep += Math::TwoPI;
                // CCW sweep → bulge 为负
                return -std::tan(sweep * 0.25);
            }
            else
            {
                // 顺时针（CW）：对应 bulge > 0（弧向左侧，ComputeArc 约定）
                sweep = sa - ea;
                if (sweep <= 0.0) sweep += Math::TwoPI;
                // CW sweep → bulge 为正
                return std::tan(sweep * 0.25);
            }
        }

        // ── 刷新 Overlay ─────────────────────────────────────────────────────
        void RefreshOverlay()
        {
            m_overlay.Clear();
            if (m_points.empty()) return;
        
            const auto& layer = m_scene.GetLayerManager().GetActiveLayer();
            const Math::Color4 layerColor  = layer.GetColor();
            const Math::Color4 rubberColor = { 0.6f, 0.6f, 0.6f, 0.5f };
            const Math::Color4 helperColor = { 0.5f, 0.5f, 0.5f, 0.25f };
            const Math::Color4 midColor    = { 1.0f, 0.8f, 0.2f, 0.8f };
        
            // 已确认的折线段（含直线段和弧段）
            if (m_points.size() >= 2)
            {
                Polyline pl(m_points, m_bulges);
                m_overlay.AddPolyline(pl, layerColor);
            }
        
			// 已确认顶点
            for (const auto& pt : m_points)
                m_overlay.AddPoint(pt, midColor);
        
            const Math::Point3& last = m_points.back();
        
            if (m_mode == DrawMode::Line)
            {
                // 直线橡皮筋
                m_overlay.AddLine(last, m_cursor, rubberColor);
            }
            else
            {
                if (!m_hasArcMid)
                {
                    // 尚未采集弧上点：辅助直线提示用户点击弧上点
                    m_overlay.AddLine(last, m_cursor, rubberColor);
                }
                else
                {
                    // 已有弧上点：实时三点弧预览
                    m_overlay.AddPoint(m_arcMid, midColor);
        
                    double bulgePreview =
                        ComputeBulgeFromThreePoints(last, m_arcMid, m_cursor);
        
                    if (std::abs(bulgePreview) > 1e-12)
                    {
                        // ── 关键修改：用 ComputeArc + AddArcGeom，保留有符号 sweep ──
                        ArcGeom arc = Polyline::ComputeArc(last, m_cursor, bulgePreview);
                        if (arc.Radius > Math::LengthEPS)
                        {
                            m_overlay.AddArcGeom(arc, rubberColor);  // ← 有符号，方向正确
                        }
                        else
                        {
                            m_overlay.AddLine(last, m_cursor, rubberColor);
                        }
                    }
                    else
                    {
                        // 三点接近共线，退化为直线
                        m_overlay.AddLine(last, m_cursor, rubberColor);
                    }
        
                    // 辅助线：帮助用户理解三点位置关系
                    m_overlay.AddLine(last,     m_arcMid, helperColor);
                    m_overlay.AddLine(m_arcMid, m_cursor, helperColor);
                }
            }
        }
        // ── 提交折线到命令栈 ─────────────────────────────────────────────────
        void Commit()
        {
            if (m_points.size() < 2)
            {
                printf("[PolylineTool] 顶点不足，放弃提交\n");
                return;
            }

            // 统计直线段 / 弧段数量，用于日志
            int arcCount = 0, lineCount = 0;
            for (double b : m_bulges)
                (std::abs(b) > 1e-12 ? arcCount : lineCount)++;

            auto id     = m_scene.NextObjectID();
            auto entity = std::make_unique<PolylineEntity>(id, m_points, m_bulges);
            auto cmd    = std::make_unique<AddEntityCommand>(std::move(entity));
            m_cmdStack.Execute(std::move(cmd), m_scene);

            printf("[PolylineTool] 提交 Id=%d  顶点=%zu  直线段=%d  弧段=%d\n", static_cast<int>(id), m_points.size(), lineCount, arcCount);
        }

        // ── 重置工具状态（不退出工具） ───────────────────────────────────────
        void Reset()
        {
            m_points.clear();
            m_bulges.clear();
            m_hasArcMid = false;
            m_arcMid    = {};
            m_cursor    = {};
            m_overlay.Clear();
        }

        // ── 获取输入点（优先使用捕捉点） ─────────────────────────────────────
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            auto p = m_viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
            return { p.x, p.y, 0.0 };
        }

    private:

        Scene&        m_scene;
        CommandStack& m_cmdStack;
        Viewport&     m_viewport;
        Overlay&      m_overlay;

        DrawMode m_mode = DrawMode::Line;

        // 已确认顶点序列和对应 Bulge 序列
        // m_bulges[i] 描述 m_points[i] → m_points[i+1] 这段的弯曲程度
        std::vector<Math::Point3> m_points;
        std::vector<double>       m_bulges;

        // 圆弧模式的中间状态
        bool         m_hasArcMid = false;  // 是否已采集弧上点
        Math::Point3 m_arcMid{};           // 弧上经过点（等待终点时暂存）

        Math::Point3 m_cursor{};           // 当前鼠标位置（橡皮筋用）
    };
}