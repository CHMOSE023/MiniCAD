#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/EntityFillet.h"
#include "Document/Command/GeometryEditCommand.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Math/Point3.hpp"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "Core/Log.h"

namespace MiniCAD
{
    // =========================================================================
    // FilletTool —— 圆角
    //
    // 依次点选两个对象（直线 / 圆弧 / 圆），在它们之间生成半径为 R 的圆角弧，
    // 两个对象修剪或延伸到切点，保留点选的一侧。圆不会被修剪。
    // 半径 R 由 Editor 持有、在多次圆角之间保留；随时键入数字即可修改（0 = 只把两条直线修剪 / 延伸到交点）。
    // 一次圆角一步可撤销；完成后回到「选择第一个对象」，右键 / ESC 退出。
    // =========================================================================
    class FilletTool : public ITool
    {
    public:
        explicit FilletTool(double* radius) : m_radius(radius) { LOG_INFO("[FilletTool] 选择第一个对象 | 键入数字修改圆角半径"); }
        ~FilletTool() { LOG_DEBUG("[FilletTool] 退出"); }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx     = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsRightClick() || e.IsCancel())
            {
                Cancel();
                return true;
            }

            if (e.IsLeftClick())
            {
                Entity* ent = HitCurve(e);
                if (!ent)
                    return true;

                if (m_firstId == Object::InvalidID)
                {
                    m_firstId   = ent->GetID();
                    m_firstPick = GetPoint(e);
                    return true;
                }
                if (ent->GetID() == m_firstId)
                    return true;                    // 两次点同一个对象：忽略，继续等第二个

                Commit(*ent, GetPoint(e));
                return true;
            }

            if (e.Type == InputEventType::MouseMove && m_firstId != Object::InvalidID)
            {
                ctx.overlay.Clear();
                if (Entity* ent = HitCurve(e); ent && ent->GetID() != m_firstId)
                    Preview(*ent, GetPoint(e));
                return false;
            }
            return false;
        }

        // 注意：m_ctx 指向 OnInput 期间的栈对象，事件之外只能用 m_overlay
        void Cancel() override
        {
            if (m_overlay) m_overlay->Clear();
            if (OnFinished) OnFinished();
        }

        void OnSceneChanged() override { m_firstId = Object::InvalidID; if (m_overlay) m_overlay->Clear(); }
        void OnFocusLost()    override { if (m_overlay) m_overlay->Clear(); }

        // 键入数字：修改圆角半径
        bool OnNumberInput(double value) override
        {
            if (!m_radius || value < 0.0)
                return false;
            *m_radius = value;
            return true;
        }

        std::string GetPrompt() const override
        {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "当前圆角半径 = %.4g。", m_radius ? *m_radius : 0.0);
            return std::string(buf) + (m_firstId == Object::InvalidID ? "选择第一个对象 [键入数字修改半径]:" : "选择第二个对象 [键入数字修改半径]:");
        }

    private:
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        Entity* HitCurve(const InputEvent& e) const
        {
            const Object::ObjectID id = m_ctx->picking.HitTest(
                { static_cast<double>(e.MouseX), static_cast<double>(e.MouseY) }, 8.0);
            if (id == Object::InvalidID) return nullptr;
            Object* obj = m_ctx->scene.GetEntity(id);
            if (obj && m_ctx->scene.IsEntityLocked(*obj)) return nullptr;     // 锁定图层上的对象不能修改
            if (!obj || !obj->IsKindOf<Entity>()) return nullptr;
            Entity* ent = static_cast<Entity*>(obj);
            if (ent->IsKindOf<LineEntity>() || ent->IsKindOf<ArcEntity>() || ent->IsKindOf<CircleEntity>())
                return ent;
            return nullptr;
        }

        void Preview(const Entity& second, const Math::Point3& pick)
        {
            const auto* first = dynamic_cast<const Entity*>(m_ctx->scene.GetEntity(m_firstId));
            if (!first) return;
            FilletResult r;
            if (!ComputeFillet(*first, m_firstPick, second, pick, *m_radius, r) || !r.arc)
                return;
            static const Math::Color4 kPreview = { 1.0, 0.85, 0.0, 0.9 };
            const Arc& a = r.arc->GetArc();
            m_ctx->overlay.AddArc(a.Center, a.Radius, a.StartAngle, a.EndAngle, kPreview);
        }

        void Commit(const Entity& second, const Math::Point3& pick)
        {
            const auto* first = dynamic_cast<const Entity*>(m_ctx->scene.GetEntity(m_firstId));
            FilletResult r;
            if (!first || !ComputeFillet(*first, m_firstPick, second, pick, *m_radius, r))
            {
                LOG_WARN("[FilletTool] 这两个对象之间无法生成圆角（平行、半径过大或类型不支持）");
                m_firstId = Object::InvalidID;
                m_ctx->overlay.Clear();
                return;
            }

            std::vector<GeometryEditCommand::Item> items;
            auto addEdit = [&](const Entity& src, std::unique_ptr<Entity> after)
            {
                if (!after) return;                     // 不需要修改（圆）
                GeometryEditCommand::Item item;
                item.id     = src.GetID();
                item.before = src.Clone(src.GetID());
                item.after  = std::move(after);
                items.push_back(std::move(item));
            };
            addEdit(*first, std::move(r.first));
            addEdit(second, std::move(r.second));

            if (r.arc)
            {
                GeometryEditCommand::Item item;
                item.id    = m_ctx->scene.NextObjectID();
                item.after = r.arc->Clone(item.id);
                items.push_back(std::move(item));
            }

            m_ctx->cmdStack.Execute(std::make_unique<GeometryEditCommand>("圆角", std::move(items)), m_ctx->scene);
            LOG_DEBUG("[FilletTool] 圆角完成，半径 %.4g", *m_radius);

            m_firstId = Object::InvalidID;              // 回到「选择第一个对象」
            m_ctx->overlay.Clear();
        }

        double*              m_radius = nullptr;        // 由 Editor 持有，多次圆角之间保留
        const EditorContext* m_ctx     = nullptr;       // 仅在 OnInput 内有效
        Overlay*             m_overlay = nullptr;
        Object::ObjectID     m_firstId = Object::InvalidID;
        Math::Point3         m_firstPick{};
    };
}
