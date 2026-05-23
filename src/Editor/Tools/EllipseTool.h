#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/Input/InputContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include <cstdio>
#include <cmath>

namespace MiniCAD
{
    class EllipseTool : public ITool
    {
    public:
        EllipseTool()
        {
            printf("[EllipseTool] 左键圆心 | 左键长轴端点 | 左键短轴端点确认 | 右键/ESC 退出\n");
        }

        ~EllipseTool()
        {
            printf("退出绘制\n");
        }

        bool OnInput(const InputContext& ctx) override
        {
            m_ctx = &ctx;
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                auto pt = GetPoint(e);
                switch (m_step)
                {
                    case 0:
                        m_center = pt;
                        m_step   = 1;
                        printf("[EllipseTool] 圆心 (%.3f, %.3f) 已定，请点击长轴端点\n",
                               pt.x, pt.y);
                        break;

                    case 1:
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

                    case 2:
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

            if (e.IsRightClick() || e.IsCancel())
            {
                m_ctx->overlay.Clear();
                Reset();
                if (OnFinished) OnFinished();
                return true;
            }

            if (e.Type == InputEventType::MouseMove && m_step > 0)
            {
                auto cursor = GetPoint(e);
                m_ctx->overlay.Clear();

                const auto& layer = m_ctx->scene.GetLayerManager().GetActiveLayer();

                const Math::Color4 layerColor  = layer.GetColor();
                const Math::Color4 helperColor = { 0.6, 0.6, 0.6, 0.7 };
                const Math::Color4 axisColor   = { 0.4, 0.8, 1.0, 0.7 };

                if (m_step == 1)
                {
                    m_ctx->overlay.AddLine(m_center, cursor, helperColor);
                    double rx = Distance2D(m_center, cursor);
                    if (rx > Math::LengthEPS)
                        m_ctx->overlay.AddCircle(m_center, rx, { layerColor.r, layerColor.g,  layerColor.b, layerColor.a });
                }
                else if (m_step == 2)
                {
                    double ry = ComputeRY(cursor);

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
                    m_ctx->overlay.AddLine(axisStart, axisEnd, axisColor);

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
                    m_ctx->overlay.AddLine(perpStart, perpEnd, axisColor);

                    if (ry > Math::LengthEPS)
                        m_ctx->overlay.AddEllipse(m_center, m_rx, ry, m_rotation, layerColor);
                }

                return false;
            }

            return false;
        }

        bool HasAnchor() const override { return m_step > 0; }

        Math::Point3 GetAnchor() const override { return m_center; }

        void OnSceneChanged() override { Reset(); }

    private:
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        static double Distance2D(const Math::Point3& a, const Math::Point3& b)
        {
            double dx = b.x - a.x, dy = b.y - a.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        double ComputeRY(const Math::Point3& p) const
        {
            double dx   = p.x - m_center.x;
            double dy   = p.y - m_center.y;
            double cosR = std::cos(m_rotation);
            double sinR = std::sin(m_rotation);
            return std::abs(dx * sinR - dy * cosR);
        }

        void Commit(const Math::Point3& center, double rx, double ry, double rot)
        {
            auto id     = m_ctx->scene.NextObjectID();
            auto entity = std::make_unique<EllipseEntity>(id, center, rx, ry, rot);
            auto cmd    = std::make_unique<AddEntityCommand>(std::move(entity));
            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);

            printf("椭圆 Id %d  center(%.3f,%.3f)  rx=%.3f  ry=%.3f  rot=%.1f°\n",
                   static_cast<int>(id),
                   center.x, center.y, rx, ry,
                   rot * 180.0 / Math::PI);
        }

        void Reset()
        {
            m_step     = 0;
            m_rx       = 0.0;
            m_rotation = 0.0;
            m_center   = {};
            if (m_ctx) m_ctx->overlay.Clear();
        }

    private:
        const InputContext* m_ctx = nullptr;

        int          m_step     = 0;
        Math::Point3 m_center{};
        double       m_rx       = 0.0;
        double       m_rotation = 0.0;
    };
}
