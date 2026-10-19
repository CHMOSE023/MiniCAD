// ── 多段线凸度（bulge）方向约定：必须与 DXF 组码 42 一致 ────────────────────────────
//
// v0.2.0 的 `Polyline::ComputeArc` 把圆心放在弦的**另一侧**、扫掠方向也相反，
// 整条弧沿弦镜像 ⇒ DWG/DXF 读进来的凸度多段线画在错误的一边。
// 这与 V0.1.0 的同一缺陷（其 `BulgeDirectionTests.cpp` 记录：test0.dwg 句柄 2D51C、
// 图层 STAIR 的 LWPOLYLINE 就是这个症状）是同一类问题，在 v0.2.0 里复发了。
//
// 权威约定（DXF 组码 42）：bulge = tan(包角/4)，**起点到终点顺时针时取负**。
//   · bulge > 0 ⇒ 逆时针(CCW) ⇒ 圆心在 A→B **左侧**，弧向行进方向**右侧**鼓出
//   · bulge < 0 ⇒ 顺时针(CW)  ⇒ 圆心在 A→B 右侧，  弧向行进方向**左侧**鼓出
// 判据（半圆，规范唯一确定）：A=(0,0) → B=(2,0) 时 bulge=+1 过 (1,-1)、bulge=-1 过 (1,+1)。
//
// 同一套约定已被 `SHXVM::EmitBulgeArc`（R39 用 gbenor.shx 字形实渲染验证）使用；
// 本文件把 `Polyline` 一侧也钉在同一约定上。
#include "Core/GeomKernel/Polyline.hpp"
#include "Core/GeomKernel/PolylineOps.hpp"
#include "Import/CadExchange.h"
#include "Scene/Scene.h"
#include "Core/Entity/PolylineEntity.hpp"
#include "Editor/Tools/PolylineTool.h"
#include "Editor/Grip/PolylineGripHandler.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace MiniCAD;

namespace
{
    int g_fail = 0;
    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok) ++g_fail;
    }
    bool Near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }
    bool NearP(const Math::Point3& p, double x, double y, double eps = 1e-9)
    {
        return Near(p.x, x, eps) && Near(p.y, y, eps);
    }

    // 弧中点（按圆心角取中点）
    Math::Point3 ArcMid(const ArcGeom& a)
    {
        const double m = a.StartAngle + a.SweepAngle * 0.5;
        return { a.Center.x + a.Radius * std::cos(m), a.Center.y + a.Radius * std::sin(m), 0.0 };
    }
}

