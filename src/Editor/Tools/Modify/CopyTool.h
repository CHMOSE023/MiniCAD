#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/CopyCommand.h"
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
#include <cstdio>
#include <cmath>

namespace MiniCAD
{
    class CopyTool : public ITool
    {
    public:
        CopyTool(std::vector<Object*> targets)
            : m_targets(std::move(targets))
        {
            m_sourceIds.reserve(m_targets.size());
            for (auto* o : m_targets)
                if (o) m_sourceIds.push_back(o->GetID());

            printf("[CopyTool] 左键基点 | 左键目标点(可重复) | 右键/ESC 结束\n");
        }

        ~CopyTool()
        {
            printf("[CopyTool] 退出\n");
        }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                auto pt = GetPoint(e);

                if (!m_hasBase)
                {
                    m_base = pt;
                    m_hasBase = true;
                    printf("[CopyTool] 基点 (%.3f, %.3f)\n", pt.x, pt.y);
                    return true;
                }

                Commit(pt);
                return true;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                Cancel();
                return true;
            }
            if (e.Type == InputEventType::KeyDown && e.Key == KeyCode::Enter)
            {
                Cancel();
                return true;
            }

            if (e.Type == InputEventType::MouseMove && m_hasBase)
            {
                RebuildPreview(GetPoint(e));
                return false;
            }

            return false;
        }

        void Cancel() override
        {
            if (m_ctx) m_ctx->overlay.Clear();
            if (OnFinished) OnFinished();
        }

        void OnSceneChanged() override { Cancel(); }

        void OnFocusLost()      override { if (m_ctx) m_ctx->overlay.Clear(); }
        void OnFocusRestored()  override {}

        bool         HasAnchor() const override { return m_hasBase; }
        Math::Point3 GetAnchor() const override { return m_base; }

    private:
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        void RebuildPreview(const Math::Point3& current)
        {
            m_ctx->overlay.Clear();

            const Math::Vec3 d{
                current.x - m_base.x,
                current.y - m_base.y,
                current.z - m_base.z
            };

            const auto& color = m_ctx->scene.GetLayerManager().GetActiveLayer().GetColor();

            m_ctx->overlay.AddLine(m_base, current, color);

            for (Object* obj : m_targets)
            {
                if (!obj || !obj->IsKindOf<Entity>()) continue;
                DrawEntityPreview(static_cast<Entity*>(obj), d, color);
            }
        }

        void DrawEntityPreview(Entity* entity, const Math::Vec3& d, const Math::Color4& color)
        {
            if (entity->IsKindOf<PointEntity>())
            {
                const auto& pt = static_cast<PointEntity*>(entity)->GetPoint();
                m_ctx->overlay.AddPoint(pt.Position + d, color);
                return;
            }
            if (entity->IsKindOf<LineEntity>())
            {
                const auto& ln = static_cast<LineEntity*>(entity)->GetLine();
                m_ctx->overlay.AddLine(ln.Start + d, ln.End + d, color);
                return;
            }
            if (entity->IsKindOf<CircleEntity>())
            {
                const auto& c = static_cast<CircleEntity*>(entity)->GetCircle();
                m_ctx->overlay.AddCircle(c.Center + d, c.Radius, color);
                return;
            }
            if (entity->IsKindOf<RectangleEntity>())
            {
                const auto& r = static_cast<RectangleEntity*>(entity)->GetRectangle();
                m_ctx->overlay.AddRect(r.P1 + d, r.P2 + d, r.P3 + d, r.P4 + d, color);
                return;
            }
            if (entity->IsKindOf<ArcEntity>())
            {
                const auto& a = static_cast<ArcEntity*>(entity)->GetArc();
                m_ctx->overlay.AddArc(a.Center + d, a.Radius, a.StartAngle, a.EndAngle, color);
                return;
            }
            if (entity->IsKindOf<EllipseEntity>())
            {
                const auto& el = static_cast<EllipseEntity*>(entity)->GetEllipse();
                m_ctx->overlay.AddEllipse(el.Center + d, el.RadiusX, el.RadiusY, el.Rotation, color);
                return;
            }
            if (entity->IsKindOf<PolylineEntity>())
            {
                Polyline pl = static_cast<PolylineEntity*>(entity)->GetPolyline();
                for (auto& p : pl.Points) p += d;
                m_ctx->overlay.AddPolyline(pl, color);
                return;
            }
            if (entity->IsKindOf<SplineEntity>())
            {
                const auto& sp = static_cast<SplineEntity*>(entity)->GetSpline();
                if (!sp.IsValid()) return;
                auto pts = sp.Tessellate(32);
                for (size_t i = 0; i + 1 < pts.size(); ++i)
                    m_ctx->overlay.AddLine(pts[i] + d, pts[i + 1] + d, color);
                return;
            }
            if (entity->IsKindOf<TextEntity>() || entity->IsKindOf<MTextEntity>())
            {
                auto box = entity->GetBoundingBox();
                m_ctx->overlay.AddRect(
                    { box.Min.x + d.x, box.Min.y + d.y, box.Min.z },
                    { box.Max.x + d.x, box.Max.y + d.y, box.Max.z },
                    color);
                return;
            }
        }

        void Commit(const Math::Point3& target)
        {
            Math::Vec3 d{
                target.x - m_base.x,
                target.y - m_base.y,
                target.z - m_base.z
            };

            if (std::fabs(d.x) < 1e-6 && std::fabs(d.y) < 1e-6 && std::fabs(d.z) < 1e-6)
            {
                printf("[CopyTool] 偏移量为零,忽略\n");
                return;
            }

            auto cmd = std::make_unique<CopyCommand>(m_sourceIds, d);
            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);

            printf("[CopyTool] 复制 %zu 个对象  delta=(%.3f, %.3f)\n",
                m_sourceIds.size(), d.x, d.y);
        }

    private:
        std::vector<Object*>          m_targets;
        std::vector<Object::ObjectID> m_sourceIds;

        const EditorContext* m_ctx = nullptr;

        bool          m_hasBase = false;
        Math::Point3  m_base{};
    };
}
