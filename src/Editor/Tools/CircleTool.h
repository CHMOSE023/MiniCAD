#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/Viewport/Viewport.h"
#include "Document/CommandStack/CommandStack.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Editor/Overlay/Overlay.h"
#include <cstdio>
#include <cmath>

namespace MiniCAD
{
    class CircleTool : public ITool
    {
    public:
        CircleTool(Scene& scene, CommandStack& cmdStack, Viewport& viewport, Overlay& overlay)
            : m_scene(scene)
            , m_cmdStack(cmdStack)
            , m_viewport(viewport)
            , m_overlay(overlay)
        {
            printf("[CircleTool] 左键定圆心 | 左键确认半径 | 右键/ESC 退出\n");
        }

        ~CircleTool()
        {
            printf("退出绘制\n");
        }

        bool OnInput(const InputEvent& e) override
        {
            if (e.IsLeftClick())
            {
                auto pt = GetPoint(e);
                if (!m_hasCenter)
                {
                    // 第一步：确定圆心
                    m_center = pt;
                    m_hasCenter = true;
                }
                else
                {
                    // 第二步：确定半径并提交
                    double r = Distance2D(m_center, pt);
                    if (r > Math::LengthEPS)
                    {
                        Commit(m_center, r);
                    }
                    Reset();
                }
                return true;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                m_overlay.Clear();
                Reset();
                if (OnFinished) OnFinished();
                return true;
            }

            if (e.Type == InputEventType::MouseMove && m_hasCenter)
            {
                auto  cursor = GetPoint(e);
                double r = Distance2D(m_center, cursor);
                m_overlay.Clear();
                m_overlay.AddCircle(m_center, r, { 0.6, 0.6, 0.6, 0.6 });  // 预览圆
                m_overlay.AddLine(m_center, cursor, { 0.6, 0.6, 0.6, 0.4 }); // 半径辅助线
                return false; // 交给渲染
            }

            return false;
        }

        // ── 锚点：圆心已定时对外暴露，供捕捉系统使用 ──────────────────────
        bool HasAnchor() const override
        {
            return m_hasCenter;
        }

        Math::Point3 GetAnchor() const override
        {
            return { m_center.x, m_center.y, 0.0 };
        }

        // ── Undo / Redo 后重置状态 ────────────────────────────────────────
        void OnSceneChanged() override
        {
            Reset();
        }

    private:
        // ── 辅助：获取当前输入点（优先捕捉点）────────────────────────────
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        // ── 辅助：XY 平面距离 ────────────────────────────────────────────
        static double Distance2D(const Math::Point3& a, const Math::Point3& b)
        {
            double dx = b.x - a.x;
            double dy = b.y - a.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        // ── 提交命令到命令栈 ─────────────────────────────────────────────
        void Commit(const Math::Point3& center, double radius)
        {
            auto id = m_scene.NextObjectID();
            auto circle = std::make_unique<CircleEntity>(id, center, radius);
            auto cmd = std::make_unique<AddEntityCommand>(std::move(circle));
            m_cmdStack.Execute(std::move(cmd), m_scene);
            printf("圆 Id %d  center(%.3f,%.3f)  r=%.3f\n",
                static_cast<int>(id), center.x, center.y, radius);
        }

        // ── 重置工具状态（不退出工具，可继续画下一个圆）──────────────────
        void Reset()
        {
            m_hasCenter = false;
            m_center = {};
            m_overlay.Clear();
        }

    private:
        Scene& m_scene;
        CommandStack& m_cmdStack;
        Viewport& m_viewport;
        Overlay& m_overlay;

        bool         m_hasCenter = false;
        Math::Point3 m_center{};
    };
}
