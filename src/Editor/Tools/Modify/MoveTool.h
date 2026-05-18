#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/Viewport/Viewport.h"
#include "Editor/Overlay/Overlay.h"
#include "Document/CommandStack/CommandStack.h"
#include "Document/Command/MoveCommand.h"   // 需要实现 MoveCommand
#include "Core/Math/Point3.hpp"
#include "Core/Object/Object.hpp"
#include <vector>
#include <cstdio>

namespace MiniCAD
{
    // ─────────────────────────────────────────────────────────────
    //  MoveTool
    //
    //  使用方式（仿 AutoCAD）：
    //    1. EditorContext 工厂传入 targets（已选对象列表）
    //    2. 左键第一次点击   →  确定基点（base point）
    //    3. 移动鼠标        →  实时 overlay 预览偏移
    //    4. 左键第二次点击   →  提交 MoveCommand，工具结束
    //    5. 右键 / ESC      →  取消，什么都不提交
    // ─────────────────────────────────────────────────────────────
    class MoveTool : public ITool
    {
    public:
        MoveTool(std::vector<Object*>  targets, Scene& scene, CommandStack& cmdStack, Viewport& viewport, Overlay& overlay)
            : m_targets(std::move(targets))
            , m_scene(scene)
            , m_cmdStack(cmdStack)
            , m_viewport(viewport)
            , m_overlay(overlay)
        {
            printf("[MoveTool] 左键基点 | 左键目标点 | 右键/ESC 取消\n");
        }

        ~MoveTool()
        {
            printf("[MoveTool] 退出\n");
        }

        // ── ITool ────────────────────────────────────────────────
        bool OnInput(const InputEvent& e) override
        {
            // ── 左键 ────────────────────────────────────────────
            if (e.IsLeftClick())
            {
                auto pt = GetPoint(e);

                if (!m_hasBase)
                {
                    // 第一次点击：确定基点
                    m_base = pt;
                    m_hasBase = true;
                    printf("[MoveTool] 基点 (%.3f, %.3f)\n", pt.x, pt.y);
                    return true;
                }
                else
                {
                    // 第二次点击：提交移动
                    Commit(pt);
                    return true;
                }
            }

            // ── 右键 / ESC ───────────────────────────────────────
            if (e.IsRightClick() || e.IsCancel())
            {
                Cancel();
                return true;
            }

            // ── 鼠标移动：overlay 预览 ───────────────────────────
            if (e.Type == InputEventType::MouseMove && m_hasBase)
            {
                auto cur = GetPoint(e);
                RebuildPreview(cur);
                return false;   // 不消费，让渲染刷新
            }

            return false;
        }

        void Cancel() override
        {
            m_overlay.Clear();
            if (OnFinished) OnFinished();
        }

        // Undo/Redo/Delete 后 Scene 内容变了，targets 指针可能悬空
        void OnSceneChanged() override
        {
            // 简单策略：立即退出工具，避免使用悬空指针
            Cancel();
        }

        void OnFocusLost() override
        {
            // 中键平移期间隐藏 overlay（可选：保留也无妨）
            m_overlay.Clear();
        }

        void OnFocusRestored() override
        {
            // 平移结束后若已有基点，重建预览（鼠标位置未知，先清空即可）
            // 下一次 MouseMove 会自动重建
        }

        // 有基点时才提供锚点（供正交约束使用）
        bool         HasAnchor()  const override { return m_hasBase; }
        Math::Point3 GetAnchor()  const override { return m_base; }

    private:
        // ── 工具点 ───────────────────────────────────────────────
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }
         

        // ── overlay 预览 ─────────────────────────────────────────
        //  按当前偏移量,把每个目标几何体在 overlay 上重画一遍
        void RebuildPreview(const Math::Point3& current)
        {
            m_overlay.Clear();

            const Math::Vec3 d{
                current.x - m_base.x,
                current.y - m_base.y,
                current.z - m_base.z
            };

            const auto& color = m_scene.GetLayerManager().GetActiveLayer().GetColor();

            // 引导线:基点 -> 当前点
            m_overlay.AddLine(m_base, current, color);

            for (Object* obj : m_targets)
            {
                if (!obj || !obj->IsKindOf<Entity>()) continue;
                DrawEntityPreview(static_cast<Entity*>(obj), d, color);
            }
        }

