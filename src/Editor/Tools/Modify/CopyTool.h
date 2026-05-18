#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/Viewport/Viewport.h"
#include "Editor/Overlay/Overlay.h"
#include "Document/CommandStack/CommandStack.h"
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

#include <vector>
#include <cstdio>
#include <cmath>

namespace MiniCAD
{
    // ─────────────────────────────────────────────────────────────
    //  CopyTool —— 仿 AutoCAD COPY:
    //    1. 由 EditorContext 工厂传入 targets(已选对象)
    //    2. 左键第一次点击 → 基点
    //    3. 之后每次左键点击 → 复制一份(支持连续)
    //    4. 右键 / Enter / ESC → 结束工具
    // ─────────────────────────────────────────────────────────────
    class CopyTool : public ITool
    {
    public:
        CopyTool(std::vector<Object*> targets, Scene& scene, CommandStack& cmdStack, Viewport& viewport, Overlay& overlay)
            : m_targets(std::move(targets))
            , m_scene(scene)
            , m_cmdStack(cmdStack)
            , m_viewport(viewport)
            , m_overlay(overlay)
        {
            // 把 targets 的 ID 抓出来一次,后续每次 Commit 都用同一份源 ID
            m_sourceIds.reserve(m_targets.size());
            for (auto* o : m_targets)
                if (o) m_sourceIds.push_back(o->GetID());

            printf("[CopyTool] 左键基点 | 左键目标点(可重复) | 右键/ESC 结束\n");
        }

        ~CopyTool()
        {
            printf("[CopyTool] 退出\n");
        }

        // ── ITool ────────────────────────────────────────────────
        bool OnInput(const InputEvent& e) override
        {
            // 左键
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

                Commit(pt);     // 连续模式:Commit 后不退出,继续等下一个目标点
                return true;
            }

            // 右键 / ESC / Enter → 结束
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

            // 鼠标移动:overlay 预览
            if (e.Type == InputEventType::MouseMove && m_hasBase)
            {
                RebuildPreview(GetPoint(e));
                return false;
            }

            return false;
        }

        void Cancel() override
        {
            m_overlay.Clear();
            if (OnFinished) OnFinished();
        }

        // 期间不允许 Undo 之类破坏 Scene:简单起见整体退出
        void OnSceneChanged() override { Cancel(); }

        void OnFocusLost()      override { m_overlay.Clear(); }
        void OnFocusRestored()  override {}

        bool         HasAnchor() const override { return m_hasBase; }
        Math::Point3 GetAnchor() const override { return m_base; }

    private:
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        // 预览:基点 -> 鼠标的引导线,以及目标位置的几何轮廓
        // 注:这里复用 MoveTool 的预览逻辑(几何 + 偏移),
        // 如果你愿意,可以把 DrawEntityPreview 抽到一个 PreviewRenderer.
        void RebuildPreview(const Math::Point3& current)
        {
            m_overlay.Clear();

            const Math::Vec3 d{
                current.x - m_base.x,
                current.y - m_base.y,
                current.z - m_base.z
            };

            const auto& color = m_scene.GetLayerManager().GetActiveLayer().GetColor();

            m_overlay.AddLine(m_base, current, color);

            for (Object* obj : m_targets)
            {
                if (!obj || !obj->IsKindOf<Entity>()) continue;
                DrawEntityPreview(static_cast<Entity*>(obj), d, color);
            }
        }

        // ── 与 MoveTool 完全一致的预览分发 ───────────────────────
        void DrawEntityPreview(Entity* entity, const Math::Vec3& d, const Math::Color4& color)
        {
            if (entity->IsKindOf<PointEntity>())
            {
                const auto& pt = static_cast<PointEntity*>(entity)->GetPoint();
                m_overlay.AddPoint(pt.Position + d, color);
                return;
            }
            if (entity->IsKindOf<LineEntity>())
            {
                const auto& ln = static_cast<LineEntity*>(entity)->GetLine();
                m_overlay.AddLine(ln.Start + d, ln.End + d, color);
                return;
            }
            if (entity->IsKindOf<CircleEntity>())
            {
                const auto& c = static_cast<CircleEntity*>(entity)->GetCircle();
                m_overlay.AddCircle(c.Center + d, c.Radius, color);
                return;
            }
            if (entity->IsKindOf<RectangleEntity>())
            {
                const auto& r = static_cast<RectangleEntity*>(entity)->GetRectangle();
                m_overlay.AddRect(r.P1 + d, r.P2 + d, r.P3 + d, r.P4 + d, color);
                return;
            }
            if (entity->IsKindOf<ArcEntity>())
            {
                const auto& a = static_cast<ArcEntity*>(entity)->GetArc();
                m_overlay.AddArc(a.Center + d, a.Radius, a.StartAngle, a.EndAngle, color);
                return;
            }
            if (entity->IsKindOf<EllipseEntity>())
            {
                const auto& el = static_cast<EllipseEntity*>(entity)->GetEllipse();
                m_overlay.AddEllipse(el.Center + d, el.RadiusX, el.RadiusY, el.Rotation, color);
                return;
            }
            if (entity->IsKindOf<PolylineEntity>())
            {
                Polyline pl = static_cast<PolylineEntity*>(entity)->GetPolyline();
                for (auto& p : pl.Points) p += d;
                m_overlay.AddPolyline(pl, color);
                return;
            }
            if (entity->IsKindOf<SplineEntity>())
            {
                const auto& sp = static_cast<SplineEntity*>(entity)->GetSpline();
                if (!sp.IsValid()) return;
                auto pts = sp.Tessellate(32);
                for (size_t i = 0; i + 1 < pts.size(); ++i)
                    m_overlay.AddLine(pts[i] + d, pts[i + 1] + d, color);
                return;
            }
        }

        // 提交一份复制
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
                return;     // 不退出,继续等下一个点
            }

            auto cmd = std::make_unique<CopyCommand>(m_sourceIds, d);
            m_cmdStack.Execute(std::move(cmd), m_scene);

            printf("[CopyTool] 复制 %zu 个对象  delta=(%.3f, %.3f)\n",
                m_sourceIds.size(), d.x, d.y);

            // 连续模式:不重置 m_base,下次还是从同一个基点算偏移
            // 也可以改为"基点跟随上一次目标点",由你设计选择
            // m_base = target;
        }

    private:
        std::vector<Object*>          m_targets;
        std::vector<Object::ObjectID> m_sourceIds;

        Scene&        m_scene;
        CommandStack& m_cmdStack;
        Viewport&     m_viewport;
        Overlay&      m_overlay;

        bool          m_hasBase = false;
        Math::Point3  m_base{};
    };
}
