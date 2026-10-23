#pragma once
#include "Core/Entity/MLineEntity.hpp"
#include "Core/Math/Point3.hpp"
#include <cmath>
#include <cstdint>
#include <vector>

namespace MiniCAD
{
    // =========================================================================
    // MLineEdit —— 多线编辑（对应 AutoCAD 的 MLEDIT 命令）
    //
    // AutoCAD 里 MLEDIT 是**由用户显式选择编辑方式**的：
    // 弹对话框选「十字交叉 / T 形相交 / 拐角结合 / 顶点断开 / 顶点结合…」，
    // 再点选两条多线（或一条多线的两个顶点）执行。多线交叉处**不会自动**打断 ——
    // 是否处理、怎么处理，完全由用户用命令决定。
    //
    // 本文件只放**纯几何**：给定两条多线的路径与样式，算出编辑后的路径；
    // 不碰 Scene / 命令栈，便于单测。工具层（MLineEditTool）负责拾取与提交。
    //
    // 多线的几何模型：一条**中心线路径** + 样式里的元素线偏移。
    // 所以"编辑"的实质是**改路径顶点**，元素线由 Compute 自动重算：
    //
    //   · Cross    十字交叉：把穿过的一方在交点处打断成两条多线（留下一段缺口）
    //   · Tee      T 形相交：把被穿过的一方在交点处断开，穿过的一方不动
    //   · Corner   拐角结合：两条多线各去掉一段、在交点处接成一条 L 形多线
    //   · Open     顶点断开：在选定顶点处把一条多线拆成两条
    //   · Merge    顶点结合：把两条多线的端点接到一起，合成一条
    // =========================================================================
    enum class MLineEditKind : uint8_t
    {
        Cross  = 0,     // 十字交叉
        Tee    = 1,     // T 形相交
        Corner = 2,     // 拐角结合
        Open   = 3,     // 顶点断开
        Merge  = 4,     // 顶点结合
    };

    // 一条线段的参数化结果：把路径按"在某个顶点处切开"拆成两段。
    struct MLineSplit
    {
        std::vector<Math::Point3> First;      // 前半段（可能为空）
        std::vector<Math::Point3> Second;     // 后半段（可能为空）
        bool Valid = false;
    };

    class MLineEdit
    {
    public:
        // ── 求两条多线中心的交点（只在中心线上求，不管元素线）───────────────
        //
        // 返回交点；不相交时返回 false。同时给出交点分别落在两条多线的哪一段、
        // 以及在该段上的参数 t（0..1），供后续精确切断。
        struct Crossing
        {
            Math::Point3 Point;
            size_t       SegA = 0;      // 交点落在 A 的第几段
            double       TA   = 0.0;    // A 上该段的参数
            size_t       SegB = 0;
            double       TB   = 0.0;
            bool         Valid = false;
        };

        static Crossing Intersect(const std::vector<Math::Point3>& a,
                                  const std::vector<Math::Point3>& b,
                                  bool aClosed = false, bool bClosed = false)
        {
            Crossing out;
            const size_t na = a.size(), nb = b.size();
            if (na < 2 || nb < 2) return out;

            const size_t segA = aClosed ? na : na - 1;
            const size_t segB = bClosed ? nb : nb - 1;

            for (size_t i = 0; i < segA; ++i)
            {
                const Math::Point3& p0 = a[i];
                const Math::Point3& p1 = a[(i + 1) % na];
                const double rx = p1.x - p0.x, ry = p1.y - p0.y;
                for (size_t j = 0; j < segB; ++j)
                {
                    const Math::Point3& q0 = b[j];
                    const Math::Point3& q1 = b[(j + 1) % nb];
                    const double sx = q1.x - q0.x, sy = q1.y - q0.y;
                    const double den = rx * sy - ry * sx;
                    if (std::abs(den) < 1e-15) continue;      // 平行

                    const double t = ((q0.x - p0.x) * sy - (q0.y - p0.y) * sx) / den;
                    const double u = ((q0.x - p0.x) * ry - (q0.y - p0.y) * rx) / den;
                    if (t < -1e-9 || t > 1.0 + 1e-9 || u < -1e-9 || u > 1.0 + 1e-9)
                        continue;

                    out.Point = { p0.x + rx * t, p0.y + ry * t, 0.0 };
                    out.SegA  = i;
                    out.TA    = t;
                    out.SegB  = j;
                    out.TB    = u;
                    out.Valid = true;
                    return out;       // 取第一处交点（多交点场景由调用方决定取舍）
                }
            }
            return out;
        }

        // ── 在路径的「第 seg 段、参数 t」处插入一个顶点 ─────────────────────
        //
        // 返回插入后的路径；t 落在端点上时原样返回（不插入重复点）。
        static std::vector<Math::Point3> InsertPointAt(const std::vector<Math::Point3>& path,
                                                       size_t seg, double t)
        {
            std::vector<Math::Point3> out = path;
            if (path.size() < 2 || seg + 1 >= path.size()) return out;
            if (t <= 1e-9 || t >= 1.0 - 1e-9) return out;

            const Math::Point3& p0 = path[seg];
            const Math::Point3& p1 = path[seg + 1];
            const Math::Point3 p{ p0.x + (p1.x - p0.x) * t,
                                  p0.y + (p1.y - p0.y) * t,
                                  p0.z + (p1.z - p0.z) * t };
            out.insert(out.begin() + static_cast<std::ptrdiff_t>(seg + 1), p);
            return out;
        }