        // 按 Entity 实际类型分发到 overlay 各 Add* 接口
        void DrawEntityPreview(Entity* entity, const Math::Vec3& d, const Math::Color4& color)
        {
            // ── Point ────────────────────────────────────────────────
            if (entity->IsKindOf<PointEntity>())
            {
                const auto& pt = static_cast<PointEntity*>(entity)->GetPoint();
                m_overlay.AddPoint(pt.Position + d, color);
                return;
            }

            // ── Line ─────────────────────────────────────────────────
            if (entity->IsKindOf<LineEntity>())
            {
                const auto& ln = static_cast<LineEntity*>(entity)->GetLine();
                m_overlay.AddLine(ln.Start + d, ln.End + d, color);
                return;
            }

            // ── Circle ───────────────────────────────────────────────
            if (entity->IsKindOf<CircleEntity>())
            {
                const auto& c = static_cast<CircleEntity*>(entity)->GetCircle();
                m_overlay.AddCircle(c.Center + d, c.Radius, color);
                return;
            }

            // ── Rectangle ────────────────────────────────────────────
            if (entity->IsKindOf<RectangleEntity>())
            {
                const auto& r = static_cast<RectangleEntity*>(entity)->GetRectangle();
                m_overlay.AddRect(r.P1 + d, r.P2 + d, r.P3 + d, r.P4 + d, color);
                return;
            }

            // ── Arc ──────────────────────────────────────────────────
            if (entity->IsKindOf<ArcEntity>())
            {
                const auto& a = static_cast<ArcEntity*>(entity)->GetArc();
                m_overlay.AddArc(a.Center + d, a.Radius, a.StartAngle, a.EndAngle, color);
                return;
            }

            // ── Ellipse ──────────────────────────────────────────────
            if (entity->IsKindOf<EllipseEntity>())
            {
                const auto& el = static_cast<EllipseEntity*>(entity)->GetEllipse();
                m_overlay.AddEllipse(el.Center + d, el.RadiusX, el.RadiusY, el.Rotation, color);
                return;
            }

            // ── Polyline ─────────────────────────────────────────────
            if (entity->IsKindOf<PolylineEntity>())
            {
                // 拷贝一份,整体平移所有顶点,再交给 overlay
                Polyline pl = static_cast<PolylineEntity*>(entity)->GetPolyline();
                for (auto& p : pl.Points) p += d;
                m_overlay.AddPolyline(pl, color);
                return;
            }

            // ── Spline ───────────────────────────────────────────────
            if (entity->IsKindOf<SplineEntity>())
            {
                // Overlay 没有 AddSpline,采用 Tessellate -> 逐段 AddLine
                const auto& sp = static_cast<SplineEntity*>(entity)->GetSpline();
                if (!sp.IsValid()) return;

                auto pts = sp.Tessellate(32);
                for (size_t i = 0; i + 1 < pts.size(); ++i)
                    m_overlay.AddLine(pts[i] + d, pts[i + 1] + d, color);
                return;
            }

            // ── 兜底:画 AABB ─────────────────────────────────────────
            auto box = entity->GetBoundingBox();
            Math::Point3 lo{ box.Min.x + d.x, box.Min.y + d.y, box.Min.z + d.z };
            Math::Point3 hi{ box.Max.x + d.x, box.Max.y + d.y, box.Max.z + d.z };
            m_overlay.AddRect(lo, hi, color);
        }

        // ── 提交 ─────────────────────────────────────────────────
        void Commit(const Math::Point3& target)
        {
            float dx = target.x - m_base.x;
            float dy = target.y - m_base.y;

            if (std::fabs(dx) < 1e-6f && std::fabs(dy) < 1e-6f)
            {
                printf("[MoveTool] 偏移量为零，忽略\n");
                Cancel();
                return;
            }

            // 收集 ObjectID
            std::vector<Object::ObjectID> ids;
            ids.reserve(m_targets.size());
            for (Object* obj : m_targets)
                if (obj) ids.push_back(obj->GetID());

            auto cmd = std::make_unique<MoveCommand>(ids, Math::Vec3{ dx, dy, 0.0 }, m_scene);

            m_cmdStack.Execute(std::move(cmd), m_scene);

            printf("[MoveTool] 移动 %zu 个对象  delta=(%.3f, %.3f)\n", ids.size(), dx, dy);

            m_overlay.Clear();
            if (OnFinished) OnFinished();
        }

    private:
        std::vector<Object*> m_targets;   // 不拥有，生命周期由 Scene 管理

        Scene&        m_scene;
        CommandStack& m_cmdStack;
        Viewport&     m_viewport;
        Overlay&      m_overlay;

        bool          m_hasBase = false;
        Math::Point3  m_base{};
    };
}
