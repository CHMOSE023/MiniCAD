#pragma once
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Document/Command/GeometryEditCommand.h"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Core/Math/Constants.hpp"
#include "Solid/ISolidKernel.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// 三维实体建模工具（开发计划 docs/Solid/PLAN.md，M3）：长方体、圆柱、球、拉伸。
// 内核由宿主经 Editor::SetSolidKernel 提供；没有内核（网页版）时这些命令不启动。
// 视图是俯视的二维视图：底面在光标点所在的水平面上，高度沿 +Z（负数沿 −Z），键入数值或用两点距离给出。
namespace MiniCAD
{
    // 工具在输入事件之外（命令行键入数值时）也要提交，那时 EditorContext 已失效：
    // 由 Editor 在创建工具时传入长期存在的对象
    struct SolidToolEnv
    {
        Scene&                                   scene;
        CommandStack&                            cmdStack;
        Picking&                                 picking;
        Overlay&                                 overlay;
        Solid::ISolidKernel&                     kernel;
        std::function<void(const std::string&)> echo;   // 输出到命令行
    };

    namespace SolidToolDetail
    {
        inline Math::Point3 CursorPoint(const EditorContext& ctx)
        {
            const auto& e = ctx.event;
            if (e.HasSnap) return e.SnapWorld;
            return ctx.viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        inline double Distance2D(const Math::Point3& a, const Math::Point3& b) { return std::hypot(b.x - a.x, b.y - a.y); }

        // 内核实体 → 新的 Solid3DEntity（当前图层与线型线宽，同 EditorContext::ApplyCurrentAttr），经撤销栈加入场景
        inline bool AddSolid(const SolidToolEnv& env, const Solid::BodyPtr& body, const char* what)
        {
            auto entity = body ? Solid3DEntity::Create(env.scene.NextObjectID(), env.kernel, body) : nullptr;
            if (!entity)
            {
                env.echo(std::string(what) + "：创建失败（" + env.kernel.LastError() + "）");
                return false;
            }
            auto& attr      = entity->GetAttr();
            attr.LayerId    = env.scene.GetLayerManager().GetActiveLayerID();
            attr.LineType   = env.scene.GetCurrentLineType();
            attr.Lineweight = env.scene.GetCurrentLineweight();
            env.cmdStack.Execute(std::make_unique<AddEntityCommand>(std::move(entity)), env.scene);
            return true;
        }

        // 闭合的平面对象 → 拉伸轮廓：圆、首尾相接的多段线（可带圆弧段）、矩形。
        // 只支持默认挤出方向、位于一个水平面上的对象
        inline std::optional<Solid::Profile> ProfileOf(const Entity& e)
        {
            if (!e.HasDefaultExtrusion())
                return std::nullopt;

            if (e.IsKindOf<CircleEntity>())
            {
                const auto& c = static_cast<const CircleEntity&>(e).GetCircle();
                return Solid::Profile::Circle(c.Center, c.Radius);
            }

            if (e.IsKindOf<RectangleEntity>())
            {
                const auto& r = static_cast<const RectangleEntity&>(e).GetRectangle();
                Solid::Profile p;
                for (const auto& v : { r.P1, r.P2, r.P3, r.P4 })
                {
                    if (std::abs(v.z - r.P1.z) > Math::LengthEPS)
                        return std::nullopt;
                    p.Vertices.push_back({ v, 0.0 });
                }
                return p;
            }

            if (e.IsKindOf<PolylineEntity>())
            {
                const auto& pl  = static_cast<const PolylineEntity&>(e).GetPolyline();
                const auto& pts = pl.Points;
                if (pts.size() < 3 || pts.front() != pts.back())
                    return std::nullopt;   // 不闭合
                Solid::Profile p;
                for (size_t i = 0; i + 1 < pts.size(); ++i)
                {
                    if (std::abs(pts[i].z - pts[0].z) > Math::LengthEPS)
                        return std::nullopt;
                    p.Vertices.push_back({ pts[i], pl.SegBulge(static_cast<int>(i)) });
                }
                return p;
            }
            return std::nullopt;
        }
    }

    // ─────────────────────────────────────────────────────────────
    // BoxTool（BOX）：两个角点定底面矩形 → 高度
    // ─────────────────────────────────────────────────────────────
    class BoxTool : public ITool
    {
    public:
        explicit BoxTool(SolidToolEnv env) : m_env(std::move(env)) {}

