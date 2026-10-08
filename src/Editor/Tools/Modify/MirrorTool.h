#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Editor/Input/KeyCode.h"
#include "Document/Command/MirrorCopyCommand.h"
#include "Document/Command/MirrorMoveCommand.h"
#include "Document/Command/EntityMirror.h"
#include "Core/Math/Point3.hpp"
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
#include "Core/Log.h"

namespace MiniCAD
{
    class MirrorTool : public ITool
    {
    public:
        MirrorTool(std::vector<Object*> targets)
            : m_targets(std::move(targets))
        {
            m_sourceIds.reserve(m_targets.size());
            for (auto* o : m_targets)
                if (o) m_sourceIds.push_back(o->GetID());

            LOG_INFO("[MirrorTool] 左键镜像线第一点");
        }

        ~MirrorTool() { LOG_DEBUG("[MirrorTool] 退出"); }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsRightClick() || e.IsCancel())
            {
                Cancel();
                return true;
            }

            switch (m_phase)
            {
            case Phase::P0:
            {
                if (e.IsLeftClick())
                {
                    m_p0 = GetPoint(e);
                    m_phase = Phase::P1;
                    LOG_INFO("[MirrorTool] 镜像线第一点 (%.3f, %.3f),左键第二点", m_p0.x, m_p0.y);
                    return true;
                }
                break;
            }
            case Phase::P1:
            {
                if (e.IsLeftClick())
                {
                    m_p1 = GetPoint(e);
                    if (std::abs(m_p1.x - m_p0.x) < 1e-9 &&
                        std::abs(m_p1.y - m_p0.y) < 1e-9)
                    {
                        LOG_WARN("[MirrorTool] 两点重合,请重新选第二点");
                        return true;
                    }
                    m_phase = Phase::AskDelete;
                    LOG_INFO("[MirrorTool] 是否删除源对象? [Y/N] <N>");
                    return true;
                }
                if (e.Type == InputEventType::MouseMove)
                {
                    auto cur = GetPoint(e);
                    if (std::abs(cur.x - m_p0.x) > 1e-9 ||
                        std::abs(cur.y - m_p0.y) > 1e-9)
                    {
                        RebuildPreview(MirrorAxis{ m_p0, cur });
                    }
                    return false;
                }
                break;
            }
            case Phase::AskDelete:
            {
                if (e.Type == InputEventType::KeyDown)
                {
                    if (e.Key == KeyCode::Y)
                    {
                        Commit(/*deleteSource=*/true);
                        return true;
                    }
                    if (e.Key == KeyCode::N || e.Key == KeyCode::Enter || e.Key == KeyCode::Space)
                    {
                        Commit(/*deleteSource=*/false);
                        return true;
                    }
                }
                break;
            }
            }
            return false;
        }

        void Cancel() override
        {
            if (m_overlay) m_overlay->Clear();
            if (OnFinished) OnFinished();
        }

        void OnSceneChanged()  override { Cancel(); }
        void OnFocusLost()     override { if (m_overlay) m_overlay->Clear(); }
        void OnFocusRestored() override {}

        bool         HasAnchor() const override { return m_phase == Phase::P1; }
        Math::Point3 GetAnchor() const override { return m_p0; }

        std::string GetPrompt() const override
        {
            switch (m_phase)
            {
                case Phase::P1:        return "指定镜像线第二点:";
                case Phase::AskDelete: return "是否删除源对象? [是(Y)/否(N)]:";
                default:               return "指定镜像线第一点:";
            }
        }

    private:
        enum class Phase { P0, P1, AskDelete };

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        void RebuildPreview(const MirrorAxis& axis)
        {
            m_ctx->overlay.Clear();
            static const Math::Color4 mirrorColor = { 0.6,0.6,0.6,0.6 };
            const auto& color = m_ctx->scene.GetLayerManager().GetActiveLayer().GetColor();

            Math::Vec3 dir{ axis.P1.x - axis.P0.x, axis.P1.y - axis.P0.y, 0.0 };
            double len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len > 1e-9)
            {
                double scale = 1000.0 / len;
                Math::Point3 a{ axis.P0.x - dir.x * scale, axis.P0.y - dir.y * scale, axis.P0.z };
                Math::Point3 b{ axis.P1.x + dir.x * scale, axis.P1.y + dir.y * scale, axis.P1.z };
                m_ctx->overlay.AddLine(a, b, mirrorColor);
            }

            for (Object* obj : m_targets)
            {
                if (!obj || !obj->IsKindOf<Entity>()) continue;
                DrawMirroredEntity(static_cast<Entity*>(obj), axis, color);
            }
        }

        void DrawMirroredEntity(Entity* src, const MirrorAxis& axis, const Math::Color4& color)
        {
            auto temp = src->Clone(0);
            MirrorEntityInPlace(*temp, axis);
            DrawEntityToOverlay(temp.get(), color);
        }

        void DrawEntityToOverlay(Entity* entity, const Math::Color4& color)
        {
            if (entity->IsKindOf<PointEntity>())
            {
                m_ctx->overlay.AddPoint(static_cast<PointEntity*>(entity)->GetPoint().Position, color);
                return;
            }
            if (entity->IsKindOf<LineEntity>())
            {
                const auto& l = static_cast<LineEntity*>(entity)->GetLine();
                m_ctx->overlay.AddLine(l.Start, l.End, color);
                return;
            }
            if (entity->IsKindOf<CircleEntity>())
            {
                const auto& c = static_cast<CircleEntity*>(entity)->GetCircle();
                m_ctx->overlay.AddCircle(c.Center, c.Radius, color);
                return;
            }
            if (entity->IsKindOf<RectangleEntity>())
            {
                const auto& r = static_cast<RectangleEntity*>(entity)->GetRectangle();
                m_ctx->overlay.AddRect(r.P1, r.P2, r.P3, r.P4, color);
                return;
            }
            if (entity->IsKindOf<ArcEntity>())
            {
                const auto& a = static_cast<ArcEntity*>(entity)->GetArc();
                m_ctx->overlay.AddArc(a.Center, a.Radius, a.StartAngle, a.EndAngle, color);
                return;
            }
            if (entity->IsKindOf<EllipseEntity>())
            {
                const auto& el = static_cast<EllipseEntity*>(entity)->GetEllipse();
                m_ctx->overlay.AddEllipse(el, color);
                return;
            }
            if (entity->IsKindOf<PolylineEntity>())
            {
                m_ctx->overlay.AddPolyline(static_cast<PolylineEntity*>(entity)->GetPolyline(), color);
                return;
            }
            if (entity->IsKindOf<SplineEntity>())
            {
                const auto& sp = static_cast<SplineEntity*>(entity)->GetSpline();
                if (!sp.IsValid()) return;
                auto pts = sp.Tessellate(32);
                for (size_t i = 0; i + 1 < pts.size(); ++i)
                    m_ctx->overlay.AddLine(pts[i], pts[i + 1], color);
                return;
            }
            if (entity->IsKindOf<TextEntity>() || entity->IsKindOf<MTextEntity>())
            {
                auto box = entity->GetBoundingBox();
                m_ctx->overlay.AddRect(box.Min, box.Max, color);
                return;
            }
        }

        void Commit(bool deleteSource)
        {
            MirrorAxis axis{ m_p0, m_p1 };

            if (deleteSource)
            {
                auto cmd = std::make_unique<MirrorMoveCommand>(m_sourceIds, axis, m_ctx->scene);
                m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);
                LOG_DEBUG("[MirrorTool] 镜像 %zu 个对象,源已删除", m_sourceIds.size());
            }
            else
            {
                auto cmd = std::make_unique<MirrorCopyCommand>(m_sourceIds, axis);
                m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);
                LOG_DEBUG("[MirrorTool] 镜像 %zu 个对象,源保留", m_sourceIds.size());
            }

            m_ctx->overlay.Clear();
            if (OnFinished) OnFinished();
        }

    private:
        std::vector<Object*>          m_targets;
        std::vector<Object::ObjectID> m_sourceIds;

        const EditorContext* m_ctx = nullptr;

        Overlay*              m_overlay = nullptr;

        Phase        m_phase = Phase::P0;
        Math::Point3 m_p0{}, m_p1{};
    };
}
