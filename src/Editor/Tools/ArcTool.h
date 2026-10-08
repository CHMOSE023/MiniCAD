#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Log.h"
#include <cmath>

namespace MiniCAD
{
    class ArcTool : public ITool
    {
    public:
        ArcTool()
        {
            LOG_DEBUG("[ArcTool] 左键起点 | 左键弧上点 | 左键终点确认 | 右键/ESC 退出");
        }

        ~ArcTool()
        {
            LOG_DEBUG("退出绘制");
        }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                auto pt = GetPoint(e);
                switch (m_step)
                {
                    case 0:
                        m_p1 = pt;
                        m_step = 1;
                        LOG_INFO("[ArcTool] 起点 (%.3f, %.3f) 已定，请点击弧上经过点",
                               pt.x, pt.y);
                        break;

                    case 1:
                        m_p2 = pt;
                        m_step = 2;
                        LOG_INFO("[ArcTool] 弧上点 (%.3f, %.3f) 已定，请点击终点",
                               pt.x, pt.y);
                        break;

                    case 2:
                    {
                        m_p3 = pt;
                        auto arc = Arc::FromThreePoints(m_p1, m_p2, m_p3);
                        if (arc)
                        {
                            Commit(*arc);
                        }
                        else
                        {
                            LOG_WARN("[ArcTool] 三点共线，无法构成圆弧，请重新选择终点");
                        }
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
                const Math::Color4 helperColor = { 0.6, 0.6, 0.6, 0.4 };

                if (m_step == 1)
                {
                    m_ctx->overlay.AddLine(m_p1, cursor, helperColor);
                }
                else if (m_step == 2)
                {
                    auto arc = Arc::FromThreePoints(m_p1, m_p2, cursor);
                    if (arc)
                    {
                        m_ctx->overlay.AddArc(arc->Center, arc->Radius, arc->StartAngle, arc->EndAngle, layerColor);
                    }
                    else
                    {
                        m_ctx->overlay.AddLine(m_p1, m_p2,  helperColor);
                        m_ctx->overlay.AddLine(m_p2, cursor, helperColor);
                    }

                    m_ctx->overlay.AddLine(m_p1, m_p2, helperColor);
                }

                return false;
            }

            return false;
        }

        bool HasAnchor() const override
        {
            return m_step > 0;
        }

        Math::Point3 GetAnchor() const override
        {
            if (m_step == 2) return m_p2;
            return m_p1;
        }

        std::string GetPrompt() const override
        {
            switch (m_step)
            {
                case 1:  return "指定圆弧上一点:";
                case 2:  return "指定终点 [右键/ESC 退出]:";
                default: return "指定起点:";
            }
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

        void Commit(const Arc& arc)
        {
            auto id     = m_ctx->scene.NextObjectID();
            auto entity = std::make_unique<ArcEntity>(id, arc.Center, arc.Radius,
                                                      arc.StartAngle, arc.EndAngle);
            m_ctx->ApplyCurrentAttr(*entity);
            auto cmd    = std::make_unique<AddEntityCommand>(std::move(entity));
            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);

            LOG_DEBUG("圆弧 Id %d  center(%.3f,%.3f)  r=%.3f  [%.1f°, %.1f°]",
                   static_cast<int>(id),
                   arc.Center.x, arc.Center.y,
                   arc.Radius,
                   arc.StartAngle * 180.0 / Math::PI,
                   arc.EndAngle   * 180.0 / Math::PI);
        }

        void Reset()
        {
            m_step = 0;
            m_p1 = m_p2 = m_p3 = {};
            if (m_overlay) m_overlay->Clear();
        }

    private:
        const EditorContext* m_ctx = nullptr;
        Overlay*              m_overlay = nullptr;

        int          m_step = 0;
        Math::Point3 m_p1{};
        Math::Point3 m_p2{};
        Math::Point3 m_p3{};
    };
}