        bool OnInput(const EditorContext& ctx) override
        {
            const auto& e = ctx.event;
            if (e.IsRightClick() || e.IsCancel())
            {
                Reset();
                if (OnFinished) OnFinished();
                return true;
            }

            if (e.IsLeftClick())
            {
                const auto p = SolidToolDetail::CursorPoint(ctx);
                if (m_step == 0)
                {
                    m_c0   = p;
                    m_step = 1;
                }
                else if (m_step == 1)
                {
                    if (std::abs(p.x - m_c0.x) < Math::LengthEPS || std::abs(p.y - m_c0.y) < Math::LengthEPS)
                        return true;   // 底面退化，等待重新指定
                    m_c1   = p;
                    m_step = 2;
                }
                else
                    Commit(SolidToolDetail::Distance2D(m_c1, p));
                return true;
            }

            if (e.Type == InputEventType::MouseMove && m_step > 0)
            {
                const auto p     = SolidToolDetail::CursorPoint(ctx);
                const auto color = ctx.scene.GetLayerManager().GetActiveLayer().GetColor();
                ctx.overlay.Clear();
                if (m_step == 1)
                    ctx.overlay.AddRect(m_c0, p, color);
                else
                {
                    ctx.overlay.AddRect(m_c0, m_c1, color);
                    ctx.overlay.AddLine(m_c1, p, { 0.6, 0.6, 0.6, 0.4 });
                }
            }
            return false;
        }

        bool OnNumberInput(double value) override
        {
            if (m_step != 2) return false;
            Commit(value);
            return true;
        }

        bool         HasAnchor() const override { return m_step > 0; }
        Math::Point3 GetAnchor() const override { return m_step == 2 ? m_c1 : m_c0; }

        std::string GetPrompt() const override
        {
            switch (m_step)
            {
            case 0:  return "长方体 指定第一个角点:";
            case 1:  return "指定对角点:";
            default: return "指定高度（键入数值，负数向下；或点取两点距离）:";
            }
        }

        void OnSceneChanged() override { Reset(); }

    private:
        void Commit(double height)
        {
            if (std::abs(height) < Math::LengthEPS)
                return;   // 高度为 0，继续等待
            const Math::Point3 mn{ std::min(m_c0.x, m_c1.x), std::min(m_c0.y, m_c1.y), m_c0.z + std::min(0.0, height) };
            const auto body = m_env.kernel.Box(mn, std::abs(m_c1.x - m_c0.x), std::abs(m_c1.y - m_c0.y), std::abs(height));
            SolidToolDetail::AddSolid(m_env, body, "长方体");
            Reset();
        }

        void Reset()
        {
            m_step = 0;
            m_env.overlay.Clear();
        }

        SolidToolEnv m_env;
        int          m_step = 0;
        Math::Point3 m_c0{}, m_c1{};
    };

    // ─────────────────────────────────────────────────────────────
    // RoundSolidTool：圆柱体（CYLINDER：底面中心 → 半径 → 高度）/ 球体（SPHERE：中心 → 半径）
    // ─────────────────────────────────────────────────────────────
    class RoundSolidTool : public ITool
    {
    public:
        enum class Kind { Cylinder, Sphere };

        RoundSolidTool(Kind kind, SolidToolEnv env) : m_kind(kind), m_env(std::move(env)) {}

        bool OnInput(const EditorContext& ctx) override
        {
            const auto& e = ctx.event;
            if (e.IsRightClick() || e.IsCancel())
            {
                Reset();
                if (OnFinished) OnFinished();
                return true;
            }

            if (e.IsLeftClick())
            {
                const auto p = SolidToolDetail::CursorPoint(ctx);
                if (m_step == 0)
                {
                    m_center = p;
                    m_step   = 1;
                }
                else if (m_step == 1)
                    SetRadius(SolidToolDetail::Distance2D(m_center, p), p);
                else
                    CommitCylinder(SolidToolDetail::Distance2D(m_radiusPoint, p));
                return true;
            }

            if (e.Type == InputEventType::MouseMove && m_step > 0)
            {
                const auto p     = SolidToolDetail::CursorPoint(ctx);
                const auto color = ctx.scene.GetLayerManager().GetActiveLayer().GetColor();
                ctx.overlay.Clear();
                const double r = m_step == 1 ? SolidToolDetail::Distance2D(m_center, p) : m_radius;
                ctx.overlay.AddCircle(m_center, r, color);
                ctx.overlay.AddLine(m_step == 1 ? m_center : m_radiusPoint, p, { 0.6, 0.6, 0.6, 0.4 });
            }
            return false;
        }

        bool OnNumberInput(double value) override
        {
            if (m_step == 1)
            {
                SetRadius(value, { m_center.x + value, m_center.y, m_center.z });
                return true;
            }
            if (m_step == 2)
            {
                CommitCylinder(value);
                return true;
            }
            return false;
        }

        bool         HasAnchor() const override { return m_step > 0; }
        Math::Point3 GetAnchor() const override { return m_step == 2 ? m_radiusPoint : m_center; }

        std::string GetPrompt() const override
        {
            switch (m_step)
            {
            case 0:  return m_kind == Kind::Cylinder ? "圆柱体 指定底面中心点:" : "球体 指定中心点:";
            case 1:  return "指定半径:";
            default: return "指定高度（键入数值，负数向下；或点取两点距离）:";
            }
        }