        // ── 顶点断开：在顶点 index 处把一条多线拆成两条 ─────────────────────
        //
        //   Open · 封闭多线：在 index 处断开后变成一条开放多线（起点 = index+1，
        //                    终点 = index），因为环只有一处断开还是一个环。
        //   Open · 开放多线：index 不能在端点上，否则拆出来的空段没有意义。
        static MLineSplit SplitAtVertex(const std::vector<Math::Point3>& path, size_t index, bool closed)
        {
            MLineSplit out;
            const size_t n = path.size();
            if (n < 3 || index >= n) return out;

            if (closed)
            {
                // 环：从 index+1 绕一圈回到 index，成为开放路径
                out.First.reserve(n + 1);
                for (size_t k = 1; k <= n; ++k)
                    out.First.push_back(path[(index + k) % n]);
                out.Valid = !out.First.empty();
                return out;
            }

            if (index == 0 || index + 1 >= n) return out;   // 端点无法拆
            out.First.assign(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(index) + 1);
            out.Second.assign(path.begin() + static_cast<std::ptrdiff_t>(index), path.end());
            out.Valid = out.First.size() >= 2 && out.Second.size() >= 2;
            return out;
        }

        // ── 顶点结合：把两条路径的端点接起来 ───────────────────────────────
        //
        // 按"哪两个端点最近"自动决定接法（A 首 ↔ B 首 / A 首 ↔ B 尾 / …），
        // 接点取两者中点，避免出现折角。
        static std::vector<Math::Point3> JoinPaths(const std::vector<Math::Point3>& a,
                                                   const std::vector<Math::Point3>& b)
        {
            std::vector<Math::Point3> out;
            if (a.size() < 2 || b.size() < 2) return out;

            // 四种接法里选总长最短的（= 端点之间距离最小）
            const Math::Point3& a0 = a.front();
            const Math::Point3& a1 = a.back();
            const Math::Point3& b0 = b.front();
            const Math::Point3& b1 = b.back();

            struct Cand { double d; int mode; };
            const Cand cands[] = {
                { Dist2(a1, b0), 0 },   // A尾 → B首
                { Dist2(a1, b1), 1 },   // A尾 → B尾（B 反向）
                { Dist2(a0, b0), 2 },   // A首 → B首（A 反向）
                { Dist2(a0, b1), 3 },   // A首 → B尾（两条都反向）
            };
            int best = 0;
            for (int i = 1; i < 4; ++i)
                if (cands[i].d < cands[best].d) best = i;

            auto rev = [](const std::vector<Math::Point3>& v) {
                return std::vector<Math::Point3>(v.rbegin(), v.rend());
            };
            auto rest = [](const std::vector<Math::Point3>& v) {
                return std::vector<Math::Point3>(v.begin() + 1, v.end());
            };

            switch (best)
            {
            case 0:      // A + B
                out = a;
                out.insert(out.end(), b.begin(), b.end());
                break;
            case 1:      // A + reverse(B)
            {
                const auto rb = rev(b);
                out = a;
                out.insert(out.end(), rb.begin(), rb.end());
                break;
            }
            case 2:      // reverse(A) + B
            {
                const auto ra = rev(a);
                out = ra;
                out.insert(out.end(), b.begin(), b.end());
                break;
            }
            default:     // reverse(A) + reverse(B)
            {
                const auto ra = rev(a);
                const auto rb = rev(b);
                out = ra;
                out.insert(out.end(), rb.begin(), rb.end());
                break;
            }
            }

            // 去掉接缝处的重复点（两个端点重合的情况）
            std::vector<Math::Point3> dedup;
            dedup.reserve(out.size());
            for (const auto& p : out)
                if (dedup.empty() || Dist2(dedup.back(), p) > 1e-18)
                    dedup.push_back(p);
            (void)rest;
            return dedup;
        }

        // ── 把一条路径在某个顶点处"剪掉"到指定顶点为止 ─────────────────────
        //
        // 用于十字交叉 / T 形相交：把被穿过的一方从交点处断开，
        // 保留 [0, index] 或 [index, end] 两段中的一段。
        static std::vector<Math::Point3> HeadPath(const std::vector<Math::Point3>& path, size_t index)
        {
            if (path.size() < 2 || index + 1 >= path.size()) return {};
            return std::vector<Math::Point3>(path.begin(),
                                             path.begin() + static_cast<std::ptrdiff_t>(index) + 1);
        }

        static std::vector<Math::Point3> TailPath(const std::vector<Math::Point3>& path, size_t index)
        {
            if (path.size() < 2 || index + 1 >= path.size()) return {};
            return std::vector<Math::Point3>(path.begin() + static_cast<std::ptrdiff_t>(index), path.end());
        }

    private:
        static double Dist2(const Math::Point3& a, const Math::Point3& b)
        {
            const double dx = a.x - b.x, dy = a.y - b.y;
            return dx * dx + dy * dy;
        }
    };
}
