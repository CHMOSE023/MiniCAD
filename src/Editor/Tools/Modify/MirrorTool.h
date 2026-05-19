#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/Viewport/Viewport.h"
#include "Editor/Overlay/Overlay.h"
#include "Editor/Input/InputEvent.h"
#include "Editor/Input/KeyCode.h"
#include "Document/CommandStack/CommandStack.h"
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

#include <vector>
#include <cstdio>

namespace MiniCAD
{
    // ─────────────────────────────────────────────────────────────
    //  MirrorTool  (仿 AutoCAD MIRROR)
    //    1. 左键 -> 镜像线第一点
    //    2. 左键 -> 镜像线第二点
    //    3. 提示"是否删除源对象 [Y/N]<N>",按 Y / N / Enter 决定
    //    右键/ESC 任意阶段取消
    // ─────────────────────────────────────────────────────────────
    class MirrorTool : public ITool
    {
    public:
        MirrorTool(std::vector<Object*> targets, Scene& scene, CommandStack& cmdStack, Viewport& viewport, Overlay& overlay)
            : m_targets(std::move(targets))
            , m_scene(scene)
            , m_cmdStack(cmdStack)
            , m_viewport(viewport)
            , m_overlay(overlay)
        {
            m_sourceIds.reserve(m_targets.size());
            for (auto* o : m_targets)
                if (o) m_sourceIds.push_back(o->GetID());

            printf("[MirrorTool] 左键镜像线第一点\n");
        }

        ~MirrorTool() { printf("[MirrorTool] 退出\n"); }

        bool OnInput(const InputEvent& e) override
        {
            // 右键 / ESC 一律取消
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
                    printf("[MirrorTool] 镜像线第一点 (%.3f, %.3f),左键第二点\n", m_p0.x, m_p0.y);
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
                        printf("[MirrorTool] 两点重合,请重新选第二点\n");
                        return true;
                    }
                    m_phase = Phase::AskDelete;
                    printf("[MirrorTool] 是否删除源对象? [Y/N] <N>\n");
                    return true;
                }
                // 鼠标移动时实时预览镜像线 + 镜像后的几何
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
            m_overlay.Clear();
            if (OnFinished) OnFinished();
        }

        void OnSceneChanged()  override { Cancel(); }
        void OnFocusLost()     override { m_overlay.Clear(); }
        void OnFocusRestored() override {}

        bool         HasAnchor() const override { return m_phase == Phase::P1; }
        Math::Point3 GetAnchor() const override { return m_p0; }

    private:
        enum class Phase { P0, P1, AskDelete };

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        // 镜像线 + 镜像后的目标几何
        void RebuildPreview(const MirrorAxis& axis)
        {
            m_overlay.Clear();
            static const Math::Color4 mirrorColor = { 0.6,0.6,0.6,0.6 };
            const auto& color = m_scene.GetLayerManager().GetActiveLayer().GetColor();

            // 镜像线本身:画一条加长的线作为视觉提示
            Math::Vec3 dir{ axis.P1.x - axis.P0.x, axis.P1.y - axis.P0.y, 0.0 };
            double len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len > 1e-9)
            {
                double scale = 1000.0 / len;       // 简单粗暴往两端各延伸
                Math::Point3 a{ axis.P0.x - dir.x * scale, axis.P0.y - dir.y * scale, axis.P0.z };
                Math::Point3 b{ axis.P1.x + dir.x * scale, axis.P1.y + dir.y * scale, axis.P1.z };
                m_overlay.AddLine(a, b, mirrorColor);
            }

            // 每个目标:镜像后的几何
            for (Object* obj : m_targets)
            {
                if (!obj || !obj->IsKindOf<Entity>()) continue;
                DrawMirroredEntity(static_cast<Entity*>(obj), axis, color);
            }
        }

        // 注:此函数把 Entity 克隆 + 原地镜像到一个临时对象,然后画.
        // 这样我们不必再写一套"几何 + 镜像"分发,直接复用 EntityMirror.
        void DrawMirroredEntity(Entity* src, const MirrorAxis& axis, const Math::Color4& color)
        {
            auto temp = src->Clone(0);          // ID 不重要,只是临时
            MirrorEntityInPlace(*temp, axis);
            DrawEntityToOverlay(temp.get(), color);
        }

        // 把任意 Entity 当前几何画到 overlay
        // (与 MoveTool::DrawEntityPreview 同样的分发,但不带 offset)
        void DrawEntityToOverlay(Entity* entity, const Math::Color4& color)
        {
            if (entity->IsKindOf<PointEntity>())
            {
                m_overlay.AddPoint(static_cast<PointEntity*>(entity)->GetPoint().Position, color);
                return;
            }
            if (entity->IsKindOf<LineEntity>())
            {
                const auto& l = static_cast<LineEntity*>(entity)->GetLine();
                m_overlay.AddLine(l.Start, l.End, color);
                return;
            }
            if (entity->IsKindOf<CircleEntity>())
            {
                const auto& c = static_cast<CircleEntity*>(entity)->GetCircle();
                m_overlay.AddCircle(c.Center, c.Radius, color);
                return;
            }
            if (entity->IsKindOf<RectangleEntity>())
            {
                const auto& r = static_cast<RectangleEntity*>(entity)->GetRectangle();
                m_overlay.AddRect(r.P1, r.P2, r.P3, r.P4, color);
                return;
            }
            if (entity->IsKindOf<ArcEntity>())
            {
                const auto& a = static_cast<ArcEntity*>(entity)->GetArc();
                m_overlay.AddArc(a.Center, a.Radius, a.StartAngle, a.EndAngle, color);
                return;
            }
            if (entity->IsKindOf<EllipseEntity>())
            {
                const auto& el = static_cast<EllipseEntity*>(entity)->GetEllipse();
                m_overlay.AddEllipse(el.Center, el.RadiusX, el.RadiusY, el.Rotation, color);
                return;
            }
            if (entity->IsKindOf<PolylineEntity>())
            {
                m_overlay.AddPolyline(static_cast<PolylineEntity*>(entity)->GetPolyline(), color);
                return;
            }
            if (entity->IsKindOf<SplineEntity>())
            {
                const auto& sp = static_cast<SplineEntity*>(entity)->GetSpline();
                if (!sp.IsValid()) return;
                auto pts = sp.Tessellate(32);
                for (size_t i = 0; i + 1 < pts.size(); ++i)
                    m_overlay.AddLine(pts[i], pts[i + 1], color);
                return;
            }
        }

        void Commit(bool deleteSource)
        {
            MirrorAxis axis{ m_p0, m_p1 };

            if (deleteSource)
            {
                auto cmd = std::make_unique<MirrorMoveCommand>(m_sourceIds, axis, m_scene);
                m_cmdStack.Execute(std::move(cmd), m_scene);
                printf("[MirrorTool] 镜像 %zu 个对象,源已删除\n", m_sourceIds.size());
            }
            else
            {
                auto cmd = std::make_unique<MirrorCopyCommand>(m_sourceIds, axis);
                m_cmdStack.Execute(std::move(cmd), m_scene);
                printf("[MirrorTool] 镜像 %zu 个对象,源保留\n", m_sourceIds.size());
            }

            m_overlay.Clear();
            if (OnFinished) OnFinished();
        }

    private:
        std::vector<Object*>          m_targets;
        std::vector<Object::ObjectID> m_sourceIds;

        Scene& m_scene;
        CommandStack& m_cmdStack;
        Viewport& m_viewport;
        Overlay& m_overlay;

        Phase        m_phase = Phase::P0;
        Math::Point3 m_p0{}, m_p1{};
    };
}
