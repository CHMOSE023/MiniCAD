#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Editor/Input/KeyCode.h"
#include "Document/Command/RotateMoveCommand.h"
#include "Document/Command/RotateCopyCommand.h"
#include "Document/Command/EntityRotate.h"
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
#include <cmath>
#include <cstdio>

namespace MiniCAD
{
    class RotateTool : public ITool
    {
    public:
        RotateTool(std::vector<Object*> targets)
            : m_targets(std::move(targets))
        {
            m_sourceIds.reserve(m_targets.size());
            for (auto* o : m_targets)
                if (o) m_sourceIds.push_back(o->GetID());

            printf("[RotateTool] 左键旋转基点\n");
        }

        ~RotateTool() { printf("[RotateTool] 退出\n"); }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            const auto& e = ctx.event;

            if (e.IsRightClick() || e.IsCancel())
            {
                Cancel();
                return true;
            }

            switch (m_phase)
            {
            case Phase::Base:
            {
                if (e.IsLeftClick())
                {
                    m_pivot = GetPoint(e);
                    m_phase = Phase::AskCopy;
                    printf("[RotateTool] 基点 (%.3f, %.3f),是否保留源对象? [Y/N] <N>\n",
                           m_pivot.x, m_pivot.y);
                    return true;
                }
                break;
            }
            case Phase::AskCopy:
            {
                if (e.Type == InputEventType::KeyDown)
                {
                    if (e.Key == KeyCode::Y)
                    {
                        m_keepSource = true;
                        EnterAngleInput();
                        return true;
                    }
                    if (e.Key == KeyCode::N || e.Key == KeyCode::Enter || e.Key == KeyCode::Space)
                    {
                        m_keepSource = false;
                        EnterAngleInput();
                        return true;
                    }
                }
                break;
            }
            case Phase::Angle:
            {
                if (e.IsLeftClick())
                {
                    auto cur = GetPoint(e);
                    double angle = ComputeAngle(cur);
                    Commit(angle);
                    return true;
                }
                if (e.Type == InputEventType::MouseMove)
                {
                    auto cur = GetPoint(e);

                    if (!m_hasRef0)
                    {
                        m_ref0    = cur;
                        m_hasRef0 = true;
                    }

                    double angle = ComputeAngle(cur);
                    RebuildPreview(cur, angle);
                    return false;
                }
                break;
            }
            }
            return false;
        }

        void Cancel() override
        {
            if (m_ctx) m_ctx->overlay.Clear();
            if (OnFinished) OnFinished();
        }

        void OnSceneChanged()  override { Cancel(); }
        void OnFocusLost()     override { if (m_ctx) m_ctx->overlay.Clear(); }
        void OnFocusRestored() override {}

        bool         HasAnchor() const override { return m_phase == Phase::Angle; }
        Math::Point3 GetAnchor() const override { return m_pivot; }

    private:
        enum class Phase { Base, AskCopy, Angle };

        void EnterAngleInput()
        {
            m_phase   = Phase::Angle;
            m_hasRef0 = false;
            printf("[RotateTool] 移动鼠标设定旋转角度,左键确定%s\n",
                   m_keepSource ? "(保留源)" : "");
        }

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        double ComputeAngle(const Math::Point3& cur) const
        {
            if (!m_hasRef0) return 0.0;
            double a1 = std::atan2(cur.y     - m_pivot.y, cur.x     - m_pivot.x);
            double a0 = std::atan2(m_ref0.y  - m_pivot.y, m_ref0.x  - m_pivot.x);
            return a1 - a0;
        }

        void RebuildPreview(const Math::Point3& cur, double angle)
        {
            m_ctx->overlay.Clear();

            static const Math::Color4 rotateColor = { 0.6,0.6,0.6,0.6 };
            const auto& color = m_ctx->scene.GetLayerManager().GetActiveLayer().GetColor();

            m_ctx->overlay.AddLine(m_pivot, cur, rotateColor);

            if (m_hasRef0)
            {
                m_ctx->overlay.AddLine(m_pivot, m_ref0, color);
            }

            for (Object* obj : m_targets)
            {
                if (!obj || !obj->IsKindOf<Entity>()) continue;
                auto temp = static_cast<Entity*>(obj)->Clone(0);
                RotateEntityInPlace(*temp, m_pivot, angle);
                DrawEntityToOverlay(temp.get(), color);
            }
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
                m_ctx->overlay.AddEllipse(el.Center, el.RadiusX, el.RadiusY, el.Rotation, color);
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

        void Commit(double angle)
        {
            if (std::fabs(angle) < 1e-9)
            {
                printf("[RotateTool] 旋转角为零,忽略\n");
                Cancel();
                return;
            }

            if (m_keepSource)
            {
                auto cmd = std::make_unique<RotateCopyCommand>(m_sourceIds, m_pivot, angle);
                m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);
                printf("[RotateTool] 旋转 %zu 个对象,源保留,angle=%.3f deg\n",
                       m_sourceIds.size(), angle * 180.0 / Math::PI);
            }
            else
            {
                auto cmd = std::make_unique<RotateMoveCommand>(m_sourceIds, m_pivot, angle, m_ctx->scene);
                m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);
                printf("[RotateTool] 旋转 %zu 个对象,源原地修改,angle=%.3f deg\n",
                       m_sourceIds.size(), angle * 180.0 / Math::PI);
            }

            m_ctx->overlay.Clear();
            if (OnFinished) OnFinished();
        }

    private:
        std::vector<Object*>          m_targets;
        std::vector<Object::ObjectID> m_sourceIds;

        const EditorContext* m_ctx = nullptr;

        Phase        m_phase = Phase::Base;
        Math::Point3 m_pivot{};
        bool         m_keepSource = false;
        bool         m_hasRef0    = false;
        Math::Point3 m_ref0{};
    };
}