        void OnSceneChanged() override { Reset(); }

    private:
        void SetRadius(double r, const Math::Point3& at)
        {
            if (!(r > Math::LengthEPS))
                return;
            m_radius      = r;
            m_radiusPoint = at;
            if (m_kind == Kind::Sphere)
            {
                SolidToolDetail::AddSolid(m_env, m_env.kernel.Sphere(m_center, r), "球体");
                Reset();
                return;
            }
            m_step = 2;
        }

        void CommitCylinder(double height)
        {
            if (std::abs(height) < Math::LengthEPS)
                return;
            const Math::Point3 base{ m_center.x, m_center.y, m_center.z + std::min(0.0, height) };
            SolidToolDetail::AddSolid(m_env, m_env.kernel.Cylinder(base, m_radius, std::abs(height)), "圆柱体");
            Reset();
        }

        void Reset()
        {
            m_step = 0;
            m_env.overlay.Clear();
        }

        Kind         m_kind;
        SolidToolEnv m_env;
        int          m_step = 0;
        Math::Point3 m_center{}, m_radiusPoint{};
        double       m_radius = 0.0;
    };

    // ─────────────────────────────────────────────────────────────
    // ExtrudeTool（EXTRUDE）：预选的闭合对象（圆、闭合多段线、矩形）→ 高度。
    // 与 AutoCAD 的默认设置相同，拉伸后删除原对象（一步撤销即全部还原）
    // ─────────────────────────────────────────────────────────────
    class ExtrudeTool : public ITool
    {
    public:
        ExtrudeTool(std::vector<Object::ObjectID> ids, SolidToolEnv env) : m_ids(std::move(ids)), m_env(std::move(env)) {}

        bool OnInput(const EditorContext& ctx) override
        {
            const auto& e = ctx.event;
            if (e.IsRightClick() || e.IsCancel())
            {
                if (OnFinished) OnFinished();
                return true;
            }
            if (e.IsLeftClick())
            {
                const auto p = SolidToolDetail::CursorPoint(ctx);
                if (!m_hasBase)
                {
                    m_base    = p;
                    m_hasBase = true;
                }
                else
                    Commit(SolidToolDetail::Distance2D(m_base, p));
                return true;
            }
            return false;
        }

        bool OnNumberInput(double value) override
        {
            Commit(value);
            return true;
        }

        bool         HasAnchor() const override { return m_hasBase; }
        Math::Point3 GetAnchor() const override { return m_base; }

        std::string GetPrompt() const override
        {
            return m_hasBase ? "指定第二点（两点距离为高度）:" : "拉伸 指定高度（键入数值，负数向下；或点取两点距离）:";
        }

    private:
        void Commit(double height)
        {
            if (std::abs(height) < Math::LengthEPS)
                return;
            auto& scene = m_env.scene;

            std::vector<GeometryEditCommand::Item> items;
            int skipped = 0, failed = 0;
            for (auto id : m_ids)
            {
                const Object* o = scene.GetEntity(id);
                if (!o || !o->IsKindOf<Entity>()) continue;
                const auto& src     = static_cast<const Entity&>(*o);
                const auto  profile = SolidToolDetail::ProfileOf(src);
                if (!profile)
                {
                    ++skipped;
                    continue;
                }

                auto solid = Solid3DEntity::Create(scene.NextObjectID(), m_env.kernel, m_env.kernel.Extrude(*profile, height));
                if (!solid)
                {
                    ++failed;
                    continue;
                }
                solid->SetAttr(src.GetAttr());   // 沿用原对象的图层、颜色等

                GeometryEditCommand::Item remove;
                remove.id     = id;
                remove.before = src.Clone(id);
                items.push_back(std::move(remove));
                GeometryEditCommand::Item add;
                add.id    = solid->GetID();
                add.after = std::move(solid);
                items.push_back(std::move(add));
            }

            const size_t created = items.size() / 2;
            if (created > 0)
            {
                m_env.picking.ClearSelection();
                m_env.picking.MarkDirty();
                m_env.cmdStack.Execute(std::make_unique<GeometryEditCommand>("拉伸", std::move(items)), scene);
            }
            std::string msg = "拉伸：已创建 " + std::to_string(created) + " 个实体";
            if (skipped > 0) msg += "，" + std::to_string(skipped) + " 个对象不是闭合的平面对象（圆、闭合多段线、矩形），已忽略";
            if (failed > 0)  msg += "，" + std::to_string(failed) + " 个失败（" + m_env.kernel.LastError() + "）";
            m_env.echo(msg);
            if (OnFinished) OnFinished();
        }

        std::vector<Object::ObjectID> m_ids;
        SolidToolEnv                  m_env;
        bool                          m_hasBase = false;
        Math::Point3                  m_base{};
    };
}
