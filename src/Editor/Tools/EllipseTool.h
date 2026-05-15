#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/Viewport/Viewport.h"
#include "Document/CommandStack/CommandStack.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Editor/Overlay/Overlay.h"
#include <cstdio>
#include <cmath>

namespace MiniCAD
{
    // ── 三步椭圆工具 ─────────────────────────────────────────────────────
    //
    //   Step 1  左键  →  圆心
    //   Step 2  左键  →  长轴端点（确定 RadiusX 和旋转角 Rotation）
    //   Step 3  左键  →  短轴端点（鼠标到长轴的垂直距离 = RadiusY）
    //   右键 / ESC    →  退出工具
    //
    class EllipseTool : public ITool
    {
    public:
        EllipseTool(Scene& scene, CommandStack& cmdStack, Viewport& viewport, Overlay& overlay)
            : m_scene(scene)
            , m_cmdStack(cmdStack)
            , m_viewport(viewport)
            , m_overlay(overlay)
        {
            printf("[EllipseTool] 左键圆心 | 左键长轴端点 | 左键短轴端点确认 | 右键/ESC 退出\n");
        }

        ~EllipseTool()
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
                    case 0:                             // 确定圆心
                        m_center = pt;
                        m_step   = 1;
                        printf("[EllipseTool] 圆心 (%.3f, %.3f) 已定，请点击长轴端点\n",
                               pt.x, pt.y);
                        break;

                    case 1:                             // 确定长轴端点
                    {
                        double rx = Distance2D(m_center, pt);
                        if (rx < Math::LengthEPS)
                        {
                            printf("[EllipseTool] 长轴太短，请重新点击\n");
                            break;
                        }
                        m_rx       = rx;
                        m_rotation = std::atan2(pt.y - m_center.y,
                                                pt.x - m_center.x);
                        m_step     = 2;
                        printf("[EllipseTool] 长轴端点已定，rx=%.3f  rot=%.1f°，请点击短轴端点\n",
                               m_rx, m_rotation * 180.0 / Math::PI);
                        break;
                    }

                    case 2:                             // 确定短轴长度，提交
                    {
                        double ry = ComputeRY(pt);
                        if (ry < Math::LengthEPS)
                        {
                            printf("[EllipseTool] 短轴太短，请重新点击\n");
                            break;
                        }
                        Commit(m_center, m_rx, ry, m_rotation);
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
                const Math::Color4 helperColor = { 0.6, 0.6, 0.6, 0.7 };
                const Math::Color4 axisColor   = { 0.4, 0.8, 1.0, 0.7 };

                if (m_step == 1)
                {
                    // 预览：从圆心到光标的长轴辅助线
                    m_overlay.AddLine(m_center, cursor, helperColor);
                    // 以当前距离为 rx 预览圆（退化参考）
                    double rx = Distance2D(m_center, cursor);
                    if (rx > Math::LengthEPS)
                        m_overlay.AddCircle(m_center, rx, { layerColor.r, layerColor.g,  layerColor.b, layerColor.a });
                }
                else if (m_step == 2)
                {
                    double ry = ComputeRY(cursor);

                    // 长轴辅助线（双向）
                    Math::Point3 axisEnd =
                    {
                        m_center.x + m_rx * std::cos(m_rotation),
                        m_center.y + m_rx * std::sin(m_rotation),
                        m_center.z
                    };
                    Math::Point3 axisStart =
                    {
                        m_center.x - m_rx * std::cos(m_rotation),
                        m_center.y - m_rx * std::sin(m_rotation),
                        m_center.z
                    };
                    m_overlay.AddLine(axisStart, axisEnd, axisColor);

                    // 短轴辅助线（过圆心，垂直于长轴）
                    double perpAngle = m_rotation + Math::PI * 0.5;
                    Math::Point3 perpEnd =
                    {
                        m_center.x + ry * std::cos(perpAngle),
                        m_center.y + ry * std::sin(perpAngle),
                        m_center.z
                    };
                    Math::Point3 perpStart =
                    {
                        m_center.x - ry * std::cos(perpAngle),
                        m_center.y - ry * std::sin(perpAngle),
                        m_center.z
                    };
                    m_overlay.AddLine(perpStart, perpEnd, axisColor);

                    // 预览椭圆
                    if (ry > Math::LengthEPS)
                        m_overlay.AddEllipse(m_center, m_rx, ry, m_rotation, layerColor);
                }

                return false;
            }

            return false;
        }

        // ── 锚点 ─────────────────────────────────────────────────────────
        bool HasAnchor() const override { return m_step > 0; }

        Math::Point3 GetAnchor() const override { return m_center; }

        // ── Undo / Redo 后重置 ───────────────────────────────────────────
        void OnSceneChanged() override { Reset(); }

    private:
        // ── 辅助：获取当前输入点 ─────────────────────────────────────────
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        // ── 辅助：XY 平面距离 ────────────────────────────────────────────
        static double Distance2D(const Math::Point3& a, const Math::Point3& b)
        {
            double dx = b.x - a.x, dy = b.y - a.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        // ── 辅助：计算点 p 到长轴直线的垂直距离（= RadiusY）─────────────
        //   长轴方向向量 d = (cos(Rotation), sin(Rotation))
        //   ry = |(p - center) × d|  （二维叉积的绝对值）
        double ComputeRY(const Math::Point3& p) const
        {
            double dx   = p.x - m_center.x;
            double dy   = p.y - m_center.y;
            double cosR = std::cos(m_rotation);
            double sinR = std::sin(m_rotation);
            // 二维叉积：dx*sinR - dy*cosR（符号代表哪侧，取绝对值）
            return std::abs(dx * sinR - dy * cosR);
        }

        // ── 提交命令到命令栈 ─────────────────────────────────────────────
        void Commit(const Math::Point3& center, double rx, double ry, double rot)
        {
            auto id     = m_scene.NextObjectID();
            auto entity = std::make_unique<EllipseEntity>(id, center, rx, ry, rot);
            auto cmd    = std::make_unique<AddEntityCommand>(std::move(entity));
            m_cmdStack.Execute(std::move(cmd), m_scene);

            printf("椭圆 Id %d  center(%.3f,%.3f)  rx=%.3f  ry=%.3f  rot=%.1f°\n",
                   static_cast<int>(id),
                   center.x, center.y, rx, ry,
                   rot * 180.0 / Math::PI);
        }

        // ── 重置（不退出工具，可继续画下一个）──────────────────────────
        void Reset()
        {
            m_step     = 0;
            m_rx       = 0.0;
            m_rotation = 0.0;
            m_center   = {};
            m_overlay.Clear();
        }

    private:
        Scene&        m_scene;
        CommandStack& m_cmdStack;
        Viewport&     m_viewport;
        Overlay&      m_overlay;

        int          m_step     = 0;    // 0=待圆心  1=待长轴端点  2=待短轴端点
        Math::Point3 m_center{};
        double       m_rx       = 0.0;  // 长轴半轴（step2 确定后固定）
        double       m_rotation = 0.0;  // 长轴旋转角（step2 确定后固定）
    };
}
