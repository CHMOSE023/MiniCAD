#include "Editor/Properties/PropertyTable.h"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/RayEntity.hpp"
#include "Core/Entity/XLineEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Core/Entity/RegionEntity.hpp"
#include "Core/Entity/ImageEntity.hpp"
#include "Core/Entity/SolidEntity.hpp"
#include "Core/Entity/TableEntity.hpp"
#include "Core/Entity/WipeoutEntity.hpp"
#include "Core/Math/Constants.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace MiniCAD
{
    namespace
    {
        constexpr double kEps = 1e-9;

        double Deg(double rad) { return rad * 180.0 / Math::PI; }
        double Rad(double deg) { return deg * Math::PI / 180.0; }

        double AsNumber(const PropValue& v) { return std::get<double>(v); }

        // ── 表里常用的三种行 ──────────────────────────────────────────────
        // T：实体具体类型；Get / Set 直接操作它。set 返回 false 表示值非法。
        template<typename T, typename G>
        PropertyDesc ReadOnly(const char* name, PropKind kind, G get)
        {
            return { name, kind,
                     [get](const Entity& e) -> PropValue { return get(static_cast<const T&>(e)); },
                     nullptr };
        }

        template<typename T, typename G, typename S>
        PropertyDesc Editable(const char* name, PropKind kind, G get, S set)
        {
            return { name, kind,
                     [get](const Entity& e) -> PropValue { return get(static_cast<const T&>(e)); },
                     [set](Entity& e, const PropValue& v) { return set(static_cast<T&>(e), AsNumber(v)); } };
        }

        // 坐标（任意实数）
        template<typename T, typename G, typename S>
        PropertyDesc Coord(const char* name, G get, S set)
        {
            return Editable<T>(name, PropKind::Number, get, [set](T& e, double v) { set(e, v); return true; });
        }

        // ── 直线 ──────────────────────────────────────────────────────────
        std::vector<PropertyDesc> MakeLineProps()
        {
            using T = LineEntity;
            auto modify = [](T& e, auto fn) { Line l = e.GetLine(); fn(l); e.SetLine(l); };
            return {
                Coord<T>("起点 X", [](const T& e) { return e.GetLine().Start.x; }, [=](T& e, double v) { modify(e, [v](Line& l) { l.Start.x = v; }); }),
                Coord<T>("起点 Y", [](const T& e) { return e.GetLine().Start.y; }, [=](T& e, double v) { modify(e, [v](Line& l) { l.Start.y = v; }); }),
                Coord<T>("端点 X", [](const T& e) { return e.GetLine().End.x; },   [=](T& e, double v) { modify(e, [v](Line& l) { l.End.x = v; }); }),
                Coord<T>("端点 Y", [](const T& e) { return e.GetLine().End.y; },   [=](T& e, double v) { modify(e, [v](Line& l) { l.End.y = v; }); }),
                // 长度：起点不动，沿原方向伸缩端点；零长度的线没有方向，不能改
                Editable<T>("长度", PropKind::Number, [](const T& e) { return e.GetLine().Length(); },
                    [=](T& e, double v)
                    {
                        const Line& l = e.GetLine();
                        const double len = l.Length();
                        if (v <= kEps || len <= kEps) return false;
                        const double k = v / len;
                        modify(e, [k](Line& m)
                        {
                            m.End.x = m.Start.x + (m.End.x - m.Start.x) * k;
                            m.End.y = m.Start.y + (m.End.y - m.Start.y) * k;
                            m.End.z = m.Start.z + (m.End.z - m.Start.z) * k;
                        });
                        return true;
                    }),
                // 角度：起点不动，绕起点转动端点（XY 平面内，长度不变）
                Editable<T>("角度", PropKind::Angle,
                    [](const T& e) { const Line& l = e.GetLine(); return Deg(std::atan2(l.End.y - l.Start.y, l.End.x - l.Start.x)); },
                    [=](T& e, double v)
                    {
                        const Line& l = e.GetLine();
                        const double dx = l.End.x - l.Start.x, dy = l.End.y - l.Start.y;
                        const double len = std::hypot(dx, dy);
                        if (len <= kEps) return false;
                        modify(e, [&](Line& m)
                        {
                            m.End.x = m.Start.x + len * std::cos(Rad(v));
                            m.End.y = m.Start.y + len * std::sin(Rad(v));
                        });
                        return true;
                    }),
            };
        }

        // ── 圆 ────────────────────────────────────────────────────────────
        std::vector<PropertyDesc> MakeCircleProps()
        {
            using T = CircleEntity;
            return {
                Coord<T>("圆心 X", [](const T& e) { return e.GetCircle().Center.x; }, [](T& e, double v) { auto c = e.GetCircle().Center; c.x = v; e.SetCenter(c); }),
                Coord<T>("圆心 Y", [](const T& e) { return e.GetCircle().Center.y; }, [](T& e, double v) { auto c = e.GetCircle().Center; c.y = v; e.SetCenter(c); }),
                Editable<T>("半径", PropKind::Number, [](const T& e) { return e.GetCircle().Radius; },
                            [](T& e, double v) { if (v <= kEps) return false; e.SetRadius(v); return true; }),
                Editable<T>("直径", PropKind::Number, [](const T& e) { return e.GetCircle().Radius * 2.0; },
                            [](T& e, double v) { if (v <= kEps) return false; e.SetRadius(v * 0.5); return true; }),
                ReadOnly<T>("周长", PropKind::Number, [](const T& e) { return Math::TwoPI * e.GetCircle().Radius; }),
                ReadOnly<T>("面积", PropKind::Number, [](const T& e) { return Math::PI * e.GetCircle().Radius * e.GetCircle().Radius; }),
            };
        }

        // ── 圆弧 ──────────────────────────────────────────────────────────
        std::vector<PropertyDesc> MakeArcProps()
        {
            using T = ArcEntity;
            return {
                Coord<T>("圆心 X", [](const T& e) { return e.GetArc().Center.x; }, [](T& e, double v) { auto c = e.GetArc().Center; c.x = v; e.SetCenter(c); }),
                Coord<T>("圆心 Y", [](const T& e) { return e.GetArc().Center.y; }, [](T& e, double v) { auto c = e.GetArc().Center; c.y = v; e.SetCenter(c); }),
                Editable<T>("半径", PropKind::Number, [](const T& e) { return e.GetArc().Radius; },
                            [](T& e, double v) { if (v <= kEps) return false; e.SetRadius(v); return true; }),
                Editable<T>("起始角", PropKind::Angle, [](const T& e) { return Deg(e.GetArc().StartAngle); },
                            [](T& e, double v) { e.SetStartAngle(Rad(v)); return true; }),
                Editable<T>("终止角", PropKind::Angle, [](const T& e) { return Deg(e.GetArc().EndAngle); },
                            [](T& e, double v) { e.SetEndAngle(Rad(v)); return true; }),
                // 总角度：起始角不动，改终止角（0 < 角度 ≤ 360）
                Editable<T>("总角度", PropKind::Angle, [](const T& e) { return Deg(e.GetArc().SweepAngle()); },
                            [](T& e, double v)
                            {
                                if (v <= kEps || v > 360.0 + kEps) return false;
                                e.SetEndAngle(e.GetArc().StartAngle + Rad(v));
                                return true;
                            }),
                ReadOnly<T>("弧长", PropKind::Number, [](const T& e) { return e.GetArc().Radius * e.GetArc().SweepAngle(); }),
            };
        }

        // ── 单行文字 ──────────────────────────────────────────────────────
        std::vector<PropertyDesc> MakeTextProps()
        {
            using T = TextEntity;
            return {
                { "内容", PropKind::String,
                  [](const Entity& e) -> PropValue { return static_cast<const T&>(e).GetText(); },
                  [](Entity& e, const PropValue& v)
                  {
                      const auto& s = std::get<std::string>(v);
                      if (s.empty()) return false;
                      static_cast<T&>(e).SetText(s);
                      return true;
                  } },
                Coord<T>("位置 X", [](const T& e) { return e.GetPosition().x; }, [](T& e, double v) { auto p = e.GetPosition(); p.x = v; e.SetPosition(p); }),
                Coord<T>("位置 Y", [](const T& e) { return e.GetPosition().y; }, [](T& e, double v) { auto p = e.GetPosition(); p.y = v; e.SetPosition(p); }),
                Editable<T>("高度", PropKind::Number, [](const T& e) { return static_cast<double>(e.GetHeight()); },
                            [](T& e, double v) { if (v <= kEps) return false; e.SetHeight(static_cast<float>(v)); return true; }),
                Editable<T>("旋转", PropKind::Angle, [](const T& e) { return Deg(static_cast<double>(e.GetRotation())); },
                            [](T& e, double v) { e.SetRotation(static_cast<float>(Rad(v))); return true; }),
            };
        }

        // ── 点 ────────────────────────────────────────────────────────────
        std::vector<PropertyDesc> MakePointProps()
        {
            using T = PointEntity;
            auto at = [](T& e, auto fn) { Point p = e.GetPoint(); fn(p.Position); e.SetPoint(p); };
            return {
                Coord<T>("位置 X", [](const T& e) { return e.GetPoint().Position.x; }, [=](T& e, double v) { at(e, [v](Math::Point3& q) { q.x = v; }); }),
                Coord<T>("位置 Y", [](const T& e) { return e.GetPoint().Position.y; }, [=](T& e, double v) { at(e, [v](Math::Point3& q) { q.y = v; }); }),
            };
        }

        // ── 矩形（四个角点，可以是旋转过的）──────────────────────────────
        // 宽 = P1→P2，高 = P2→P3。改宽 / 高时 P1 不动，沿原边方向伸缩
        std::vector<PropertyDesc> MakeRectangleProps()
        {
            using T = RectangleEntity;
            auto width   = [](const Rectangle& r) { return std::hypot(r.P2.x - r.P1.x, r.P2.y - r.P1.y); };
            auto height  = [](const Rectangle& r) { return std::hypot(r.P3.x - r.P2.x, r.P3.y - r.P2.y); };
            auto centerX = [](const Rectangle& r) { return (r.P1.x + r.P2.x + r.P3.x + r.P4.x) * 0.25; };
            auto centerY = [](const Rectangle& r) { return (r.P1.y + r.P2.y + r.P3.y + r.P4.y) * 0.25; };
            auto shift = [](T& e, double dx, double dy)
            {
                Rectangle r = e.GetRectangle();
                for (Math::Point3* p : { &r.P1, &r.P2, &r.P3, &r.P4 }) { p->x += dx; p->y += dy; }
                e.SetRectangle(r);
            };
            return {
                Coord<T>("中心 X", [=](const T& e) { return centerX(e.GetRectangle()); }, [=](T& e, double v) { shift(e, v - centerX(e.GetRectangle()), 0.0); }),
                Coord<T>("中心 Y", [=](const T& e) { return centerY(e.GetRectangle()); }, [=](T& e, double v) { shift(e, 0.0, v - centerY(e.GetRectangle())); }),
                Editable<T>("宽度", PropKind::Number, [=](const T& e) { return width(e.GetRectangle()); },
                    [=](T& e, double v)
                    {
                        Rectangle r = e.GetRectangle();
                        const double w = width(r);
                        if (v <= kEps || w <= kEps) return false;
                        const double k = v / w;
                        const double vx = r.P3.x - r.P2.x, vy = r.P3.y - r.P2.y;       // 高度方向的边向量
                        r.P2.x = r.P1.x + (r.P2.x - r.P1.x) * k;  r.P2.y = r.P1.y + (r.P2.y - r.P1.y) * k;
                        r.P3.x = r.P2.x + vx;                      r.P3.y = r.P2.y + vy;
                        r.P4.x = r.P1.x + vx;                      r.P4.y = r.P1.y + vy;
                        e.SetRectangle(r);
                        return true;
                    }),
                Editable<T>("高度", PropKind::Number, [=](const T& e) { return height(e.GetRectangle()); },
                    [=](T& e, double v)
                    {
                        Rectangle r = e.GetRectangle();
                        const double h = height(r);
                        if (v <= kEps || h <= kEps) return false;
                        const double k = v / h;
                        const double vx = (r.P3.x - r.P2.x) * k, vy = (r.P3.y - r.P2.y) * k;
                        r.P3.x = r.P2.x + vx;  r.P3.y = r.P2.y + vy;
                        r.P4.x = r.P1.x + vx;  r.P4.y = r.P1.y + vy;
                        e.SetRectangle(r);
                        return true;
                    }),
                ReadOnly<T>("面积", PropKind::Number, [=](const T& e) { return width(e.GetRectangle()) * height(e.GetRectangle()); }),
                ReadOnly<T>("周长", PropKind::Number, [=](const T& e) { return 2.0 * (width(e.GetRectangle()) + height(e.GetRectangle())); }),
            };
        }

        // ── 椭圆 / 椭圆弧 ─────────────────────────────────────────────────
        std::vector<PropertyDesc> MakeEllipseProps()
        {
            using T = EllipseEntity;
            auto modify = [](T& e, auto fn) { Ellipse el = e.GetEllipse(); fn(el); e.SetEllipse(el); };
            return {
                Coord<T>("圆心 X", [](const T& e) { return e.GetEllipse().Center.x; }, [=](T& e, double v) { modify(e, [v](Ellipse& el) { el.Center.x = v; }); }),
                Coord<T>("圆心 Y", [](const T& e) { return e.GetEllipse().Center.y; }, [=](T& e, double v) { modify(e, [v](Ellipse& el) { el.Center.y = v; }); }),
                Editable<T>("半轴 X", PropKind::Number, [](const T& e) { return e.GetEllipse().RadiusX; },
                            [=](T& e, double v) { if (v <= kEps) return false; modify(e, [v](Ellipse& el) { el.RadiusX = v; }); return true; }),
                Editable<T>("半轴 Y", PropKind::Number, [](const T& e) { return e.GetEllipse().RadiusY; },
                            [=](T& e, double v) { if (v <= kEps) return false; modify(e, [v](Ellipse& el) { el.RadiusY = v; }); return true; }),
                Editable<T>("旋转", PropKind::Angle, [](const T& e) { return Deg(e.GetEllipse().Rotation); },
                            [=](T& e, double v) { modify(e, [v](Ellipse& el) { el.Rotation = Rad(v); }); return true; }),
                Editable<T>("起始参数", PropKind::Angle, [](const T& e) { return Deg(e.GetEllipse().StartParam); },
                            [=](T& e, double v) { modify(e, [v](Ellipse& el) { el.StartParam = Rad(v); }); return true; }),
                Editable<T>("终止参数", PropKind::Angle, [](const T& e) { return Deg(e.GetEllipse().EndParam); },
                            [=](T& e, double v) { modify(e, [v](Ellipse& el) { el.EndParam = Rad(v); }); return true; }),
            };
        }

        // ── 多段线 ────────────────────────────────────────────────────────
        std::vector<PropertyDesc> MakePolylineProps()
        {
            using T = PolylineEntity;
            auto movePoint = [](T& e, size_t index, bool isX, double v)
            {
                Polyline pl = e.GetPolyline();
                if (index >= pl.Points.size()) return false;
                if (isX) pl.Points[index].x = v; else pl.Points[index].y = v;
                e.SetPolyline(std::move(pl));
                return true;
            };
            auto last = [](const T& e) { return e.GetPolyline().Points.empty() ? size_t(0) : e.GetPolyline().Points.size() - 1; };
            auto coord = [](const T& e, size_t i, bool isX)
            {
                const auto& pts = e.GetPolyline().Points;
                return pts.empty() ? 0.0 : (isX ? pts[i].x : pts[i].y);
            };
            return {
                ReadOnly<T>("顶点数", PropKind::Number, [](const T& e) { return static_cast<double>(e.GetPolyline().Points.size()); }),
                Coord<T>("起点 X", [=](const T& e) { return coord(e, 0, true); },        [=](T& e, double v) { movePoint(e, 0, true, v); }),
                Coord<T>("起点 Y", [=](const T& e) { return coord(e, 0, false); },       [=](T& e, double v) { movePoint(e, 0, false, v); }),
                Coord<T>("终点 X", [=](const T& e) { return coord(e, last(e), true); },  [=](T& e, double v) { movePoint(e, last(e), true, v); }),
                Coord<T>("终点 Y", [=](const T& e) { return coord(e, last(e), false); }, [=](T& e, double v) { movePoint(e, last(e), false, v); }),
                ReadOnly<T>("长度", PropKind::Number, [](const T& e) { return e.GetPolyline().Length(); }),
                Editable<T>("线宽", PropKind::Number, [](const T& e) { return e.GetWidth(); },
                            [](T& e, double v) { if (v < 0.0) return false; e.SetWidth(v); return true; }),
            };
        }

        // ── 多行文字 ──────────────────────────────────────────────────────
        std::vector<PropertyDesc> MakeMTextProps()
        {
            using T = MTextEntity;
            return {
                { "内容", PropKind::String,
                  [](const Entity& e) -> PropValue { return static_cast<const T&>(e).GetText(); },
                  [](Entity& e, const PropValue& v)
                  {
                      const auto& s = std::get<std::string>(v);
                      if (s.empty()) return false;
                      static_cast<T&>(e).SetText(s);
                      return true;
                  } },
                Coord<T>("位置 X", [](const T& e) { return e.GetPosition().x; }, [](T& e, double v) { auto p = e.GetPosition(); p.x = v; e.SetPosition(p); }),
                Coord<T>("位置 Y", [](const T& e) { return e.GetPosition().y; }, [](T& e, double v) { auto p = e.GetPosition(); p.y = v; e.SetPosition(p); }),
                Editable<T>("高度", PropKind::Number, [](const T& e) { return e.GetHeight(); },
                            [](T& e, double v) { if (v <= kEps) return false; e.SetHeight(v); return true; }),
                Editable<T>("旋转", PropKind::Angle, [](const T& e) { return Deg(e.GetRotation()); },
                            [](T& e, double v) { e.SetRotation(Rad(v)); return true; }),
                Editable<T>("框宽度", PropKind::Number, [](const T& e) { return e.GetBoxWidth(); },      // 0 = 不限宽度，不自动换行
                            [](T& e, double v) { if (v < 0.0) return false; e.SetBoxWidth(v); return true; }),
            };
        }

        // ── 块插入 ────────────────────────────────────────────────────────
        std::vector<PropertyDesc> MakeInsertProps()
        {
            using T = InsertEntity;
            auto scaleOf = [](T& e, bool isX, double v) { auto sc = e.GetScale(); if (isX) sc.x = v; else sc.y = v; e.SetScale(sc); };
            return {
                ReadOnly<T>("块名", PropKind::String, [](const T& e) { return e.GetBlockName(); }),
                Coord<T>("位置 X", [](const T& e) { return e.GetPosition().x; }, [](T& e, double v) { auto p = e.GetPosition(); p.x = v; e.SetPosition(p); }),
                Coord<T>("位置 Y", [](const T& e) { return e.GetPosition().y; }, [](T& e, double v) { auto p = e.GetPosition(); p.y = v; e.SetPosition(p); }),
                // 缩放不能为 0（负值表示镜像）
                Editable<T>("缩放 X", PropKind::Number, [](const T& e) { return e.GetScale().x; },
                            [=](T& e, double v) { if (std::abs(v) <= kEps) return false; scaleOf(e, true, v); return true; }),
                Editable<T>("缩放 Y", PropKind::Number, [](const T& e) { return e.GetScale().y; },
                            [=](T& e, double v) { if (std::abs(v) <= kEps) return false; scaleOf(e, false, v); return true; }),
                Editable<T>("旋转", PropKind::Angle, [](const T& e) { return Deg(e.GetRotation()); },
                            [](T& e, double v) { e.SetRotation(Rad(v)); return true; }),
            };
        }

        // ── 射线 / 构造线：基点 + 方向角（方向向量长度保持不变）──────────
        template<typename T, typename Origin, typename Dir, typename SetOrigin, typename SetDir>
        std::vector<PropertyDesc> MakeLinearProps(Origin origin, Dir dir, SetOrigin setOrigin, SetDir setDir)
        {
            return {
                Coord<T>("基点 X", [=](const T& e) { return origin(e).x; }, [=](T& e, double v) { auto p = origin(e); p.x = v; setOrigin(e, p); }),
                Coord<T>("基点 Y", [=](const T& e) { return origin(e).y; }, [=](T& e, double v) { auto p = origin(e); p.y = v; setOrigin(e, p); }),
                Editable<T>("角度", PropKind::Angle,
                    [=](const T& e) { const auto d = dir(e); return Deg(std::atan2(d.y, d.x)); },
                    [=](T& e, double v)
                    {
                        auto d = dir(e);
                        const double len = std::hypot(d.x, d.y);
                        if (len <= kEps) return false;
                        d.x = len * std::cos(Rad(v));
                        d.y = len * std::sin(Rad(v));
                        setDir(e, d);
                        return true;
                    }),
            };
        }

        // ── 填充 ──────────────────────────────────────────────────────────
        std::vector<PropertyDesc> MakeHatchProps()
        {
            using T = HatchEntity;
            return {
                ReadOnly<T>("图案", PropKind::String, [](const T& e) { return e.GetPattern().Name; }),
                Editable<T>("比例", PropKind::Number, [](const T& e) { return e.GetScale(); },
                            [](T& e, double v) { if (v <= kEps) return false; e.SetScale(v); return true; }),
                Editable<T>("角度", PropKind::Angle, [](const T& e) { return e.GetAngle(); },
                            [](T& e, double v) { e.SetAngle(v); return true; }),
            };
        }

        // ── 面域（全部只读）──────────────────────────────────────────────
        std::vector<PropertyDesc> MakeRegionProps()
        {
            using T = RegionEntity;
            return {
                ReadOnly<T>("面积", PropKind::Number, [](const T& e) { return e.Area(); }),
                ReadOnly<T>("周长", PropKind::Number, [](const T& e) { return e.Perimeter(); }),
            };
        }
    }

    const std::vector<PropertyDesc>& PropertiesOf(const Entity& e)
    {
        static const std::vector<PropertyDesc> kNone;
        static const std::vector<PropertyDesc> kLine   = MakeLineProps();
        static const std::vector<PropertyDesc> kCircle = MakeCircleProps();
        static const std::vector<PropertyDesc> kArc    = MakeArcProps();
        static const std::vector<PropertyDesc> kText   = MakeTextProps();
        static const std::vector<PropertyDesc> kPoint   = MakePointProps();
        static const std::vector<PropertyDesc> kRect    = MakeRectangleProps();
        static const std::vector<PropertyDesc> kEllipse = MakeEllipseProps();
        static const std::vector<PropertyDesc> kPoly    = MakePolylineProps();
        static const std::vector<PropertyDesc> kMText   = MakeMTextProps();
        static const std::vector<PropertyDesc> kInsert  = MakeInsertProps();
        static const std::vector<PropertyDesc> kHatch   = MakeHatchProps();
        static const std::vector<PropertyDesc> kRegion  = MakeRegionProps();
        static const std::vector<PropertyDesc> kRay = MakeLinearProps<RayEntity>(
            [](const RayEntity& e) { return e.GetOrigin(); }, [](const RayEntity& e) { return e.GetDirection(); },
            [](RayEntity& e, const Math::Point3& p) { e.SetOrigin(p); }, [](RayEntity& e, const Math::Vec3& d) { e.SetDirection(d); });
        static const std::vector<PropertyDesc> kXLine = MakeLinearProps<XLineEntity>(
            [](const XLineEntity& e) { return e.GetOrigin(); }, [](const XLineEntity& e) { return e.GetDirection(); },
            [](XLineEntity& e, const Math::Point3& p) { e.SetOrigin(p); }, [](XLineEntity& e, const Math::Vec3& d) { e.SetDirection(d); });

        // 派生类要先于基类判断：面域是填充的子类；图像 / 实心填充是矩形的子类；表格是多行文字的子类；
        // 擦除是多段线的子类。后四者的几何语义与基类不同，暂无描述表，不能套用基类的表
        if (e.IsKindOf<RegionEntity>())  return kRegion;
        if (e.IsKindOf<ImageEntity>() || e.IsKindOf<SolidEntity>() || e.IsKindOf<TableEntity>() || e.IsKindOf<WipeoutEntity>())
            return kNone;

        if (e.IsKindOf<LineEntity>())   return kLine;
        if (e.IsKindOf<CircleEntity>()) return kCircle;
        if (e.IsKindOf<ArcEntity>())    return kArc;
        if (e.IsKindOf<TextEntity>())   return kText;
        if (e.IsKindOf<PointEntity>())     return kPoint;
        if (e.IsKindOf<RectangleEntity>()) return kRect;
        if (e.IsKindOf<EllipseEntity>())   return kEllipse;
        if (e.IsKindOf<PolylineEntity>())  return kPoly;
        if (e.IsKindOf<MTextEntity>())     return kMText;
        if (e.IsKindOf<InsertEntity>())    return kInsert;
        if (e.IsKindOf<RayEntity>())       return kRay;
        if (e.IsKindOf<XLineEntity>())     return kXLine;
        if (e.IsKindOf<HatchEntity>())     return kHatch;
        return kNone;
    }

    bool PropValueEqual(const PropValue& a, const PropValue& b)
    {
        if (a.index() != b.index())
            return false;
        if (a.index() == 0)
        {
            const double x = std::get<double>(a), y = std::get<double>(b);
            return std::abs(x - y) <= 1e-9 * std::max(1.0, std::max(std::abs(x), std::abs(y)));
        }
        return std::get<std::string>(a) == std::get<std::string>(b);
    }

    std::vector<CommonProperty> CommonProperties(const std::vector<const Entity*>& entities)
    {
        std::vector<CommonProperty> result;
        if (entities.empty())
            return result;

        for (const PropertyDesc& d : PropertiesOf(*entities.front()))
        {
            CommonProperty cp;
            cp.name     = d.name;
            cp.kind     = d.kind;
            cp.editable = static_cast<bool>(d.set);
            cp.value    = d.get(*entities.front());

            bool inAll = true;
            for (size_t i = 1; i < entities.size() && inAll; ++i)
            {
                const PropertyDesc* other = nullptr;
                for (const PropertyDesc& o : PropertiesOf(*entities[i]))
                    if (std::string_view(o.name) == d.name && o.kind == d.kind) { other = &o; break; }
                if (!other) { inAll = false; break; }

                cp.editable = cp.editable && static_cast<bool>(other->set);
                if (cp.value && !PropValueEqual(*cp.value, other->get(*entities[i])))
                    cp.value.reset();
            }
            if (inAll)
                result.push_back(std::move(cp));
        }
        return result;
    }

    std::unique_ptr<Entity> ApplyProperty(const Entity& entity, const std::string& name, const PropValue& value)
    {
        for (const PropertyDesc& d : PropertiesOf(entity))
        {
            if (name != d.name)
                continue;
            if (!d.set)
                return nullptr;
            // 值的类型要与特性一致（数值特性不接受字符串）
            if ((d.kind == PropKind::String) != std::holds_alternative<std::string>(value))
                return nullptr;

            auto copy = entity.Clone(entity.GetID());
            if (!d.set(*copy, value))
                return nullptr;
            if (PropValueEqual(d.get(entity), d.get(*copy)))
                return nullptr;         // 没有变化
            return copy;
        }
        return nullptr;
    }
}
