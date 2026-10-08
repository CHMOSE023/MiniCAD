#include "Picking.h"
#include "Core/Log.h"
#include "Scene/Scene.h"
#include "Viewport/Viewport.h"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Math/Box2.hpp"
#include "Core/Math/MathUtils.hpp"
#include "Core/Math/Vec3.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/SolidEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/XLineEntity.hpp"
#include "Core/Entity/RayEntity.hpp"
#include "Core/Entity/Entity.hpp"
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Object/Object.hpp"
#include "Core/Math/Point2.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>
#include <Core/Math/Circle2.hpp>

namespace MiniCAD
{
    namespace
    {
        // 拾取用绘制收集器:把实体 Draw() 输出的线段 / 填充三角形收集到世界空间。
        // 凡未在 HitTest / BoxSelect 中单独特化的实体类型(Dimension /
        // Leader / MLeader / Tolerance / Hatch / Insert 等),都通过让其"绘制"到本
        // Sink、再在屏幕空间统一做距离 / 区域命中测试来支持拾取。
        // (XLine / Ray 已单独特化,见 AsXLineGeometry / ParamLineClipBox2。)
        // 这样组合型实体(标注、块引用展开的子实体)无需在拾取里重复其几何构造逻辑。
        class PickCollectSink : public IDrawSink
        {
        public:
            std::vector<std::pair<Math::Point3, Math::Point3>> Segments;
            std::vector<std::array<Math::Point3, 3>>           Triangles;

            void DrawLine(const Math::Point3& a, const Math::Point3& b,const Math::Color4&, bool) override
            {
                Segments.emplace_back(a, b);
            }
            void FillTriangle(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c, const Math::Color4&) override
            {
                Triangles.push_back({ a, b, c });
            }
            // 文本不参与拾取几何:标注 / 引线 / 公差的框线、引线本身已足够命中。
        };

        // 该实体是否已在拾取中单独特化(避免被通用回退重复处理)。
        bool HasSpecializedPick(const Object& obj)
        {
            return obj.IsKindOf<LineEntity>()      || obj.IsKindOf<RectangleEntity>()
                || obj.IsKindOf<CircleEntity>()    || obj.IsKindOf<PointEntity>()
                || obj.IsKindOf<ArcEntity>()       || obj.IsKindOf<EllipseEntity>()
                || obj.IsKindOf<PolylineEntity>()  || obj.IsKindOf<SplineEntity>()
                || obj.IsKindOf<TextEntity>()      || obj.IsKindOf<MTextEntity>()
                || obj.IsKindOf<XLineEntity>()     || obj.IsKindOf<RayEntity>();
        }

        // 取实体承载的无限直线几何(XLine 与 Ray 共用 XLine: 基点 + 方向)。
        // 非 XLine/Ray 返回 nullptr;isRay 输出该实体是否为单侧射线。
        const XLine* AsXLineGeometry(const Object& obj, bool& isRay)
        {
            if (obj.IsKindOf<XLineEntity>())
            {
                isRay = false;
                return &static_cast<const XLineEntity*>(&obj)->GetXLine();
            }
            if (obj.IsKindOf<RayEntity>())
            {
                isRay = true;
                return &static_cast<const RayEntity*>(&obj)->GetRay();
            }
            return nullptr;
        }

        // 参数化直线 p(t)=o+t*d 与轴对齐盒在 t∈[tMin,tMax] 内是否相交(Liang–Barsky)。
        //   XLine 传 tMin=-inf, tMax=+inf;Ray 传 tMin=0, tMax=+inf。
        // 约定 d 已归一化,使平行判定阈值在屏幕像素尺度下稳定。
        bool ParamLineClipBox2(const Math::Point2& o, const Math::Point2& d,
                               double xMin, double yMin, double xMax, double yMax,
                               double tMin, double tMax)
        {
            // 逐边界裁剪:p*t <= q
            auto clip = [&](double p, double q) -> bool
            {
                if (std::abs(p) < 1e-12)
                    return q >= 0.0;        // 平行于该边界:仅当起点在内侧才可能相交
                double r = q / p;
                if (p < 0.0) { if (r > tMax) return false; if (r > tMin) tMin = r; }
                else         { if (r < tMin) return false; if (r < tMax) tMax = r; }
                return true;
            };

            if (!clip(-d.x, o.x - xMin)) return false;   // x >= xMin
            if (!clip( d.x, xMax - o.x)) return false;   // x <= xMax
            if (!clip(-d.y, o.y - yMin)) return false;   // y >= yMin
            if (!clip( d.y, yMax - o.y)) return false;   // y <= yMax

            return tMin <= tMax;
        }

        // 点是否落在屏幕空间三角形内(叉积同号法)。用于实心填充(Hatch / 箭头)区域命中。
        bool PointInTriangle2(const Math::Point2& p, const Math::Point2& a,
                              const Math::Point2& b, const Math::Point2& c)
        {
            double d1 = Math::Cross(b - a, p - a);
            double d2 = Math::Cross(c - b, p - b);
            double d3 = Math::Cross(a - c, p - c);
            bool hasNeg = (d1 < 0.0) || (d2 < 0.0) || (d3 < 0.0);
            bool hasPos = (d1 > 0.0) || (d2 > 0.0) || (d3 > 0.0);
            return !(hasNeg && hasPos);
        }
    }

    // ───────────────── 绑定 ─────────────────
    void Picking::Bind(Scene& scene, Viewport& viewport)
    {
        m_scene = &scene;
        m_viewport = &viewport;
        m_indexVersion = ~0ull;   // 强制下次查询时重建索引
    }

    // ───────────────── 悬停开关 ─────────────────
    void Picking::SetHoverEnabled(bool enabled)
    {
        if (m_hoverEnabled == enabled) return;
        m_hoverEnabled = enabled;
        if (!enabled)
            m_hovered.clear();   // 禁用即清空当前悬停（下一帧不再高亮）
    }

