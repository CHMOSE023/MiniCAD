#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/EntityChamfer.h"
#include "Document/Command/GeometryEditCommand.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Math/Point3.hpp"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "Core/Log.h"

namespace MiniCAD
{
    // =========================================================================
    // ChamferTool —— 倒角（两条直线）
    //
    // 依次点选两条直线：从交点出发沿点选的一侧量出距离 D1（第一条）、D2（第二条），
    // 两条线修剪 / 延伸到这两个点，再连一条倒角线。一次倒角一步可撤销，完成后回到「选择第一条直线」。
    // 距离由 Editor 持有、多次倒角之间保留：键入一个数字同时设置 D1 和 D2，键入 "D1,D2" 分别设置；
    // 两个距离都为 0 时只把两条线修剪 / 延伸到交点。右键 / ESC 退出。
    // =========================================================================
    class ChamferTool : public ITool
    {
    public:
        ChamferTool(double* d1, double* d2) : m_d1(d1), m_d2(d2) { LOG_INFO("[ChamferTool] 选择第一条直线 | 键入数字修改倒角距离"); }
        ~ChamferTool() { LOG_DEBUG("[ChamferTool] 退出"); }

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
                Entity* ent = HitLine(e);
                if (!ent)
                    return true;

                if (m_firstId == Object::InvalidID)
                {
                    m_firstId   = ent->GetID();
                    m_firstPick = GetPoint(e);
                    return true;
                }
                if (ent->GetID() == m_firstId)
                    return true;

                Commit(*ent, GetPoint(e));
                return true;
            }

            if (e.Type == InputEventType::MouseMove && m_firstId != Object::InvalidID)
            {
                ctx.overlay.Clear();
                if (Entity* ent = HitLine(e); ent && ent->GetID() != m_firstId)
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

        bool OnNumberInput(double value) override { return OnNumberPairInput(value, value); }

        bool OnNumberPairInput(double d1, double d2) override
        {
            if (!m_d1 || !m_d2 || d1 < 0.0 || d2 < 0.0)
                return false;
            *m_d1 = d1;
            *m_d2 = d2;
            return true;
        }

        std::string GetPrompt() const override
        {
            char buf[112];
            std::snprintf(buf, sizeof(buf), "当前倒角距离 = %.4g, %.4g。", m_d1 ? *m_d1 : 0.0, m_d2 ? *m_d2 : 0.0);
            return std::string(buf) + (m_firstId == Object::InvalidID ? "选择第一条直线 [键入 距离 或 D1,D2 修改]:" : "选择第二条直线 [键入 距离 或 D1,D2 修改]:");
        }

    private:
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        Entity* HitLine(const InputEvent& e) const
        {
            const Object::ObjectID id = m_ctx->picking.HitTest(
                { static_cast<double>(e.MouseX), static_cast<double>(e.MouseY) }, 8.0);
            if (id == Object::InvalidID) return nullptr;
            Object* obj = m_ctx->scene.GetEntity(id);
            if (obj && m_ctx->scene.IsEntityLocked(*obj)) return nullptr;     // 锁定图层上的对象不能修改
            return obj && obj->IsKindOf<LineEntity>() ? static_cast<Entity*>(obj) : nullptr;
        }

        void Preview(const Entity& second, const Math::Point3& pick)
        {
            const auto* first = dynamic_cast<const Entity*>(m_ctx->scene.GetEntity(m_firstId));
            if (!first) return;
            ChamferResult r;
            if (!ComputeChamfer(*first, m_firstPick, second, pick, *m_d1, *m_d2, r) || !r.line)
                return;
            static const Math::Color4 kPreview = { 1.0, 0.85, 0.0, 0.9 };
            m_ctx->overlay.AddLine(r.line->GetLine().Start, r.line->GetLine().End, kPreview);
        }

        void Commit(const Entity& second, const Math::Point3& pick)
        {
            const auto* first = dynamic_cast<const Entity*>(m_ctx->scene.GetEntity(m_firstId));
            ChamferResult r;
            if (!first || !ComputeChamfer(*first, m_firstPick, second, pick, *m_d1, *m_d2, r))
            {
                LOG_WARN("[ChamferTool] 这两条直线之间无法倒角（平行、距离超过线的长度，或只有一个距离为 0）");
                m_firstId = Object::InvalidID;
                m_ctx->overlay.Clear();
                return;
            }

            std::vector<GeometryEditCommand::Item> items;
            auto addEdit = [&](const Entity& src, std::unique_ptr<LineEntity> after)
            {
                GeometryEditCommand::Item item;
                item.id     = src.GetID();
                item.before = src.Clone(src.GetID());
                item.after  = std::move(after);
                items.push_back(std::move(item));
            };
            addEdit(*first, std::move(r.first));
            addEdit(second, std::move(r.second));

            if (r.line)
            {
                GeometryEditCommand::Item item;
                item.id    = m_ctx->scene.NextObjectID();
                item.after = r.line->Clone(item.id);
                items.push_back(std::move(item));
            }

            m_ctx->cmdStack.Execute(std::make_unique<GeometryEditCommand>("倒角", std::move(items)), m_ctx->scene);
            LOG_DEBUG("[ChamferTool] 倒角完成，距离 %.4g, %.4g", *m_d1, *m_d2);

            m_firstId = Object::InvalidID;              // 回到「选择第一条直线」
            m_ctx->overlay.Clear();
        }

        double*              m_d1 = nullptr;            // 由 Editor 持有，多次倒角之间保留
        double*              m_d2 = nullptr;
        const EditorContext* m_ctx     = nullptr;       // 仅在 OnInput 内有效
        Overlay*             m_overlay = nullptr;
        Object::ObjectID     m_firstId = Object::InvalidID;
        Math::Point3         m_firstPick{};
    };
}
