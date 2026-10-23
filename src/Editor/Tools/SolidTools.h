#pragma once
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Document/Command/BatchAddCommand.h"
#include "Document/Command/GeometryEditCommand.h"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Core/Math/Constants.hpp"
#include "Solid/CurveFit.h"
#include "Solid/ISolidKernel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

// 三维实体建模工具（开发计划 docs/Solid/PLAN.md）：长方体、圆柱、球、拉伸（M3）；截面、平面投影（M4）；
// 旋转、扫掠、圆角边、倒角边（M5）。
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
            attr.InPaperSpace = env.scene.CurrentSpaceIsPaper();      // 当前空间（同 ApplyCurrentAttr）
            attr.LayoutKey    = env.scene.CurrentLayoutKey();
            env.cmdStack.Execute(std::make_unique<AddEntityCommand>(std::move(entity)), env.scene);
            return true;
        }

        // 凸度：MiniCAD 的多段线正值为顺时针（见 Polyline::ComputeArc），内核接口与 DXF 相同、正值为逆时针
        inline double KernelBulge(const Polyline& pl, size_t i) { return -pl.SegBulge(static_cast<int>(i)); }

        // 闭合的平面对象 → 拉伸 / 旋转 / 扫掠的轮廓：圆、首尾相接的多段线（可带圆弧段）、矩形。
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
                    p.Vertices.push_back({ pts[i], KernelBulge(pl, i) });
                }
                return p;
            }
            return std::nullopt;
        }

        inline double ProfileZ(const Solid::Profile& p) { return p.IsCircle ? p.Center.z : p.Vertices.front().Point.z; }

        // 直线实体（定义在 Editor.cpp：LineEntity.hpp 在全局作用域 using namespace MiniCAD::Math，不能经 Editor.h 带进界面代码）
        std::unique_ptr<Entity> MakeLineEntity(Object::ObjectID id, const Math::Point3& a, const Math::Point3& b);
        std::optional<std::pair<Math::Point3, Math::Point3>> LineEndpoints(const Entity& e);   // 不是直线时为空

        // 扫掠路径：直线、圆弧、圆、多段线（闭合或不闭合，可带圆弧段）、矩形；位于一个水平面上
        inline std::optional<Solid::SweepPath> PathOf(const Entity& e)
        {
            if (!e.HasDefaultExtrusion())
                return std::nullopt;
            Solid::SweepPath path;
            if (const auto line = LineEndpoints(e))
            {
                const auto& [a, b] = *line;
                if (std::abs(a.z - b.z) > Math::LengthEPS || Distance2D(a, b) < Math::LengthEPS)
                    return std::nullopt;
                path.Vertices = { { a, 0.0 }, { b, 0.0 } };
                return path;
            }
            if (e.IsKindOf<ArcEntity>())
            {
                const auto& a = static_cast<const ArcEntity&>(e).GetArc();
                double sweep = std::fmod(a.EndAngle - a.StartAngle, 2 * Math::PI);
                if (sweep <= 1e-12)
                    sweep += 2 * Math::PI;
                const Math::Point3 p0{ a.Center.x + a.Radius * std::cos(a.StartAngle), a.Center.y + a.Radius * std::sin(a.StartAngle), a.Center.z };
                const Math::Point3 p1{ a.Center.x + a.Radius * std::cos(a.EndAngle),   a.Center.y + a.Radius * std::sin(a.EndAngle),   a.Center.z };
                path.Vertices = { { p0, std::tan(sweep / 4) }, { p1, 0.0 } };   // 逆时针
                return path;
            }
            if (e.IsKindOf<CircleEntity>())
            {
                const auto& c = static_cast<const CircleEntity&>(e).GetCircle();
                path.Vertices = { { { c.Center.x + c.Radius, c.Center.y, c.Center.z }, 1.0 }, { { c.Center.x - c.Radius, c.Center.y, c.Center.z }, 1.0 } };
                path.Closed   = true;
                return path;
            }
            if (e.IsKindOf<RectangleEntity>() || e.IsKindOf<PolylineEntity>())
            {
                if (auto profile = ProfileOf(e))   // 闭合
                {
                    path.Vertices = std::move(profile->Vertices);
                    path.Closed   = true;
                    return path;
                }
                if (!e.IsKindOf<PolylineEntity>())
                    return std::nullopt;
                const auto& pl  = static_cast<const PolylineEntity&>(e).GetPolyline();
                const auto& pts = pl.Points;
                if (pts.size() < 2)
                    return std::nullopt;
                for (size_t i = 0; i < pts.size(); ++i)
                {
                    if (std::abs(pts[i].z - pts[0].z) > Math::LengthEPS)
                        return std::nullopt;
                    path.Vertices.push_back({ pts[i], i + 1 < pts.size() ? KernelBulge(pl, i) : 0.0 });
                }
                return path;
            }
            return std::nullopt;
        }

        // 预选的轮廓逐个生成实体，替换原对象（同 AutoCAD 的默认设置），一步撤销全部还原。
        // make 返回内核实体；轮廓不是闭合平面对象的计入 skipped
        template<class Make>
        void ReplaceProfiles(const SolidToolEnv& env, const std::vector<Object::ObjectID>& ids, const char* what, Make make)
        {
            auto& scene = env.scene;
            std::vector<GeometryEditCommand::Item> items;
            int skipped = 0, failed = 0;
            std::string error;
            for (auto id : ids)
            {
                const Object* o = scene.GetEntity(id);
                if (!o || !o->IsKindOf<Entity>()) continue;
                const auto& src     = static_cast<const Entity&>(*o);
                const auto  profile = ProfileOf(src);
                if (!profile)
                {
                    ++skipped;
                    continue;
                }

                auto solid = Solid3DEntity::Create(scene.NextObjectID(), env.kernel, make(*profile));
                if (!solid)
                {
                    ++failed;
                    error = env.kernel.LastError();
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
                env.picking.ClearSelection();
                env.picking.MarkDirty();
                env.cmdStack.Execute(std::make_unique<GeometryEditCommand>(what, std::move(items)), scene);
            }
            std::string msg = std::string(what) + "：已创建 " + std::to_string(created) + " 个实体";
            if (skipped > 0) msg += "，" + std::to_string(skipped) + " 个对象不是闭合的平面对象（圆、闭合多段线、矩形），已忽略";
            if (failed > 0)  msg += "，" + std::to_string(failed) + " 个失败（" + error + "）";
            env.echo(msg);
        }

        // 选择集里的三维实体（按 ID 升序），并统计其他对象个数
        inline std::vector<const Solid3DEntity*> SelectedSolids(const Scene& scene, const std::vector<Object::ObjectID>& ids, int* others = nullptr)
        {
            std::vector<const Solid3DEntity*> solids;
            for (auto id : ids)
            {
                const Object* o = scene.GetEntity(id);
                if (o && o->IsKindOf<Solid3DEntity>())
                    solids.push_back(static_cast<const Solid3DEntity*>(o));
                else if (others)
                    ++*others;
            }
            std::sort(solids.begin(), solids.end(), [](auto* a, auto* b) { return a->GetID() < b->GetID(); });
            return solids;
        }

        // 二维出图结果的收集：内核折线（已换算到图纸平面）→ 拟合 → 直线 / 圆弧 / 圆 / 多段线，最后一步加入场景
        class DrawingOutput
        {
        public:
            explicit DrawingOutput(const SolidToolEnv& env) : m_env(env) {}

            void Add(const std::vector<Math::Point3>& line, double tol, LineTypeID lineType, double lineTypeScale)
            {
                if (line.size() < 2)
                    return;
                const Solid::FittedCurve c = Solid::FitCurve(line, tol);
                const Object::ObjectID id = m_env.scene.NextObjectID();
                std::unique_ptr<Entity> e;
                switch (c.Type)
                {
                case Solid::FittedCurve::Kind::Line:
                    e = MakeLineEntity(id, c.Poly.Points[0], c.Poly.Points[1]);
                    break;
                case Solid::FittedCurve::Kind::Arc:
                    e = std::make_unique<ArcEntity>(id, c.Center, c.Radius, c.StartAngle, c.EndAngle);
                    break;
                case Solid::FittedCurve::Kind::Circle:
                    e = std::make_unique<CircleEntity>(id, c.Center, c.Radius);
                    break;
                default:
                    if (c.Poly.Points.size() < 2)
                        return;
                    e = std::make_unique<PolylineEntity>(id, c.Poly);
                    break;
                }
                auto& attr         = e->GetAttr();
                attr.LayerId       = m_env.scene.GetLayerManager().GetActiveLayerID();
                attr.LineType      = lineType;
                attr.Lineweight    = m_env.scene.GetCurrentLineweight();
                attr.LinetypeScale = lineTypeScale;
                attr.InPaperSpace  = m_env.scene.CurrentSpaceIsPaper();   // 当前空间（同 ApplyCurrentAttr）
                attr.LayoutKey     = m_env.scene.CurrentLayoutKey();
                m_entities.push_back(std::move(e));
            }

            size_t Count() const { return m_entities.size(); }

            // 全部加入场景，一步撤销
            void Commit()
            {
                if (m_entities.empty())
                    return;
                std::vector<std::unique_ptr<Object>> objects;
                for (auto& e : m_entities)
                    objects.push_back(std::move(e));
                m_entities.clear();
                m_env.cmdStack.Execute(std::make_unique<BatchAddCommand>(std::move(objects)), m_env.scene);
            }

        private:
            const SolidToolEnv&                  m_env;
            std::vector<std::unique_ptr<Entity>> m_entities;
        };

        // line 的每个点和每段中点都落在 others 的某条线段上（误差 tol 以内）：投影后与已有的线重合
        inline bool CoveredBy(const std::vector<Math::Point3>& line, const std::vector<std::vector<Math::Point3>>& others, double tol)
        {
            auto onOthers = [&](const Math::Point3& p) {
                for (const auto& o : others)
                    for (size_t i = 1; i < o.size(); ++i)
                        if (Solid::CurveFitDetail::DistToSegment(p, o[i - 1], o[i]) <= tol)
                            return true;
                return false;
            };
            for (size_t i = 0; i < line.size(); ++i)
            {
                if (!onOthers(line[i]))
                    return false;
                if (i > 0 && !onOthers({ (line[i - 1].x + line[i].x) / 2, (line[i - 1].y + line[i].y) / 2, 0.0 }))
                    return false;
            }
            return true;
        }

        // 拟合容差：图形尺寸的 1e-5
        inline double FitTolerance(const AABB& box)
        {
            const double size = std::max({ box.Max.x - box.Min.x, box.Max.y - box.Min.y, box.Max.z - box.Min.z });
            return std::max(size * 1e-5, 1e-9);
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
            SolidToolDetail::ReplaceProfiles(m_env, m_ids, "拉伸", [&](const Solid::Profile& p) { return m_env.kernel.Extrude(p, height); });
            if (OnFinished) OnFinished();
        }

        std::vector<Object::ObjectID> m_ids;
        SolidToolEnv                  m_env;
        bool                          m_hasBase = false;
        Math::Point3                  m_base{};
    };

    // ─────────────────────────────────────────────────────────────
    // SectionTool（SOLSECTION）：预选的三维实体与平面的截交线，生成二维曲线（M4）
    //   - 键入数值 z：水平截面（平面 z = 常数），曲线留在原来的 XY 位置，可直接再拉伸
    //   - 点取两点：过这两点的铅垂截面，展开到图纸平面后放在第三点：第一点 → 放置点，沿两点方向为 +X，原来的 Z 为 +Y
    //     （即站在两点连线的右侧看向左侧）
    // ─────────────────────────────────────────────────────────────
    class SectionTool : public ITool
    {
    public:
        SectionTool(std::vector<Object::ObjectID> ids, SolidToolEnv env) : m_ids(std::move(ids)), m_env(std::move(env)) {}

        bool OnInput(const EditorContext& ctx) override
        {
            const auto& e = ctx.event;
            if (e.IsRightClick() || e.IsCancel())
            {
                Finish();
                return true;
            }
            if (e.IsLeftClick())
            {
                const auto p = SolidToolDetail::CursorPoint(ctx);
                if (m_step == 0)
                {
                    m_p1   = p;
                    m_step = 1;
                }
                else if (m_step == 1)
                {
                    if (SolidToolDetail::Distance2D(m_p1, p) < Math::LengthEPS)
                        return true;
                    m_p2   = p;
                    m_step = 2;
                }
                else
                    CommitVertical(p);
                return true;
            }
            if (e.Type == InputEventType::MouseMove && m_step == 1)
            {
                ctx.overlay.Clear();
                ctx.overlay.AddLine(m_p1, SolidToolDetail::CursorPoint(ctx), ctx.scene.GetLayerManager().GetActiveLayer().GetColor());
            }
            return false;
        }

        bool OnNumberInput(double value) override
        {
            if (m_step != 0)
                return false;
            CommitHorizontal(value);
            return true;
        }

        bool         HasAnchor() const override { return m_step > 0; }
        Math::Point3 GetAnchor() const override { return m_step == 2 ? m_p2 : m_p1; }

        std::string GetPrompt() const override
        {
            switch (m_step)
            {
            case 0:  return "截面 键入水平截面的高度 Z，或指定铅垂截切线的第一点:";
            case 1:  return "指定截切线的第二点:";
            default: return "指定剖面图的放置点（对应第一点、Z = 0）:";
            }
        }

        void OnSceneChanged() override { m_env.overlay.Clear(); }

    private:
        // 平面 z = 常数：曲线原位输出（z 取 0）
        void CommitHorizontal(double z)
        {
            Run({ 0, 0, z }, { 0, 0, 1 }, [](const Math::Point3& q) { return Math::Point3{ q.x, q.y, 0.0 }; });
        }

        void CommitVertical(const Math::Point3& place)
        {
            const double len = SolidToolDetail::Distance2D(m_p1, m_p2);
            const double dx = (m_p2.x - m_p1.x) / len, dy = (m_p2.y - m_p1.y) / len;
            const Math::Point3 p1 = m_p1;
            Run({ p1.x, p1.y, 0 }, { -dy, dx, 0 }, [=](const Math::Point3& q) {
                const double u = (q.x - p1.x) * dx + (q.y - p1.y) * dy;
                return Math::Point3{ place.x + u, place.y + q.z, 0.0 };
            });
        }

        template<class Map>
        void Run(const Math::Point3& planePoint, const Math::Vec3& normal, Map map)
        {
            int others = 0;
            const auto solids = SolidToolDetail::SelectedSolids(m_env.scene, m_ids, &others);
            SolidToolDetail::DrawingOutput out(m_env);
            int failed = 0;
            for (const auto* solid : solids)
            {
                const auto body = solid->GetWorldBody(m_env.kernel);
                if (!body)
                {
                    ++failed;
                    continue;
                }
                const double tol = SolidToolDetail::FitTolerance(solid->GetBoundingBox());
                std::vector<std::vector<Math::Point3>> lines;
                for (const auto& line : m_env.kernel.Section(body, planePoint, normal).Lines)
                {
                    std::vector<Math::Point3> mapped;
                    for (const auto& q : line)
                        mapped.push_back(map(q));
                    lines.push_back(std::move(mapped));
                }
                for (const auto& line : Solid::JoinPolylines(std::move(lines), tol * 10))   // 按面分段的截交线连成闭合轮廓
                    out.Add(line, tol, m_env.scene.GetCurrentLineType(), 1.0);
            }
            const size_t count = out.Count();
            out.Commit();
            std::string msg = count > 0 ? "截面：生成 " + std::to_string(count) + " 条曲线" : std::string("截面：截平面与所选实体不相交");
            if (others > 0) msg += "，" + std::to_string(others) + " 个对象不是三维实体，已忽略";
            if (failed > 0) msg += "，" + std::to_string(failed) + " 个实体失败（" + m_env.kernel.LastError() + "）";
            m_env.echo(msg);
            Finish();
        }

        void Finish()
        {
            m_env.overlay.Clear();
            if (OnFinished) OnFinished();
        }

        std::vector<Object::ObjectID> m_ids;
        SolidToolEnv                  m_env;
        int                           m_step = 0;
        Math::Point3                  m_p1{}, m_p2{};
    };

    // ─────────────────────────────────────────────────────────────
    // FlatshotTool（FLATSHOT）：预选的三维实体沿标准视图方向投影成二维图（M4）。
    // 可见线用当前线型，被挡住的线用 HIDDEN 线型（H 切换是否生成）；多个实体先合并，互相遮挡也能消隐（合并失败时逐个投影）。
    // 投影图的左下角放在点取的位置，同一组实体的各视图按同一个点对齐即可拼成三视图
    // ─────────────────────────────────────────────────────────────
    class FlatshotTool : public ITool
    {
    public:
        enum class View { Top, Bottom, Front, Back, Right, Left };

        // 视线方向与图纸平面上的 X、Y 轴（X × Y = −视线方向）
        struct ViewBasis { Math::Vec3 Dir, U, V; };
        static ViewBasis BasisOf(View v)
        {
            switch (v)
            {
            case View::Top:    return { { 0, 0, -1 }, { 1, 0, 0 },  { 0, 1, 0 } };
            case View::Bottom: return { { 0, 0, 1 },  { 1, 0, 0 },  { 0, -1, 0 } };
            case View::Front:  return { { 0, 1, 0 },  { 1, 0, 0 },  { 0, 0, 1 } };
            case View::Back:   return { { 0, -1, 0 }, { -1, 0, 0 }, { 0, 0, 1 } };
            case View::Right:  return { { -1, 0, 0 }, { 0, 1, 0 },  { 0, 0, 1 } };
            default:           return { { 1, 0, 0 },  { 0, -1, 0 }, { 0, 0, 1 } };
            }
        }
        static const char* NameOf(View v)
        {
            static const char* names[] = { "俯视", "仰视", "前视", "后视", "右视", "左视" };
            return names[static_cast<int>(v)];
        }

        FlatshotTool(std::vector<Object::ObjectID> ids, SolidToolEnv env) : m_ids(std::move(ids)), m_env(std::move(env)) {}

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
                Commit(SolidToolDetail::CursorPoint(ctx));
                return true;
            }
            if (e.Type == InputEventType::KeyDown)
            {
                static const std::pair<KeyCode, View> keys[] = {
                    { KeyCode::T, View::Top }, { KeyCode::U, View::Bottom }, { KeyCode::F, View::Front },
                    { KeyCode::B, View::Back }, { KeyCode::R, View::Right }, { KeyCode::L, View::Left } };
                for (const auto& [key, view] : keys)
                    if (e.Key == key)
                    {
                        m_view = view;
                        m_env.echo(std::string("投影：") + NameOf(view));
                        return true;
                    }
                if (e.Key == KeyCode::H)
                {
                    m_hidden = !m_hidden;
                    m_env.echo(m_hidden ? "投影：生成隐藏线" : "投影：不生成隐藏线");
                    return true;
                }
            }
            return false;
        }

        std::string GetPrompt() const override
        {
            return std::string("投影 [俯视(T)/仰视(U)/前视(F)/后视(B)/右视(R)/左视(L)/隐藏线(H)] 当前：") + NameOf(m_view)
                 + (m_hidden ? "，含隐藏线" : "，无隐藏线") + "；指定放置点（投影图左下角）:";
        }

    private:
        void Commit(const Math::Point3& place)
        {
            int others = 0;
            const auto solids = SolidToolDetail::SelectedSolids(m_env.scene, m_ids, &others);
            auto& kernel = m_env.kernel;

            // 合并成一个实体，互相遮挡的部分才能消隐
            std::vector<Solid::BodyPtr> bodies;
            for (const auto* s : solids)
                if (auto b = s->GetWorldBody(kernel))
                    bodies.push_back(std::move(b));
            if (bodies.size() > 1)
            {
                Solid::BodyPtr merged = bodies[0];
                for (size_t i = 1; i < bodies.size() && merged; ++i)
                    merged = kernel.Boolean(Solid::BooleanOp::Union, merged, bodies[i]);
                if (merged)
                    bodies = { merged };
            }

            const ViewBasis basis = BasisOf(m_view);
            auto project = [&](const Math::Point3& q) {
                return Math::Point3{ q.x * basis.U.x + q.y * basis.U.y + q.z * basis.U.z,
                                     q.x * basis.V.x + q.y * basis.V.y + q.z * basis.V.z, 0.0 };
            };

            std::vector<std::vector<Math::Point3>> visible, hidden;
            AABB box = AABB::Empty();
            for (const auto& body : bodies)
            {
                Solid::HiddenLineResult r = kernel.HiddenLines(body, basis.Dir);
                for (int pass = 0; pass < 2; ++pass)
                    for (const auto& line : (pass == 0 ? r.Visible : r.Hidden).Lines)
                    {
                        std::vector<Math::Point3> flat;
                        for (const auto& q : line)
                        {
                            flat.push_back(project(q));
                            box.Expand(flat.back());
                        }
                        (pass == 0 ? visible : hidden).push_back(std::move(flat));
                    }
            }

            SolidToolDetail::DrawingOutput out(m_env);
            if (!visible.empty())
            {
                const double tol = SolidToolDetail::FitTolerance(box);
                auto moveToPlace = [&](std::vector<Math::Point3>& line) {
                    for (auto& q : line)
                        q = { place.x + q.x - box.Min.x, place.y + q.y - box.Min.y, 0.0 };
                };
                // 与可见线重合（如正对视线的面后面的边）或彼此重复的隐藏线不画
                std::vector<std::vector<Math::Point3>> kept;
                for (auto& line : hidden)
                    if (!SolidToolDetail::CoveredBy(line, visible, tol) && !SolidToolDetail::CoveredBy(line, kept, tol))
                        kept.push_back(std::move(line));
                hidden = std::move(kept);
                for (auto& line : visible)
                {
                    moveToPlace(line);
                    out.Add(line, tol, m_env.scene.GetCurrentLineType(), 1.0);
                }
                if (m_hidden && !hidden.empty())
                {
                    // 虚线一段约为图形尺寸的 1/50
                    const LineTypeID hiddenType = m_env.scene.GetLineTypeTable().Add({ "HIDDEN", "Hidden __ __ __ __", { 0.25, -0.125 }, 0.375 });
                    const double size  = std::max(box.Max.x - box.Min.x, box.Max.y - box.Min.y);
                    const double scale = std::max(size / 50.0 / 0.25, 1e-3);
                    for (auto& line : hidden)
                    {
                        moveToPlace(line);
                        out.Add(line, tol, hiddenType, scale);
                    }
                }
            }

            const size_t count = out.Count();
            out.Commit();
            std::string msg = count > 0 ? std::string("投影（") + NameOf(m_view) + "）：生成 " + std::to_string(count) + " 条曲线"
                                        : "投影：没有生成曲线（" + (solids.empty() ? std::string("没有选中三维实体") : kernel.LastError()) + "）";
            if (others > 0) msg += "，" + std::to_string(others) + " 个对象不是三维实体，已忽略";
            m_env.echo(msg);
            if (OnFinished) OnFinished();
        }

        std::vector<Object::ObjectID> m_ids;
        SolidToolEnv                  m_env;
        View                          m_view   = View::Front;
        bool                          m_hidden = true;
    };

    // ─────────────────────────────────────────────────────────────
    // RevolveTool（REVOLVE）：预选的闭合对象绕轴旋转（M5）。
    // 轴由两点给出（或 X / Y 键取世界坐标的 X / Y 轴），位于轮廓所在的水平面内；角度按右手定则绕第一点 → 第二点，
    // 回车 / 右键为整圈。与拉伸相同，原对象被替换
    // ─────────────────────────────────────────────────────────────
    class RevolveTool : public ITool
    {
    public:
        RevolveTool(std::vector<Object::ObjectID> ids, SolidToolEnv env) : m_ids(std::move(ids)), m_env(std::move(env)) {}

        bool OnInput(const EditorContext& ctx) override
        {
            const auto& e = ctx.event;
            if (e.IsCancel() || (e.IsRightClick() && m_step < 2))
            {
                Finish();
                return true;
            }
            if (m_step == 2 && (e.IsRightClick() || e.IsKeyPressed(KeyCode::Enter) || e.IsKeyPressed(KeyCode::Space)))
            {
                Commit(360.0);
                return true;
            }
            if (m_step == 0 && (e.IsKeyPressed(KeyCode::X) || e.IsKeyPressed(KeyCode::Y)))
            {
                m_p1   = { 0, 0, 0 };
                m_p2   = e.Key == KeyCode::X ? Math::Point3{ 1, 0, 0 } : Math::Point3{ 0, 1, 0 };
                m_step = 2;
                m_env.echo(e.Key == KeyCode::X ? "旋转轴：X 轴" : "旋转轴：Y 轴");
                return true;
            }
            if (e.IsLeftClick() && m_step < 2)
            {
                const auto p = SolidToolDetail::CursorPoint(ctx);
                if (m_step == 0)
                {
                    m_p1   = p;
                    m_step = 1;
                }
                else if (SolidToolDetail::Distance2D(m_p1, p) > Math::LengthEPS)
                {
                    m_p2   = p;
                    m_step = 2;
                    ctx.overlay.Clear();
                    ctx.overlay.AddLine(m_p1, m_p2, kAxisColor);
                }
                return true;
            }
            if (e.Type == InputEventType::MouseMove && m_step == 1)
            {
                ctx.overlay.Clear();
                ctx.overlay.AddLine(m_p1, SolidToolDetail::CursorPoint(ctx), kAxisColor);
            }
            return false;
        }

        bool OnNumberInput(double value) override
        {
            if (m_step != 2)
                return false;
            Commit(value);
            return true;
        }

        bool         HasAnchor() const override { return m_step == 1; }
        Math::Point3 GetAnchor() const override { return m_p1; }

        std::string GetPrompt() const override
        {
            switch (m_step)
            {
            case 0:  return "旋转 指定轴的起点 或 [X 轴(X)/Y 轴(Y)]:";
            case 1:  return "指定轴的端点:";
            default: return "指定旋转角度（键入度数，按右手定则绕轴的起点 → 端点）<360>:";
            }
        }

        void OnSceneChanged() override { m_env.overlay.Clear(); }

    private:
        static constexpr Math::Color4 kAxisColor = { 0.6, 0.6, 0.6, 0.8 };

        void Commit(double angleDeg)
        {
            if (std::abs(angleDeg) < 1e-9 || std::abs(angleDeg) > 360.0 + 1e-9)
            {
                m_env.echo("旋转：角度应在 0 到 360 度之间（不含 0）");
                return;
            }
            const Math::Vec3 dir{ m_p2.x - m_p1.x, m_p2.y - m_p1.y, 0.0 };
            SolidToolDetail::ReplaceProfiles(m_env, m_ids, "旋转", [&](const Solid::Profile& p) {
                return m_env.kernel.Revolve(p, { m_p1.x, m_p1.y, SolidToolDetail::ProfileZ(p) }, dir, angleDeg);
            });
            Finish();
        }

        void Finish()
        {
            m_env.overlay.Clear();
            if (OnFinished) OnFinished();
        }

        std::vector<Object::ObjectID> m_ids;
        SolidToolEnv                  m_env;
        int                           m_step = 0;
        Math::Point3                  m_p1{}, m_p2{};
    };

    // ─────────────────────────────────────────────────────────────
    // SweepTool（SWEEP）：预选的闭合对象沿点选的路径扫掠（M5）。
    // 路径：直线、圆弧、圆、多段线（可不闭合、带圆弧段）、矩形，与轮廓同在水平面内即可；
    // 轮廓的中心移到路径起点、转到与路径垂直后扫掠。原对象（轮廓）被替换，路径保留，可再用于其他轮廓
    // ─────────────────────────────────────────────────────────────
    class SweepTool : public ITool
    {
    public:
        SweepTool(std::vector<Object::ObjectID> ids, SolidToolEnv env) : m_ids(std::move(ids)), m_env(std::move(env)) {}

        bool OnInput(const EditorContext& ctx) override
        {
            const auto& e = ctx.event;
            if (e.IsCancel() || e.IsRightClick())
            {
                if (OnFinished) OnFinished();
                return true;
            }
            if (!e.IsLeftClick())
                return false;

            const Object::ObjectID id = m_env.picking.HitTest({ static_cast<double>(e.MouseX), static_cast<double>(e.MouseY) }, 8.0);
            const Object* o = id != Object::InvalidID ? m_env.scene.GetEntity(id) : nullptr;
            if (!o || !o->IsKindOf<Entity>())
                return true;
            if (std::find(m_ids.begin(), m_ids.end(), id) != m_ids.end())
            {
                m_env.echo("扫掠：路径不能是要扫掠的轮廓");
                return true;
            }
            const auto path = SolidToolDetail::PathOf(static_cast<const Entity&>(*o));
            if (!path)
            {
                m_env.echo("扫掠：路径应为直线、圆弧、圆、多段线或矩形（位于水平面内）");
                return true;
            }
            SolidToolDetail::ReplaceProfiles(m_env, m_ids, "扫掠", [&](const Solid::Profile& p) { return m_env.kernel.Sweep(p, *path); });
            if (OnFinished) OnFinished();
            return true;
        }

        std::string GetPrompt() const override { return "扫掠 选择扫掠路径（直线、圆弧、圆、多段线）:"; }

    private:
        std::vector<Object::ObjectID> m_ids;
        SolidToolEnv                  m_env;
    };

    // ─────────────────────────────────────────────────────────────
    // SolidEdgeTool：三维实体的边倒圆角（FILLETEDGE）/ 倒角（CHAMFEREDGE）（M5）。
    // 点选同一个实体上的边（再点一次取消），键入数值改半径 / 距离，回车 / 右键完成。
    // 俯视图里重叠的边（如长方体的顶边与底边）按离光标的距离、投影成一点的竖直边、高度（高的在前）排序，Tab 依次切换
    // ─────────────────────────────────────────────────────────────
    class SolidEdgeTool : public ITool
    {
    public:
        enum class Kind { Fillet, Chamfer };

        // value：半径 / 距离，由 Editor 持有，命令之间保留
        SolidEdgeTool(Kind kind, double* value, SolidToolEnv env) : m_kind(kind), m_value(value), m_env(std::move(env)) {}

        bool OnInput(const EditorContext& ctx) override
        {
            const auto& e = ctx.event;
            if (e.IsCancel())
            {
                Finish();
                return true;
            }
            if (e.IsRightClick() || e.IsKeyPressed(KeyCode::Enter) || e.IsKeyPressed(KeyCode::Space))
            {
                if (m_selected.empty())
                    Finish();
                else
                    Commit();
                return true;
            }
            if (e.IsKeyPressed(KeyCode::Tab))
            {
                if (m_candidates.size() > 1)
                {
                    m_cycle = (m_cycle + 1) % m_candidates.size();
                    Redraw();
                }
                return true;
            }
            if (e.Type == InputEventType::MouseMove)
            {
                UpdateCandidates(ctx, false);
                Redraw();
                return false;
            }
            if (e.IsLeftClick())
            {
                UpdateCandidates(ctx, true);
                if (!m_candidates.empty())
                {
                    const int edge = m_candidates[m_cycle];
                    if (auto it = std::find(m_selected.begin(), m_selected.end(), edge); it != m_selected.end())
                        m_selected.erase(it);
                    else
                        m_selected.push_back(edge);
                    m_solidId = m_selected.empty() ? Object::InvalidID : m_edgeSolid;   // 第一条边定下实体
                }
                Redraw();
                return true;
            }
            return false;
        }

        bool OnNumberInput(double value) override
        {
            if (value > Math::LengthEPS)
                *m_value = value;
            return true;
        }

        std::string GetPrompt() const override
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g", *m_value);
            const bool fillet = m_kind == Kind::Fillet;
            std::string p = std::string(fillet ? "圆角边" : "倒角边") + " 选择边（Tab 切换重叠的边；键入数值改" + (fillet ? "半径" : "距离")
                          + " <" + buf + ">）";
            if (!m_selected.empty())
                p += "，已选 " + std::to_string(m_selected.size()) + " 条，回车完成";
            return p + ":";
        }

        void OnSceneChanged() override
        {
            m_solidId = m_edgeSolid = Object::InvalidID;
            m_body.reset();
            m_edges = {};
            m_selected.clear();
            m_candidates.clear();
            m_env.overlay.Clear();
        }

    private:
        static constexpr Math::Color4 kSelectedColor = { 1.0, 0.55, 0.0, 1.0 };
        static constexpr Math::Color4 kHoverColor    = { 0.2, 0.8, 1.0, 1.0 };
        static constexpr double       kPickPixels    = 8.0;

        // 光标附近的边：已选过边时只看那个实体，否则看光标下的实体
        void UpdateCandidates(const EditorContext& ctx, bool clicked)
        {
            const auto& e = ctx.event;
            const Math::Point2 mouse{ static_cast<double>(e.MouseX), static_cast<double>(e.MouseY) };
            Object::ObjectID id = m_solidId;
            if (id == Object::InvalidID)
            {
                id = m_env.picking.HitTest(mouse, kPickPixels);
                const Object* o = id != Object::InvalidID ? m_env.scene.GetEntity(id) : nullptr;
                if (!o || !o->IsKindOf<Solid3DEntity>() || m_env.scene.IsEntityLocked(*o))
                    id = Object::InvalidID;
            }

            if (id != m_edgeSolid)
            {
                m_edgeSolid = id;
                m_body.reset();
                m_edges = {};
                if (const Object* o = id != Object::InvalidID ? m_env.scene.GetEntity(id) : nullptr)
                {
                    m_body = static_cast<const Solid3DEntity*>(o)->GetWorldBody(m_env.kernel);
                    if (m_body)
                        m_edges = m_env.kernel.Edges(m_body);   // 编号对世界坐标下的这个实体有效
                }
            }

            std::vector<int> found = FindEdges(ctx, mouse);
            if (clicked && found.empty() && m_solidId != Object::InvalidID)
            {
                const Object::ObjectID other = m_env.picking.HitTest(mouse, kPickPixels);
                const Object* o = other != Object::InvalidID ? m_env.scene.GetEntity(other) : nullptr;
                if (other != m_solidId && o && o->IsKindOf<Solid3DEntity>())
                    m_env.echo("只能选择同一个实体上的边；先回车完成当前实体");
            }
            if (found != m_candidates)
            {
                m_candidates = std::move(found);
                m_cycle      = 0;
            }
        }

        // m_edges 中离光标不超过 kPickPixels 的边，排序：距离（按像素取整）→ 投影成一点的竖直边优先 → 最近点高的优先
        std::vector<int> FindEdges(const EditorContext& ctx, const Math::Point2& mouse) const
        {
            const auto& camera = ctx.viewport.GetCamera();
            struct Hit { long Dist; int Vertical; double NegZ; int Index; };
            std::vector<Hit> hits;
            for (size_t i = 0; i < m_edges.Lines.size(); ++i)
            {
                const auto& line = m_edges.Lines[i];
                if (line.empty() || m_edges.Ids[i] < 0)
                    continue;
                const Math::Point2 first = camera.WorldToScreen(line[0]);
                double best = Math::Distance(mouse, first), z = line[0].z, extent = 0.0;
                Math::Point2 prev = first;
                for (size_t k = 1; k < line.size(); ++k)
                {
                    const Math::Point2 cur = camera.WorldToScreen(line[k]);
                    extent = std::max(extent, Math::Distance(first, cur));
                    const double dx = cur.x - prev.x, dy = cur.y - prev.y, len2 = dx * dx + dy * dy;
                    const double t  = len2 > 0 ? std::clamp(((mouse.x - prev.x) * dx + (mouse.y - prev.y) * dy) / len2, 0.0, 1.0) : 0.0;
                    const double d  = std::hypot(prev.x + dx * t - mouse.x, prev.y + dy * t - mouse.y);
                    if (d < best)
                    {
                        best = d;
                        z    = line[k - 1].z + (line[k].z - line[k - 1].z) * t;
                    }
                    prev = cur;
                }
                if (best <= kPickPixels)
                {
                    const bool vertical = extent < 1.0;
                    hits.push_back({ std::lround(best), vertical ? 0 : 1, vertical ? -std::max(line.front().z, line.back().z) : -z, static_cast<int>(i) });
                }
            }
            std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
                return std::tie(a.Dist, a.Vertical, a.NegZ) < std::tie(b.Dist, b.Vertical, b.NegZ);
            });
            std::vector<int> out;
            for (const auto& h : hits)
                out.push_back(h.Index);
            return out;
        }

        void Redraw()
        {
            m_env.overlay.Clear();
            auto draw = [&](int index, const Math::Color4& color) {
                const auto& line = m_edges.Lines[index];
                for (size_t k = 1; k < line.size(); ++k)
                    m_env.overlay.AddLine(line[k - 1], line[k], color);
                if (SolidToolDetail::Distance2D(line.front(), line.back()) < Math::LengthEPS && line.size() == 2)
                    m_env.overlay.AddPoint(line[0], color);   // 竖直边投影成一点
            };
            for (int index : m_selected)
                draw(index, kSelectedColor);
            if (!m_candidates.empty())
                draw(m_candidates[m_cycle], kHoverColor);
        }

        void Commit()
        {
            const bool  fillet = m_kind == Kind::Fillet;
            const char* what   = fillet ? "圆角" : "倒角";
            const Object* o = m_env.scene.GetEntity(m_solidId);
            if (!o || !m_body || m_edgeSolid != m_solidId)
            {
                Finish();
                return;
            }
            std::vector<int> ids;
            for (int index : m_selected)
                ids.push_back(m_edges.Ids[index]);
            const double value = *m_value;
            const Solid::BodyPtr result = fillet ? m_env.kernel.Fillet(m_body, ids, value) : m_env.kernel.Chamfer(m_body, ids, value);
            auto entity = result ? Solid3DEntity::Create(m_env.scene.NextObjectID(), m_env.kernel, result) : nullptr;
            if (!entity)
            {
                m_env.echo(std::string(what) + "失败，未做修改（" + m_env.kernel.LastError() + "）；可换较小的" + (fillet ? "半径" : "距离") + "再试");
                Finish();
                return;
            }
            const auto& src = static_cast<const Solid3DEntity&>(*o);
            entity->SetAttr(src.GetAttr());

            std::vector<GeometryEditCommand::Item> items(2);
            items[0].id     = m_solidId;
            items[0].before = src.Clone(m_solidId);
            items[1].id     = entity->GetID();
            items[1].after  = std::move(entity);
            m_env.picking.ClearSelection();
            m_env.picking.MarkDirty();
            m_env.cmdStack.Execute(std::make_unique<GeometryEditCommand>(what, std::move(items)), m_env.scene);

            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s：%zu 条边，%s = %g", what, ids.size(), fillet ? "半径" : "距离", value);
            m_env.echo(buf);
            Finish();
        }

        void Finish()
        {
            m_env.overlay.Clear();
            if (OnFinished) OnFinished();
        }

        Kind                 m_kind;
        double*              m_value;
        SolidToolEnv         m_env;
        Object::ObjectID     m_solidId   = Object::InvalidID;   // 已选边所在的实体
        Object::ObjectID     m_edgeSolid = Object::InvalidID;   // m_body / m_edges 对应的实体
        Solid::BodyPtr       m_body;                            // 世界坐标
        Solid::Polylines     m_edges;
        std::vector<int>     m_selected;                        // m_edges 中的下标
        std::vector<int>     m_candidates;
        size_t               m_cycle = 0;
    };
}