    // ───────────────── 空间索引 ─────────────────
    void Picking::EnsureIndex()
    {
        if (!m_scene) return;
        const uint64_t v = m_scene->GeometryVersion();
        if (v == m_indexVersion) return;

        // 尚未构建 / 全局脏 / 增量退化(墓碑、溢出积累过多) → 整表重建;
        // 否则按场景脏实体集增量更新——几万实体的场景里新增/拖动一个图形
        // 只动该实体的索引项,不再每次全量重建。
        if (m_indexVersion == ~0ull || m_scene->IsGeometryAllDirty() || m_index.PreferRebuild())
        {
            m_index.Rebuild(*m_scene);
        }
        else
        {
            for (ObjectID id : m_scene->GetDirtyEntities())
                m_index.Update(id, *m_scene);
        }
        m_indexVersion = v;
    }

    AABB Picking::ScreenRectToWorldAABB(double x0, double y0, double x1, double y1) const
    {
        AABB b = AABB::Empty();
        auto& cam = m_viewport->GetCamera();

        auto add = [&](double sx, double sy)
        {
            auto w = cam.ScreenToWorld(static_cast<int>(std::lround(sx)),
                                       static_cast<int>(std::lround(sy)));
            b.Expand({ w.x, w.y, 0.0 });
        };
        // 屏幕矩形四角反投影，覆盖相机可能的旋转
        add(x0, y0); add(x1, y0); add(x1, y1); add(x0, y1);

        // z 不参与 XY 网格；置为全范围，避免实体 z 与查询 z 不一致而误剔除
        constexpr double inf = std::numeric_limits<double>::infinity();
        b.Min.z = -inf;
        b.Max.z =  inf;
        return b;
    }

    // ───────────────── 输入入口 ─────────────────
    bool Picking::OnInput(const InputEvent& e)
    {
        switch (e.Type)
        {
        case InputEventType::MouseButtonDown:
            if (e.Button == MouseButton::Left) { OnMouseDown(e); return true; }
            break;

        case InputEventType::MouseMove:
            OnMouseMove(e); return true;

        case InputEventType::MouseButtonUp:
            if (e.Button == MouseButton::Left) { OnMouseUp(e); return true; }
            break;

        case InputEventType::KeyDown:
            OnKeyDown(e); return true;

        default:
            break;
        }
        return false;
    }

    // 实体是否可被拾取:实体本身可见,且所在图层未关闭（锁定的图层仍可拾取）。
    bool Picking::IsPickable(const Object& obj) const
    {
        if (!obj.IsKindOf<Entity>()) return true;

        const auto& attr = static_cast<const Entity&>(obj).GetAttr();
        if (!attr.Visible) return false;

        if (const Layer* layer = m_scene->GetLayerManager().GetLayer(attr.LayerId))
            if (!layer->IsVisible())
                return false;       // 锁定的图层可拾取（可选中、可看特性），修改时由各编辑入口跳过

        return true;
    }

