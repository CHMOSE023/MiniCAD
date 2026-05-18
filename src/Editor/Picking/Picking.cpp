#include "Picking.h"
#include "Scene/Scene.h"
#include "Editor/Viewport/Viewport.h"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Math/Box2.hpp"
#include "Core/Math/MathUtils.hpp"
#include "Core/Math/Vec3.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Core/Object/Object.hpp"
#include "Core/Math/Point2.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <Core/Math/Circle2.hpp>

namespace MiniCAD
{
    // ───────────────── 构造 ─────────────────
    Picking::Picking(Scene& scene, Viewport& viewport) : m_scene(scene), m_viewport(viewport) {}

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

    // ───────────────── 查询接口 ─────────────────
    // 点选命中测试：返回距离最近且在阈值内的对象 ID
    Picking::ObjectID Picking::HitTest(const Math::Point2& pt, double thresh)
    {
        ObjectID best = Object::InvalidID;
        double   bestDist = std::numeric_limits<double>::max();

        auto& camera = m_viewport.GetCamera();

        m_scene.ForEachObject([&](const Object& obj)
            {
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
                    if (radialDist >= thresh)
                        return false;   // ForEachObject 用 lambda，此处用 goto / return false 替代

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

                    // 椭圆细分为折线后，逐段判断距离（屏幕空间）
                    constexpr int kSeg = 64;
                    double minD = std::numeric_limits<double>::max();

                    for (int i = 0; i < kSeg; ++i)
                    {
                        double t0 = Math::TwoPI *  i      / kSeg;
                        double t1 = Math::TwoPI * (i + 1) / kSeg;

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

            });

        if (best > 0)
        {
            printf("[Picking] HitTest at (%.1f, %.1f)  BestDist=%.2f  HitID=%d\n", pt.x, pt.y, bestDist, static_cast<int>(best));
        }
        return best;
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

        auto& camera = m_viewport.GetCamera();
        std::unordered_set<ObjectID> result;

        m_scene.ForEachObject([&](const Object& obj)
            {
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

                    // 细分椭圆为折线，复用折线框选逻辑
                    constexpr int kSeg = 64;
                    std::vector<Math::Point2> screenPts;
                    screenPts.reserve(kSeg + 1);

                    for (int i = 0; i <= kSeg; ++i)
                    {
                        double t = Math::TwoPI * i / kSeg;
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
            });
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
        Math::Point2 pt{ (double)e.MouseX, (double)e.MouseY };
        ObjectID id = HitTest(pt, HOVER_THRESH);

        if (id == Object::InvalidID)
        {
            if (!m_hovered.empty())
            {
                m_hovered.clear();
                MarkDirty();
            }
            return;
        }

        if (m_hovered.size() == 1 && *m_hovered.begin() == id)
            return;

        m_hovered.clear();
        m_hovered.insert(id);
        MarkDirty();
    }

    void Picking::DoPointPick(const InputEvent& e)
    {
        bool ctrl = e.HasModifier(ModifierKey::Ctrl);
        bool alt = e.HasModifier(ModifierKey::Alt);
        bool shift = e.HasModifier(ModifierKey::Shift);

        Math::Point2 pt{ (double)e.MouseX, (double)e.MouseY };
        ObjectID id = HitTest(pt, PICK_THRESH);

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
