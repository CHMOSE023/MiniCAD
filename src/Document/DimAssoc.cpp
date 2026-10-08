#include "DimAssoc.h"
#include "Scene/Scene.h"
#include "Document/CommandStack/ICommand.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/ICurveEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/GeomKernel/CurveIntersect.hpp"
#include "Core/Math/MathUtils.hpp"
#include "Core/Math/Constants.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace MiniCAD
{
    namespace
    {
        using Kind = DimAssocRef::Kind;

        constexpr DimAssocSlot kSlots[] = { DimAssocSlot::P1, DimAssocSlot::P2, DimAssocSlot::Center };

        // 位置是否重合：相对容差，坐标很大时仍可靠
        bool Near(const Math::Point3& a, const Math::Point3& b)
        {
            const double scale = 1.0 + std::max({ std::abs(a.x), std::abs(a.y), std::abs(b.x), std::abs(b.y) });
            return (a - b).Length() <= 1e-7 * scale;
        }

        const Entity* EntityOf(const Scene& scene, Object::ObjectID id)
        {
            const Object* o = scene.GetEntity(id);
            return o && o->IsKindOf<Entity>() ? static_cast<const Entity*>(o) : nullptr;
        }

        FeatureKind ToFeature(Kind k)
        {
            switch (k)
            {
            case Kind::Midpoint: return FeatureKind::Midpoint;
            case Kind::Quadrant: return FeatureKind::Quadrant;
            case Kind::Center:   return FeatureKind::Center;
            default:             return FeatureKind::Endpoint;
            }
        }

        Kind FromFeature(FeatureKind k)
        {
            switch (k)
            {
            case FeatureKind::Midpoint: return Kind::Midpoint;
            case FeatureKind::Quadrant: return Kind::Quadrant;
            case FeatureKind::Center:   return Kind::Center;
            default:                    return Kind::Endpoint;
            }
        }

        // 两条直线实体按无限长求交；平行或不是直线时返回空
        std::optional<Math::Point3> InfiniteLineIntersection(const Entity& a, const Entity& b)
        {
            if (!a.IsKindOf<LineEntity>() || !b.IsKindOf<LineEntity>()) return std::nullopt;
            const auto& l1 = static_cast<const LineEntity&>(a).GetLine();
            const auto& l2 = static_cast<const LineEntity&>(b).GetLine();
            const Math::Vec3 u1 = l1.End - l1.Start, u2 = l2.End - l2.Start;
            const double den = u1.x * u2.y - u1.y * u2.x;
            if (std::abs(den) < 1e-12 * std::max(1.0, u1.LengthSq() * u2.LengthSq())) return std::nullopt;
            const Math::Vec3 w = l2.Start - l1.Start;
            const double t = (w.x * u2.y - w.y * u2.x) / den;
            return l1.Start + u1 * t;
        }

        // 把 [-∞, ∞) 角度规整到 [0, 2π)
        double NormalizePositive(double a)
        {
            a = std::fmod(a, Math::TwoPI);
            return a < 0.0 ? a + Math::TwoPI : a;
        }

        double AngleOf(const Math::Point3& c, const Math::Point3& p) { return std::atan2(p.y - c.y, p.x - c.x); }

        // ─────────────────────────────────────────────────────────────────────
        // 定义点跟随几何后，调整没有关联的派生点（尺寸线位置、折弯、替代圆心、手动文字位置）
        // old 为跟随前的标注，dim 已写入新的定义点
        // ─────────────────────────────────────────────────────────────────────
        void AdjustDerivedPoints(const DimensionEntity& old, DimensionEntity& dim)
        {
            const Math::Vec3 d1 = dim.GetP1() - old.GetP1();
            const Math::Vec3 d2 = dim.GetP2() - old.GetP2();

            switch (dim.GetType())
            {
            case DimType::Aligned:
            {
                // 尺寸线保持相对两标注点连线的偏移：沿连线方向按长度比例，垂直方向保持距离
                const Math::Vec3 u0 = old.GetP2() - old.GetP1();
                const Math::Vec3 u1 = dim.GetP2() - dim.GetP1();
                const double len0 = u0.Length(), len1 = u1.Length();
                if (len0 < Math::LengthEPS || len1 < Math::LengthEPS)
                {
                    dim.SetDimLinePoint(old.GetDimLinePoint() + (d1 + d2) * 0.5);
                    break;
                }
                const Math::Vec3 w = old.GetDimLinePoint() - old.GetP1();
                const Math::Vec3 n0{ -u0.y / len0, u0.x / len0, 0.0 };
                const Math::Vec3 n1{ -u1.y / len1, u1.x / len1, 0.0 };
                const double along = Math::Dot(w, u0) / (len0 * len0);
                const double off   = Math::Dot(w, n0);
                dim.SetDimLinePoint(dim.GetP1() + u1 * along + n1 * off);
                break;
            }
            case DimType::Linear:
                // 两端同样位移（整体平移）时尺寸线随之平移；只动一端时尺寸线位置不变（同 AutoCAD 转角标注）
                if (Near(old.GetP1() + d1, old.GetP1() + d2))
                    dim.SetDimLinePoint(old.GetDimLinePoint() + d1);
                break;
            case DimType::Ordinate:
                dim.SetDimLinePoint(old.GetDimLinePoint() + d1);
                break;
            case DimType::Radius:
                dim.SetDimLinePoint(dim.GetP1());
                break;
            case DimType::Diameter:
            {
                const Math::Point3 mid = Math::Midpoint(dim.GetP1(), dim.GetP2());
                dim.SetCenterPoint(mid);
                dim.SetDimLinePoint(mid);
                break;
            }
            case DimType::JoggedRadius:
            {
                // 替代圆心与折弯位置随圆心平移
                const Math::Vec3 dc = dim.GetCenterPoint() - old.GetCenterPoint();
                dim.SetP2(old.GetP2() + dc);
                dim.SetDimLinePoint(old.GetDimLinePoint() + dc);
                break;
            }
            case DimType::Angular:
            case DimType::ArcLength:
            {
                // 标注弧经过点：在两条边之间的角度比例不变；弧长标注保持与被测弧的径向间距，角度标注保持半径
                const Math::Point3& c0 = old.GetCenterPoint();
                const Math::Point3& c1 = dim.GetCenterPoint();
                const double a1o = AngleOf(c0, old.GetP1()), a2o = AngleOf(c0, old.GetP2());
                const double aDo = AngleOf(c0, old.GetDimLinePoint());
                const double spanCCW = NormalizePositive(a2o - a1o);
                const double toD     = NormalizePositive(aDo - a1o);
                const bool   ccw     = toD <= spanCCW;
                const double spanO   = ccw ? spanCCW : spanCCW - Math::TwoPI;
                const double frac    = std::abs(spanO) > 1e-12 ? (ccw ? toD : toD - Math::TwoPI) / spanO : 0.5;

                const double a1n   = AngleOf(c1, dim.GetP1()), a2n = AngleOf(c1, dim.GetP2());
                const double spanN = ccw ? NormalizePositive(a2n - a1n) : NormalizePositive(a2n - a1n) - Math::TwoPI;
                const double aDn   = a1n + frac * spanN;

                const double rO = (old.GetDimLinePoint() - c0).Length();
                double rN = rO;
                if (dim.GetType() == DimType::ArcLength)
                    rN = (dim.GetP1() - c1).Length() + (rO - (old.GetP1() - c0).Length());
                if (rN <= Math::LengthEPS) rN = std::max(rO, Math::LengthEPS * 10.0);
                dim.SetDimLinePoint(c1 + Math::Vec3{ std::cos(aDn), std::sin(aDn), 0.0 } * rN);
                break;
            }
            }

            // 手动放置的文字随默认文字位置一起平移
            if (!dim.IsUsingDefaultTextPosition())
                dim.SetTextPosition(old.TextPosition() + (dim.DefaultTextPosition() - old.DefaultTextPosition()));
        }

        // 按定义点的现位置重新建立同一类关联（标注与几何一起被变换后）
        DimAssocRef Reanchor(const Scene& scene, const DimAssocRef& ref, const Math::Point3& at)
        {
            DimAssocRef r;
            switch (ref.RefKind)
            {
            case Kind::Endpoint:
            case Kind::Midpoint:
            case Kind::Quadrant:
            case Kind::Center:
                r = DimAssoc::Feature(scene, ref.Id, ToFeature(ref.RefKind), at);
                break;
            case Kind::Nearest:
                r = DimAssoc::Nearest(scene, ref.Id, at);
                break;
            case Kind::Intersection:
                r = DimAssoc::Intersection(scene, ref.Id, ref.Id2, at, ref.Index == 1);
                break;
            default:
                break;
            }
            return r;
        }

        // 一条标注跟上几何：cur 为场景里的标注，out 为它的副本（写入新状态）。返回是否有改动
        bool UpdateOne(const Scene& scene, const DimensionEntity& cur, DimensionEntity& out)
        {
            bool changed = false, follow = false;
            for (DimAssocSlot s : kSlots)
            {
                const DimAssocRef& ref = cur.GetAssoc(s);
                if (!ref.IsValid()) continue;

                const auto ev = DimAssoc::Evaluate(scene, ref);
                if (!ev)                                   // 关联对象没了 / 没有这个特征点了
                {
                    out.ClearAssoc(s);
                    changed = true;
                    continue;
                }

                const Math::Point3 at = cur.SlotPoint(s);
                const bool geomMoved = !Near(*ev, ref.Last);
                const bool dimMoved  = !Near(at, ref.Last);
                if (!geomMoved && !dimMoved) continue;

                if (geomMoved && !dimMoved)                // 跟随几何
                {
                    DimAssocRef r = ref;
                    r.Last = *ev;
                    out.SetAssoc(s, r);
                    out.SetSlotPoint(s, *ev);
                    changed = follow = true;
                }
                else if (geomMoved)                        // 与几何一起被变换：按现位置重新挂接
                {
                    const DimAssocRef r = Reanchor(scene, ref, at);
                    if (r.IsValid()) out.SetAssoc(s, r);
                    else             out.ClearAssoc(s);
                    changed = true;
                }
                else                                       // 只有标注被改：解除关联
                {
                    out.ClearAssoc(s);
                    changed = true;
                }
            }

            if (follow)
                AdjustDerivedPoints(cur, out);
            return changed;
        }

        // 关联同步产生的改动：标注前后两份快照，原位覆盖（不改变绘制次序与选择集）
        class DimAssocUpdateCommand : public ICommand
        {
        public:
            struct Item
            {
                Object::ObjectID                 id = Object::InvalidID;
                std::unique_ptr<DimensionEntity> before, after;
            };

            explicit DimAssocUpdateCommand(std::vector<Item> items) : m_items(std::move(items)) {}

            bool Execute(Scene& scene) override
            {
                for (auto& it : m_items) Apply(scene, it.id, *it.after);
                return true;
            }
            void Undo(Scene& scene) override
            {
                for (auto it = m_items.rbegin(); it != m_items.rend(); ++it) Apply(scene, it->id, *it->before);
            }
            std::string GetName() const override { return "关联标注更新"; }

        private:
            static void Apply(Scene& scene, Object::ObjectID id, const DimensionEntity& snap)
            {
                Object* o = scene.GetEntity(id);
                if (!o || !o->IsKindOf<DimensionEntity>()) return;
                static_cast<DimensionEntity*>(o)->CopyFrom(snap);
                scene.MarkEntityDirty(id);
            }

            std::vector<Item> m_items;
        };

        std::unique_ptr<DimensionEntity> CloneDim(const DimensionEntity& d)
        {
            return std::unique_ptr<DimensionEntity>(static_cast<DimensionEntity*>(d.Clone(d.GetID()).release()));
        }
    }

    namespace DimAssoc
    {
        DimAssocRef Feature(const Scene& scene, Object::ObjectID id, FeatureKind kind, const Math::Point3& pt)
        {
            const Entity* e = EntityOf(scene, id);
            if (!e) return {};
            std::vector<Math::Point3> pts;
            CollectFeaturePoints(*e, kind, pts);
            for (size_t i = 0; i < pts.size(); ++i)
                if (Near(pts[i], pt))
                {
                    DimAssocRef r;
                    r.RefKind = FromFeature(kind);
                    r.Id      = id;
                    r.Index   = static_cast<int32_t>(i);
                    r.Last    = pts[i];
                    return r;
                }
            return {};
        }

        DimAssocRef Center(const Scene& scene, Object::ObjectID id)
        {
            const Entity* e = EntityOf(scene, id);
            if (!e) return {};
            std::vector<Math::Point3> pts;
            CollectFeaturePoints(*e, FeatureKind::Center, pts);
            if (pts.empty()) return {};
            DimAssocRef r;
            r.RefKind = Kind::Center;
            r.Id      = id;
            r.Last    = pts.front();
            return r;
        }

        DimAssocRef Nearest(const Scene& scene, Object::ObjectID id, const Math::Point3& pt)
        {
            const Entity* e = EntityOf(scene, id);
            const ICurveEntity* ce = e ? e->AsCurveEntity() : nullptr;
            if (!ce) return {};
            const auto curve = ce->MakeCurve();
            const double t = curve->ProjectParam(pt);
            const Math::Point3 on = curve->PointAt(t);
            if (!Near(on, pt)) return {};
            DimAssocRef r;
            r.RefKind = Kind::Nearest;
            r.Id      = id;
            r.Param   = t;
            r.Last    = on;
            return r;
        }

        DimAssocRef Intersection(const Scene& scene, Object::ObjectID a, Object::ObjectID b,
                                 const Math::Point3& pt, bool extended)
        {
            if (a == Object::InvalidID || b == Object::InvalidID || a == b) return {};
            DimAssocRef r;
            r.RefKind = Kind::Intersection;
            r.Id      = a;
            r.Id2     = b;
            r.Index   = extended ? 1 : 0;
            r.Last    = pt;
            const auto ev = Evaluate(scene, r);
            if (!ev || !Near(*ev, pt)) return {};
            r.Last = *ev;
            return r;
        }

        std::optional<Math::Point3> Evaluate(const Scene& scene, const DimAssocRef& ref)
        {
            if (!ref.IsValid()) return std::nullopt;
            const Entity* e = EntityOf(scene, ref.Id);
            if (!e) return std::nullopt;

            switch (ref.RefKind)
            {
            case Kind::Endpoint:
            case Kind::Midpoint:
            case Kind::Quadrant:
            case Kind::Center:
            {
                std::vector<Math::Point3> pts;
                CollectFeaturePoints(*e, ToFeature(ref.RefKind), pts);
                if (ref.Index < 0 || static_cast<size_t>(ref.Index) >= pts.size()) return std::nullopt;
                return pts[static_cast<size_t>(ref.Index)];
            }
            case Kind::Nearest:
            {
                const ICurveEntity* ce = e->AsCurveEntity();
                if (!ce) return std::nullopt;
                return ce->MakeCurve()->PointAt(ref.Param);
            }
            case Kind::Intersection:
            {
                const Entity* e2 = EntityOf(scene, ref.Id2);
                if (!e2) return std::nullopt;
                if (ref.Index == 1)
                    if (auto p = InfiniteLineIntersection(*e, *e2)) return p;

                const ICurveEntity* c1 = e->AsCurveEntity();
                const ICurveEntity* c2 = e2->AsCurveEntity();
                if (!c1 || !c2) return std::nullopt;
                const auto pts = Geom::IntersectCurves(*c1->MakeCurve(), *c2->MakeCurve());
                if (pts.empty()) return std::nullopt;
                // 多个交点时取离上次位置最近的
                const auto it = std::min_element(pts.begin(), pts.end(), [&](const Math::Point3& p, const Math::Point3& q)
                    { return (p - ref.Last).LengthSq() < (q - ref.Last).LengthSq(); });
                return *it;
            }
            default:
                return std::nullopt;
            }
        }

        std::unique_ptr<ICommand> Sync(Scene& scene)
        {
            std::vector<Object::ObjectID> ids;
            scene.ForEachObject([&](const Object& o)
                {
                    if (o.IsKindOf<DimensionEntity>() && static_cast<const DimensionEntity&>(o).IsAssociative())
                        ids.push_back(o.GetID());
                });
            if (ids.empty()) return nullptr;

            std::vector<DimAssocUpdateCommand::Item> items;
            for (Object::ObjectID id : ids)
            {
                auto* dim = static_cast<DimensionEntity*>(scene.GetEntity(id));
                auto after = CloneDim(*dim);
                if (!UpdateOne(scene, *dim, *after)) continue;

                DimAssocUpdateCommand::Item it;
                it.id     = id;
                it.before = CloneDim(*dim);
                dim->CopyFrom(*after);
                scene.MarkEntityDirty(id);
                it.after  = std::move(after);
                items.push_back(std::move(it));
            }
            if (items.empty()) return nullptr;
            return std::make_unique<DimAssocUpdateCommand>(std::move(items));
        }

        void RemapCopy(Entity& copy, const std::unordered_map<Object::ObjectID, Object::ObjectID>& srcToNew)
        {
            if (!copy.IsKindOf<DimensionEntity>()) return;
            auto& dim = static_cast<DimensionEntity&>(copy);
            for (DimAssocSlot s : kSlots)
            {
                DimAssocRef r = dim.GetAssoc(s);
                if (!r.IsValid()) continue;
                const auto a = srcToNew.find(r.Id);
                const auto b = r.Id2 ? srcToNew.find(r.Id2) : srcToNew.end();
                if (a == srcToNew.end() || (r.Id2 && b == srcToNew.end()))
                {
                    dim.ClearAssoc(s);
                    continue;
                }
                r.Id = a->second;
                if (r.Id2) r.Id2 = b->second;
                dim.SetAssoc(s, r);
            }
        }

        void RemapCopies(Scene& scene, const std::vector<std::pair<Object::ObjectID, Object::ObjectID>>& srcToNew)
        {
            std::unordered_map<Object::ObjectID, Object::ObjectID> map(srcToNew.begin(), srcToNew.end());
            for (const auto& [src, dst] : srcToNew)
                if (Object* o = scene.GetEntity(dst); o && o->IsKindOf<DimensionEntity>())
                    RemapCopy(static_cast<Entity&>(*o), map);
        }

        void RemapCopies(Scene& scene, const std::vector<Object::ObjectID>& src, const std::vector<Object::ObjectID>& copies)
        {
            std::vector<std::pair<Object::ObjectID, Object::ObjectID>> pairs;
            for (size_t i = 0; i < src.size() && i < copies.size(); ++i)
                pairs.emplace_back(src[i], copies[i]);
            RemapCopies(scene, pairs);
        }

        void Strip(Entity& e)
        {
            if (e.IsKindOf<DimensionEntity>())
                static_cast<DimensionEntity&>(e).ClearAllAssoc();
        }
    }
}