    // ───────────────── 查询接口 ─────────────────
    // 点选命中测试：返回距离最近且在阈值内的对象 ID
    Picking::ObjectID Picking::HitTest(const Math::Point2& pt, double thresh)
    {
        ObjectID best = Object::InvalidID;
        double   bestDist = std::numeric_limits<double>::max();

        auto& camera = m_viewport->GetCamera();

        // 单实体精确命中测试（含细分）。仅对空间索引筛出的候选执行。
        auto testOne = [&](const Object& obj)
            {
                if (!IsPickable(obj)) return;

                if (obj.IsKindOf<LineEntity>())
                {
                    auto line = static_cast<const LineEntity*>(&obj);
                    auto a = camera.WorldToScreen(line->GetLine().Start);
                    auto b = camera.WorldToScreen(line->GetLine().End);

                    double d = Math::Distance(pt, Math::ClosestPointOnSegment(pt, a, b));
                    if (d < thresh && d < bestDist)
                    {
                        bestDist = d;
                        best = obj.GetID();
                    }
                }

                if (obj.IsKindOf<RectangleEntity>())
                {
                    auto rectEntity = static_cast<const RectangleEntity*>(&obj);
                    auto& rect = rectEntity->GetRectangle();
                    auto p1 = camera.WorldToScreen(rect.P1);
                    auto p2 = camera.WorldToScreen(rect.P2);
                    auto p3 = camera.WorldToScreen(rect.P3);
                    auto p4 = camera.WorldToScreen(rect.P4);

                    auto testEdge = [&](const Math::Point2& a, const Math::Point2& b)
                        {
                            double d = Math::Distance(pt, Math::ClosestPointOnSegment(pt, a, b));
                            if (d < thresh && d < bestDist)
                            {
                                bestDist = d;
                                best = obj.GetID();
                            }
                        };

                    testEdge(p1, p2);
                    testEdge(p2, p3);
                    testEdge(p3, p4);
                    testEdge(p4, p1);

                    // 二维填充是实心的：落在内部也算命中
                    if (obj.IsKindOf<SolidEntity>() && bestDist > 0.0
                        && (PointInTriangle2(pt, p1, p2, p3) || PointInTriangle2(pt, p1, p3, p4)))
                    {
                        bestDist = 0.0;
                        best     = obj.GetID();
                    }
                }

                if (obj.IsKindOf<CircleEntity>())
                {
                    auto  circle = static_cast<const CircleEntity*>(&obj);
                    auto& c = circle->GetCircle();

                    // 圆心投影到屏幕
                    auto centerSS = camera.WorldToScreen(c.Center);

                    // 用圆心 +X 偏移一个半径的世界点换算屏幕半径
                    Math::Point3 edgeWorld{ c.Center.x + c.Radius, c.Center.y, c.Center.z };
                    double screenRadius = Math::Distance(centerSS, camera.WorldToScreen(edgeWorld));

                    // 点到圆环的距离 = |点到圆心距 − 屏幕半径|
                    double d = std::abs(Math::Distance(pt, centerSS) - screenRadius);
                    if (d < thresh && d < bestDist)
                    {
                        bestDist = d;
                        best = obj.GetID();
                    }
                }

                if (obj.IsKindOf<PointEntity>())
                {
                    auto point = static_cast<const PointEntity*>(&obj);
                    auto a = camera.WorldToScreen(point->GetPoint().Position);

                    double d = Math::Distance(pt, a);
                    if (d < thresh && d < bestDist)
                    {
                        bestDist = d;
                        best = obj.GetID();
                    }
                }

                // ── ArcEntity 点选 ─────────────────────────────────────────────────────
                if (obj.IsKindOf<ArcEntity>())
                {
                    auto        arcEnt = static_cast<const ArcEntity*>(&obj);
                    const auto& arc = arcEnt->GetArc();

                    // ── 屏幕空间圆心 + 屏幕半径 ──────────────────────────────────────
                    auto         centerSS = camera.WorldToScreen(arc.Center);
                    Math::Point3 edgeW = { arc.Center.x + arc.Radius, arc.Center.y, arc.Center.z };
                    double       sr = Math::Distance(centerSS, camera.WorldToScreen(edgeW));

                    // ── 点到圆心的屏幕距离 ───────────────────────────────────────────
                    double distToCenter = Math::Distance(pt, centerSS);

                    // 点到弧线的径向距离（点是否在弧所在圆环附近）
                    double radialDist = std::abs(distToCenter - sr);

                    // 径向距离超出阈值，直接跳过（不可能命中弧线）
                    // 注意：lambda 必须保持 void 返回类型，不能 `return false;`
                    // 否则 clang 会把 lambda 返回类型推导为 bool，其他分支缺少 return
                    // 在 wasm 下会被插入 `unreachable` trap。
                    if (radialDist >= thresh)
                        return;

                    // ── 关键修复：将屏幕坐标反投影到世界空间再判断角度 ──────────────
                    //
                    // 直接用屏幕空间的 atan2 与世界空间的 StartAngle/EndAngle 比较会
                    // 在摄像机有缩放/旋转时出错。
                    // 正确做法：把光标对应的"圆上最近点"反投影到世界空间，
                    // 再求该世界点相对圆心的角度，用 Arc::ContainsAngle 判断。
                    //
                    // 屏幕空间：光标在圆上的投影点
                    double screenAngle = std::atan2(pt.y - centerSS.y, pt.x - centerSS.x);
                    Math::Point2 circlePointSS =
                    {
                        centerSS.x + sr * std::cos(screenAngle),
                        centerSS.y + sr * std::sin(screenAngle)
                    };

                    // 反投影到世界空间，求世界角度
                    Math::Point3 circlePointW = camera.ScreenToWorld(circlePointSS.x, circlePointSS.y);
                    double worldAngle = std::atan2(circlePointW.y - arc.Center.y,
                        circlePointW.x - arc.Center.x);

                    if (arc.ContainsAngle(worldAngle))
                    {
                        // 光标投影落在弧段范围内：径向距离即为命中距离
                        if (radialDist < thresh && radialDist < bestDist)
                        {
                            bestDist = radialDist;
                            best = obj.GetID();
                        }
                    }
                    else
                    {
                        // 光标投影落在弧段之外：检测是否在端点附近（沿弧线方向）
                        // 使用端点在屏幕上的位置，但只有径向距离也满足时才命中，
                        // 避免在端点"外侧"误选
                        auto startSS = camera.WorldToScreen(arc.StartPoint());
                        auto endSS = camera.WorldToScreen(arc.EndPoint());

                        double dStart = Math::Distance(pt, startSS);
                        double dEnd = Math::Distance(pt, endSS);
                        double dNearest = std::min(dStart, dEnd);

                        // 端点命中：需要同时满足
                        //   1. 距端点屏幕距离 < 阈值
                        //   2. 径向距离 < 阈值（确保是沿弧线方向靠近，而不是从外侧靠近）
                        if (dNearest < thresh && radialDist < thresh && dNearest < bestDist)
                        {
                            bestDist = dNearest;
                            best = obj.GetID();
                        }
                    }

                }

                // ── EllipseEntity 点选 ────────────────────────────────────────────────
                if (obj.IsKindOf<EllipseEntity>())
                {
                    auto  ellEnt   = static_cast<const EllipseEntity*>(&obj);
                    const auto& el = ellEnt->GetEllipse();

                    // 椭圆（弧）细分为折线后，逐段判断距离（屏幕空间）
                    constexpr int kSeg = 64;
                    const double tBeg  = el.IsFull() ? 0.0 : el.StartParam;
                    const double sweep = el.SweepParam();
                    double minD = std::numeric_limits<double>::max();

                    for (int i = 0; i < kSeg; ++i)
                    {
                        double t0 = tBeg + sweep *  i      / kSeg;
                        double t1 = tBeg + sweep * (i + 1) / kSeg;

                        auto a0 = camera.WorldToScreen(el.PointAt(t0));
                        auto a1 = camera.WorldToScreen(el.PointAt(t1));

                        double d = Math::Distance(pt, Math::ClosestPointOnSegment(pt, a0, a1));
                        minD = std::min(minD, d);
                    }

                    if (minD < thresh && minD < bestDist)
                    {
                        bestDist = minD;
                        best     = obj.GetID();
                    }
                }

                // ── PolylineEntity 点选 ───────────────────────────────────────────────
                if (obj.IsKindOf<PolylineEntity>())
                {
                    auto  plEnt = static_cast<const PolylineEntity*>(&obj);
                    const auto& pl = plEnt->GetPolyline();

                    // 使用 Polyline::Tessellate 细分（含弧段），逐段检测
                    auto pts = pl.Tessellate();
                    double minD = std::numeric_limits<double>::max();

                    for (size_t i = 0; i + 1 < pts.size(); ++i)
                    {
                        auto a0 = camera.WorldToScreen(pts[i]);
                        auto a1 = camera.WorldToScreen(pts[i + 1]);

                        double d = Math::Distance(pt, Math::ClosestPointOnSegment(pt, a0, a1));
                        minD = std::min(minD, d);
                    }

                    if (minD < thresh && minD < bestDist)
                    {
                        bestDist = minD;
                        best     = obj.GetID();
                    }
                }

                // ── SplineEntity 点选 ─────────────────────────────────────────────────
                if (obj.IsKindOf<SplineEntity>())
                {
                    auto  spEnt = static_cast<const SplineEntity*>(&obj);
                    const auto& sp = spEnt->GetSpline();

                    if (sp.IsValid())
                    {
                        // 细分后逐段检测
                        auto pts = sp.Tessellate(32);
                        double minD = std::numeric_limits<double>::max();

                        for (size_t i = 0; i + 1 < pts.size(); ++i)
                        {
                            auto a0 = camera.WorldToScreen(pts[i]);
                            auto a1 = camera.WorldToScreen(pts[i + 1]);

                            double d = Math::Distance(pt, Math::ClosestPointOnSegment(pt, a0, a1));
                            minD = std::min(minD, d);
                        }

                        if (minD < thresh && minD < bestDist)
                        {
                            bestDist = minD;
                            best     = obj.GetID();
                        }
                    }
                }

                // ── TextEntity / MTextEntity 点选 ────────────────────────────────────
                // 区域型实体：点在包围盒屏幕投影内即命中，用点到中心距离竞争
                {
                    const Entity* textEnt = nullptr;
                    if      (obj.IsKindOf<TextEntity>())  textEnt = static_cast<const TextEntity*>(&obj);
                    else if (obj.IsKindOf<MTextEntity>()) textEnt = static_cast<const MTextEntity*>(&obj);

                    if (textEnt)
                    {
                        auto bbox  = textEnt->GetBoundingBox();
                        auto ss_bl = camera.WorldToScreen({ bbox.Min.x, bbox.Min.y, bbox.Min.z });
                        auto ss_tr = camera.WorldToScreen({ bbox.Max.x, bbox.Max.y, bbox.Max.z });

                        double sx0 = std::min(ss_bl.x, ss_tr.x), sy0 = std::min(ss_bl.y, ss_tr.y);
                        double sx1 = std::max(ss_bl.x, ss_tr.x), sy1 = std::max(ss_bl.y, ss_tr.y);

                        // 扩展 thresh 像素，与线段类实体手感一致
                        if (pt.x >= sx0 - thresh && pt.x <= sx1 + thresh &&
                            pt.y >= sy0 - thresh && pt.y <= sy1 + thresh)
                        {
                            Math::Point2 center = { (sx0 + sx1) * 0.5, (sy0 + sy1) * 0.5 };
                            double d = Math::Distance(pt, center);
                            if (d < bestDist)
                            {
                                bestDist = d;
                                best     = obj.GetID();
                            }
                        }
                    }
                }

                // ── XLineEntity / RayEntity 点选 ─────────────────────────────────────
                // 无限直线/射线:在屏幕空间求光标到直线(射线)的投影距离。
                // 端点在 ±1e6 处,不能用 Draw() 折线近似(投影后坐标巨大、精度差),
                // 故单独特化精确求距。
                if (bool isRay = false; const XLine* geo = AsXLineGeometry(obj, isRay))
                {
                    if (geo->IsValid())
                    {
                        auto oSS = camera.WorldToScreen(geo->Origin);
                        auto o2  = camera.WorldToScreen(geo->Origin + geo->UnitDirection());
                        Math::Point2 dSS{ o2.x - oSS.x, o2.y - oSS.y };

                        double len2 = dSS.x * dSS.x + dSS.y * dSS.y;
                        if (len2 > 1e-18)
                        {
                            double t = ((pt.x - oSS.x) * dSS.x + (pt.y - oSS.y) * dSS.y) / len2;
                            if (isRay && t < 0.0) t = 0.0;   // 射线:投影不能落在起点之前
                            Math::Point2 proj{ oSS.x + t * dSS.x, oSS.y + t * dSS.y };

                            double d = Math::Distance(pt, proj);
                            if (d < thresh && d < bestDist)
                            {
                                bestDist = d;
                                best     = obj.GetID();
                            }
                        }
                    }
                }

                // ── 通用拾取回退 ──────────────────────────────────────────────────────
                // 未单独特化的实体(Dimension / Leader / MLeader /
                // Tolerance / Hatch / Insert 等):收集其 Draw() 输出的线段与三角形,
                // 在屏幕空间统一做距离 / 区域命中测试。
                if (obj.IsKindOf<Entity>() && !HasSpecializedPick(obj))
                {
                    const auto* ent = static_cast<const Entity*>(&obj);

                    PickCollectSink sink;
                    ent->Draw(sink, false, false);

                    double minD = std::numeric_limits<double>::max();

                    for (const auto& seg : sink.Segments)
                    {
                        auto a = camera.WorldToScreen(seg.first);
                        auto b = camera.WorldToScreen(seg.second);
                        minD = std::min(minD, Math::Distance(pt, Math::ClosestPointOnSegment(pt, a, b)));
                    }

                    for (const auto& tri : sink.Triangles)
                    {
                        auto a = camera.WorldToScreen(tri[0]);
                        auto b = camera.WorldToScreen(tri[1]);
                        auto c = camera.WorldToScreen(tri[2]);

                        // 落在实心三角形内即直接命中(距离为 0)
                        if (PointInTriangle2(pt, a, b, c)) { minD = 0.0; break; }

                        // 否则按到三角形边的距离参与竞争(细线宽实体退化为线)
                        minD = std::min(minD, Math::Distance(pt, Math::ClosestPointOnSegment(pt, a, b)));
                        minD = std::min(minD, Math::Distance(pt, Math::ClosestPointOnSegment(pt, b, c)));
                        minD = std::min(minD, Math::Distance(pt, Math::ClosestPointOnSegment(pt, c, a)));
                    }

                    if (minD < thresh && minD < bestDist)
                    {
                        bestDist = minD;
                        best     = obj.GetID();
                    }
                }

            };

        // 包围盒粗筛 + 空间索引：仅对世界查询窗口内的候选做精确测试
        EnsureIndex();
        AABB q = ScreenRectToWorldAABB(pt.x - thresh, pt.y - thresh,
                                       pt.x + thresh, pt.y + thresh);
        m_index.Query(q, m_candidates);
        for (ObjectID id : m_candidates)
            if (const Object* o = m_scene->GetEntity(id))
                testOne(*o);

        if (best > 0)
        {
            LOG_TRACE("[Picking] HitTest at (%.1f, %.1f)  BestDist=%.2f  HitID=%d", pt.x, pt.y, bestDist, static_cast<int>(best));
        }
        return best;
    }

