#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Editor/Tools/DimAssocPick.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Vec3.hpp"
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Log.h"
#include <cmath>

namespace MiniCAD
{
    // 创建尺寸标注：三步——标注点 1 → 标注点 2 → 尺寸线位置。
    // 默认对齐线性（Aligned）；尺寸线方向锁定为水平/垂直时给出转角线性（Linear）。
    class DimensionTool : public ITool
    {
    public:
        DimensionTool()
        {
            LOG_DEBUG("[DimensionTool] 左键第一标注点 | 左键第二标注点 | 左键尺寸线位置 | 右键/ESC 退出");
        }

        ~DimensionTool() { LOG_DEBUG("退出尺寸标注"); }

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
                        m_ref1 = SnapAssocRef(ctx, pt);
                        m_step = 1;
                        LOG_INFO("[DimensionTool] 第一标注点 (%.3f, %.3f) 已定", pt.x, pt.y);
                        break;

                    case 1:
                        m_p2 = pt;
                        m_ref2 = SnapAssocRef(ctx, pt);
                        m_step = 2;
                        LOG_INFO("[DimensionTool] 第二标注点 (%.3f, %.3f) 已定，请指定尺寸线位置",
                               pt.x, pt.y);
                        break;

                    case 2:
                        Commit(pt);
                        Reset();
                        break;
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

                // 始终提示两标注点连线。
                m_ctx->overlay.AddLine(m_p1, m_p2, helperColor);

                if (m_step == 2)
                {
                    DrawPreview(cursor, layerColor, helperColor);
                }

                return false;
            }

            return false;
        }

        bool HasAnchor() const override { return m_step > 0; }

        Math::Point3 GetAnchor() const override
        {
            return m_step >= 2 ? m_p2 : m_p1;
        }

        std::string GetPrompt() const override
        {
            switch (m_step)
            {
                case 1:  return "指定第二标注点:";
                case 2:  return "指定尺寸线位置 [右键/ESC 退出]:";
                default: return "指定第一标注点:";
            }
        }

        void OnSceneChanged() override { Reset(); }

    private:
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        // 把尺寸线方向锁定到水平/垂直（接近时），否则对齐两标注点连线。
        // 返回 true 表示转角线性（Linear，带固定角度），false 表示对齐线性（Aligned）。
        bool ResolveAngle(double& angle) const
        {
            Math::Vec3 d = m_p2 - m_p1;
            double len = d.Length();
            if (len < Math::LengthEPS)
            {
                angle = 0.0;
                return true;
            }
            double a = std::atan2(d.y, d.x);
            // 规整到 [0, π)，便于判断方向。
            if (a < 0.0)        a += Math::PI;
            if (a >= Math::PI)  a -= Math::PI;

            const double tol = 1.0 * Math::PI / 180.0;   // 1° 容差
            if (a < tol || a > Math::PI - tol) { angle = 0.0;          return true; }   // 水平
            if (std::abs(a - Math::HalfPI) < tol) { angle = Math::HalfPI; return true; } // 垂直
            return false;
        }

        void DrawPreview(const Math::Point3& dimLinePoint,
                         const Math::Color4& dimColor,
                         const Math::Color4& helperColor) const
        {
            double angle = 0.0;
            bool linear = ResolveAngle(angle);

            Math::Vec3 u = linear
                ? Math::Vec3{ std::cos(angle), std::sin(angle), 0.0 }
                : (m_p2 - m_p1).Normalized();
            if (u.LengthSq() < 0.5) u = { 1, 0, 0 };

            // 把两标注点投影到「过 dimLinePoint、方向 u」的尺寸线上。
            Math::Point3 d1 = ProjectOnLine(m_p1, dimLinePoint, u);
            Math::Point3 d2 = ProjectOnLine(m_p2, dimLinePoint, u);

            m_ctx->overlay.AddLine(m_p1, d1, helperColor);   // 尺寸界线
            m_ctx->overlay.AddLine(m_p2, d2, helperColor);
            m_ctx->overlay.AddLine(d1, d2, dimColor);        // 尺寸线
        }

        static Math::Point3 ProjectOnLine(const Math::Point3& p,
                                          const Math::Point3& base, const Math::Vec3& u)
        {
            Math::Vec3 w = p - base;
            double t = w.x * u.x + w.y * u.y + w.z * u.z;
            return base + u * t;
        }

        void Commit(const Math::Point3& dimLinePoint)
        {
            double angle = 0.0;
            bool linear = ResolveAngle(angle);

            auto id  = m_ctx->scene.NextObjectID();
            auto dim = linear
                ? DimensionEntity::MakeLinear(id, m_p1, m_p2, dimLinePoint, angle)
                : DimensionEntity::MakeAligned(id, m_p1, m_p2, dimLinePoint);

            double measure = dim->Measurement();
            dim->SetAssoc(DimAssocSlot::P1, m_ref1);     // 用对象捕捉拾取的标注点关联到对象
            dim->SetAssoc(DimAssocSlot::P2, m_ref2);
            m_ctx->ApplyCurrentAttr(*dim);
            auto cmd = std::make_unique<AddEntityCommand>(std::move(dim));
            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);

            LOG_DEBUG("尺寸标注 Id %d  %s  测量值=%.3f",
                   static_cast<int>(id), linear ? "Linear" : "Aligned", measure);
        }

        void Reset()
        {
            m_step = 0;
            m_p1 = m_p2 = {};
            m_ref1 = m_ref2 = {};
            if (m_overlay) m_overlay->Clear();
        }

    private:
        const EditorContext* m_ctx = nullptr;
        Overlay*              m_overlay = nullptr;

        int          m_step = 0;
        Math::Point3 m_p1{};
        Math::Point3 m_p2{};
        DimAssocRef  m_ref1, m_ref2;     // 两标注点的关联（无捕捉时无效）
    };
}
