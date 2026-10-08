#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Log.h"
#include <cmath>

namespace MiniCAD
{
    // 椭圆 / 椭圆弧
    //   椭圆  ：圆心 → 长轴端点 → 短轴端点
    //   椭圆弧：同上定出椭圆后 → 起始角方向点 → 终止角方向点（逆时针，同 AutoCAD ELLIPSE 的「圆弧」选项）
    //           方向点经 Ellipse::ParamOfPoint 换算为参数角：椭圆弧端点落在「圆心→方向点」射线上
    class EllipseTool : public ITool
    {
    public:
        explicit EllipseTool(bool arcMode = false)
            : m_arcMode(arcMode)
        {
            LOG_DEBUG("[EllipseTool] 左键圆心 | 左键长轴端点 | 左键短轴端点%s | 右键/ESC 退出",
                      arcMode ? " | 左键起始角 | 左键终止角" : "确认");
        }

        ~EllipseTool()
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
                        m_center = pt;
                        m_step   = 1;
                        LOG_INFO("[EllipseTool] 圆心 (%.3f, %.3f) 已定，请点击长轴端点",
                               pt.x, pt.y);
                        break;

                    case 1:
                    {
                        double rx = Distance2D(m_center, pt);
                        if (rx < Math::LengthEPS)
                        {
                            LOG_WARN("[EllipseTool] 长轴太短，请重新点击");
                            break;
                        }
                        m_rx       = rx;
                        m_rotation = std::atan2(pt.y - m_center.y,
                                                pt.x - m_center.x);
                        m_step     = 2;
                        LOG_INFO("[EllipseTool] 长轴端点已定，rx=%.3f  rot=%.1f°，请点击短轴端点",
                               m_rx, m_rotation * 180.0 / Math::PI);
                        break;
                    }

                    case 2:
                    {
                        double ry = ComputeRY(pt);
                        if (ry < Math::LengthEPS)
                        {
                            LOG_WARN("[EllipseTool] 短轴太短，请重新点击");
                            break;
                        }
                        if (!m_arcMode)
                        {
                            Commit(Ellipse(m_center, m_rx, ry, m_rotation));
                            Reset();
                            break;
                        }
                        m_ry   = ry;
                        m_step = 3;
                        break;
                    }

                    case 3:
                        m_startParam = CurrentEllipse().ParamOfPoint(pt);
                        m_step       = 4;
                        break;

                    case 4:
                    {
                        Ellipse el = CurrentEllipse();
                        el.SetParams(m_startParam, el.ParamOfPoint(pt));
                        if (el.IsFull())
                        {
                            LOG_WARN("[EllipseTool] 起止角重合，请重新点击终止角");
                            break;
                        }
                        Commit(el);
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
                else if (m_step == 3)
                {
                    // 整椭圆淡显 + 起始角方向线
                    m_ctx->overlay.AddEllipse(CurrentEllipse(), helperColor);
                    m_ctx->overlay.AddLine(m_center, cursor, axisColor);
                }
                else if (m_step == 4)
                {
                    Ellipse el = CurrentEllipse();
                    m_ctx->overlay.AddEllipse(el, helperColor);
                    m_ctx->overlay.AddLine(m_center, el.PointAt(m_startParam), axisColor);
                    m_ctx->overlay.AddLine(m_center, cursor, axisColor);
                    el.SetParams(m_startParam, el.ParamOfPoint(cursor));
                    if (!el.IsFull())
                        m_ctx->overlay.AddEllipse(el, layerColor);
                }

                return false;
            }

            return false;
        }

        bool HasAnchor() const override { return m_step > 0; }

        Math::Point3 GetAnchor() const override { return m_center; }

        std::string GetPrompt() const override
        {
            switch (m_step)
            {
                case 1:  return "指定长轴端点:";
                case 2:  return m_arcMode ? "指定短轴端点:" : "指定短轴端点 [右键/ESC 退出]:";
                case 3:  return "指定起始角度:";
                case 4:  return "指定终止角度 [右键/ESC 退出]:";
                default: return "指定圆心:";
            }
        }

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

        Ellipse CurrentEllipse() const { return Ellipse(m_center, m_rx, m_ry, m_rotation); }

        void Commit(const Ellipse& el)
        {
            auto id     = m_ctx->scene.NextObjectID();
            auto entity = std::make_unique<EllipseEntity>(id, el);
            m_ctx->ApplyCurrentAttr(*entity);
            auto cmd    = std::make_unique<AddEntityCommand>(std::move(entity));
            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);

            LOG_DEBUG("椭圆 Id %d  center(%.3f,%.3f)  rx=%.3f  ry=%.3f  rot=%.1f°  param[%.1f°, %.1f°]",
                   static_cast<int>(id),
                   el.Center.x, el.Center.y, el.RadiusX, el.RadiusY,
                   el.Rotation * 180.0 / Math::PI,
                   el.StartParam * 180.0 / Math::PI, el.EndParam * 180.0 / Math::PI);
        }

        void Reset()
        {
            m_step       = 0;
            m_rx         = 0.0;
            m_ry         = 0.0;
            m_rotation   = 0.0;
            m_startParam = 0.0;
            m_center   = {};
            if (m_overlay) m_overlay->Clear();
        }

    private:
        const EditorContext* m_ctx = nullptr;
        Overlay*              m_overlay = nullptr;

        bool         m_arcMode    = false;
        int          m_step       = 0;
        Math::Point3 m_center{};
        double       m_rx         = 0.0;
        double       m_ry         = 0.0;
        double       m_rotation   = 0.0;
        double       m_startParam = 0.0;     // 椭圆弧：起始参数角
    };
}