    void Picking::CollectSnapCandidates(const Math::Point2& pt, double radiusPx, std::vector<ObjectID>& out)
    {
        EnsureIndex();
        AABB q = ScreenRectToWorldAABB(pt.x - radiusPx, pt.y - radiusPx,  pt.x + radiusPx, pt.y + radiusPx);
        m_index.Query(q, out);
    }

    // 框选：返回命中的对象 ID 集合（右框全包含 / 左框触碰）
    std::unordered_set<Picking::ObjectID> Picking::BoxSelect(const Math::Point2& a, const Math::Point2& b)
    {
        double xMin = std::min(a.x, b.x);
        double xMax = std::max(a.x, b.x);
        double yMin = std::min(a.y, b.y);
        double yMax = std::max(a.y, b.y);

        Box2 box({ std::min(a.x, b.x), std::min(a.y, b.y) }, { std::max(a.x, b.x), std::max(a.y, b.y) });

        bool fullyContain = (b.x > a.x);

        auto& camera = m_viewport->GetCamera();
        std::unordered_set<ObjectID> result;

        // 单实体框选测试。仅对空间索引筛出的候选执行。
        auto testOne = [&](const Object& obj)
            {
                if (!IsPickable(obj)) return;

                if (obj.IsKindOf<PointEntity>())
                {
                    auto point = static_cast<const PointEntity*>(&obj);
                    auto s     = camera.WorldToScreen(point->GetPoint().Position);

                    if (box.Contains(s))
                    {
                        result.insert(obj.GetID());
                    }

                }

                if (obj.IsKindOf<LineEntity>())
                {
                    auto line = static_cast<const LineEntity*>(&obj);
                    auto s    = camera.WorldToScreen(line->GetLine().Start);
                    auto e    = camera.WorldToScreen(line->GetLine().End);

                    bool hit = fullyContain  ? box.Contains(s) && box.Contains(e)   
                        : Math::SegmentIntersectsBox2(s, e, box);

                    if (hit)
                        result.insert(obj.GetID());
                }

                if (obj.IsKindOf<RectangleEntity>())
                {
                    auto rectEntity = static_cast<const RectangleEntity*>(&obj);
                    auto& rect      = rectEntity->GetRectangle();

                    auto p1 = camera.WorldToScreen(rect.P1);
                    auto p2 = camera.WorldToScreen(rect.P2);
                    auto p3 = camera.WorldToScreen(rect.P3);
                    auto p4 = camera.WorldToScreen(rect.P4);

                    Point2 pts[4] = { p1, p2, p3, p4 };

                    bool hit = false;

                    if (fullyContain)
                    {
                        hit = Math::AllPointsInBox2(pts, 4, box);
                    }
                    else
                    {
                        hit =
                            Math::AnyPointsInBox2(pts, 4, box) ||
                            Math::SegmentIntersectsBox2(p1, p2, box) ||
                            Math::SegmentIntersectsBox2(p2, p3, box) ||
                            Math::SegmentIntersectsBox2(p3, p4, box) ||
                            Math::SegmentIntersectsBox2(p4, p1, box);
                    }

                    if (hit)
                    {
                        result.insert(obj.GetID());
                    }

                }

                if (obj.IsKindOf<CircleEntity>())
                {
                    auto circle = static_cast<const CircleEntity*>(&obj);
                    const auto& c = circle->GetCircle();

                    // 圆心屏幕坐标
                    auto centerSS = camera.WorldToScreen(c.Center);

                    // 用 screen-space 近似半径（避免透视误差）
                    Math::Vec3 offset = { c.Radius, 0.0, 0.0 };
                    double sr = Math::Distance(centerSS, camera.WorldToScreen(c.Center + offset));

                    bool hit = false;

                    Point2 corners[4] =
                    {
                        { xMin, yMin },
                        { xMax, yMin },
                        { xMax, yMax },
                        { xMin, yMax }
                    };

                    if (fullyContain)
                    {
                        // 屏幕包围盒完全包含圆
                        hit = (centerSS.x - sr >= xMin) && (centerSS.x + sr <= xMax) && (centerSS.y - sr >= yMin) && (centerSS.y + sr <= yMax);
                    }
                    else
                    {
                        // 一、如果选择框完全包含圆，则直接选中
                        bool boxContainsCircle =
                            (centerSS.x - sr >= xMin) &&
                            (centerSS.x + sr <= xMax) &&
                            (centerSS.y - sr >= yMin) &&
                            (centerSS.y + sr <= yMax);

                        if (boxContainsCircle)
                        {
                            hit = true;
                        }
                        else   // 二、如果选择框不完全包含圆，则满足以下任一条件即可：
                        {
                            // 1. 选择框四个角是否全部在圆内 
                            bool allInside = true;

                            for (const auto& p : corners)
                            {
                                double dx = p.x - centerSS.x;
                                double dy = p.y - centerSS.y;

                                if (dx * dx + dy * dy > sr * sr)
                                {
                                    allInside = false;
                                    break;
                                }
                            }

                            // 2.选择框在圆内 
                            if (allInside)
                            {
                                hit = false;
                                if (box.Contains(centerSS))
                                {
                                    hit = true;
                                }
                            }
                            else  // 3. 判断圆边线是否与选择框相交
                            {

                                hit = Math::CircleIntersectsBoxEdges(centerSS, sr, box);
                            }
                        }

                    }

                    if (hit)
                    {
                        result.insert(obj.GetID());
                    }
                }

                // ── ArcEntity 框选 ────────────────────────────────────────────────────
                if (obj.IsKindOf<ArcEntity>())
                {
                    auto  arcEnt = static_cast<const ArcEntity*>(&obj);
                    const auto& arc = arcEnt->GetArc();

                    auto   centerSS = camera.WorldToScreen(arc.Center);
                    Math::Point3 edgeW = { arc.Center.x + arc.Radius, arc.Center.y, arc.Center.z };
                    double sr = Math::Distance(centerSS, camera.WorldToScreen(edgeW));

                    auto startSS = camera.WorldToScreen(arc.StartPoint());
                    auto endSS = camera.WorldToScreen(arc.EndPoint());

                    bool hit = false;

                    if (fullyContain)
                    {
                        // 右向框选：弧段包围盒完全在框内
                        // 用弧的 AABB（世界空间）投影到屏幕近似判断
                        auto boundsMin = camera.WorldToScreen(
                            Math::Point3{ arc.GetBounds().Min.x, arc.GetBounds().Min.y, arc.Center.z });
                        auto boundsMax = camera.WorldToScreen(
                            Math::Point3{ arc.GetBounds().Max.x, arc.GetBounds().Max.y, arc.Center.z });

                        hit = box.Contains(boundsMin) && box.Contains(boundsMax);
                    }
                    else
                    {
                        // 左向框选：
                        // 1. 端点在框内
                        if (box.Contains(startSS) || box.Contains(endSS))
                        {
                            hit = true;
                        }
                        // 2. 圆心在框内且弧在框范围内有角度经过
                        if (!hit)
                        {
                            // 圆弧与框任意一边相交：用圆与框的相交检测 + 角度范围过滤
                            // 先检测圆是否与框相交
                            double cx2 = std::clamp(centerSS.x, xMin, xMax);
                            double cy2 = std::clamp(centerSS.y, yMin, yMax);
                            double dx = centerSS.x - cx2;
                            double dy = centerSS.y - cy2;
                            bool circleHitsBox = (dx * dx + dy * dy) <= (sr * sr);

                            if (circleHitsBox)
                            {
                                // 再用框四角和框中心检测对应角度是否在弧内
                                // 框对圆心方向中有落在弧范围内的点，则相交
                                Math::Point2 testPts[5] =
                                {
                                    { xMin, yMin }, { xMax, yMin },
                                    { xMax, yMax }, { xMin, yMax },
                                    { (xMin + xMax) * 0.5, (yMin + yMax) * 0.5 }
                                };
                                for (const auto& tp : testPts)
                                {
                                    double ang = std::atan2(tp.y - centerSS.y, tp.x - centerSS.x);
                                    if (arc.ContainsAngle(ang)) { hit = true; break; }
                                }

                                // 若框完全在圆内（所有角在弧扫过范围），也命中
                                if (!hit)
                                {
                                    bool allAnglesInArc = true;
                                    for (const auto& tp : testPts)
                                    {
                                        double ang = std::atan2(tp.y - centerSS.y, tp.x - centerSS.x);
                                        if (!arc.ContainsAngle(ang)) { allAnglesInArc = false; break; }
                                    }
                                    if (allAnglesInArc) hit = true;
                                }
                            }
                        }
                    }

                    if (hit)
                        result.insert(obj.GetID());
                }

                // ── EllipseEntity 框选 ────────────────────────────────────────────────
                if (obj.IsKindOf<EllipseEntity>())
                {
                    auto  ellEnt   = static_cast<const EllipseEntity*>(&obj);
                    const auto& el = ellEnt->GetEllipse();

                    // 细分椭圆（弧）为折线，复用折线框选逻辑
                    constexpr int kSeg = 64;
                    const double tBeg  = el.IsFull() ? 0.0 : el.StartParam;
                    const double sweep = el.SweepParam();
                    std::vector<Math::Point2> screenPts;
                    screenPts.reserve(kSeg + 1);

                    for (int i = 0; i <= kSeg; ++i)
                    {
                        double t = tBeg + sweep * i / kSeg;
                        screenPts.push_back(camera.WorldToScreen(el.PointAt(t)));
                    }

                    bool hit = false;

                    if (fullyContain)
                    {
                        // 所有采样点在框内
                        hit = true;
                        for (const auto& sp : screenPts)
                            if (!box.Contains(sp)) { hit = false; break; }
                    }
                    else
                    {
                        // 任一采样点在框内，或相邻段与框相交
                        for (size_t i = 0; i + 1 < screenPts.size() && !hit; ++i)
                        {
                            if (box.Contains(screenPts[i]))
                            {
                                hit = true;
                            }
                            else if (Math::SegmentIntersectsBox2(screenPts[i], screenPts[i + 1], box))
                            {
                                hit = true;
                            }
                        }

                        // 椭圆完全包含选择框：用框中心检测
                        if (!hit)
                        {
                            Math::Point2 boxCenter = { (xMin + xMax) * 0.5, (yMin + yMax) * 0.5 };
                            // 把框中心变换到椭圆局部坐标，判断是否在椭圆内
                            auto centerSS = camera.WorldToScreen(el.Center);
                            // 近似：若框中心到椭圆中心距离小于最小屏幕半径，则在椭圆内
                            // 精确判断：世界空间转换
                            auto worldCenter = camera.ScreenToWorld(boxCenter.x, boxCenter.y);
                            if (el.DistanceToPoint(worldCenter) < Math::LengthEPS * 100)
                                hit = true;
                        }
                    }

                    if (hit)
                        result.insert(obj.GetID());
                }

                // ── PolylineEntity 框选 ───────────────────────────────────────────────
                if (obj.IsKindOf<PolylineEntity>())
                {
                    auto  plEnt = static_cast<const PolylineEntity*>(&obj);
                    const auto& pl = plEnt->GetPolyline();

                    auto worldPts = pl.Tessellate();

                    // 投影到屏幕
                    std::vector<Math::Point2> screenPts;
                    screenPts.reserve(worldPts.size());
                    for (const auto& wp : worldPts)
                        screenPts.push_back(camera.WorldToScreen(wp));

                    bool hit = false;

                    if (fullyContain)
                    {
                        // 所有顶点都在框内
                        hit = true;
                        for (const auto& sp : screenPts)
                            if (!box.Contains(sp)) { hit = false; break; }
                    }
                    else
                    {
                        // 任一顶点在框内，或任一段与框相交
                        for (size_t i = 0; i + 1 < screenPts.size() && !hit; ++i)
                        {
                            if (box.Contains(screenPts[i]))
                                hit = true;
                            else if (Math::SegmentIntersectsBox2(screenPts[i], screenPts[i + 1], box))
                                hit = true;
                        }
                        // 检查最后一个顶点
                        if (!hit && !screenPts.empty() && box.Contains(screenPts.back()))
                            hit = true;
                    }

                    if (hit)
                        result.insert(obj.GetID());
                }

                // ── SplineEntity 框选 ─────────────────────────────────────────────────
                if (obj.IsKindOf<SplineEntity>())
                {
                    auto  spEnt = static_cast<const SplineEntity*>(&obj);
                    const auto& sp = spEnt->GetSpline();

                    if (!sp.IsValid()) return;

                    auto worldPts = sp.Tessellate(32);

                    std::vector<Math::Point2> screenPts;
                    screenPts.reserve(worldPts.size());
                    for (const auto& wp : worldPts)
                        screenPts.push_back(camera.WorldToScreen(wp));

                    bool hit = false;

                    if (fullyContain)
                    {
                        // 所有拟合点在框内（用拟合点而不是细分点，与 AutoCAD 行为一致）
                        hit = true;
                        for (const auto& fp : sp.FitPoints)
                        {
                            auto ss = camera.WorldToScreen(fp);
                            if (!box.Contains(ss)) { hit = false; break; }
                        }
                    }
                    else
                    {
                        // 任一细分段顶点在框内，或与框相交
                        for (size_t i = 0; i + 1 < screenPts.size() && !hit; ++i)
                        {
                            if (box.Contains(screenPts[i]))
                                hit = true;
                            else if (Math::SegmentIntersectsBox2(screenPts[i], screenPts[i + 1], box))
                                hit = true;
                        }
                        if (!hit && !screenPts.empty() && box.Contains(screenPts.back()))
                            hit = true;
                    }

                    if (hit)
                        result.insert(obj.GetID());
                }

                // ── TextEntity / MTextEntity 框选 ────────────────────────────────────
                {
                    const Entity* textEnt = nullptr;
                    if      (obj.IsKindOf<TextEntity>())  textEnt = static_cast<const TextEntity*>(&obj);
                    else if (obj.IsKindOf<MTextEntity>()) textEnt = static_cast<const MTextEntity*>(&obj);

                    if (textEnt)
                    {
                        auto bbox  = textEnt->GetBoundingBox();
                        // 包围盒4角（世界→屏幕）
                        Math::Point2 corners[4] = {
                            camera.WorldToScreen({ bbox.Min.x, bbox.Min.y, bbox.Min.z }),
                            camera.WorldToScreen({ bbox.Max.x, bbox.Min.y, bbox.Min.z }),
                            camera.WorldToScreen({ bbox.Max.x, bbox.Max.y, bbox.Min.z }),
                            camera.WorldToScreen({ bbox.Min.x, bbox.Max.y, bbox.Min.z }),
                        };

                        bool hit = false;
                        if (fullyContain)
                        {
                            hit = Math::AllPointsInBox2(corners, 4, box);
                        }
                        else
                        {
                            hit = Math::AnyPointsInBox2(corners, 4, box)         ||
                                  Math::SegmentIntersectsBox2(corners[0], corners[1], box) ||
                                  Math::SegmentIntersectsBox2(corners[1], corners[2], box) ||
                                  Math::SegmentIntersectsBox2(corners[2], corners[3], box) ||
                                  Math::SegmentIntersectsBox2(corners[3], corners[0], box);
                        }

                        if (hit)
                            result.insert(obj.GetID());
                    }
                }

                // ── XLineEntity / RayEntity 框选 ─────────────────────────────────────
                // 无限直线/射线向无穷延伸,永远不可能被窗口(全包含)选中;
                // 仅交叉框选:用参数化直线/射线裁剪选择框,判断是否穿过框区域。
                if (bool isRay = false; const XLine* geo = AsXLineGeometry(obj, isRay))
                {
                    if (geo->IsValid() && !fullyContain)
                    {
                        auto oSS = camera.WorldToScreen(geo->Origin);
                        auto o2  = camera.WorldToScreen(geo->Origin + geo->UnitDirection());
                        Math::Point2 dSS{ o2.x - oSS.x, o2.y - oSS.y };

                        // 归一化屏幕方向,保证 Liang–Barsky 平行判定阈值稳定
                        double dl = std::hypot(dSS.x, dSS.y);
                        if (dl > 1e-9)
                        {
                            dSS.x /= dl; dSS.y /= dl;

                            constexpr double inf = std::numeric_limits<double>::infinity();
                            double tMin = isRay ? 0.0 : -inf;   // 射线只向 +Direction 延伸

                            if (ParamLineClipBox2(oSS, dSS, xMin, yMin, xMax, yMax, tMin, inf))
                                result.insert(obj.GetID());
                        }
                    }
                }

                // ── 通用框选回退 ──────────────────────────────────────────────────────
                // 未单独特化的实体:收集其 Draw() 输出的线段(三角形拆为三条边),
                // 投影到屏幕空间。右框(fullyContain)要求全部顶点在框内;左框(crossing)
                // 任一段触碰框即命中。
                if (obj.IsKindOf<Entity>() && !HasSpecializedPick(obj))
                {
                    const auto* ent = static_cast<const Entity*>(&obj);

                    PickCollectSink sink;
                    ent->Draw(sink, false, false);

                    std::vector<std::pair<Math::Point2, Math::Point2>> segs;
                    auto addSeg = [&](const Math::Point3& wa, const Math::Point3& wb)
                    { segs.emplace_back(camera.WorldToScreen(wa), camera.WorldToScreen(wb)); };

                    for (const auto& s : sink.Segments)  addSeg(s.first, s.second);
                    for (const auto& t : sink.Triangles)
                    {
                        addSeg(t[0], t[1]);
                        addSeg(t[1], t[2]);
                        addSeg(t[2], t[0]);
                    }

                    if (!segs.empty())
                    {
                        bool hit;
                        if (fullyContain)
                        {
                            hit = true;
                            for (const auto& s : segs)
                                if (!box.Contains(s.first) || !box.Contains(s.second)) { hit = false; break; }
                        }
                        else
                        {
                            hit = false;
                            for (const auto& s : segs)
                                if (box.Contains(s.first) || box.Contains(s.second) ||
                                    Math::SegmentIntersectsBox2(s.first, s.second, box)) { hit = true; break; }
                        }

                        if (hit)
                            result.insert(obj.GetID());
                    }
                }

            };

        // 包围盒粗筛 + 空间索引：仅对与选择框相交的候选做精确测试
        EnsureIndex();
        AABB q = ScreenRectToWorldAABB(a.x, a.y, b.x, b.y);
        m_index.Query(q, m_candidates);
        for (ObjectID id : m_candidates)
            if (const Object* o = m_scene->GetEntity(id))
                testOne(*o);

        return result;
    }


