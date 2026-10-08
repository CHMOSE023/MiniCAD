#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Editor/Tools/DimAssocPick.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Math/MathUtils.hpp"
#include "Core/Log.h"
#include <cmath>
#include <memory>
#include <string>

namespace MiniCAD
{
    // =========================================================================
    // 标注工具（仿 AutoCAD DIMANGULAR / DIMRADIUS / DIMDIAMETER / DIMJOGGED / DIMARC / DIMORDINATE）
    // 线性 / 对齐标注见 DimensionTool。
    // 每个工具标注一个对象后回到第一步，可连续标注；右键 / Esc 退出。
    // 预览直接绘制将要生成的标注实体（线框，不含文字）。
    // =========================================================================
    class DimToolBase : public ITool
    {
    public:
        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsRightClick() || e.IsCancel())
            {
                m_ctx->overlay.Clear();
                Reset();
                if (OnFinished) OnFinished();
                return true;
            }
            if (e.IsKeyPressed(KeyCode::Enter) || e.IsKeyPressed(KeyCode::Space))
                return OnEnter();
            if (e.IsLeftClick())
            {
                OnClick(e, GetPoint(e));
                UpdatePreview(GetPoint(e));
                return true;
            }
            if (e.Type == InputEventType::MouseMove)
            {
                UpdatePreview(GetPoint(e));
                return false;
            }
            return false;
        }

        void OnSceneChanged() override { Reset(); }

    protected:
        virtual void OnClick(const InputEvent& e, const Math::Point3& pt) = 0;
        virtual bool OnEnter() { return false; }
        // 当前步骤下光标处将生成的标注；没有则返回 nullptr（不预览）
        virtual std::unique_ptr<DimensionEntity> Build(const Math::Point3& cursor) const = 0;
        virtual void Reset() = 0;
        // 提交前为标注填写关联（定义点 ↔ 被标注对象），dim 的定义点已是最终位置
        virtual void Associate(DimensionEntity& dim) const { (void)dim; }

        // 点击点经对象捕捉得到时的关联
        DimAssocRef SnapRef(const Math::Point3& pt) const { return SnapAssocRef(*m_ctx, pt); }

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        // 光标下的实体（屏幕容差 8 像素）
        Entity* HitEntity(const InputEvent& e) const
        {
            const Object::ObjectID id = m_ctx->picking.HitTest(
                { static_cast<double>(e.MouseX), static_cast<double>(e.MouseY) }, 8.0);
            if (id == Object::InvalidID) return nullptr;
            Object* obj = m_ctx->scene.GetEntity(id);
            return obj && obj->IsKindOf<Entity>() ? static_cast<Entity*>(obj) : nullptr;
        }

        // 圆或圆弧：取圆心与半径
        static bool CircleOf(const Entity* ent, Math::Point3& center, double& radius)
        {
            if (!ent) return false;
            if (ent->IsKindOf<CircleEntity>())
            {
                const auto& c = static_cast<const CircleEntity*>(ent)->GetCircle();
                center = c.Center; radius = c.Radius;
                return true;
            }
            if (ent->IsKindOf<ArcEntity>())
            {
                const auto& a = static_cast<const ArcEntity*>(ent)->GetArc();
                center = a.Center; radius = a.Radius;
                return true;
            }
            return false;
        }

        static Math::Vec3 DirOr(const Math::Vec3& v, const Math::Vec3& fallback)
        {
            const double len = v.Length();
            return len > Math::LengthEPS ? v / len : fallback;
        }

        void UpdatePreview(const Math::Point3& cursor)
        {
            m_ctx->overlay.Clear();
            auto dim = Build(cursor);
            if (!dim) return;
            PreviewSink sink(m_ctx->overlay, m_ctx->scene.GetLayerManager().GetActiveLayer().GetColor());
            dim->Draw(sink, false, false);
        }

        // Build 用无效 ID 建预览实体（不消耗场景 ID），提交时再按新 ID 克隆
        void Commit(std::unique_ptr<DimensionEntity> dim)
        {
            if (!dim) return;
            const double measure = dim->Measurement();
            std::unique_ptr<Entity> real = dim->Clone(m_ctx->scene.NextObjectID());
            Associate(static_cast<DimensionEntity&>(*real));
            m_ctx->ApplyCurrentAttr(*real);
            m_ctx->cmdStack.Execute(std::make_unique<AddEntityCommand>(std::move(real)), m_ctx->scene);
            LOG_DEBUG("[DimTool] 标注已创建，测量值 %.4f", measure);
            Reset();
        }

        static constexpr Object::ObjectID kPreviewId = Object::InvalidID;

        const EditorContext* m_ctx = nullptr;

        Overlay*              m_overlay = nullptr;

    private:
        // 标注实体输出的线段 / 三角形转为 Overlay 线框
        class PreviewSink : public IDrawSink
        {
        public:
            PreviewSink(Overlay& ov, const Math::Color4& color) : m_ov(ov), m_color(color) {}
            void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4&, bool) override { m_ov.AddLine(a, b, m_color); }
            void FillTriangle(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c, const Math::Color4&) override
            {
                m_ov.AddLine(a, b, m_color);
                m_ov.AddLine(b, c, m_color);
                m_ov.AddLine(c, a, m_color);
            }
        private:
            Overlay&     m_ov;
            Math::Color4 m_color;
        };
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 角度标注
    //   选两条直线 → 光标所在象限决定标注哪一个角（四个角均可）
    //   选圆弧     → 标注圆心角
    //   选圆       → 拾取点为第一端点，再指定第二端点
    //   回车       → 三点方式：顶点 → 第一端点 → 第二端点
    //   最后指定标注弧位置（决定半径，以及标注角还是其优角）
    // ─────────────────────────────────────────────────────────────────────────
    class DimAngularTool : public DimToolBase
    {
    public:
        DimAngularTool() { LOG_DEBUG("[DimAngular] 选择圆弧、圆、直线，或回车指定顶点"); }

        std::string GetPrompt() const override
        {
            switch (m_step)
            {
            case Step::SecondLine:   return "选择第二条直线:";
            case Step::Vertex:       return "指定角的顶点:";
            case Step::FirstPoint:   return "指定角的第一个端点:";
            case Step::SecondPoint:  return "指定角的第二个端点:";
            case Step::Place:        return "指定标注弧线位置 [右键/ESC 退出]:";
            default:                 return "选择圆弧、圆、直线或 <回车 指定顶点>:";
            }
        }
        bool HasAnchor() const override { return m_step == Step::FirstPoint || m_step == Step::SecondPoint; }
        Math::Point3 GetAnchor() const override { return m_vertex; }

    protected:
        enum class Step { Select, SecondLine, Vertex, FirstPoint, SecondPoint, Place };

        bool OnEnter() override
        {
            if (m_step != Step::Select) return false;
            m_step = Step::Vertex;
            return true;
        }

        void OnClick(const InputEvent& e, const Math::Point3& pt) override
        {
            switch (m_step)
            {
            case Step::Select:
            {
                Entity* ent = HitEntity(e);
                if (ent && ent->IsKindOf<LineEntity>())
                {
                    m_line1 = static_cast<LineEntity*>(ent)->GetLine();
                    m_id1   = ent->GetID();
                    m_step  = Step::SecondLine;
                }
                else if (ent && ent->IsKindOf<ArcEntity>())
                {
                    const auto& a = static_cast<ArcEntity*>(ent)->GetArc();
                    m_vertex = a.Center;
                    m_p1     = a.StartPoint();
                    m_p2     = a.EndPoint();
                    m_fromLines = false;
                    m_refs[Slot(DimAssocSlot::Center)] = DimAssoc::Center(m_ctx->scene, ent->GetID());
                    m_refs[Slot(DimAssocSlot::P1)]     = DimAssoc::Feature(m_ctx->scene, ent->GetID(), FeatureKind::Endpoint, m_p1);
                    m_refs[Slot(DimAssocSlot::P2)]     = DimAssoc::Feature(m_ctx->scene, ent->GetID(), FeatureKind::Endpoint, m_p2);
                    m_step   = Step::Place;
                }
                else if (ent && ent->IsKindOf<CircleEntity>())
                {
                    const auto& c = static_cast<CircleEntity*>(ent)->GetCircle();
                    m_vertex = c.Center;
                    m_p1     = c.Center + DirOr(pt - c.Center, { 1, 0, 0 }) * c.Radius;
                    m_fromLines = false;
                    m_refs[Slot(DimAssocSlot::Center)] = DimAssoc::Center(m_ctx->scene, ent->GetID());
                    m_refs[Slot(DimAssocSlot::P1)]     = DimAssoc::Nearest(m_ctx->scene, ent->GetID(), m_p1);
                    m_step   = Step::SecondPoint;
                }
                else
                    LOG_WARN("[DimAngular] 请选择直线、圆弧或圆，或回车指定顶点");
                break;
            }
            case Step::SecondLine:
            {
                Entity* ent = HitEntity(e);
                if (!ent || !ent->IsKindOf<LineEntity>())
                {
                    LOG_WARN("[DimAngular] 请选择第二条直线");
                    break;
                }
                m_line2 = static_cast<LineEntity*>(ent)->GetLine();
                m_id2   = ent->GetID();
                const Math::Vec3 u1 = DirOr(m_line1.End - m_line1.Start, { 1, 0, 0 });
                const Math::Vec3 u2 = DirOr(m_line2.End - m_line2.Start, { 1, 0, 0 });
                const double den = u1.x * u2.y - u1.y * u2.x;
                if (std::abs(den) < 1e-9)
                {
                    LOG_WARN("[DimAngular] 两条直线平行，无法标注角度");
                    m_step = Step::Select;
                    break;
                }
                // 两条无限直线的交点为顶点
                const Math::Vec3 w = m_line2.Start - m_line1.Start;
                const double t = (w.x * u2.y - w.y * u2.x) / den;
                m_vertex    = m_line1.Start + u1 * t;
                m_u1 = u1; m_u2 = u2;
                m_fromLines = true;
                m_step      = Step::Place;
                break;
            }
            case Step::Vertex:
                m_vertex = pt;
                m_refs[Slot(DimAssocSlot::Center)] = SnapRef(pt);
                m_step = Step::FirstPoint;
                break;
            case Step::FirstPoint:
                m_p1 = pt;
                m_refs[Slot(DimAssocSlot::P1)] = SnapRef(pt);
                m_fromLines = false;
                m_step = Step::SecondPoint;
                break;
            case Step::SecondPoint:
                m_p2 = pt;
                m_refs[Slot(DimAssocSlot::P2)] = SnapRef(pt);
                m_step = Step::Place;
                break;
            case Step::Place:       Commit(Build(pt)); break;
            }
        }

        std::unique_ptr<DimensionEntity> Build(const Math::Point3& cursor) const override
        {
            if (m_step != Step::Place) return nullptr;
            Math::Point3 p1 = m_p1, p2 = m_p2;
            if (m_fromLines)
            {
                // 光标方向 w = α·u1 + β·u2：α、β 的符号选出包含光标的那一对射线
                const Math::Vec3 w = cursor - m_vertex;
                const double den   = m_u1.x * m_u2.y - m_u1.y * m_u2.x;
                const double alpha = (w.x * m_u2.y - w.y * m_u2.x) / den;
                const double beta  = (m_u1.x * w.y - m_u1.y * w.x) / den;
                const Math::Vec3 r1 = m_u1 * (alpha >= 0.0 ? 1.0 : -1.0);
                const Math::Vec3 r2 = m_u2 * (beta  >= 0.0 ? 1.0 : -1.0);
                p1 = m_vertex + r1 * RayReach(m_line1, r1);
                p2 = m_vertex + r2 * RayReach(m_line2, r2);
            }
            return DimensionEntity::MakeAngular(kPreviewId, m_vertex, p1, p2, cursor);
        }

        void Reset() override
        {
            m_step = Step::Select;
            m_fromLines = false;
            m_id1 = m_id2 = Object::InvalidID;
            for (auto& r : m_refs) r = {};
            if (m_overlay) m_overlay->Clear();
        }

        void Associate(DimensionEntity& dim) const override
        {
            if (m_fromLines)
            {
                // 顶点 = 两条直线（按无限长）的交点，两条尺寸界线的起点各在一条直线上
                const Scene& scene = m_ctx->scene;
                dim.SetAssoc(DimAssocSlot::Center, DimAssoc::Intersection(scene, m_id1, m_id2, dim.GetCenterPoint(), true));
                dim.SetAssoc(DimAssocSlot::P1, DimAssoc::Nearest(scene, m_id1, dim.GetP1()));
                dim.SetAssoc(DimAssocSlot::P2, DimAssoc::Nearest(scene, m_id2, dim.GetP2()));
                return;
            }
            dim.SetAssoc(DimAssocSlot::Center, m_refs[Slot(DimAssocSlot::Center)]);
            dim.SetAssoc(DimAssocSlot::P1,     m_refs[Slot(DimAssocSlot::P1)]);
            dim.SetAssoc(DimAssocSlot::P2,     m_refs[Slot(DimAssocSlot::P2)]);
        }

    private:
        static size_t Slot(DimAssocSlot s) { return static_cast<size_t>(s); }

        // 直线在射线方向上伸出顶点的距离（尺寸界线从这里引出）；直线全在另一侧时取极小值，界线从顶点引出
        double RayReach(const Line& l, const Math::Vec3& ray) const
        {
            const double ta = Math::Dot(l.Start - m_vertex, ray);
            const double tb = Math::Dot(l.End   - m_vertex, ray);
            return std::max({ ta, tb, 1e-3 });
        }

        Step         m_step = Step::Select;
        bool         m_fromLines = false;
        Line         m_line1, m_line2;
        Math::Vec3   m_u1{ 1, 0, 0 }, m_u2{ 0, 1, 0 };
        Math::Point3 m_vertex, m_p1, m_p2;
        Object::ObjectID m_id1 = Object::InvalidID, m_id2 = Object::InvalidID;   // 选中的两条直线
        DimAssocRef  m_refs[static_cast<size_t>(DimAssocSlot::Count)];            // 选圆弧 / 圆或三点方式时的关联
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 半径 / 直径 / 折弯半径标注：先选圆或圆弧
    //   半径、直径：再指定尺寸线位置（光标方向）
    //   折弯：替代圆心 → 尺寸线位置 → 折弯位置
    // ─────────────────────────────────────────────────────────────────────────
    class DimRadialTool : public DimToolBase
    {
    public:
        enum class Kind { Radius, Diameter, Jogged };

        explicit DimRadialTool(Kind kind) : m_kind(kind) {}

        std::string GetPrompt() const override
        {
            switch (m_step)
            {
            case 1:  return m_kind == Kind::Jogged ? "指定图示中心位置（替代圆心）:" : "指定尺寸线位置 [右键/ESC 退出]:";
            case 2:  return "指定尺寸线位置:";
            case 3:  return "指定折弯位置 [右键/ESC 退出]:";
            default: return "选择圆弧或圆:";
            }
        }

    protected:
        void OnClick(const InputEvent& e, const Math::Point3& pt) override
        {
            switch (m_step)
            {
            case 0:
                if (Entity* ent = HitEntity(e); CircleOf(ent, m_center, m_radius))
                {
                    m_entId = ent->GetID();
                    m_step = 1;
                }
                else
                    LOG_WARN("[DimRadial] 请选择圆或圆弧");
                break;
            case 1:
                if (m_kind == Kind::Jogged) { m_override = pt; m_step = 2; }
                else                          Commit(Build(pt));
                break;
            case 2:
                m_onArc = OnCircle(pt);
                m_step  = 3;
                break;
            case 3:
                Commit(Build(pt));
                break;
            }
        }

        std::unique_ptr<DimensionEntity> Build(const Math::Point3& cursor) const override
        {
            if (m_step == 0) return nullptr;
            switch (m_kind)
            {
            case Kind::Radius:
                return DimensionEntity::MakeRadius(kPreviewId, m_center, OnCircle(cursor));
            case Kind::Diameter:
            {
                const Math::Point3 b = OnCircle(cursor);
                const Math::Point3 a = m_center - (b - m_center);
                return DimensionEntity::MakeDiameter(kPreviewId, a, b);
            }
            case Kind::Jogged:
            default:
                if (m_step == 1) return nullptr;
                if (m_step == 2)        // 尺寸线位置待定：折弯先取替代圆心与弧之间的中点
                {
                    const Math::Point3 tip = OnCircle(cursor);
                    return DimensionEntity::MakeJoggedRadius(kPreviewId, m_center, tip, m_override, Math::Midpoint(m_override, tip));
                }
                return DimensionEntity::MakeJoggedRadius(kPreviewId, m_center, m_onArc, m_override, cursor);
            }
        }

        void Reset() override
        {
            m_step = 0;
            m_entId = Object::InvalidID;
            if (m_overlay) m_overlay->Clear();
        }

        // 半径 / 折弯：圆心与箭头点；直径：两端（圆心取中点，不单独关联）
        void Associate(DimensionEntity& dim) const override
        {
            const Scene& scene = m_ctx->scene;
            dim.SetAssoc(DimAssocSlot::P1, DimAssoc::Nearest(scene, m_entId, dim.GetP1()));
            if (m_kind == Kind::Diameter)
                dim.SetAssoc(DimAssocSlot::P2, DimAssoc::Nearest(scene, m_entId, dim.GetP2()));
            else
                dim.SetAssoc(DimAssocSlot::Center, DimAssoc::Center(scene, m_entId));
        }

    private:
        Math::Point3 OnCircle(const Math::Point3& p) const
        {
            return m_center + DirOr(p - m_center, { 1, 0, 0 }) * m_radius;
        }

        Kind         m_kind;
        int          m_step = 0;
        Math::Point3 m_center, m_override, m_onArc;
        double       m_radius = 0.0;
        Object::ObjectID m_entId = Object::InvalidID;   // 选中的圆 / 圆弧
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 弧长标注：选圆弧 → 指定标注弧位置（在弧内侧或外侧均可）
    // ─────────────────────────────────────────────────────────────────────────
    class DimArcLengthTool : public DimToolBase
    {
    public:
        std::string GetPrompt() const override
        {
            return m_has ? "指定弧长标注位置 [右键/ESC 退出]:" : "选择圆弧:";
        }

    protected:
        void OnClick(const InputEvent& e, const Math::Point3& pt) override
        {
            if (!m_has)
            {
                Entity* ent = HitEntity(e);
                if (!ent || !ent->IsKindOf<ArcEntity>())
                {
                    LOG_WARN("[DimArc] 请选择圆弧");
                    return;
                }
                m_arc   = static_cast<ArcEntity*>(ent)->GetArc();
                m_entId = ent->GetID();
                m_has   = true;
                return;
            }
            Commit(Build(pt));
        }

        std::unique_ptr<DimensionEntity> Build(const Math::Point3& cursor) const override
        {
            if (!m_has) return nullptr;
            // 被测弧由标注弧点的方向确定：光标方向不在弧的角度范围内时改用弧中点方向，始终标注这段弧本身
            const double ang = std::atan2(cursor.y - m_arc.Center.y, cursor.x - m_arc.Center.x);
            Math::Point3 through = cursor;
            if (!m_arc.ContainsAngle(ang))
            {
                const double r  = std::max((cursor - m_arc.Center).Length(), Math::LengthEPS * 10.0);
                const double am = m_arc.MidAngle();
                through = m_arc.Center + Math::Vec3{ std::cos(am), std::sin(am), 0.0 } * r;
            }
            return DimensionEntity::MakeArcLength(kPreviewId, m_arc.Center, m_arc.StartPoint(), m_arc.EndPoint(), through);
        }

        void Reset() override
        {
            m_has = false;
            if (m_overlay) m_overlay->Clear();
        }

        // 圆心与弧的两端
        void Associate(DimensionEntity& dim) const override
        {
            const Scene& scene = m_ctx->scene;
            dim.SetAssoc(DimAssocSlot::Center, DimAssoc::Center(scene, m_entId));
            dim.SetAssoc(DimAssocSlot::P1, DimAssoc::Feature(scene, m_entId, FeatureKind::Endpoint, dim.GetP1()));
            dim.SetAssoc(DimAssocSlot::P2, DimAssoc::Feature(scene, m_entId, FeatureKind::Endpoint, dim.GetP2()));
        }

    private:
        bool m_has = false;
        Arc  m_arc;
        Object::ObjectID m_entId = Object::InvalidID;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 坐标标注：特征点 → 引线端点。引线偏水平时标注 Y 坐标，偏竖直时标注 X 坐标（同 AutoCAD）
    // ─────────────────────────────────────────────────────────────────────────
    class DimOrdinateTool : public DimToolBase
    {
    public:
        std::string GetPrompt() const override
        {
            return m_has ? "指定引线端点位置 [右键/ESC 退出]:" : "指定点坐标:";
        }
        bool HasAnchor() const override { return m_has; }
        Math::Point3 GetAnchor() const override { return m_feature; }

    protected:
        void OnClick(const InputEvent&, const Math::Point3& pt) override
        {
            if (!m_has) { m_feature = pt; m_ref = SnapRef(pt); m_has = true; return; }
            Commit(Build(pt));
        }

        std::unique_ptr<DimensionEntity> Build(const Math::Point3& cursor) const override
        {
            if (!m_has) return nullptr;
            const Math::Vec3 d = cursor - m_feature;
            const OrdinateAxis axis = std::abs(d.x) > std::abs(d.y) ? OrdinateAxis::Y : OrdinateAxis::X;
            return DimensionEntity::MakeOrdinate(kPreviewId, m_feature, cursor, axis);
        }

        void Reset() override
        {
            m_has = false;
            m_ref = {};
            if (m_overlay) m_overlay->Clear();
        }

        void Associate(DimensionEntity& dim) const override { dim.SetAssoc(DimAssocSlot::P1, m_ref); }

    private:
        bool         m_has = false;
        Math::Point3 m_feature;
        DimAssocRef  m_ref;          // 特征点的关联
    };
}