int RunBulgeDirectionTests()
{
    g_fail = 0;
    std::printf("── 多段线凸度方向（DXF 组码 42）──\n");

    const Math::Point3 A{ 0, 0, 0 };
    const Math::Point3 B{ 2, 0, 0 };

    // ── 1. 半圆判据（规范唯一确定）────────────────────────────────────────
    {
        const ArcGeom p = Polyline::ComputeArc(A, B, +1.0);
        std::printf("    bulge=+1：圆心=(%.3f,%.3f) 包角=%.3f 弧中点=(%.3f,%.3f)\n",
                    p.Center.x, p.Center.y, p.SweepAngle, ArcMid(p).x, ArcMid(p).y);
        Check(p.SweepAngle > 0.0, "**bulge=+1 包角为正（CCW）**");
        Check(NearP(ArcMid(p), 1, -1, 1e-6), "**bulge=+1 的弧过 (1,-1)（向右鼓）**");
        Check(NearP(p.Center, 1, 0, 1e-9), "bulge=+1 的圆心在 (1,0)");

        const ArcGeom n = Polyline::ComputeArc(A, B, -1.0);
        std::printf("    bulge=-1：圆心=(%.3f,%.3f) 包角=%.3f 弧中点=(%.3f,%.3f)\n",
                    n.Center.x, n.Center.y, n.SweepAngle, ArcMid(n).x, ArcMid(n).y);
        Check(n.SweepAngle < 0.0, "bulge=-1 包角为负（CW）");
        Check(NearP(ArcMid(n), 1, 1, 1e-6), "**bulge=-1 的弧过 (1,+1)（向左鼓）**");
    }

    // ── 2. 非半圆时的圆心侧（两个方向都要查，避免只对半圆成立）──────────────
    {
        const double b = std::tan(Math::PI / 8.0);      // 90° 弧
        const ArcGeom pos = Polyline::ComputeArc(A, B, +b);
        Check(NearP(pos.Center, 1, 1, 1e-9), "**bulge>0 的圆心在 A→B 左侧 (1,+1)**");

        const ArcGeom neg = Polyline::ComputeArc(A, B, -b);
        Check(NearP(neg.Center, 1, -1, 1e-9), "bulge<0 的圆心在 A→B 右侧 (1,-1)");
    }

    // ── 3. 渲染真正走的细分路径与参数化取点 ─────────────────────────────────
    {
        std::vector<Math::Point3> dense;
        Polyline::TessellateArc(A, B, 1.0, dense, 0.01);
        Check(!dense.empty() && dense[dense.size() / 2].y < 0.0,
              "**TessellateArc 的细分点落在下半圆（不给镜像结果）**");

        const Polyline pl({ { 0, 0, 0 }, { 2, 0, 0 } }, { 1.0 });
        Check(NearP(PolylineOps::PointAt(pl, 0.5), 1, -1, 1e-6),
              "PointAt(0.5) 与 ComputeArc 同侧");
    }

    // ── 4. 走真实导入器：DXF 里的正凸度必须画出向右鼓 ───────────────────────
    //   夹具：一条两点多段线 (0,0) → (10,0)，组码 42 = 1（半圆）。
    //   按规范弧应过 (5,-5)。
    {
        const std::string dxf =
            "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n0\nENDSEC\n"
            "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n1\n"
            "0\nLAYER\n2\n0\n70\n0\n62\n7\n6\nCONTINUOUS\n0\nENDTAB\n0\nENDSEC\n"
            "0\nSECTION\n2\nENTITIES\n"
            "0\nLWPOLYLINE\n5\nBG\n8\n0\n100\nAcDbPolyline\n90\n2\n70\n0\n"
            "10\n0.0\n20\n0.0\n42\n1.0\n"
            "10\n10.0\n20\n0.0\n42\n0.0\n"
            "0\nENDSEC\n0\nEOF\n";
        Scene scene;
        std::string err;
        const bool ok = ImportCad(std::vector<std::uint8_t>(dxf.begin(), dxf.end()), scene, &err, nullptr);
        Check(ok, "含正凸度的 LWPOLYLINE 能导入");
        if (ok)
        {
            const PolylineEntity* ent = nullptr;
            scene.ForEachObject([&](const Object& o)
            {
                if (const auto* p = dynamic_cast<const PolylineEntity*>(&o)) ent = p;
            });
            Check(ent != nullptr, "找到导入的多段线");
            if (ent)
            {
                std::printf("    导入后 bulge = %.4f，PointAt(0.5) = (%.3f, %.3f)\n",
                            ent->GetPolyline().SegBulge(0),
                            PolylineOps::PointAt(ent->GetPolyline(), 0.5).x,
                            PolylineOps::PointAt(ent->GetPolyline(), 0.5).y);
                Check(Near(ent->GetPolyline().SegBulge(0), 1.0), "**组码 42 原样导入（不做取反）**");
                Check(NearP(PolylineOps::PointAt(ent->GetPolyline(), 0.5), 5, -5, 1e-6),
                      "**导入的正凸度弧过 (5,-5)：向右鼓（修前会镜像到 +5）**");
            }
        }
    }

    // ── 5. 三个点求凸度必须与 ComputeArc 自洽（工具侧的反向函数）────────────
    {
        // 过 (0,0)、(1,-1)、(2,0) 的圆弧：中点在下侧 ⇒ 凸度应为正
        const double b = Polyline::BulgeFromTangent({ 0, 0, 0 }, { 2, 0, 0 }, { 0, -1, 0 });
        const ArcGeom g = Polyline::ComputeArc({ 0, 0, 0 }, { 2, 0, 0 }, b);
        Check(b > 0.0, "**起点切向朝下 ⇒ 凸度为正（与 ComputeArc 同一约定）**");
        Check(Near(g.SweepAngle, Math::PI, 1e-6), "还原出的弧是半圆");
        Check(NearP(ArcMid(g), 1, -1, 1e-6), "还原出的弧仍过 (1,-1)");
    }

    // ── 6. 非半圆：BulgeFromTangent 必须还原出**同一条**弧 ────────────────────
    //
    // ⚠ 上面第 5 节用的是半圆。半圆时圆心恰好落在弦上（side == 0），
    //   「圆心在右 ⇒ 正」与「圆心在右 ⇒ 负」两种符号给出的 |bulge| 都是 1，
    //   于是符号写反了也测不出来。必须用非半圆。
    {
        const Math::Point3 A{ 0, 0, 0 }, B{ 2, 0, 0 };
        // 圆心在弦右侧的劣弧：bulge = -0.4
        const ArcGeom truth = Polyline::ComputeArc(A, B, -0.4);
        // 取该弧在 A 处的真实切向（沿行进方向）
        const Math::Point3 T{ -std::sin(truth.StartAngle), std::cos(truth.StartAngle), 0.0 };
        const double b = Polyline::BulgeFromTangent(A, B, T);
        const ArcGeom got = Polyline::ComputeArc(A, B, b);
        std::printf("    非半圆：bulge=%.4f，圆心=(%.3f,%.3f)，切向=(%.3f,%.3f)\n",
                    b, truth.Center.x, truth.Center.y, T.x, T.y);
        Check(Near(b, -0.4, 1e-9), "**非半圆：切向还原出的凸度为 -0.4（圆心在右 ⇒ 负）**");
        Check(NearP(got.Center, truth.Center.x, truth.Center.y, 1e-9),
              "**非半圆：还原出的圆心与真值一致（修前会到弦的另一侧）**");
        Check(Near(got.SweepAngle, truth.SweepAngle, 1e-9),
              "**非半圆：还原出的包角与真值一致**");
    }
    {
        const Math::Point3 A{ 0, 0, 0 }, B{ 2, 0, 0 };
        // 圆心在弦左侧的劣弧：bulge = +0.4
        const ArcGeom truth = Polyline::ComputeArc(A, B, 0.4);
        const Math::Point3 T{ -std::sin(truth.StartAngle), std::cos(truth.StartAngle), 0.0 };
        const double b = Polyline::BulgeFromTangent(A, B, T);
        const ArcGeom got = Polyline::ComputeArc(A, B, b);
        Check(Near(b, 0.4, 1e-9), "**非半圆：切向还原出的凸度为 +0.4（圆心在左 ⇒ 正）**");
        Check(NearP(got.Center, truth.Center.x, truth.Center.y, 1e-9),
              "**非半圆：圆心在左时同样还原正确**");
    }

    // ── 7. 三点弧必须经过中间点（工具真正使用的函数）────────────────────────
    //
    // PolylineTool 画圆弧、PolylineGripHandler 拖弧中点夹点，用的都是
    // ComputeBulgeFromThreePoints(A, M, B)。它与 ComputeArc 必须是同一套约定，
    // 否则画出的弧不经过用户点、预览同样如此。
    {
        const Math::Point3 A{ 0, 0, 0 }, B{ 2, 0, 0 }, M{ 1, -0.5, 0 };
        const double b = PolylineTool::ComputeBulgeFromThreePoints(A, M, B);
        const ArcGeom g = Polyline::ComputeArc(A, B, b);
        std::printf("    三点弧 M=(1,-0.5)：bulge = %.4f，弧中点 = (%.4f, %.4f)\n",
                    b, ArcMid(g).x, ArcMid(g).y);
        Check(b > 0.0, "**三点弧：中点在弦下方 ⇒ 凸度为正**");
        Check(NearP(ArcMid(g), 1, -0.5, 1e-6), "**画出的弧经过中间点 (1,-0.5)**");
        Check(!Near(std::abs(b), 1.0, 1e-9), "该例不是半圆（符号判据有效）");
    }
    {
        const Math::Point3 A{ 0, 0, 0 }, B{ 2, 0, 0 }, M{ 1, 0.5, 0 };
        const double b = PolylineTool::ComputeBulgeFromThreePoints(A, M, B);
        const ArcGeom g = Polyline::ComputeArc(A, B, b);
        Check(b < 0.0, "**三点弧：中点在弦上方 ⇒ 凸度为负**");
        Check(NearP(ArcMid(g), 1, 0.5, 1e-6), "**画出的弧经过中间点 (1,+0.5)**");
    }
    {
        // 非半圆 + 非对称：A=(0,0) → B=(4,0)，M 在 (1,-0.5)
        const Math::Point3 A{ 0, 0, 0 }, B{ 4, 0, 0 }, M{ 1, -0.5, 0 };
        const double b = PolylineTool::ComputeBulgeFromThreePoints(A, M, B);
        const ArcGeom g = Polyline::ComputeArc(A, B, b);
        // M 在圆上的参数
        const double am = std::atan2(M.y - g.Center.y, M.x - g.Center.x);
        double t = (am - g.StartAngle) / g.SweepAngle;
        while (t < 0.0) t += 1.0;
        while (t > 1.0) t -= 1.0;
        const Math::Point3 p = ArcMid(ArcGeom{ g.Center, g.Radius,
                                               g.StartAngle + g.SweepAngle * t, 0.0 });
        std::printf("    三点弧 A=(0,0) B=(4,0) M=(1,-0.5)：bulge = %.4f, t(M) = %.4f\n", b, t);
        Check(b > 0.0, "**非对称三点弧：中点在弦下方 ⇒ 凸度为正**");
        Check(NearP(p, 1, -0.5, 1e-6), "**非对称三点弧经过 M**");
    }
    {
        // 旋转坐标系：A=(0,0) → B=(0,2)，M=(0.5,1)（弦右侧）
        const Math::Point3 A{ 0, 0, 0 }, B{ 0, 2, 0 }, M{ 0.5, 1, 0 };
        const double b = PolylineTool::ComputeBulgeFromThreePoints(A, M, B);
        const ArcGeom g = Polyline::ComputeArc(A, B, b);
        Check(NearP(ArcMid(g), 0.5, 1, 1e-6), "**三点弧与坐标系无关（旋转后仍经过 M）**");
    }
    {
        // 交换起终点 ⇒ 同一条弧，凸度取反
        const Math::Point3 A{ 0, 0, 0 }, B{ 2, 0, 0 }, M{ 1, -0.5, 0 };
        const double f = PolylineTool::ComputeBulgeFromThreePoints(A, M, B);
        const double r = PolylineTool::ComputeBulgeFromThreePoints(B, M, A);
        Check(Near(f, -r, 1e-9), "**三点弧：交换起终点后凸度取反（同一条弧）**");
    }
    {
        // PolylineGripHandler 里那份拷贝必须给同样的结果
        const Math::Point3 A{ 0, 0, 0 }, B{ 2, 0, 0 }, M{ 1, -0.5, 0 };
        const double tool = PolylineTool::ComputeBulgeFromThreePoints(A, M, B);
        const double grip = PolylineGripHandler::ComputeBulgeFromThreePoints(A, M, B);
        Check(Near(tool, grip, 1e-12),
              "**PolylineGripHandler 与 PolylineTool 的三点求凸度结果一致**");
    }

    std::printf("凸度方向用例失败：%d\n", g_fail);
    return g_fail;
}