    void Picking::RestoreLastSelection()
    {
        if (m_lastSelection.empty()) return;
        m_selection = m_lastSelection;
        MarkDirty();
    }

    // ───────────────── 输入处理 ─────────────────
    void Picking::OnMouseDown(const InputEvent& e)
    {
        m_drag = DragState::Pressing;
        m_pressX = e.MouseX;
        m_pressY = e.MouseY;
        m_currX = e.MouseX;
        m_currY = e.MouseY;
    }

    void Picking::OnMouseMove(const InputEvent& e)
    {
        m_currX = e.MouseX;
        m_currY = e.MouseY;

        if (m_drag == DragState::Pressing)
        {
            int dx = e.MouseX - m_pressX;
            int dy = e.MouseY - m_pressY;
            if (std::abs(dx) > DRAG_THRESH || std::abs(dy) > DRAG_THRESH)
                m_drag = DragState::BoxSelecting;
        }

        if (m_drag != DragState::BoxSelecting)
            UpdateHovered(e);
    }

    void Picking::OnMouseUp(const InputEvent& e)
    {
        if (m_drag == DragState::Pressing)
            DoPointPick(e);
        else if (m_drag == DragState::BoxSelecting)
            DoBoxPick(e);

        m_drag = DragState::Idle;
    }

