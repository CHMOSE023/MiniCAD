#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/Input/InputContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include <cstdio>
#include <cmath>

namespace MiniCAD
{
    class CircleTool : public ITool
    {
    public:
        CircleTool()
        {
            printf("[CircleTool] 左键定圆心 | 左键确认半径 | 右键/ESC 退出\n");
        }

        ~CircleTool()
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
                if (!m_hasCenter)
                {
                    m_center = pt;
                    m_hasCenter = true;
                }
                else
                {
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
                m_ctx->overlay.Clear();
                Reset();
                if (OnFinished) OnFinished();
                return true;
            }

            if (e.Type == InputEventType::MouseMove && m_hasCenter)
            {
                auto  cursor = GetPoint(e);
                double r = Distance2D(m_center, cursor);
                m_ctx->overlay.Clear();

                const  auto& layer = m_ctx->scene.GetLayerManager().GetActiveLayer();

                m_ctx->overlay.AddCircle(m_center, r, layer.GetColor());
                m_ctx->overlay.AddLine  (m_center, cursor, { 0.6, 0.6, 0.6, 0.4 });

                return false;
            }

            return false;
        }

        bool HasAnchor() const override
        {
            return m_hasCenter;
        }

        Math::Point3 GetAnchor() const override
        {
            return { m_center.x, m_center.y, 0.0 };
        }

        void OnSceneChanged() override
        {
            Reset();
        }

    private:
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        static double Distance2D(const Math::Point3& a, const Math::Point3& b)
        {
            double dx = b.x - a.x;
            double dy = b.y - a.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        void Commit(const Math::Point3& center, double radius)
        {
            auto id = m_ctx->scene.NextObjectID();
            auto circle = std::make_unique<CircleEntity>(id, center, radius);
            auto cmd = std::make_unique<AddEntityCommand>(std::move(circle));
            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);
            printf("圆 Id %d  center(%.3f,%.3f)  r=%.3f\n",
                static_cast<int>(id), center.x, center.y, radius);
        }

        void Reset()
        {
            m_hasCenter = false;
            m_center = {};
            if (m_ctx) m_ctx->overlay.Clear();
        }

    private:
        const InputContext* m_ctx = nullptr;

        bool         m_hasCenter = false;
        Math::Point3 m_center{};
    };
}
