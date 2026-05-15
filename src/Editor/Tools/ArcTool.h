#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/Viewport/Viewport.h"
#include "Document/CommandStack/CommandStack.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Editor/Overlay/Overlay.h"
#include <cstdio>
#include <cmath>

namespace MiniCAD
{
    // ── 三点圆弧工具 ────────────────────────────────────────────────────────
    //
    //   Step 1  左键  →  起点 p1
    //   Step 2  左键  →  弧上经过点 p2
    //   Step 3  左键  →  终点 p3，提交圆弧
    //   右键 / ESC    →  退出工具
    //
    class ArcTool : public ITool
    {
    public:
        ArcTool(Scene& scene, CommandStack& cmdStack, Viewport& viewport, Overlay& overlay)
            : m_scene(scene)
            , m_cmdStack(cmdStack)
            , m_viewport(viewport)
            , m_overlay(overlay)
        {
            printf("[ArcTool] 左键起点 | 左键弧上点 | 左键终点确认 | 右键/ESC 退出\n");
        }

        ~ArcTool()
        {
            printf("退出绘制\n");
        }

        bool OnInput(const InputEvent& e) override
        {
            // ── 左键：逐步采集三个点 ─────────────────────────────────────
            if (e.IsLeftClick())
            {
                auto pt = GetPoint(e);
                switch (m_step)
                {
                    case 0:                     // 确定起点
                        m_p1 = pt;
                        m_step = 1;
                        printf("[ArcTool] 起点 (%.3f, %.3f) 已定，请点击弧上经过点\n",
                               pt.x, pt.y);
                        break;

                    case 1:                     // 确定弧上经过点
                        m_p2 = pt;
                        m_step = 2;
                        printf("[ArcTool] 弧上点 (%.3f, %.3f) 已定，请点击终点\n",
                               pt.x, pt.y);
                        break;

                    case 2:                     // 确定终点，提交
                    {
                        m_p3 = pt;
                        auto arc = Arc::FromThreePoints(m_p1, m_p2, m_p3);
                        if (arc)
                        {
                            Commit(*arc);
                        }
                        else
                        {
                            printf("[ArcTool] 三点共线，无法构成圆弧，请重新选择终点\n");
                        }
                        Reset();
                        break;
                    }
                }
                return true;
            }

            // ── 右键 / ESC：退出工具 ─────────────────────────────────────
            if (e.IsRightClick() || e.IsCancel())
            {
                m_overlay.Clear();
                Reset();
                if (OnFinished) OnFinished();
                return true;
            }

            // ── 鼠标移动：动态预览 ───────────────────────────────────────
            if (e.Type == InputEventType::MouseMove && m_step > 0)
            {
                auto cursor = GetPoint(e);
                m_overlay.Clear();

                const auto& layer = m_scene.GetLayerManager().GetActiveLayer();
                const Math::Color4 layerColor  = layer.GetColor();
                const Math::Color4 helperColor = { 0.6, 0.6, 0.6, 0.4 };

                if (m_step == 1)
                {
                    // 只有起点：画从起点到光标的辅助线
                    m_overlay.AddLine(m_p1, cursor, helperColor);
                }
                else if (m_step == 2)
                {
                    // 有起点 + 弧上点：尝试用光标作终点预览圆弧
                    auto arc = Arc::FromThreePoints(m_p1, m_p2, cursor);
                    if (arc)
                    {
                        m_overlay.AddArc(arc->Center, arc->Radius, arc->StartAngle, arc->EndAngle, layerColor); 
                    }
                    else
                    {
                        // 退化（三点接近共线）：退回直线预览
                        m_overlay.AddLine(m_p1, m_p2,  helperColor);
                        m_overlay.AddLine(m_p2, cursor, helperColor);
                    }

                    // 辅助：标注已定的两个点
                    m_overlay.AddLine(m_p1, m_p2, helperColor);
                }

                return false;   // 不消耗事件，交给渲染
            }

            return false;
        }

        // ── 锚点：供捕捉系统使用 ─────────────────────────────────────────
        bool HasAnchor() const override
        {
            return m_step > 0;
        }

        Math::Point3 GetAnchor() const override
        {
            // 始终暴露最后一个已确认的点作为锚点
            if (m_step == 2) return m_p2;
            return m_p1;
        }

        // ── Undo / Redo 后重置状态 ───────────────────────────────────────
        void OnSceneChanged() override
        {
            Reset();
        }

    private:
        // ── 辅助：获取当前输入点（优先捕捉点）───────────────────────────
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        // ── 提交命令到命令栈 ─────────────────────────────────────────────
        void Commit(const Arc& arc)
        {
            auto id     = m_scene.NextObjectID();
            auto entity = std::make_unique<ArcEntity>(id, arc.Center, arc.Radius,
                                                      arc.StartAngle, arc.EndAngle);
            auto cmd    = std::make_unique<AddEntityCommand>(std::move(entity));
            m_cmdStack.Execute(std::move(cmd), m_scene);

            printf("圆弧 Id %d  center(%.3f,%.3f)  r=%.3f  [%.1f°, %.1f°]\n",
                   static_cast<int>(id),
                   arc.Center.x, arc.Center.y,
                   arc.Radius,
                   arc.StartAngle * 180.0 / Math::PI,
                   arc.EndAngle   * 180.0 / Math::PI);
        }

        // ── 重置工具状态（不退出工具，可继续画下一段弧）────────────────
        void Reset()
        {
            m_step = 0;
            m_p1 = m_p2 = m_p3 = {};
            m_overlay.Clear();
        }

    private:
        Scene&        m_scene;
        CommandStack& m_cmdStack;
        Viewport&     m_viewport;
        Overlay&      m_overlay;

        int          m_step = 0;    // 0=待起点  1=待弧上点  2=待终点
        Math::Point3 m_p1{};        // 起点
        Math::Point3 m_p2{};        // 弧上经过点
        Math::Point3 m_p3{};        // 终点（仅提交时用）
    };
}