    void Picking::OnKeyDown(const InputEvent& e)
    {
        if (e.IsCancel())
        {
            if (!m_selection.empty())
            {
                m_selection.clear();
                MarkDirty();
            }
        }
    }

    // ───────────────── 核心逻辑 ─────────────────

    void Picking::UpdateHovered(const InputEvent& e)
    {
        // 悬停禁用：不做命中测试，保持无悬停
        if (!m_hoverEnabled)
        {
            m_hovered.clear();
            return;
        }

        Math::Point2 pt{ (double)e.MouseX, (double)e.MouseY };
        ObjectID id = HitTest(pt, HOVER_THRESH);

        // 注意：悬停变化不再 MarkDirty —— 悬停高亮改由 Editor 每帧以 overlay 重建，
        // 不触发整场景顶点重建（鼠标移动驱动的重绘已足够刷新高亮）。
        if (id == Object::InvalidID)
        {
            m_hovered.clear();
            return;
        }

        if (m_hovered.size() == 1 && *m_hovered.begin() == id)
            return;

        m_hovered.clear();
        m_hovered.insert(id);
    }

    void Picking::DoPointPick(const InputEvent& e)
    {
        bool ctrl = e.HasModifier(ModifierKey::Ctrl);
        bool alt = e.HasModifier(ModifierKey::Alt);
        bool shift = e.HasModifier(ModifierKey::Shift);

        // 复用悬停结果：悬停启用时，UpdateHovered 已在最近一次鼠标移动算出光标下实体，
        // 直接拿来作为命中对象（“看到高亮的就是点中的”），省掉这里再做一次全场景命中测试。
        // 悬停的 HOVER_THRESH ≥ PICK_THRESH，故无悬停即光标下无可选实体。
        // 悬停禁用时才回退到即时 HitTest。
        ObjectID id;
        if (m_hoverEnabled)
            id = m_hovered.empty() ? Object::InvalidID : *m_hovered.begin();
        else
        {
            Math::Point2 pt{ (double)e.MouseX, (double)e.MouseY };
            id = HitTest(pt, PICK_THRESH);
        }

        std::unordered_set<ObjectID> newSel = m_selection;

        if (id == Object::InvalidID)
        {
            if (!ctrl && !alt && !shift) newSel.clear();
        }
        else
        {
            if (alt)
            {
                // Alt：添加到选择集
                newSel.insert(id);
            }
            else if (shift)
            {
                // Shift：从选择集移除
                newSel.erase(id);
            }
            else if (ctrl)
            {
                // Ctrl：切换
                if (newSel.contains(id)) newSel.erase(id);
                else                     newSel.insert(id);
            }
            else
            {
                // 无修饰键：替换
                newSel = { id };
            }
        }

        if (!SetEquals(newSel, m_selection))
        {
            m_lastSelection = m_selection; // 保存上次
            m_selection = std::move(newSel);
            MarkDirty();
        }
    }

