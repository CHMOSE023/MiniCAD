#pragma once
#include "Document/DimAssoc.h"
#include <unordered_map>
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/BatchAddCommand.h"
#include "Document/Command/EntityTranslate.h"
#include "Core/GeomKernel/AABB.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Vec3.hpp"
#include "Core/Object/Object.hpp"
#include "Core/Entity/Entity.hpp"

#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"

#include <vector>
#include <memory>
#include <functional>
#include "Core/Log.h"

namespace MiniCAD
{
    // 粘贴工具:剪贴板实体跟随光标预览,左键指定插入点落场(类 AutoCAD Ctrl+V)。
    // 基点取剪贴板实体整体包围盒左下角;剪贴板由 DocumentManager 持有,工具只读。
    class PasteTool : public ITool
    {
    public:
        using Clipboard = std::vector<std::unique_ptr<Entity>>;

        PasteTool(const Clipboard* clipboard, std::function<void()> onCommitted,
                  const Math::Point3* basePoint = nullptr)
            : m_clipboard(clipboard)
            , m_onCommitted(std::move(onCommitted))
        {
            if (basePoint)
            {
                m_base = *basePoint;   // 带基点复制(COPYBASE):粘贴时光标即基点
            }
            else
            {
                AABB box = AABB::Empty();
                for (const auto& e : *m_clipboard)
                    box.Expand(e->GetBoundingBox());
                m_base = box.Min;      // AutoCAD 默认以复制集左下角为插入基点
            }

            LOG_INFO("[PasteTool] 左键指定插入点 | 右键/ESC 取消");
        }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                Commit(GetPoint(e));
                Cancel();           // 单次落场后结束工具
                return true;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                Cancel();
                return true;
            }

            if (e.Type == InputEventType::MouseMove)
            {
                RebuildPreview(GetPoint(e));
                return false;
            }

            return false;
        }

        void Cancel() override
        {
            // 不要经由 m_ctx 清 overlay:EditorContext 是 OnInput 的栈上临时对象,
            // 文档切换时 Editor::Unbind() 在输入分发之外调用 Cancel,m_ctx 已悬空。
            // overlay 由 Editor 统一清理(Unbind / ActivateTool / OnFinished 回调)。
            if (OnFinished) OnFinished();
        }

        void OnSceneChanged() override { Cancel(); }
        void OnFocusLost()     override { if (m_overlay) m_overlay->Clear(); }
        void OnFocusRestored() override {}

        std::string GetPrompt() const override
        {
            return "指定插入点 [右键/ESC 取消]:";
        }

    private:
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        Math::Vec3 DeltaTo(const Math::Point3& target) const
        {
            return {
                target.x - m_base.x,
                target.y - m_base.y,
                target.z - m_base.z
            };
        }

        void RebuildPreview(const Math::Point3& current)
        {
            m_ctx->overlay.Clear();

            const Math::Vec3 d     = DeltaTo(current);
            const auto&      color = m_ctx->scene.GetLayerManager().GetActiveLayer().GetColor();

            for (const auto& e : *m_clipboard)
                DrawEntityPreview(e.get(), d, color);
        }

        void DrawEntityPreview(const Entity* entity, const Math::Vec3& d, const Math::Color4& color)
        {
            if (entity->IsKindOf<PointEntity>())
            {
                const auto& pt = static_cast<const PointEntity*>(entity)->GetPoint();
                m_ctx->overlay.AddPoint(pt.Position + d, color);
                return;
            }
            if (entity->IsKindOf<LineEntity>())
            {
                const auto& ln = static_cast<const LineEntity*>(entity)->GetLine();
                m_ctx->overlay.AddLine(ln.Start + d, ln.End + d, color);
                return;
            }
            if (entity->IsKindOf<CircleEntity>())
            {
                const auto& c = static_cast<const CircleEntity*>(entity)->GetCircle();
                m_ctx->overlay.AddCircle(c.Center + d, c.Radius, color);
                return;
            }
            if (entity->IsKindOf<RectangleEntity>())
            {
                const auto& r = static_cast<const RectangleEntity*>(entity)->GetRectangle();
                m_ctx->overlay.AddRect(r.P1 + d, r.P2 + d, r.P3 + d, r.P4 + d, color);
                return;
            }
            if (entity->IsKindOf<ArcEntity>())
            {
                const auto& a = static_cast<const ArcEntity*>(entity)->GetArc();
                m_ctx->overlay.AddArc(a.Center + d, a.Radius, a.StartAngle, a.EndAngle, color);
                return;
            }
            if (entity->IsKindOf<EllipseEntity>())
            {
                const auto& el = static_cast<const EllipseEntity*>(entity)->GetEllipse();
                { Ellipse moved = el; moved.Center = el.Center + d; m_ctx->overlay.AddEllipse(moved, color); }
                return;
            }
            if (entity->IsKindOf<PolylineEntity>())
            {
                Polyline pl = static_cast<const PolylineEntity*>(entity)->GetPolyline();
                for (auto& p : pl.Points) p += d;
                m_ctx->overlay.AddPolyline(pl, color);
                return;
            }
            if (entity->IsKindOf<SplineEntity>())
            {
                const auto& sp = static_cast<const SplineEntity*>(entity)->GetSpline();
                if (!sp.IsValid()) return;
                auto pts = sp.Tessellate(32);
                for (size_t i = 0; i + 1 < pts.size(); ++i)
                    m_ctx->overlay.AddLine(pts[i] + d, pts[i + 1] + d, color);
                return;
            }

            // 其余类型(文字/标注/填充等)统一用包围盒示意
            auto box = entity->GetBoundingBox();
            m_ctx->overlay.AddRect(
                { box.Min.x + d.x, box.Min.y + d.y, box.Min.z },
                { box.Max.x + d.x, box.Max.y + d.y, box.Max.z },
                color);
        }

        void Commit(const Math::Point3& target)
        {
            Scene& scene = m_ctx->scene;
            const Math::Vec3 d = DeltaTo(target);

            std::vector<std::unique_ptr<Entity>> copies;
            std::unordered_map<Object::ObjectID, Object::ObjectID> srcToNew;   // 剪贴板里的原 ID → 新 ID
            copies.reserve(m_clipboard->size());
            for (const auto& src : *m_clipboard)
            {
                auto clone = src->Clone(scene.NextObjectID());
                TranslateEntityInPlace(*clone, d);
                srcToNew[src->GetID()] = clone->GetID();
                copies.push_back(std::move(clone));
            }

            // 一起粘贴的标注改为关联到副本，关联对象没有一起复制的解除关联
            std::vector<std::unique_ptr<Object>> clones;
            clones.reserve(copies.size());
            for (auto& c : copies)
            {
                DimAssoc::RemapCopy(*c, srcToNew);
                clones.push_back(std::move(c));
            }

            auto cmd = std::make_unique<BatchAddCommand>(std::move(clones));
            if (m_ctx->cmdStack.Execute(std::move(cmd), scene))
            {
                if (m_onCommitted) m_onCommitted();
                LOG_DEBUG("[PasteTool] 粘贴 %zu 个对象 at (%.3f, %.3f)",
                    m_clipboard->size(), target.x, target.y);
            }
        }

    private:
        const Clipboard*      m_clipboard;        // 非拥有,由 DocumentManager 持有
        std::function<void()> m_onCommitted;      // 落场成功回调(标脏等)

        const EditorContext* m_ctx = nullptr;

        Overlay*              m_overlay = nullptr;
        Math::Point3         m_base{};
    };
}