    void Picking::DoBoxPick(const InputEvent& e)
    {
        bool ctrl  = e.HasModifier(ModifierKey::Ctrl);
        bool alt   = e.HasModifier(ModifierKey::Alt);
        bool shift = e.HasModifier(ModifierKey::Shift);

        Math::Point2 a{ (double)m_pressX, (double)m_pressY };
        Math::Point2 b{ (double)e.MouseX, (double)e.MouseY };

        auto result = BoxSelect(a, b);

        std::unordered_set<ObjectID> newSel;
        
        if (alt)
        {
            // Alt：添加到选择集
            newSel = m_selection;
            newSel.insert(result.begin(), result.end());
        }
        else if (shift)
        {
            // Shift：从选择集移除
            newSel = m_selection;
            for (const auto& id : result)
                newSel.erase(id);
        }
        else if (ctrl)
        {
            // Ctrl：添加到选择集（与原来保持一致）
            newSel = m_selection;
            newSel.insert(result.begin(), result.end());
        }
        else
        {
            // 无修饰键：替换
            newSel = std::move(result);
        }

        if (!SetEquals(newSel, m_selection))
        {
            m_lastSelection = m_selection;  // 保存上次
            m_selection = std::move(newSel);
            MarkDirty();
        }
    }

    // ───────────────── 工具实现 ─────────────────

    template<typename T>
    bool Picking::SetEquals(const std::unordered_set<T>& a, const std::unordered_set<T>& b)
    {
        if (a.size() != b.size()) return false;
        for (const auto& v : a)
            if (!b.contains(v)) return false;
        return true;
    }

    // ───────────────── 辅助接口 ─────────────────

    Math::Point2 Picking::GetBoxStart()    const { return { (double)m_pressX, (double)m_pressY }; }
    Math::Point2 Picking::GetBoxEnd()      const { return { (double)m_currX,  (double)m_currY }; }
    bool         Picking::IsBoxSelecting() const { return m_drag == DragState::BoxSelecting; }

}
