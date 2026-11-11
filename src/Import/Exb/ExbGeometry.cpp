#include "Import/Exb/ExbGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace MiniCAD::Exb
{
namespace
{
    // ── 小端读取（不依赖主机字节序）──────────────────────────────────────
    std::uint32_t ReadU32(const std::uint8_t* p)
    {
        return static_cast<std::uint32_t>(p[0])
             | (static_cast<std::uint32_t>(p[1]) << 8)
             | (static_cast<std::uint32_t>(p[2]) << 16)
             | (static_cast<std::uint32_t>(p[3]) << 24);
    }

    float ReadF32(const std::uint8_t* p)
    {
        const std::uint32_t u = ReadU32(p);
        float f = 0.0f;
        std::memcpy(&f, &u, sizeof(f));
        return f;
    }

    double ReadF64(const std::uint8_t* p)
    {
        const std::uint64_t u = static_cast<std::uint64_t>(ReadU32(p))
                              | (static_cast<std::uint64_t>(ReadU32(p + 4)) << 32);
        double d = 0.0;
        std::memcpy(&d, &u, sizeof(d));
        return d;
    }

    std::uint16_t ReadU16(const std::uint8_t* p)
    {
        return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
    }

    // 坐标合理性判据：有限、量级不超过 1e7、且不是非规格化小量。
    //
    // 不设"图纸范围"下限：空白图第一点可能是 (1000, 2000)，写死 3000 以上的
    // 窗口在小图纸上会整片失败，而本图 y 低到 −11680，窗口会整片漏掉。
    // 但必须排除 4.17e-317 这类非规格化浮点 —— 它们是字节错位读出的垃圾，
    // 在记录里成片出现，不排除会被当成坐标。
    bool PlausibleCoord(double v)
    {
        if (!std::isfinite(v) || std::fabs(v) >= 1.0e7) return false;
        return v == 0.0 || std::fabs(v) > 1.0e-6;
    }

    // 一个点不能两轴都为 0：折线数组之后紧跟 (0,0,0,1.0) 填充，
    // 不排除的话每条折线都会多读一个"到原点的假顶点"，渲染时全图汇聚一点。
    bool PlausiblePoint(double x, double y)
    {
        return PlausibleCoord(x) && PlausibleCoord(y) &&
               (std::fabs(x) > 1.0e-9 || std::fabs(y) > 1.0e-9);
    }

    // 记录头至少 47 字节（GenA 实测 47、GenC 75），几何不可能出现在这之前
    constexpr std::size_t kMinGeometryRel = 40;

    constexpr double kTwoPi = 6.283185307179586;

    bool AllowedTextChar(char32_t c)
    {
        if (c == 0) return true;
        if (c >= 0x20 && c <= 0x7E) return true;
        if (c >= 0x4E00 && c <= 0x9FFF) return true;   // CJK 统一汉字
        if (c >= 0x3000 && c <= 0x303F) return true;   // CJK 标点
        if (c >= 0xFF00 && c <= 0xFFEF) return true;   // 全角
        if (c >= 0x00B0 && c <= 0x00FF) return true;   // °、±、×
        if (c >= 0x2000 && c <= 0x206F) return true;   // 通用标点
        return false;
    }

    bool DecodeUtf16Le(const std::uint8_t* p, std::size_t bytes, std::string& out, bool& allAllowed)
    {
        out.clear();
        allAllowed = true;
        std::size_t i = 0;
        while (i + 1 < bytes)
        {
            char32_t c = static_cast<char32_t>(p[i] | (p[i + 1] << 8));
            i += 2;
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < bytes)
            {
                const char32_t lo = static_cast<char32_t>(p[i] | (p[i + 1] << 8));
                if (lo >= 0xDC00 && lo <= 0xDFFF)
                {
                    c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                    i += 2;
                }
            }
            if (!AllowedTextChar(c)) allAllowed = false;
            if (c == 0) break;                     // 终止符
            if (c < 0x80) out.push_back(static_cast<char>(c));
            else if (c < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (c >> 6)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
            else if (c < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (c >> 12)));
                out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (c >> 18)));
                out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
        }
        return !out.empty();
    }

    // ── 记录边界 ─────────────────────────────────────────────────────────
    struct Mark
    {
        std::size_t   Pos = 0;
        std::uint32_t Seq = 0;
    };

    // 记录头 = [u32 序号][u32 0xFFFFFFFB]。一次性扫出全部 `FB FF FF FF`，
    // 取其前 4 字节当序号——比"每条记录都重新搜索一遍"快两个数量级。
    //
    // 不在这里按序号取值范围过滤：旧代小文件的首条记录那 4 字节不是序号（实测
    // 0xF11A0531 之类的值），过滤掉就一条记录都剩不下。范围判断交给下面的链式检测。
    void CollectMarks(const std::uint8_t* d, std::size_t size, std::vector<Mark>& marks)
    {
        for (std::size_t q = 4; q + 4 <= size; ++q)
        {
            if (d[q] == 0xFB && d[q + 1] == 0xFF && d[q + 2] == 0xFF && d[q + 3] == 0xFF)
                marks.push_back(Mark{q - 4, ReadU32(d + q - 4)});
        }
    }

    // ── 结构识别 ─────────────────────────────────────────────────────────
    struct Span
    {
        std::size_t Begin = 0;   // 记录起点
        std::size_t End   = 0;   // 记录终点（下一条记录起点）
    };

    bool MatchText(const std::uint8_t* d, const Span& r, ExbGeometryItem& item, std::uint32_t maxBytes = 600)
    {
        for (std::size_t o = r.Begin + kMinGeometryRel; o + 12 <= r.End; ++o)
        {
            const std::uint32_t n = ReadU32(d + o);
            if (n < 4 || n > maxBytes || (n & 1u) || o + 4 + n > r.End) continue;
            if (o + 4 + n + 88 > r.End) continue;

            const std::size_t t = o + 4 + n;
            std::string text;
            bool allowed = false;
            if (!DecodeUtf16Le(d + o + 4, n, text, allowed)) continue;
            if (!allowed || text.empty()) continue;
            if (d[o + 4 + n - 2] != 0 || d[o + 4 + n - 1] != 0) continue;   // 必须以 NUL 结束

            // 字高在串尾 +0（与同图 DWG 的 TEXT / MTEXT 字高逐条相符）；+8 / +16 是文字块的总高与宽，单行文字为 0
            const double height = ReadF64(d + t);
            if (!(height > 0.01 && height < 1000.0)) continue;

            const double x = ReadF64(d + t + 53);
            const double y = ReadF64(d + t + 61);
            if (!PlausibleCoord(x) || !PlausibleCoord(y)) continue;

            // 旋转角在字符串尾 +24（与 height 的 +8、插入点的 +53/+61 同一相对布局，
            // 因此不随格式代变化）。取值实测只有 11 种，且精确落在 0 / π/2 / 3π/2：
            //   0°   398 条（图纸主体文字）
            //   90°  183 条
            //   270° 126 条
            //   其余 29 条为 77°~103°（沿轮廓方向的标注）
            // 五版分布逐条相同，是稳定字段，不是浮点巧合。
            double rotation = 0.0;
            if (t + 32 <= r.End)
            {
                const double v = ReadF64(d + t + 24);
                constexpr double kTwoPiLocal = 6.28318530717958647692;
                if (v >= 0.0 && v < kTwoPiLocal) rotation = v;
            }

            item.Kind     = ExbGeometryKind::Text;
            item.Raw      = text;
            item.Plain    = StripTextCodes(text);
            item.Insert   = ExbPoint2{x, y};
            item.Height   = height;
            item.Rotation = rotation;
            return true;
        }
        return false;
    }

    bool MatchPolyline(const std::uint8_t* d, const Span& r, ExbGeometryItem& item)
    {
        for (std::size_t o = r.Begin + kMinGeometryRel; o + 64 <= r.End; ++o)
        {
            const double x = ReadF64(d + o);
            const double y = ReadF64(d + o + 8);
            if (!PlausiblePoint(x, y)) continue;
            if (ReadF64(d + o + 16) != 0.0 || ReadF64(d + o + 24) != 1.0) continue;

            std::vector<ExbPoint2> pts;
            pts.push_back(ExbPoint2{x, y});
            std::size_t q = o + 32;
            while (q + 32 <= r.End)
            {
                const double nx = ReadF64(d + q);
                const double ny = ReadF64(d + q + 8);
                if (!PlausiblePoint(nx, ny)) break;
                if (ReadF64(d + q + 16) != 0.0 || ReadF64(d + q + 24) != 1.0) break;
                pts.push_back(ExbPoint2{nx, ny});
                q += 32;
            }
            if (pts.size() < 2) continue;
            double peak = 0.0;
            double minX = pts[0].X, maxX = pts[0].X;
            double minY = pts[0].Y, maxY = pts[0].Y;
            double step = 0.0;
            for (std::size_t k = 0; k < pts.size(); ++k)
            {
                const ExbPoint2& p = pts[k];
                peak = std::max(peak, std::max(std::fabs(p.X), std::fabs(p.Y)));
                minX = std::min(minX, p.X);  maxX = std::max(maxX, p.X);
                minY = std::min(minY, p.Y);  maxY = std::max(maxY, p.Y);
                if (k > 0) step += std::hypot(p.X - pts[k - 1].X, p.Y - pts[k - 1].Y);
            }
            if (peak <= 1.0) continue;                 // 纯 0/1 常数序列，不是几何

            // 尾部长度判据：
            //   普通折线/直线的数组之后只有十几到几十字节，上限 300 足够。
            //   但样条 / 轮廓曲线在顶点数组之后还会跟一段**节点向量**（knot vector，
            //   16 字节步长、成对重复、单调不减，实测最长 12 KB），于是整批被拒，
            //   这些曲线就一直缺失（本图 127 条，占 0.76%）。
            //   这类记录的顶点是**密集采样**出来的：相邻点平均间距只有图形自身尺寸的
            //   3.5%（中位），直接当折线画，形状误差远小于线宽，肉眼无损。
            //   因此用「采样密度」当放行条件，而不是去解析节点向量：
            //   点足够多 + 平均步长远小于包围盒 → 确系密集采样的曲线，接受。
            if ((r.End - q) > 300)
            {
                const double ext = std::max(maxX - minX, maxY - minY);
                const double avg = step / static_cast<double>(pts.size() - 1);
                if (pts.size() < 3 || !(ext > 0.0) || avg / ext > 0.15) continue;
            }

            item.Kind   = ExbGeometryKind::Polyline;
            item.Points = std::move(pts);
            return true;
        }
        return false;
    }

    // 有理二次 B 样条（NURBS）表示的圆 / 圆弧。
    //
    // CAXA 对一部分圆和弧并不存「圆心 + 半径」，而是存 NURBS：控制点仍是 32 字节
    // 一组 (x, y, 0.0, w)，但第 4 个字段不是 1.0，而是 1.0 与 √2/2 交替 —— 这是
    // 「四段 90° 圆弧拼成整圆」的标准权重配置（w = cos(θ/2)，θ = 90° → √2/2）。
    // 之前的 MatchPolyline 死要求 w == 1.0，于是这些记录在第 2 个控制点就断链，
    // 退化成一个点，最终落到 MatchCurve 的单点兜底 → 全图多了 300 个落在原点的假点，
    // 而真正的那批圆和弧一个都没画出来（实测本图 300 条，1.8%）。
    //
    // 判据不依赖节点向量存在哪里，只靠几何自洽：
    //   对 w = (1, √2/2, 1) 的 90° 弧，圆心**恰为** p0 + p2 − p1。
    //   验证：单位四分之一圆 p0=(1,0) p1=(1,1) p2=(0,1)
    //         → (1,0) + (0,1) − (1,1) = (0,0)，正是圆心。
    //   因此每三个控制点都能独立算出一个圆心；只有全部重合才认（本图 127/140 条
    //   的圆心偏差 < 1e-6·r，半径偏差恒为 0.000，其余 13 条本就不同圆，自然被拒）。
    bool MatchRationalArc(const std::uint8_t* d, const Span& r, ExbGeometryItem& item)
    {
        for (std::size_t o = r.Begin + kMinGeometryRel; o + 32 <= r.End; ++o)
        {
            if (!PlausiblePoint(ReadF64(d + o), ReadF64(d + o + 8))) continue;
            if (ReadF64(d + o + 16) != 0.0) continue;
            if (ReadF64(d + o + 24) != 1.0) continue;          // 序列以 w=1 开头

            std::vector<ExbPoint2> pts;
            std::vector<double>    ws;
            std::size_t q = o;
            while (q + 32 <= r.End)
            {
                const double x = ReadF64(d + q);
                const double y = ReadF64(d + q + 8);
                const double z = ReadF64(d + q + 16);
                const double w = ReadF64(d + q + 24);
                if (!PlausiblePoint(x, y) || z != 0.0) break;
                if (!(w > 0.05 && w <= 1.0)) break;
                pts.push_back(ExbPoint2{x, y});
                ws.push_back(w);
                q += 32;
            }
            if (pts.size() < 3 || (pts.size() % 2) != 1) continue;

            // 端点权重恒为 1，中间权重全部相同（同一角度分段的圆弧）。
            // 中间权重 w = cos(θ/2)，于是每段圆心角 θ = 2·acos(w)：
            //   w = √2/2 → 90°（整圆要 4 段，本图 127 条）
            //   w = 0.5   → 120°（3 段成一圈）
            // 实测还有 37°~85° 等各种角度（共 75 条），因此不能写死 90°。
            const double wm = ws[1];
            if (!(wm > 0.05 && wm < 1.0 - 1.0e-9)) continue;
            bool uniform = true;
            for (std::size_t k = 0; k < ws.size() && uniform; ++k)
                uniform = (k % 2 == 0) ? (ws[k] == 1.0)
                                       : (std::fabs(ws[k] - wm) < 1.0e-6);
            if (!uniform) continue;

            // 圆心：设 m = (P0+P2)/2，P1 为切线交点，可推得
            //   P1 − c = (m − c)/w²   →   c = (m − w²·P1)/(1 − w²)
            // 对 w=√2/2 化简即 c = P0 + P2 − P1（前一轮用的就是这个特例）。
            const double k2  = wm * wm;
            const double den = 1.0 - k2;
            if (!(den > 1.0e-6)) continue;                     // w→1 退化成直线段

            const std::size_t segs = (ws.size() - 1) / 2;
            double cx = 0.0, cy = 0.0;
            std::vector<ExbPoint2> centres;
            centres.reserve(segs);
            for (std::size_t k = 0; k < segs; ++k)
            {
                const ExbPoint2& a = pts[2 * k];
                const ExbPoint2& m = pts[2 * k + 1];
                const ExbPoint2& b = pts[2 * k + 2];
                const double mx = 0.5 * (a.X + b.X);
                const double my = 0.5 * (a.Y + b.Y);
                const ExbPoint2 c{(mx - k2 * m.X) / den, (my - k2 * m.Y) / den};
                centres.push_back(c);
                cx += c.X; cy += c.Y;
            }
            cx /= static_cast<double>(segs);
            cy /= static_cast<double>(segs);

            const double radius = std::hypot(pts[0].X - cx, pts[0].Y - cy);
            if (!(radius > 0.05 && radius < 1.0e6)) continue;

            double dev = 0.0;
            for (const ExbPoint2& c : centres)
                dev = std::max(dev, std::hypot(c.X - cx, c.Y - cy));
            if (dev > radius * 1.0e-3) continue;               // 圆心不重合 → 不是同一个圆

            const double segAngle = 2.0 * std::acos(wm);
            const double total    = static_cast<double>(segs) * segAngle;

            item.Center = ExbPoint2{cx, cy};
            item.Radius = radius;
            if (std::fabs(total - kTwoPi) < 1.0e-4)            // 首尾闭合 = 整圆
            {
                item.Kind = ExbGeometryKind::Circle;
                return true;
            }

            // 不到一圈：一段圆弧（本图 98 条）或多段的部分弧。
            // 中间控制点位于圆心→弧中点的射线上，用它判定绕向；圆弧无方向，
            // 顺时针时把起终点对调即可，几何不变。
            auto norm2pi = [](double a)
            {
                while (a < 0.0) a += kTwoPi;
                while (a >= kTwoPi) a -= kTwoPi;
                return a;
            };
            constexpr double kPi = 3.141592653589793;
            const double a0 = norm2pi(std::atan2(pts[0].Y - cy, pts[0].X - cx));
            const double aN = norm2pi(std::atan2(pts.back().Y - cy, pts.back().X - cx));
            const double am = norm2pi(std::atan2(pts[1].Y - cy, pts[1].X - cx));
            const bool ccw  = norm2pi(am - a0) < kPi;

            item.Kind       = ExbGeometryKind::Arc;
            item.StartAngle = ccw ? a0 : aN;
            if (item.StartAngle >= kTwoPi - 1.0e-9) item.StartAngle = 0.0;
            item.EndAngle   = item.StartAngle + total;
            return true;
        }
        return false;
    }

    // 旧代记录的头部自校验：
    //   rel 8    : u8  实体类型（100 = 折线，实测 pl3/pl10 都是 100）
    //   rel 9    : u8  可选「扩展块」标志。为 1 时紧跟 24 字节，构成为
    //                  [01][u32 1][16 B 样式对象 GUID][u8 0][u16 0]
    //                  —— 只要实体被显式改过属性（实测：把线宽 0.18 改成 0.25），
    //                  CAXA 就会写出这一块，指向 StyleStream 里的格式对象。
    //   rel 10(+24): u16 「本记录剩余长度」= 记录跨度 − 14 − 扩展块
    // 后者的等式在 GenA/GenB/GenC 大文件里从未成立（否则 record span 会撞上），
    // 因此可以放心用它当"这是旧代布局"的开关，避免 40 字节顶点数组在常规记录上误命中。
    bool IsLegacyRecord(const std::uint8_t* d, const Span& r, std::size_t& headerExtra)
    {
        const std::size_t span = r.End - r.Begin;
        headerExtra = 0;
        if (span < 24 || span > 0x10000) return false;

        // 先按"带扩展块"试，再退回"不带"。flag 为 1 但长度对不上时不能直接放弃，
        // 否则一个恰好的 0/1 字节会让整条记录失去几何。
        for (const std::size_t ext : {std::size_t{24}, std::size_t{0}})
        {
            if (span < 14 + ext) continue;
            if (d[r.Begin + 9] != (ext ? 1 : 0)) continue;
            if (ReadU16(d + r.Begin + 10 + ext) == static_cast<std::uint16_t>(span - 14 - ext))
            {
                headerExtra = ext;
                return true;
            }
        }
        return false;
    }

    // 旧代布局的顶点数组：[u32 n][n × 40 B (f64 x, f64 y, 0.0, 0.0, 0.0)]
    //
    // 与 24/32 字节那套的区别只在步长与尾部填充，但两者混在同一个记录里无法用几何偏移
    // 区分，所以靠"后 24 字节必须全 0"这个强特征单独识别。数组之后只允许很短尾巴
    // （实测 28 B），否则说明这不是顶点数组而是巧合。
    bool MatchVertexArray(const std::uint8_t* d, const Span& r, ExbGeometryItem& item)
    {
        constexpr std::size_t kStride  = 40;
        constexpr std::size_t kMaxCount = 4096;

        for (std::size_t o = r.Begin + kMinGeometryRel; o + 4 + 2 * kStride <= r.End; ++o)
        {
            const std::uint32_t n = ReadU32(d + o);
            if (n < 2 || n > kMaxCount) continue;
            const std::size_t need = 4 + static_cast<std::size_t>(n) * kStride;
            if (o + need > r.End) continue;
            if ((r.End - (o + need)) > 300) continue;

            std::vector<ExbPoint2> pts;
            pts.reserve(n);
            bool ok = true;
            for (std::uint32_t i = 0; i < n && ok; ++i)
            {
                const std::size_t b = o + 4 + static_cast<std::size_t>(i) * kStride;
                const double px = ReadF64(d + b);
                const double py = ReadF64(d + b + 8);
                if (!PlausiblePoint(px, py)) { ok = false; break; }
                if (ReadF64(d + b + 16) != 0.0 || ReadF64(d + b + 24) != 0.0 ||
                    ReadF64(d + b + 32) != 0.0) { ok = false; break; }
                pts.push_back(ExbPoint2{px, py});
            }
            if (!ok) continue;

            item.Kind   = (n == 2) ? ExbGeometryKind::Line : ExbGeometryKind::Polyline;
            item.Points = std::move(pts);
            return true;
        }
        return false;
    }

    // 曲线类：同一条记录里的第一个点就可能属于「圆 / 多点线 / 孤立点」三种之一，
    // 因此必须在同一个循环里按「圆 → 多点 → 单点」分支判断。分成两个函数会让
    // 单点分支把直线的首点吃掉，结果直线数变 0、点数暴涨（踩过）。
    //
    // 🔴 单点必须是**兜底**而不是"第一个命中就返回"（2026-11-11 修）：
    //   3800 条带 `CtrlFlag` 属性的记录里，属性区尾部有一段参数
    //   `f32 1.0 | f64 0.0 | i32 −1 | f64 0.1 | f64 0.0 | f64 0.0`，
    //   它前面的 (0.1, 0) 完全满足 PlausiblePoint，于是整条记录被提前判成
    //   "孤立点 (0.1, 0)"，真正的几何（在它之后 60 多字节处）全部丢失。
    //   实测本图 425 条记录因此变假点，其中 3680/3800 条后面确有真实多点几何。
    //   改成"强判据（圆/多点）扫完整条记录，都不中才退回首个候选点"。
    bool MatchCurve(const std::uint8_t* d, const Span& r, ExbGeometryItem& item, bool allowPoint = true)
    {
        constexpr std::size_t kMaxTrailer = 300;   // 几何数组之后紧跟定长尾部

        bool     haveFallback = false;             // 单点兜底候选（取第一个）
        ExbPoint2 fallback{};

        for (std::size_t o = r.Begin + kMinGeometryRel; o + 24 <= r.End; ++o)
        {
            const double x = ReadF64(d + o);
            const double y = ReadF64(d + o + 8);
            if (!PlausiblePoint(x, y)) continue;
            if (ReadF64(d + o + 16) != 0.0) continue;

            // ① 圆 / 圆弧：单点之后紧跟 (半径, cos起始角, sin起始角)，记录末尾还有扫过角
            //
            // 起始角以方向余弦形式存：+32 = cos、+40 = sin（实测整圆是 (1.0, 0.0)）。
            // 因此**不能**用「+32 == 1.0」当判据——那是"起始角为 0"的特例，会把起始角
            // 非零的弧整批漏成孤立点（本图 26 条弧里漏了 20 条）。改成单位向量判据。
            if (o + 48 <= r.End && (r.End - (o + 40)) <= kMaxTrailer)
            {
                const double radius = ReadF64(d + o + 24);
                const double cosA   = ReadF64(d + o + 32);
                const double sinA   = ReadF64(d + o + 40);
                const double norm   = cosA * cosA + sinA * sinA;
                // 半径上限放宽到 1e6：本图有 r≈1.4 万 / 1.8 万的浅弧，卡 5000 会漏掉。
                // 误判风险由下面的单位向量 + 尾部长度两个判据承担，不靠半径上限。
                if (radius > 0.05 && radius < 1.0e6 &&
                    cosA >= -1.001 && cosA <= 1.001 &&
                    sinA >= -1.001 && sinA <= 1.001 &&
                    std::fabs(norm - 1.0) < 1.0e-3)
                {
                    item.Center = ExbPoint2{x, y};
                    item.Radius = radius;
                    item.Kind   = ExbGeometryKind::Circle;

                    // +80 = 扫过角（不是绝对终止角）。整圆实测恰为 2π；断圆的第二段是
                    // 「起始角 + 扫过角 = 2π」的补弧，两者相加填满一圈——已用 26 条弧核对。
                    if (o + 88 <= r.End)
                    {
                        const double sweep = ReadF64(d + o + 80);
                        if (sweep > 0.0 && sweep < kTwoPi - 1.0e-9)
                        {
                            double start = std::atan2(sinA, cosA);
                            if (start < 0.0) start += kTwoPi;
                            if (start >= kTwoPi - 1.0e-9) start = 0.0;   // 2π 与 0 同一处
                            item.Kind       = ExbGeometryKind::Arc;
                            item.StartAngle = start;
                            item.EndAngle   = start + sweep;
                        }
                    }
                    return true;
                }
            }

            // ② 多点：按 24 字节步长连续读 (x, y, 0)
            std::vector<ExbPoint2> pts;
            pts.push_back(ExbPoint2{x, y});
            std::size_t q = o + 24;
            while (q + 24 <= r.End)
            {
                const double nx = ReadF64(d + q);
                const double ny = ReadF64(d + q + 8);
                if (!PlausiblePoint(nx, ny)) break;
                if (ReadF64(d + q + 16) != 0.0) break;
                pts.push_back(ExbPoint2{nx, ny});
                q += 24;
            }
            if (pts.size() >= 2 && (r.End - q) <= kMaxTrailer)
            {
                // 坐标量级自洽：同一串顶点必须落在同一个数量级带内。
                //
                // 属性区（含 `CtrlFlag` 的那批记录）尾部有一段参数
                //   `f32 1.0 | f64 0.0 | i32 −1 | f64 0.1 | f64 0.0 | f64 0.0`
                // 其中的 (0.1, 0) 完全满足 PlausiblePoint。若从这里起按 24 字节步长
                // 扫一段 32 字节的控制点数组，会错位拼出一串"顶点"，画出来就是从
                // (0.1, 0) 射向四方的放射线（实测 305 条，把整张图变成了扇面）。
                //
                // 本图坐标域 3.5e3~5e4，而 0.1 / 1.0 这类参数差 4 个数量级以上，
                // 用「最大幅值 / 最小非零幅值」就能干净地把两者分开。
                double lo = 1.0e300, hi = 0.0;
                for (const ExbPoint2& p : pts)
                {
                    for (const double v : {std::fabs(p.X), std::fabs(p.Y)})
                    {
                        if (v == 0.0) continue;
                        lo = (v < lo) ? v : lo;
                        hi = (v > hi) ? v : hi;
                    }
                }
                if (lo <= 0.0 || hi / lo <= 1.0e4)
                {
                    item.Kind   = pts.size() == 2 ? ExbGeometryKind::Line : ExbGeometryKind::Polyline;
                    item.Points = std::move(pts);
                    return true;
                }
            }

            // ③ 单点：先记成兜底候选，继续往后扫。若整条记录都没有更强的判据命中，
            //    才回退到它（真正的孤立点记录只有 87 字节，扫完就是它自己）。
            if (!haveFallback)
            {
                haveFallback = true;
                fallback     = ExbPoint2{x, y};
            }
        }

        if (haveFallback && allowPoint)
        {
            item.Kind   = ExbGeometryKind::Point;
            item.Points = {fallback};
            return true;
        }
        return false;
    }
} // namespace

    std::string Utf16LeToUtf8(const std::uint8_t* data, std::size_t size)
    {
        std::string out;
        bool allowed = true;
        DecodeUtf16Le(data, size, out, allowed);
        return out;
    }

    std::string StripTextCodes(const std::string& utf8)
    {
        // 带参数的格式码：\fSimHei|b0|i0;  \C0;  \H2.5;  \A3; ...
        auto isCode = [](char c) {
            switch (c)
            {
            case 'A': case 'a': case 'C': case 'c': case 'F': case 'f':
            case 'H': case 'h': case 'L': case 'l': case 'O': case 'o':
            case 'Q': case 'q': case 'S': case 's': case 'T': case 't':
            case 'W': case 'w':
                return true;
            default:
                return false;
            }
        };
        std::string out;
        out.reserve(utf8.size());
        std::size_t i = 0;
        if (utf8.size() >= 4 && (utf8.compare(0, 4, "\\A3;") == 0 || utf8.compare(0, 4, "\\A1;") == 0))
            i = 4;

        while (i < utf8.size())
        {
            const char c = utf8[i];
            if (c == '\\' && i + 1 < utf8.size())
            {
                const char nxt = utf8[i + 1];
                if (nxt == 'P') { out.push_back('\n'); i += 2; continue; }   // 段落
                if (nxt == '~') { out.push_back(' ');  i += 2; continue; }   // 不间断空格
                if (isCode(nxt))
                {
                    const std::size_t semi = utf8.find(';', i);
                    i = (semi == std::string::npos) ? utf8.size() : semi + 1;
                    continue;
                }
                i += 2;
                continue;
            }
            if (c == '{' || c == '}') { ++i; continue; }   // 组只丢括号，内容保留
            out.push_back(c);
            ++i;
        }
        // 去首尾空白（含全角空格）
        const auto notSpace = [](unsigned char ch) { return ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n'; };
        std::size_t b = 0, e = out.size();
        while (b < e && !notSpace(static_cast<unsigned char>(out[b]))) ++b;
        while (e > b && !notSpace(static_cast<unsigned char>(out[e - 1]))) --e;
        return out.substr(b, e - b);
    }

    // 图层 / 线型 / 颜色三连：记录头里两对 `[u16 0x0080][i32 值]`，第二对之后 4 字节是颜色字。
    //
    // 两对**恒定相距 8 字节**（每对 = 2 B 标签 + 4 B 值 + 2 B 填充），这是唯一可靠的判据：
    //   GenA/GenB 在 rel 18/26，GenC 在 rel 42/50，旧代小文件在 rel 40/48 到 60/68 之间浮动
    //   （GUID 数量不同）。所以不能写死偏移，也不能用固定窗口——旧代 rec2 的第二对在
    //   rel 68，72 B 的窗口刚好切掉。改成「扫第一个标签，再验 o+8 处是否也是标签」。
    bool ReadStyleRef(const std::uint8_t* d, const Span& r, ExbGeometryStyle& out)
    {
        out = ExbGeometryStyle{};
        // 只在记录头范围内扫（坐标区从 rel 75 起，120 已覆盖所有已知代的属性区尾部）
        const std::size_t limit = (r.End < r.Begin + 120) ? r.End : r.Begin + 120;

        for (std::size_t o = r.Begin + 8; o + 16 <= limit; ++o)
        {
            if (ReadU16(d + o) != 0x0080 || ReadU16(d + o + 8) != 0x0080) continue;

            const std::int32_t layer = static_cast<std::int32_t>(ReadU32(d + o + 2));
            const std::int32_t ltype = static_cast<std::int32_t>(ReadU32(d + o + 10));
            if (layer == 0 || layer < -2000 || layer > 200000) continue;
            if (ltype == 0 || ltype < -2000 || ltype > 200000) continue;

            out.LayerId    = layer;
            out.LineTypeId = ltype;
            // 颜色字紧跟第二对（标签 + 值之后再 2 B 填充）
            if (o + 18 <= r.End)
                out.ColorWord = ReadU32(d + o + 14);
            out.Valid = true;
            return true;
        }
        return false;
    }

    static bool ParseBlockStreamByMarks(const std::uint8_t* data, std::size_t size, ExbGeometryResult& out)
    {
        out = ExbGeometryResult{};
        if (!data || size < 64) return false;

        std::vector<Mark> marks;
        CollectMarks(data, size, marks);
        if (marks.empty()) return false;

        // 链式检测只在"序号像是序号"的标记上做：旧代小文件的首条记录那 4 字节不是序号，
        // 放进 posOf 会让链在随机位置断掉。
        std::unordered_map<std::uint32_t, std::size_t> posOf;
        posOf.reserve(marks.size() * 2);
        for (const Mark& m : marks)
            if (m.Seq <= 5000000u) posOf.emplace(m.Seq, m.Pos);

        // 找最长的一段「序号连续」链：块内序号从某个值开始严格 +1
        std::vector<Mark> chain;
        std::vector<Mark> best;
        for (const Mark& m : marks)
        {
            if (m.Seq > 5000000u) continue;
            if (posOf.count(m.Seq - 1)) continue;      // 不是链首
            chain.clear();
            std::uint32_t cur = m.Seq;
            std::size_t p = m.Pos;
            while (true)
            {
                chain.push_back(Mark{p, cur});
                const auto it = posOf.find(cur + 1);
                if (it == posOf.end()) break;
                p = it->second;
                cur += 1;
            }
            if (chain.size() > best.size()) best = std::move(chain);
            if (best.size() > 100000) break;
        }

        if (best.size() < 2)
        {
            // 退化情形：旧代小文件只有一两条记录（blank 只有 1 条），序号既不连续也可能
            // 根本不是序号。此时放弃链式检测，直接按 -5 标记的位置切段。
            best.clear();
            for (const Mark& m : marks) best.push_back(m);
            std::sort(best.begin(), best.end(),
                      [](const Mark& a, const Mark& b) { return a.Pos < b.Pos; });
            best.erase(std::unique(best.begin(), best.end(),
                                   [](const Mark& a, const Mark& b) { return a.Pos == b.Pos; }),
                       best.end());
        }
        if (best.empty()) return false;

        out.RecordCount = best.size();
        for (std::size_t i = 0; i < best.size(); ++i)
        {
            const std::size_t end = (i + 1 < best.size()) ? best[i + 1].Pos : size;
            Span r{best[i].Pos, end};
            if (r.End <= r.Begin || r.End - r.Begin > 4u * 1024 * 1024) continue;

            ExbGeometryItem item;
            bool matched = false;
            std::size_t headerExtra = 0;
            if (IsLegacyRecord(data, r, headerExtra))
                matched = MatchVertexArray(data, r, item);
            if (!matched)
                matched = MatchText(data, r, item) ||
                          MatchPolyline(data, r, item) ||
                          MatchRationalArc(data, r, item) ||
                          MatchCurve(data, r, item);
            if (!matched)
            {
                ++out.UnclassifiedCount;
                continue;
            }

            // 样式三连独立于几何识别：只有块定义类记录会读不到（五版实测 3800 条），
            // 那些记录本来也不出几何，不影响。
            ReadStyleRef(data, r, item.Style);
            item.Offset = r.Begin;
            item.Size   = r.End - r.Begin;

            switch (item.Kind)
            {
            case ExbGeometryKind::Text:     ++out.TextCount; break;
            case ExbGeometryKind::Polyline: ++out.PolylineCount; break;
            case ExbGeometryKind::Line:     ++out.LineCount; break;
            case ExbGeometryKind::Circle:   ++out.CircleCount; break;
            case ExbGeometryKind::Arc:      ++out.ArcCount; break;
            case ExbGeometryKind::Point:    ++out.PointCount; break;
            default: break;
            }
            out.Items.push_back(std::move(item));
        }
        return true;
    }

    // ── 记录切分与按类解码 ───────────────────────────────────────────────
    // 记录与各类正文的布局参照 ezexb（MIT，版权声明见 THIRD_PARTY_NOTICES.md）。
    //
    // 块流 = 流头（版本串 + 补零）+ 一串记录，每条：
    //   [16B 类标识][i32 对象 ID][i32 所有者 ID][u8 记录版本][u8 有无扩展属性]
    //   有扩展属性时：[u32 组数] 每组 [16B 组标识][u32 属性数][属性…]
    //   [u32 正文长度][正文]
    //   记录版本 3 另带扩展段：关系表 + 10 字节头 + 重复一遍的实体头 + 显示缓存（一串嵌套记录）+ 1 字节 0
    // 属性：[u8 类型][文字 名字][值]；值按类型 0 → 0，1 → 1，2～5 → 4，6 → 8，8 → 2，9 → 24，7 → 文字，
    //   10 → [u32 n] + n 个嵌套属性；类型 12 没有名字：[16B 对象标识][u32 字节数][数据]。
    //   "文字" = [u32 字节数][UTF-16 含终止符]，字节数为 0 时只有这 4 字节。
    // 实体正文开头是实体头：主版本号 18 起 37 字节，之前 33 字节：
    //   [u32][i32 图层][u32][i32 线型][u32 颜色][f32 线宽][u8 线型方式][u8 线宽方式][u8 颜色方式][u16 隐藏][f32 线型比例]（+ u32）
    //   三个"方式"：0 随层、1 随块、2 用自己的值。
    // 第一条记录是块自己（模型空间 ID −5、所有者 −4），其余记录的所有者是块的 ID；一个流里可以有多个块。
    namespace
    {
        constexpr int HexValue(char c)
        {
            return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c - 'A' + 10;
        }

        // "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" → 文件里的 16 字节（前三段按小端存）
        constexpr std::array<std::uint8_t, 16> Guid(const char* s)
        {
            std::array<std::uint8_t, 16> raw{};
            int n = 0;
            for (int i = 0; s[i] != 0 && n < 16; ++i)
            {
                if (s[i] == '-') continue;
                raw[n++] = static_cast<std::uint8_t>(HexValue(s[i]) * 16 + HexValue(s[i + 1]));
                ++i;
            }
            return { raw[3], raw[2], raw[1], raw[0], raw[5], raw[4], raw[7], raw[6],
                     raw[8], raw[9], raw[10], raw[11], raw[12], raw[13], raw[14], raw[15] };
        }

        constexpr auto kLineClass      = Guid("17ecf72d-c046-4e9a-8416-fd8220fd33b5");
        constexpr auto kCircleClass    = Guid("7587733c-3df6-4c9d-8b08-20aeffe18291");
        constexpr auto kArcClass       = Guid("91d6556b-0fb8-4e13-998f-bdacbfd31025");
        constexpr auto kPointClass     = Guid("d6da372e-0f4f-4bfb-9e92-3b3363ac6c22");
        constexpr auto kPolylineClass  = Guid("56d8e732-ad1f-426d-a4b0-6c5998434583");
        constexpr auto kTextClass      = Guid("76d76221-4358-465d-bc53-4449b3004657");
        constexpr auto kHatchClass     = Guid("79b9b625-ee7f-4fc9-b9a6-39036a219cf7");
        constexpr auto kSolidClass     = Guid("d7676d4e-b9e0-46c3-894d-e9ef7fb66fb2");
        constexpr auto kNurbsClass     = Guid("838896be-c069-4566-8991-afd750b4547c");
        constexpr auto kEllipseClass   = Guid("85dc7b5d-daac-47c8-a126-ffad50d6cd68");
        constexpr auto kInsertClass    = Guid("d38b4eba-a81f-4b28-aa19-71a60d884004");
        constexpr auto kXLineClass     = Guid("3d98eac3-4cf1-444a-8204-fb392ccd23c8");
        constexpr auto kRayClass       = Guid("db8a11ce-fcc8-4097-8291-f1924cb55ac9");
        constexpr auto kAttributeClass = Guid("60d85baf-4d2e-4000-84f0-e77d42fb999c");
        constexpr auto kAttDefClass    = Guid("e218226d-dd73-4a9d-aa95-764b4d3bc072");
        constexpr auto kWipeoutClass   = Guid("a843a444-3017-45ca-83e8-e581368b51a1");
        constexpr auto kPolyline3dClass = Guid("d5cee002-6408-4d9b-be79-6c4cf4efe4d6");
        constexpr auto kImageClass      = Guid("b4500e0a-c715-424e-bc25-08a152dbb98b");
        constexpr auto kPdfClass        = Guid("cef45391-2505-4f4c-badf-9b88a82fc975");
        constexpr auto kImageDefClass   = Guid("25e9cbf1-beb1-4c72-bcfe-825088c48b21");
        constexpr auto kViewportClass   = Guid("257d27f1-f719-424a-bcd0-4f1bd6540b14");
        constexpr auto kRelationList   = Guid("0fbb54e7-fc10-4b09-9499-924097c3bf94");
        constexpr auto kSingleRelation = Guid("62691bae-8d34-4142-a32f-b5e5ba30d444");
        constexpr auto kRefPtRelation  = Guid("77e8ac27-a318-44dc-bed7-edaac7fadf6e");
        constexpr auto kLoopRelation   = Guid("fa486143-85fe-4cb7-b5e2-c437d21f18a1");
        constexpr auto kDisplayCache   = Guid("0045f336-69f0-4e1a-b117-6f062840b2ab");

        bool IsClass(const std::uint8_t* p, const std::array<std::uint8_t, 16>& id)
        {
            return std::memcmp(p, id.data(), 16) == 0;
        }

        struct Record
        {
            std::size_t  Header  = 0;
            std::size_t  Body    = 0;
            std::size_t  BodyEnd = 0;
            std::size_t  End     = 0;   // 含记录版本 3 的扩展段
            std::int32_t Id      = 0;
            std::int32_t Owner   = 0;
            std::vector<Record> Display;   // 记录版本 3 的显示缓存
        };

        struct Format
        {
            unsigned    Major     = 16;
            std::size_t AttrWidth = 33;
        };

        constexpr int kMaxDepth = 16;

        // 越界或数值非法后一直保持失败，调用方最后查一次 Ok()
        class Cursor
        {
        public:
            Cursor(const std::uint8_t* d, std::size_t pos, std::size_t end) : m_d(d), m_pos(pos), m_end(end), m_ok(pos <= end) {}

            bool        Ok() const { return m_ok; }
            std::size_t Pos() const { return m_pos; }
            std::size_t Left() const { return m_ok ? m_end - m_pos : 0; }
            const std::uint8_t* Ptr() const { return m_d + m_pos; }
            void Fail() { m_ok = false; }
            void Seek(std::size_t pos) { if (pos > m_end) m_ok = false; else m_pos = pos; }

            bool Skip(std::size_t n)
            {
                if (!m_ok || n > m_end - m_pos) { m_ok = false; return false; }
                m_pos += n;
                return true;
            }
            std::uint8_t U8() { return Skip(1) ? m_d[m_pos - 1] : 0; }
            std::uint16_t U16() { return Skip(2) ? ReadU16(m_d + m_pos - 2) : 0; }
            std::uint32_t U32() { return Skip(4) ? ReadU32(m_d + m_pos - 4) : 0; }
            double F64()
            {
                if (!Skip(8)) return 0.0;
                const double v = ReadF64(m_d + m_pos - 8);
                if (!std::isfinite(v)) { m_ok = false; return 0.0; }
                return v;
            }
            // 计数：每个元素至少 minBytes 字节，超过剩余长度就是布局对不上
            std::uint32_t Count(std::size_t minBytes = 1)
            {
                const std::uint32_t n = U32();
                if (m_ok && n > (m_end - m_pos) / minBytes) m_ok = false;
                return m_ok ? n : 0;
            }
            bool Text(std::string* out = nullptr)
            {
                const std::uint32_t n = U32();
                if (!m_ok) return false;
                if (n == 0) return true;
                if (n < 2 || (n & 1u) || n > m_end - m_pos) { m_ok = false; return false; }
                if (out) *out = Utf16LeToUtf8(m_d + m_pos, n);
                m_pos += n;
                return true;
            }
            bool Is(const std::array<std::uint8_t, 16>& id)
            {
                return Skip(16) && IsClass(m_d + m_pos - 16, id);
            }

        private:
            const std::uint8_t* m_d;
            std::size_t m_pos;
            std::size_t m_end;
            bool        m_ok;
        };

        bool SkipProperty(Cursor& c, int depth)
        {
            if (depth > kMaxDepth) { c.Fail(); return false; }
            const std::uint8_t tag = c.U8();
            if (tag == 12)
            {
                c.Skip(16);
                c.Skip(c.U32());
                return c.Ok();
            }
            std::size_t width = 0;
            switch (tag)
            {
            case 0: case 7: case 10: break;
            case 1: width = 1; break;
            case 2: case 3: case 4: case 5: width = 4; break;
            case 6: width = 8; break;
            case 8: width = 2; break;
            case 9: width = 24; break;
            default: c.Fail(); return false;
            }
            c.Text();
            if (tag == 7)
                c.Text();
            else if (tag == 10)
            {
                const std::uint32_t n = c.Count();
                for (std::uint32_t i = 0; i < n && c.Ok(); ++i) SkipProperty(c, depth + 1);
            }
            else
                c.Skip(width);
            return c.Ok();
        }

        bool ParseRecord(const std::uint8_t* d, std::size_t limit, std::size_t p, const Format& fmt, int depth, Record& out);

        // 记录版本 3 正文之后的扩展段。读不下来时返回 false，记录按"正文到此为止"处理，上层靠重新同步跳过。
        bool ParseExtension(const std::uint8_t* d, std::size_t limit, const Format& fmt, int depth, Record& rec)
        {
            Cursor c(d, rec.BodyEnd, limit);
            if (!c.Is(kRelationList)) return false;
            const std::uint32_t relations = c.Count(16);
            for (std::uint32_t i = 0; i < relations && c.Ok(); ++i)
            {
                if (!c.Skip(16)) return false;
                const std::uint8_t* type = c.Ptr() - 16;
                if (IsClass(type, kSingleRelation))
                    c.Skip(8);
                else if (IsClass(type, kRefPtRelation))
                {
                    c.Skip(24);
                    for (int k = 0; k < 2; ++k) c.Skip(static_cast<std::size_t>(c.Count(4)) * 4);
                    for (const std::size_t width : {24u, 24u, 8u, 8u, 24u})
                    {
                        const std::uint8_t present = c.U8();
                        if (present > 1) return false;
                        if (present) c.Skip(width);
                    }
                }
                else if (IsClass(type, kLoopRelation))
                {
                    c.Skip(4);
                    const std::uint32_t groups = c.Count(4);
                    for (std::uint32_t g = 0; g < groups && c.Ok(); ++g)
                    {
                        const std::uint32_t entries = c.Count(5);
                        for (std::uint32_t e = 0; e < entries && c.Ok(); ++e)
                        {
                            c.Skip(4);
                            const std::uint8_t present = c.U8();
                            if (present > 1) return false;
                            if (present)
                            {
                                c.Skip(static_cast<std::size_t>(c.Count(4)) * 4);
                                c.Skip(4);
                            }
                        }
                    }
                    const std::uint32_t pairGroups = c.Count(4);
                    for (std::uint32_t g = 0; g < pairGroups && c.Ok(); ++g)
                        c.Skip(static_cast<std::size_t>(c.Count(8)) * 8);
                    c.Skip(4);
                }
                else
                    return false;
            }

            if (c.U16() != 0 || c.U32() != 1) return false;
            const std::uint32_t headerWord = c.U32();
            if (!c.Ok() || (headerWord != 0xf000fe00u && headerWord != 0xf000fe40u)) return false;

            if (rec.BodyEnd - rec.Body < fmt.AttrWidth || c.Left() < fmt.AttrWidth) return false;
            if (std::memcmp(d + rec.Body, c.Ptr(), fmt.AttrWidth) != 0) return false;
            c.Skip(fmt.AttrWidth);

            std::vector<Record> display;
            const std::uint8_t present = c.U8();
            if (!c.Ok() || present > 1) return false;
            if (present)
            {
                if (!c.Is(kDisplayCache)) return false;
                const std::uint32_t count = c.Count(26);
                for (std::uint32_t i = 0; i < count && c.Ok(); ++i)
                {
                    Record child;
                    if (!ParseRecord(d, limit, c.Pos(), fmt, depth + 1, child)) return false;
                    c.Seek(child.End);
                    display.push_back(std::move(child));
                }
            }
            if (c.U8() != 0 || !c.Ok()) return false;

            rec.End     = c.Pos();
            rec.Display = std::move(display);
            return true;
        }

        bool ParseRecord(const std::uint8_t* d, std::size_t limit, std::size_t p, const Format& fmt, int depth, Record& out)
        {
            if (depth > kMaxDepth || p > limit || limit - p < 30) return false;
            const std::uint8_t schema = d[p + 24];
            const std::uint8_t hasProperties = d[p + 25];
            const bool known = (schema == 1 && hasProperties <= 1) || (schema == 100 && hasProperties == 1) ||
                               (schema == 3 && hasProperties <= 1);
            if (!known) return false;

            Cursor c(d, p + 26, limit);
            if (hasProperties)
            {
                const std::uint32_t groups = c.Count(20);
                for (std::uint32_t g = 0; g < groups && c.Ok(); ++g)
                {
                    c.Skip(16);
                    const std::uint32_t values = c.Count();
                    for (std::uint32_t k = 0; k < values && c.Ok(); ++k) SkipProperty(c, depth);
                }
            }
            const std::uint32_t len = c.U32();
            if (!c.Ok() || len > c.Left()) return false;

            out = Record{};
            out.Header  = p;
            out.Body    = c.Pos();
            out.BodyEnd = c.Pos() + len;
            out.End     = out.BodyEnd;
            out.Id      = static_cast<std::int32_t>(ReadU32(d + p + 16));
            out.Owner   = static_cast<std::int32_t>(ReadU32(d + p + 20));
            if (schema == 3) ParseExtension(d, limit, fmt, depth, out);
            return true;
        }

        bool LooksLikeEntityBody(const std::uint8_t* d, const Record& r)
        {
            return r.BodyEnd - r.Body >= 33 && ReadU16(d + r.Body) == 0 && ReadU16(d + r.Body + 2) == 0x0080 &&
                   ReadU16(d + r.Body + 8) == 0 && ReadU16(d + r.Body + 10) == 0x0080;
        }

        bool SameClass(const std::uint8_t* d, const Record& a, const Record& b)
        {
            return std::memcmp(d + a.Header, d + b.Header, 16) == 0;
        }

        // 没给主版本号时，从第一条直线或点的正文长度看实体头是 33 还是 37 字节
        Format DetectFormat(const std::uint8_t* d, std::size_t size, unsigned major)
        {
            Format fmt;
            if (major >= 10)
            {
                fmt.Major     = major;
                fmt.AttrWidth = major >= 18 ? 37 : 33;
                return fmt;
            }
            for (std::size_t p = 0; p + 30 <= size; ++p)
            {
                const bool line = IsClass(d + p, kLineClass), point = !line && IsClass(d + p, kPointClass);
                if ((!line && !point) || d[p + 24] != 1 || d[p + 25] != 0) continue;
                const std::size_t len = ReadU32(d + p + 26), fixed = line ? 48 : 24;
                if (len != fixed + 33 && len != fixed + 37) continue;
                fmt.AttrWidth = len - fixed;
                fmt.Major     = fmt.AttrWidth == 37 ? 18 : 16;
                break;
            }
            return fmt;
        }

        // blockOf[i] 是 records[i] 所属块在 records 里的下标，块记录指向自己。
        // 走不通（找不到第一条块记录）时返回 false，调用方退回按标记切分。
        bool WalkRecords(const std::uint8_t* d, std::size_t size, const Format& fmt, std::vector<Record>& records,
                         std::vector<std::size_t>& blockOf, std::size_t& unparsedBytes)
        {
            records.clear();
            blockOf.clear();
            unparsedBytes = 0;

            Record head;
            bool found = false;
            for (std::size_t p = 0; p + 30 <= size && p < 65536; ++p)
            {
                if (!ParseRecord(d, size, p, fmt, 0, head)) continue;
                if (head.End == size) { found = true; break; }
                Record next;
                if (ParseRecord(d, size, head.End, fmt, 0, next) &&
                    (next.Owner == head.Id || SameClass(d, next, head)))
                {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
            records.push_back(head);
            blockOf.push_back(0);

            std::size_t current = 0;
            const auto push = [&](Record&& r)
            {
                const bool isBlock = SameClass(d, r, head);
                records.push_back(std::move(r));
                if (isBlock) current = records.size() - 1;
                blockOf.push_back(current);
            };

            std::size_t pos = head.End;
            while (pos < size)
            {
                Record r;
                if (ParseRecord(d, size, pos, fmt, 0, r) && (SameClass(d, r, head) || r.Owner == records[current].Id))
                {
                    pos = r.End;
                    push(std::move(r));
                    continue;
                }

                const std::uint32_t ownerWord = static_cast<std::uint32_t>(records[current].Id);
                bool resynced = false;
                std::size_t q = pos + 1;
                for (; q + 30 <= size; ++q)
                {
                    const bool isBlock = std::memcmp(d + q, d + head.Header, 16) == 0;
                    if (!isBlock && ReadU32(d + q + 20) != ownerWord) continue;
                    if (!ParseRecord(d, size, q, fmt, 0, r)) continue;
                    if (!isBlock && !LooksLikeEntityBody(d, r)) continue;
                    if (r.End != size)
                    {
                        Record next;
                        if (!ParseRecord(d, size, r.End, fmt, 0, next)) continue;
                        if (!SameClass(d, next, head) && next.Owner != (isBlock ? r.Id : records[current].Id)) continue;
                    }
                    resynced = true;
                    break;
                }
                if (!resynced)
                {
                    unparsedBytes += size - pos;
                    break;
                }
                unparsedBytes += q - pos;
                pos = r.End;
                push(std::move(r));
            }
            return true;
        }

        std::string ClassHex(const std::uint8_t* p)
        {
            static const char* kDigits = "0123456789abcdef";
            std::string out;
            out.reserve(32);
            for (int i = 0; i < 16; ++i)
            {
                out.push_back(kDigits[p[i] >> 4]);
                out.push_back(kDigits[p[i] & 15]);
            }
            return out;
        }

        void ReadAttributes(const std::uint8_t* d, const Record& rec, const Format& fmt, ExbGeometryStyle& out)
        {
            out = ExbGeometryStyle{};
            if (rec.BodyEnd - rec.Body < fmt.AttrWidth) return;
            const std::uint8_t* b = d + rec.Body;
            out.LayerId      = static_cast<std::int32_t>(ReadU32(b + 4));
            out.LineTypeId   = static_cast<std::int32_t>(ReadU32(b + 12));
            out.ColorWord    = ReadU32(b + 16);
            out.LineWidth    = ReadF32(b + 20);
            out.LineTypeMode = b[24];
            out.WidthMode    = b[25];
            out.ColorMode    = b[26];
            out.Valid        = true;
        }

        bool PlanarPoint(Cursor& c, ExbPoint2& out)
        {
            out.X = c.F64();
            out.Y = c.F64();
            c.F64();
            return c.Ok() && std::fabs(out.X) < 1.0e12 && std::fabs(out.Y) < 1.0e12;
        }

        // 圆 / 圆弧：圆心、半径、X 轴、Y 轴、扫过角。两轴成左手系时沿顺时针走。
        bool ReadCircular(Cursor& c, ExbGeometryItem& item)
        {
            ExbPoint2 center, xAxis, yAxis;
            if (!PlanarPoint(c, center)) return false;
            const double radius = c.F64();
            if (!PlanarPoint(c, xAxis) || !PlanarPoint(c, yAxis)) return false;
            const double sweep = c.F64();
            if (!c.Ok() || !(radius > 0.0) || !(sweep > 0.0)) return false;

            item.Center = center;
            item.Radius = radius;
            item.Kind   = ExbGeometryKind::Circle;
            if (sweep < kTwoPi - 1.0e-9)
            {
                double start = std::atan2(xAxis.Y, xAxis.X);
                if (xAxis.X * yAxis.Y - xAxis.Y * yAxis.X < 0.0) start -= sweep;
                while (start < 0.0) start += kTwoPi;
                while (start >= kTwoPi) start -= kTwoPi;
                item.Kind       = ExbGeometryKind::Arc;
                item.StartAngle = start;
                item.EndAngle   = start + sweep;
            }
            return true;
        }

        bool ReadEllipse(Cursor& c, ExbGeometryItem& item)
        {
            ExbPoint2 center, xAxis, yAxis;
            if (!PlanarPoint(c, center) || !PlanarPoint(c, xAxis) || !PlanarPoint(c, yAxis)) return false;
            const double majorRadius = c.F64(), minorRadius = c.F64();
            const double start = c.F64(), end = c.F64(), rotation = c.F64();
            if (!c.Ok() || !(majorRadius > 0.0) || !(minorRadius > 0.0)) return false;

            item.Kind        = ExbGeometryKind::Ellipse;
            item.Center      = center;
            item.Radius      = majorRadius;
            item.RadiusMinor = minorRadius;
            item.Rotation    = (xAxis.X != 0.0 || xAxis.Y != 0.0) ? std::atan2(xAxis.Y, xAxis.X) : rotation;
            item.StartAngle  = start;
            item.EndAngle    = end > start ? end : start + kTwoPi;
            return true;
        }

        // 样条存的是控制点（带权）和节点向量；MiniCAD 的样条按拟合点建，这里直接求值成折线。
        bool ReadNurbs(Cursor& c, ExbGeometryItem& item)
        {
            c.Skip(32);
            const std::uint32_t n = c.Count(32);
            const std::uint32_t degree = c.U32();
            c.Skip(8);
            if (!c.Ok() || n < 2 || degree < 1 || degree > 16 || degree >= n) return false;

            std::vector<std::array<double, 3>> control(n);
            for (auto& p : control)
            {
                const double x = c.F64(), y = c.F64();
                c.F64();
                const double w = c.F64();
                p = { x * w, y * w, w };
            }
            const std::size_t knotCount = static_cast<std::size_t>(n) + degree + 1;
            if (c.Left() < knotCount * 8) return false;
            std::vector<double> knots(knotCount);
            for (double& k : knots) k = c.F64();
            if (!c.Ok()) return false;
            for (std::size_t i = 1; i < knotCount; ++i)
                if (knots[i] < knots[i - 1]) return false;

            const double u0 = knots[degree], u1 = knots[n];
            if (!(u1 > u0)) return false;
            const std::size_t samples = std::min<std::size_t>(std::max<std::size_t>(32, static_cast<std::size_t>(n) * 8), 4096);
            std::vector<double> basis(degree + 1), left(degree + 1), right(degree + 1);
            item.Points.clear();
            for (std::size_t s = 0; s <= samples; ++s)
            {
                const double u = s == samples ? u1 : u0 + (u1 - u0) * static_cast<double>(s) / static_cast<double>(samples);
                std::size_t span = degree;
                while (span + 1 < n && u >= knots[span + 1]) ++span;

                basis[0] = 1.0;
                for (std::uint32_t j = 1; j <= degree; ++j)
                {
                    left[j]  = u - knots[span + 1 - j];
                    right[j] = knots[span + j] - u;
                    double saved = 0.0;
                    for (std::uint32_t r = 0; r < j; ++r)
                    {
                        const double denom = right[r + 1] + left[j - r];
                        const double temp  = denom != 0.0 ? basis[r] / denom : 0.0;
                        basis[r] = saved + right[r + 1] * temp;
                        saved    = left[j - r] * temp;
                    }
                    basis[j] = saved;
                }

                double x = 0.0, y = 0.0, w = 0.0;
                for (std::uint32_t j = 0; j <= degree; ++j)
                {
                    const auto& p = control[span - degree + j];
                    x += basis[j] * p[0];
                    y += basis[j] * p[1];
                    w += basis[j] * p[2];
                }
                if (!(std::fabs(w) > 1.0e-12)) return false;
                item.Points.push_back(ExbPoint2{x / w, y / w});
            }
            item.Kind = ExbGeometryKind::Polyline;
            return true;
        }

        // 多段线：[u32 n] + n 个顶点 (x, y, [主版本 ≤ 10 多一个 z], 凸度, 起点宽, 终点宽) + [u32 闭合] + [主版本 > 10：法向]。
        // 凸度、宽度同 DXF 的 LWPOLYLINE，属于从该顶点出发的那一段。
        bool ReadPolyline(Cursor& c, const Format& fmt, ExbGeometryItem& item)
        {
            const std::size_t stride = fmt.Major <= 10 ? 48 : 40;
            const std::uint32_t n = c.Count(stride);
            if (!c.Ok() || n < 2) return false;
            item.Points.clear();
            item.Bulges.clear();
            item.StartWidths.clear();
            item.EndWidths.clear();
            for (std::uint32_t i = 0; i < n; ++i)
            {
                const double x = c.F64(), y = c.F64();
                if (fmt.Major <= 10) c.F64();
                item.Points.push_back(ExbPoint2{x, y});
                item.Bulges.push_back(c.F64());
                item.StartWidths.push_back(c.F64());
                item.EndWidths.push_back(c.F64());
            }
            const std::uint32_t closed = c.U32();
            if (fmt.Major > 10) c.Skip(24);
            if (!c.Ok() || closed > 1) return false;
            item.Closed = closed != 0;
            item.Kind   = ExbGeometryKind::Polyline;
            return true;
        }

        // 填充边界里的曲线类型码；单独的实体正文用的是同一套布局
        bool ReadCurve(Cursor& c, std::uint32_t type, const Format& fmt, ExbGeometryItem& item)
        {
            switch (type)
            {
            case 1010:
            {
                ExbPoint2 p;
                if (!PlanarPoint(c, p)) return false;
                item.Kind   = ExbGeometryKind::Point;
                item.Points = {p};
                return true;
            }
            case 1011:
            {
                ExbPoint2 a, b;
                if (!PlanarPoint(c, a) || !PlanarPoint(c, b)) return false;
                item.Kind   = ExbGeometryKind::Line;
                item.Points = {a, b};
                return true;
            }
            case 1012: case 1013: return ReadCircular(c, item);
            case 1014: return ReadEllipse(c, item);
            case 1015: case 1016: return ReadNurbs(c, item);
            case 1019: return ReadPolyline(c, fmt, item);
            default: return false;
            }
        }

        // 文字：[文字 内容] 字高、文字块高、文字块宽、转角、[u8][u32 对齐][f64 比例][u32][i32 样式 ID]、插入点 …
        bool ReadText(Cursor& c, ExbGeometryItem& item)
        {
            std::string raw;
            if (!c.Text(&raw) || raw.empty()) return false;
            const double height = c.F64();
            c.Skip(8);
            const double boxWidth = c.F64();
            const double rotation = c.F64();
            c.Skip(1);
            const std::uint32_t align = c.U32();
            c.Skip(8 + 4);
            const std::int32_t styleId = static_cast<std::int32_t>(c.U32());
            ExbPoint2 insert;
            if (!PlanarPoint(c, insert) || !(height > 0.0) || !(height < 1.0e6)) return false;

            item.Kind     = ExbGeometryKind::Text;
            item.Raw      = raw;
            item.Plain    = StripTextCodes(raw);
            item.Insert   = insert;
            item.Height   = height;
            item.BoxWidth = boxWidth > 0.0 ? boxWidth : 0.0;
            item.StyleId  = styleId;
            item.Align    = align <= 8 ? static_cast<std::uint8_t>(align) : 0;
            item.Rotation = (rotation >= 0.0 && rotation < kTwoPi) ? rotation : 0.0;
            return true;
        }

        bool ClassifyRecord(const std::uint8_t* d, const Record& rec, const Format& fmt, int depth, ExbGeometryItem& item);

        // [u32 n] + n 条嵌套记录 → 子条目。认不出的嵌套记录跳过，但每条都必须能切出来。
        bool ReadEmbedded(const std::uint8_t* d, Cursor& c, const Format& fmt, int depth,
                          std::vector<ExbGeometryItem>* children)
        {
            const std::uint32_t count = c.Count(26);
            for (std::uint32_t i = 0; i < count && c.Ok(); ++i)
            {
                Record child;
                if (!ParseRecord(d, c.Pos() + c.Left(), c.Pos(), fmt, depth + 1, child)) return false;
                c.Seek(child.End);
                ExbGeometryItem sub;
                if (children && ClassifyRecord(d, child, fmt, depth + 1, sub)) children->push_back(std::move(sub));
            }
            return c.Ok();
        }

        // 块参照：[u32 显示记录数][显示记录…] 转角、三向比例、插入点、4×4 矩阵（行向量约定，平移在第 4 行）、
        //   [u32][i32 块 ID][u32 属性数][属性记录…] 别名 …
        // 属性记录读成文字，作为子条目跟在块参照后面（坐标已是所在空间的坐标）。
        bool ReadInsert(const std::uint8_t* d, Cursor& c, const Format& fmt, int depth, ExbGeometryItem& item)
        {
            if (!ReadEmbedded(d, c, fmt, depth, nullptr)) return false;
            c.Skip(8 + 24 + 24);
            double m[16];
            for (double& v : m) v = c.F64();
            c.Skip(4);
            const std::int32_t blockId = static_cast<std::int32_t>(c.U32());
            if (!c.Ok()) return false;

            const double sx = std::hypot(m[0], m[1]);
            if (!(sx > 1.0e-12) || std::fabs(m[12]) >= 1.0e12 || std::fabs(m[13]) >= 1.0e12) return false;
            item.Kind     = ExbGeometryKind::Insert;
            item.BlockRef = blockId;
            item.Insert   = ExbPoint2{m[12], m[13]};
            item.ScaleX   = sx;
            item.ScaleY   = (m[0] * m[5] - m[1] * m[4]) / sx;
            item.Rotation = std::atan2(m[1], m[0]);

            std::vector<ExbGeometryItem> attributes;
            Cursor rest = c;
            if (ReadEmbedded(d, rest, fmt, depth, &attributes)) item.Children = std::move(attributes);
            return true;
        }

        // 填充 / 实心填充的边界：拾取点、[u32 环数] 每环 [u32 曲线数][类型码…][曲线…]，之后是设置（主版本 > 10）。
        bool ReadBoundary(Cursor& c, const Format& fmt, std::vector<ExbGeometryItem>& curves,
                          std::vector<std::size_t>* loopSizes = nullptr)
        {
            c.Skip(24);
            const std::uint32_t loops = c.Count(4);
            for (std::uint32_t l = 0; l < loops && c.Ok(); ++l)
            {
                const std::uint32_t n = c.Count(4);
                if (loopSizes) loopSizes->push_back(n);
                std::vector<std::uint32_t> types(n);
                for (std::uint32_t& t : types) t = c.U32();
                for (const std::uint32_t t : types)
                {
                    ExbGeometryItem curve;
                    if (!c.Ok() || !ReadCurve(c, t, fmt, curve)) { c.Fail(); return false; }
                    curves.push_back(std::move(curve));
                }
            }
            if (fmt.Major > 10)
            {
                c.Skip(8);
                const std::uint32_t groups = c.Count(4);
                for (std::uint32_t g = 0; g < groups && c.Ok(); ++g)
                {
                    const std::uint32_t entries = c.Count(5);
                    for (std::uint32_t e = 0; e < entries && c.Ok(); ++e)
                    {
                        c.Skip(4);
                        const std::uint8_t present = c.U8();
                        if (present > 1) { c.Fail(); return false; }
                        if (present)
                        {
                            c.Skip(static_cast<std::size_t>(c.Count(4)) * 4);
                            c.Skip(4);
                        }
                    }
                }
                const std::uint32_t pairGroups = c.Count(4);
                for (std::uint32_t g = 0; g < pairGroups && c.Ok(); ++g)
                    c.Skip(static_cast<std::size_t>(c.Count(8)) * 8);
                c.Skip(4 + 24 + 24 + 20 + 24 + 4 + 8);
                c.Skip(static_cast<std::size_t>(loops) * 4);
            }
            return c.Ok();
        }

        // 一条边界曲线 → 有方向的顶点串（带凸度）。点不出边界，返回 false。
        bool CurvePath(const ExbGeometryItem& curve, ExbFillLoop& path)
        {
            path = ExbFillLoop{};
            const auto at = [&curve](double angle)
            {
                return ExbPoint2{curve.Center.X + curve.Radius * std::cos(angle), curve.Center.Y + curve.Radius * std::sin(angle)};
            };
            switch (curve.Kind)
            {
            case ExbGeometryKind::Line:
                if (curve.Points.size() != 2) return false;
                path.Points = curve.Points;
                path.Bulges = {0.0};
                return true;
            case ExbGeometryKind::Arc:
                path.Points = {at(curve.StartAngle), at(curve.EndAngle)};
                path.Bulges = {std::tan((curve.EndAngle - curve.StartAngle) / 4.0)};
                return true;
            case ExbGeometryKind::Circle:
                path.Points = {at(0.0), at(3.141592653589793), at(0.0)};
                path.Bulges = {1.0, 1.0};
                return true;
            case ExbGeometryKind::Ellipse:
            {
                constexpr int kSegments = 64;
                const double cosR = std::cos(curve.Rotation), sinR = std::sin(curve.Rotation);
                for (int i = 0; i <= kSegments; ++i)
                {
                    const double t = curve.StartAngle + (curve.EndAngle - curve.StartAngle) * i / kSegments;
                    const double x = curve.Radius * std::cos(t), y = curve.RadiusMinor * std::sin(t);
                    path.Points.push_back(ExbPoint2{curve.Center.X + x * cosR - y * sinR, curve.Center.Y + x * sinR + y * cosR});
                }
                path.Bulges.assign(kSegments, 0.0);
                return true;
            }
            case ExbGeometryKind::Polyline:
                if (curve.Points.size() < 2) return false;
                path.Points = curve.Points;
                if (curve.Bulges.size() == curve.Points.size())
                {
                    path.Bulges.assign(curve.Bulges.begin(), curve.Bulges.end() - 1);
                    if (curve.Closed)
                    {
                        path.Points.push_back(curve.Points.front());
                        path.Bulges.push_back(curve.Bulges.back());
                    }
                }
                else
                    path.Bulges.assign(curve.Points.size() - 1, 0.0);
                return true;
            default:
                return false;
            }
        }

        // 把一个环里的曲线首尾接起来；曲线方向与环的走向相反时倒过来接（凸度变号）
        void BuildFillLoops(const std::vector<ExbGeometryItem>& curves, const std::vector<std::size_t>& loopSizes,
                            std::vector<ExbFillLoop>& loops)
        {
            const auto near = [](const ExbPoint2& a, const ExbPoint2& b)
            {
                const double scale = std::max({1.0, std::fabs(a.X), std::fabs(a.Y)});
                return std::hypot(a.X - b.X, a.Y - b.Y) <= 1.0e-6 * scale;
            };
            std::size_t next = 0;
            for (const std::size_t count : loopSizes)
            {
                ExbFillLoop loop;
                for (std::size_t i = 0; i < count && next < curves.size(); ++i, ++next)
                {
                    ExbFillLoop path;
                    if (!CurvePath(curves[next], path)) continue;
                    if (!loop.Points.empty() && !near(loop.Points.back(), path.Points.front()) &&
                        near(loop.Points.back(), path.Points.back()))
                    {
                        std::reverse(path.Points.begin(), path.Points.end());
                        std::reverse(path.Bulges.begin(), path.Bulges.end());
                        for (double& b : path.Bulges) b = -b;
                    }
                    if (loop.Points.empty())
                        loop = std::move(path);
                    else
                    {
                        const bool joined = near(loop.Points.back(), path.Points.front());
                        if (!joined) loop.Bulges.push_back(0.0);
                        loop.Points.insert(loop.Points.end(), path.Points.begin() + (joined ? 1 : 0), path.Points.end());
                        loop.Bulges.insert(loop.Bulges.end(), path.Bulges.begin(), path.Bulges.end());
                    }
                }
                if (loop.Points.size() < 3) continue;
                if (!near(loop.Points.back(), loop.Points.front()))
                {
                    loop.Points.push_back(loop.Points.front());
                    loop.Bulges.push_back(0.0);
                }
                loops.push_back(std::move(loop));
            }
        }

        // 实心填充边界之后的渐变：[u32 类型][文字 名字] 角度、偏移、[u32 单色]、明暗、两个颜色字、两个数
        void ReadGradient(Cursor c, ExbGeometryItem& item)
        {
            c.Skip(4);
            std::string name;
            c.Text(&name);
            const double angle = c.F64(), shift = c.F64();
            const std::uint32_t singleColor = c.U32();
            const double tint = c.F64();
            const std::uint32_t first = c.U32(), second = c.U32();
            c.Skip(16);
            // 普通实心填充的名字是 SOLID
            if (!c.Ok() || name.empty() || name == "SOLID") return;
            item.GradientName        = name;
            item.GradientAngle       = angle;
            item.GradientShift       = shift;
            item.GradientSingleColor = singleColor != 0;
            item.GradientTint        = tint;
            item.GradientColors[0]   = first;
            item.GradientColors[1]   = second;
        }

        bool SkipHatchPattern(Cursor& c)
        {
            c.Skip(32 + 24);
            const std::uint32_t lines = c.Count(56 + 4 + 400);
            for (std::uint32_t i = 0; i < lines && c.Ok(); ++i)
            {
                c.Skip(56);
                if (c.U32() > 50) { c.Fail(); return false; }
                c.Skip(400);
            }
            return c.Ok();
        }

        // 填充：[可选：(u32 0) + 已展开的图案线端点表][边界][图案]。三种开头依次试，以正好读完正文为准。
        // 有图案线端点表时两点一段直接画出来，不自己按图案生成。
        bool ReadHatch(const std::uint8_t* d, const Record& rec, const Format& fmt, ExbGeometryItem& item)
        {
            for (int layout = 0; layout < 3; ++layout)
            {
                Cursor c(d, rec.Body + fmt.AttrWidth, rec.BodyEnd);
                std::vector<ExbGeometryItem> children;
                if (layout == 2 && c.U32() != 0) continue;
                if (layout >= 1)
                {
                    const std::uint32_t points = c.Count(24);
                    std::vector<ExbPoint2> cache(points);
                    for (ExbPoint2& p : cache) PlanarPoint(c, p);
                    if (!c.Ok()) continue;
                    for (std::size_t i = 0; i + 1 < cache.size(); i += 2)
                    {
                        ExbGeometryItem line;
                        line.Kind   = ExbGeometryKind::Line;
                        line.Points = {cache[i], cache[i + 1]};
                        children.push_back(std::move(line));
                    }
                }
                if (!ReadBoundary(c, fmt, children) || !SkipHatchPattern(c) || c.Left() != 0) continue;
                item.Kind     = ExbGeometryKind::Group;
                item.Children = std::move(children);
                return true;
            }
            return false;
        }

        bool ClassifyRecord(const std::uint8_t* d, const Record& rec, const Format& fmt, int depth, ExbGeometryItem& item)
        {
            if (depth > kMaxDepth) return false;
            const std::uint8_t* classId = d + rec.Header;
            const std::size_t len = rec.BodyEnd - rec.Body;
            const auto finish = [&]()
            {
                ReadAttributes(d, rec, fmt, item.Style);
                item.Offset  = rec.Header;
                item.Size    = rec.End - rec.Header;
                item.ClassId = ClassHex(classId);
                item.Id      = rec.Id;
                for (ExbGeometryItem& child : item.Children)
                    if (!child.Style.Valid) child.Style = item.Style;
                return true;
            };
            if (len < fmt.AttrWidth) return false;

            Cursor c(d, rec.Body + fmt.AttrWidth, rec.BodyEnd);
            const auto exact = [&](bool ok) { return ok && c.Ok() && c.Left() == 0; };

            if (IsClass(classId, kLineClass))   { if (exact(ReadCurve(c, 1011, fmt, item))) return finish(); return false; }
            if (IsClass(classId, kPointClass))  { if (exact(ReadCurve(c, 1010, fmt, item))) return finish(); return false; }
            if (IsClass(classId, kCircleClass) || IsClass(classId, kArcClass))
            {
                if (exact(ReadCircular(c, item))) return finish();
                return false;
            }
            if (IsClass(classId, kEllipseClass)) { if (exact(ReadEllipse(c, item))) return finish(); return false; }
            if (IsClass(classId, kXLineClass) || IsClass(classId, kRayClass))
            {
                ExbPoint2 base, direction;
                if (!PlanarPoint(c, base) || !PlanarPoint(c, direction) || c.Left() != 0) return false;
                if (direction.X == 0.0 && direction.Y == 0.0) return false;
                item.Kind      = IsClass(classId, kXLineClass) ? ExbGeometryKind::XLine : ExbGeometryKind::Ray;
                item.Points    = {base};
                item.Direction = direction;
                return finish();
            }
            if (IsClass(classId, kPolylineClass) && exact(ReadPolyline(c, fmt, item))) return finish();
            if (IsClass(classId, kNurbsClass))
            {
                // 主版本 15 起样条前面多一段 [i32 拟合点数 = −1][f64 拟合容差]；两种都试
                for (const std::size_t prefix : {std::size_t{12}, std::size_t{0}})
                {
                    Cursor n(d, rec.Body + fmt.AttrWidth + prefix, rec.BodyEnd);
                    ExbGeometryItem curve;
                    if (ReadNurbs(n, curve) && n.Left() == 0)
                    {
                        item = std::move(curve);
                        return finish();
                    }
                }
            }
            if (IsClass(classId, kInsertClass))
            {
                if (ReadInsert(d, c, fmt, depth, item)) return finish();
                return false;
            }
            if (IsClass(classId, kTextClass))
            {
                Cursor t(d, rec.Body + fmt.AttrWidth, rec.BodyEnd);
                if (ReadText(t, item)) return finish();
            }
            if (IsClass(classId, kHatchClass) && ReadHatch(d, rec, fmt, item)) return finish();
            if (IsClass(classId, kSolidClass))
            {
                Cursor s(d, rec.Body + fmt.AttrWidth, rec.BodyEnd);
                std::vector<ExbGeometryItem> curves;
                std::vector<std::size_t> loopSizes;
                if (ReadBoundary(s, fmt, curves, &loopSizes) && !curves.empty())
                {
                    BuildFillLoops(curves, loopSizes, item.Loops);
                    if (fmt.Major > 10) ReadGradient(s, item);
                    item.Kind     = ExbGeometryKind::Group;
                    item.Children = std::move(curves);
                    item.Filled   = true;
                    return finish();
                }
            }

            // 标注、工程符号等：正文开头是一串显示用的嵌套记录
            {
                Cursor p(d, rec.Body + fmt.AttrWidth, rec.BodyEnd);
                std::vector<ExbGeometryItem> children;
                if (len >= fmt.AttrWidth + 4 && ReadEmbedded(d, p, fmt, depth, &children) && !children.empty())
                {
                    item.Kind     = ExbGeometryKind::Group;
                    item.Children = std::move(children);
                    return finish();
                }
            }

            // 记录版本 3 的显示缓存（表格等）
            if (!rec.Display.empty())
            {
                std::vector<ExbGeometryItem> children;
                for (const Record& child : rec.Display)
                {
                    ExbGeometryItem sub;
                    if (ClassifyRecord(d, child, fmt, depth + 1, sub)) children.push_back(std::move(sub));
                }
                if (!children.empty())
                {
                    item.Kind     = ExbGeometryKind::Group;
                    item.Children = std::move(children);
                    return finish();
                }
            }

            // 三维多段线：[u32 n] + n 个 (x, y, z) + [u32 闭合]，按平面折线显示
            if (IsClass(classId, kPolyline3dClass))
            {
                Cursor q(d, rec.Body + fmt.AttrWidth, rec.BodyEnd);
                const std::uint32_t n = q.Count(24);
                item.Points.assign(n, ExbPoint2{});
                bool ok = n >= 2;
                for (ExbPoint2& p : item.Points) ok = PlanarPoint(q, p) && ok;
                const std::uint32_t closed = q.U32();
                if (!ok || !q.Ok() || q.Left() != 0 || closed > 1) return false;
                if (closed) item.Points.push_back(item.Points.front());
                item.Kind = ExbGeometryKind::Polyline;
                return finish();
            }

            // 图片：对角两点 (x, y)、转角、两个数、[u16 0][u16 0x80][i32 图片定义 ID] …；带转角的还没核对过，不显示
            if (IsClass(classId, kImageClass))
            {
                Cursor q(d, rec.Body + fmt.AttrWidth, rec.BodyEnd);
                const double x0 = q.F64(), y0 = q.F64(), x1 = q.F64(), y1 = q.F64(), rotation = q.F64();
                q.Skip(16 + 4);
                const std::int32_t imageId = static_cast<std::int32_t>(q.U32());
                if (!q.Ok() || std::fabs(rotation) > 1.0e-9 || !(std::fabs(x1 - x0) > 0.0) || !(std::fabs(y1 - y0) > 0.0))
                    return false;
                item.Kind     = ExbGeometryKind::Image;
                item.Points   = {ExbPoint2{x0, y0}, ExbPoint2{x1, y1}};
                item.ImageRef = imageId;
                return finish();
            }

            // 区域覆盖：正文末尾是 [u32 n][u32 n] + n 个边框顶点 (x, y, z)，只画边框
            if (IsClass(classId, kWipeoutClass))
            {
                for (std::size_t n = 3; n <= 4096 && fmt.AttrWidth + 8 + n * 24 <= len; ++n)
                {
                    const std::uint8_t* tail = d + rec.BodyEnd - n * 24;
                    if (ReadU32(tail - 4) != n || ReadU32(tail - 8) != n) continue;
                    Cursor w(d, rec.BodyEnd - n * 24, rec.BodyEnd);
                    item.Points.assign(n, ExbPoint2{});
                    bool ok = true;
                    for (ExbPoint2& p : item.Points) ok = PlanarPoint(w, p) && ok;
                    if (!ok) break;
                    item.Kind = ExbGeometryKind::Polyline;
                    return finish();
                }
                return false;
            }

            // 属性、属性定义：正文前面还有标记名等字段，文字部分的布局与文字实体相同，按结构找出来
            if (IsClass(classId, kAttributeClass) || IsClass(classId, kAttDefClass))
            {
                const Span r{rec.Body - 14, rec.BodyEnd};
                if (MatchText(d, r, item)) return finish();
            }
            // 其余没对上类型的类不出几何：按结构去猜会画出不存在的线
            return false;
        }

        // 块记录正文：[文字 块名] + 其他
        std::string BlockName(const std::uint8_t* d, const Record& rec)
        {
            Cursor c(d, rec.Body, rec.BodyEnd);
            std::string name;
            c.Text(&name);
            return c.Ok() ? name : std::string{};
        }

        struct WalkStats
        {
            std::size_t UnparsedBytes = 0;
            std::map<std::string, std::size_t> UnclassifiedByClass;
        };

        bool ParseBlocks(const std::uint8_t* data, std::size_t size, unsigned major, std::vector<ExbBlockDef>& blocks,
                         WalkStats& stats)
        {
            blocks.clear();
            const Format fmt = DetectFormat(data, size, major);
            std::vector<Record> records;
            std::vector<std::size_t> blockOf;
            if (!WalkRecords(data, size, fmt, records, blockOf, stats.UnparsedBytes)) return false;

            std::vector<std::size_t> slot(records.size(), 0);
            for (std::size_t i = 0; i < records.size(); ++i)
            {
                if (blockOf[i] == i)
                {
                    slot[i] = blocks.size();
                    ExbBlockDef def;
                    def.Id   = records[i].Id;
                    def.Name = BlockName(data, records[i]);
                    blocks.push_back(std::move(def));
                    continue;
                }

                ExbBlockDef& def = blocks[slot[blockOf[i]]];
                ++def.RecordCount;
                ExbGeometryItem item;
                if (!ClassifyRecord(data, records[i], fmt, 0, item))
                {
                    ++def.UnclassifiedCount;
                    ++stats.UnclassifiedByClass[ClassHex(data + records[i].Header)];
                    continue;
                }
                def.Items.push_back(std::move(item));
            }
            return true;
        }
    }

    // 图片定义流：流头之后一条记录，正文是 [文字 绝对路径][文字 相对路径] …
    bool ParseImageDef(const std::uint8_t* data, std::size_t size, ExbImageDef& def, unsigned productMajor)
    {
        def = ExbImageDef{};
        if (!data || size < 64) return false;
        Format fmt;
        if (productMajor >= 10) fmt.Major = productMajor;
        for (std::size_t p = 0; p + 30 <= size; ++p)
        {
            if (!IsClass(data + p, kImageDefClass)) continue;
            Record rec;
            if (!ParseRecord(data, size, p, fmt, 0, rec)) continue;
            Cursor c(data, rec.Body, rec.BodyEnd);
            c.Text(&def.Path);
            c.Text(&def.RelativePath);
            if (!c.Ok()) continue;
            def.Id = rec.Id;
            return true;
        }
        return false;
    }

    std::string UnsupportedClassName(const std::string& classId)
    {
        if (classId == ClassHex(kPdfClass.data())) return "PDF underlay";
        if (classId == ClassHex(kViewportClass.data())) return "layout viewport";
        if (classId == ClassHex(kImageClass.data())) return "rotated image";
        if (classId == ClassHex(kHatchClass.data())) return "hatch";
        return {};
    }

    // 文字样式正文：[文字 名字][文字 别名][文字 西文字体][文字 中文字体] 西文宽度因子、中文宽度因子、字距、行距、
    //   字高、倾斜角、缩进（都是 f64），主版本 > 10 再跟 [u32][文字 形文件]，18 起再跟 3 个 u16。
    void ParseTextStyles(const std::uint8_t* data, std::size_t size, std::vector<ExbTextStyleDef>& styles,
                         unsigned productMajor)
    {
        styles.clear();
        if (!data || size < 64) return;
        static constexpr auto kTextStyleClass = Guid("b9bd09d2-0eaa-4fe7-9ff6-a23f19fe8954");
        Format fmt;
        if (productMajor >= 10) fmt.Major = productMajor;
        for (std::size_t p = 0; p + 30 <= size; ++p)
        {
            if (!IsClass(data + p, kTextStyleClass)) continue;
            Record rec;
            if (!ParseRecord(data, size, p, fmt, 0, rec)) continue;
            Cursor c(data, rec.Body, rec.BodyEnd);
            ExbTextStyleDef def;
            def.Id = rec.Id;
            c.Text(&def.Name);
            c.Text();
            c.Text(&def.WesternFont);
            c.Text(&def.CjkFont);
            def.WidthFactor = c.F64();
            c.Skip(24);
            def.Height  = c.F64();
            def.Oblique = c.F64();
            if (!c.Ok()) continue;
            styles.push_back(std::move(def));
            p = rec.BodyEnd - 1;
        }
    }

    bool ParseBlockDefinitions(const std::uint8_t* data, std::size_t size, std::vector<ExbBlockDef>& blocks,
                               unsigned productMajor, std::map<std::string, std::size_t>* unclassified)
    {
        blocks.clear();
        if (!data || size < 64) return false;
        WalkStats stats;
        if (!ParseBlocks(data, size, productMajor, blocks, stats)) return false;
        if (unclassified)
            for (const auto& [classId, count] : stats.UnclassifiedByClass) (*unclassified)[classId] += count;
        return true;
    }

    bool ParseBlockStream(const std::uint8_t* data, std::size_t size, ExbGeometryResult& out, unsigned productMajor)
    {
        out = ExbGeometryResult{};
        if (!data || size < 64) return false;

        std::vector<ExbBlockDef> blocks;
        WalkStats stats;
        if (!ParseBlocks(data, size, productMajor, blocks, stats))
            return ParseBlockStreamByMarks(data, size, out);

        out.UnparsedBytes       = stats.UnparsedBytes;
        out.UnclassifiedByClass = std::move(stats.UnclassifiedByClass);
        out.RecordCount         = blocks.front().RecordCount;
        out.UnclassifiedCount   = blocks.front().UnclassifiedCount;
        out.Items               = std::move(blocks.front().Items);
        for (const ExbGeometryItem& item : out.Items)
        {
            switch (item.Kind)
            {
            case ExbGeometryKind::Text:     ++out.TextCount; break;
            case ExbGeometryKind::Polyline: ++out.PolylineCount; break;
            case ExbGeometryKind::Line:     ++out.LineCount; break;
            case ExbGeometryKind::Circle:   ++out.CircleCount; break;
            case ExbGeometryKind::Arc:      ++out.ArcCount; break;
            case ExbGeometryKind::Point:    ++out.PointCount; break;
            case ExbGeometryKind::Insert:   ++out.InsertCount; break;
            default:                        ++out.OtherCount; break;
            }
        }
        return true;
    }

    // ── 图层表解析 ────────────────────────────────────────────────────────
    //
    // 图层记录（payload）是**四个连续的「长度 + UTF-16 串」**：
    //
    //   [u32 nameLen][name][u32 altLen][alt][u32 keyLen][key][8B 颜色/状态]
    //   [u32 28][desc = "UnInit Layer!\0"]  [f32 线宽] ...
    //
    // 旧实现拿 `0x5C42` 当「图层类 ID」去找记录——**那是错的**，0x5C42 是汉字
    // 「层」的 UTF-16 码点，命中的其实是中文名以「层」结尾的位置。于是只读到了紧跟
    // 在「层」字后面的 alternate_name（英文名），凡是中文名不以「层」结尾的图层
    // （45 缝纫线 / 46 折线 / 47 轮廓线 / 48 美工 / 52 _改_车头 / 53 摊开）全被漏掉，
    // 之前只能靠硬编码表按「数量 + 渲染形态」去猜 ID 对应哪一层，而且猜错了。
    //
    // 现在改为以 desc 恒为 "UnInit Layer!" 为锚，向前回溯出四个串，ID 从记录头取。
    // 跨代差异：大图（GenA/B/C）记录头 30 字节、ID 在 payload−14；旧代小文件
    // （pl3/lay6/工程图文档1）该位置的取值是同一个常数（实测 −1725969797），
    // 不是 ID，需要退回「连续两个负 i32」回溯。用「候选值是否互异」自动区分。
    namespace
    {
        constexpr char kLayerDesc[] = "UnInit Layer!";   // 13 个 ASCII 字符
        constexpr std::size_t kLayerDescChars = 13;
        constexpr std::size_t kLayerDescBytes = kLayerDescChars * 2 + 2;   // 含 NUL = 28
    }

    void ParseStyleStream(const std::uint8_t* data, std::size_t size, std::vector<ExbLayerDef>& layers)
    {
        layers.clear();

        struct Hit { std::size_t Payload; std::size_t Desc; std::string Name; std::string Alt; };
        std::vector<Hit> hits;

        constexpr std::uint32_t kDescLen = static_cast<std::uint32_t>(kLayerDescBytes);

        for (std::size_t p = 4; p + kLayerDescBytes <= size; ++p)
        {
            // 快速筛：desc 的 UTF-16 首字符必须是 'U'
            if (data[p] != 'U' || data[p + 1] != 0) continue;
            if (ReadU32(data + p - 4) != kDescLen) continue;

            bool same = true;
            for (std::size_t k = 0; k < kLayerDescChars && same; ++k)
                same = (data[p + 2 * k] == static_cast<std::uint8_t>(kLayerDesc[k]))
                    && (data[p + 2 * k + 1] == 0);
            if (!same) continue;
            if (data[p + 26] != 0 || data[p + 27] != 0) continue;

            // 向前回溯：8 字节颜色/状态 → key → alt → name
            const std::size_t pColor = p - 4 - 8;
            if (pColor < 4) continue;

            // key 字段是中文名的副本，长度随名字变化（"0层"=2 字节含 NUL，
            // "中心线层"=10 字节），范围放到 2..64，只对偶数试
            bool found = false;
            for (std::size_t keyLen = 2; keyLen <= 64 && !found; keyLen += 2)
            {
                if (pColor < keyLen + 4) continue;
                const std::size_t keyPos = pColor - keyLen - 4;
                if (ReadU32(data + keyPos) != keyLen) continue;

                // alt：长度 0..200 都合法（45..53 是空串，长度 2）
                for (std::size_t altLen = 0; altLen <= 200; altLen += 2)
                {
                    if (keyPos < altLen + 4) continue;
                    const std::size_t altPos = keyPos - altLen - 4;
                    if (ReadU32(data + altPos) != altLen) continue;
                    // 串必须以 NUL 结尾，否则是错位匹配（内容在 altPos+4 起，长 altLen）
                    if (altLen >= 2 &&
                        (data[altPos + altLen + 2] != 0 || data[altPos + altLen + 3] != 0))
                        continue;

                    std::string alt;
                    if (altLen > 2)
                    {
                        bool ok = false;
                        if (!DecodeUtf16Le(data + altPos + 4, altLen, alt, ok) || !ok) continue;
                    }

                    // name 的长度字段在 altPos 之前，长度不定，逐个试
                    std::string name;
                    std::size_t namePos = 0;
                    for (std::size_t nameLen = 2; nameLen <= 200; nameLen += 2)
                    {
                        if (altPos < nameLen + 4) break;
                        const std::size_t nlPos = altPos - nameLen - 4;
                        if (ReadU32(data + nlPos) != nameLen) continue;
                        if (data[altPos - 2] != 0 || data[altPos - 1] != 0) continue;
                        bool ok = false;
                        if (!DecodeUtf16Le(data + nlPos + 4, nameLen, name, ok) ||
                            !ok || name.empty())
                            continue;
                        if (name.rfind("UnInit", 0) == 0) continue;
                        namePos = nlPos;
                        break;
                    }
                    if (namePos == 0) continue;

                    hits.push_back(Hit{namePos, p, std::move(name), std::move(alt)});
                    found = true;
                    break;
                }
            }
        }

        if (hits.empty()) return;

        // 判断 payload−14 是否为真 ID：大图 17 个图层各不相同，旧代小文件是同一个常数
        bool idUsable = false;
        if (hits.size() >= 2)
        {
            const std::int32_t a = static_cast<std::int32_t>(ReadU32(data + hits[0].Payload - 14));
            for (std::size_t i = 1; i < hits.size() && !idUsable; ++i)
                idUsable = (static_cast<std::int32_t>(ReadU32(data + hits[i].Payload - 14)) != a);
        }

        for (const Hit& h : hits)
        {
            ExbLayerDef def;
            // 面板显示优先英文名（与既有行为一致），没有英文名的图层（45..53）用中文名
            def.Name = h.Alt.empty() ? h.Name : h.Alt;

            // 图层属性：颜色在 desc 长度字段前 12 字节（u32），
            // 线宽在 desc 内容起点 +28（f32），线型在 +40（i32）。三处均经 ezexb 真值核对。
            if (h.Desc >= 12)
            {
                def.ColorWord = ReadU32(data + h.Desc - 12);
                {
                    const std::uint32_t status = ReadU32(data + h.Desc - 8);
                    def.Visible = (status & 0x40000000u) != 0 && (status & 0x04000000u) == 0;
                    def.Locked  = (status & 0x20000000u) != 0;
                }
                if ((def.ColorWord & 0xFFFFFFu) == 0) def.ColorWord = 0x00FFFFFFu;
            }
            if (h.Desc + 44 <= size)
            {
                // 线宽是 f32。2024 版实测是 0 / 0.25 / 0.5（CAXA 换了 ISO 默认线宽表，
                // 0 表示随默认），2013 版是 0.18 / 0.35，两者都合法，所以只做范围过滤。
                const double w = static_cast<double>(ReadF32(data + h.Desc + 28));
                if (w >= 0.0 && w <= 10.0) def.LineWidth = w;
                def.LineTypeId = static_cast<std::int32_t>(ReadU32(data + h.Desc + 40));
            }

            if (idUsable && h.Payload >= 14)
            {
                def.Handle = static_cast<std::int32_t>(ReadU32(data + h.Payload - 14));
            }
            else
            {
                // 旧代：句柄在名字前面，形如 `[i32 H][i32 K]`，两者都负且 H < K
                // （lay6 是 (−20,−19)，pl3 是 (−26,−25)）
                for (std::size_t back = 4; back <= 64; ++back)
                {
                    if (h.Payload < back + 4) break;
                    const std::int32_t hh = static_cast<std::int32_t>(ReadU32(data + h.Payload - back));
                    const std::int32_t kk = static_cast<std::int32_t>(ReadU32(data + h.Payload - back + 4));
                    if (hh >= 0 || kk >= 0 || hh >= kk || hh < -100000) continue;
                    def.Handle = hh;
                    break;
                }
            }

            if (std::none_of(layers.begin(), layers.end(),
                             [&](const ExbLayerDef& e) { return e.Name == def.Name; }))
                layers.push_back(std::move(def));
        }
    }

    void ParseStyleStream(const std::uint8_t* data, std::size_t size, std::vector<std::string>& layers)
    {
        std::vector<ExbLayerDef> defs;
        ParseStyleStream(data, size, defs);
        layers.clear();
        layers.reserve(defs.size());
        for (const ExbLayerDef& d : defs) layers.push_back(d.Name);
    }

    // ── 线型表 ────────────────────────────────────────────────────────────
    //
    // 线型表和图层表在同一个 /*StyleStream 里：线型表在前（offset 380 起），
    // 图层表在后。两类记录各自用固定标记开头，所以能独立扫描，互不干扰。
    //
    // 结构（跨 5 版一致，逐字段与 ezexb 的 linetypes 对过）：
    //
    //     [6B 标记 87 92 1b 4f 5d 0b][i32 ID][i32 −12][u16 1][u32 记录长−30]
    //     [u32 名长][UTF-16 主名]
    //     [u32 说明长][UTF-16 示意图串]
    //     [f32 图案比例][u32 节拍数][u16 0xFFFF][n × f32 节拍]
    //     [u32 英文名长][UTF-16 英文名]
    //
    // 记忆里「6 字节标记」容易被巧合命中，所以三重校验：主名可解码且非空、
    // 说明可解码、节拍数 ≤ 64 且整条记录不越过流尾。
    namespace
    {
        constexpr std::uint8_t kLineTypeMagic[6] = { 0x87, 0x92, 0x1b, 0x4f, 0x5d, 0x0b };
    }

    void ParseLineTypeTable(const std::uint8_t* data, std::size_t size,
                            std::vector<ExbLineTypeDef>& lineTypes)
    {
        lineTypes.clear();
        if (data == nullptr || size < 64) return;

        for (std::size_t p = 0; p + 24 <= size; ++p)
        {
            // 快速筛：标记首字节出现频率极低
            if (data[p] != kLineTypeMagic[0]) continue;
            bool same = true;
            for (std::size_t k = 1; k < 6 && same; ++k) same = (data[p + k] == kLineTypeMagic[k]);
            if (!same) continue;
            if (p + 28 > size) continue;

            const std::int32_t id = static_cast<std::int32_t>(ReadU32(data + p + 6));
            // ID 只有两类：内置负数（−13..−18）与文件自定义正数（41..44）。留宽一点。
            if (id < -64 || id == 0 || id > 4096) continue;

            const std::uint32_t nameLen = ReadU32(data + p + 20);
            if (nameLen < 2 || nameLen > 128 || (nameLen & 1u) != 0 || p + 24 + nameLen > size) continue;

            std::string name;
            {
                bool ok = false;
                if (!DecodeUtf16Le(data + p + 24, nameLen, name, ok) || !ok || name.empty()) continue;
            }

            const std::size_t q = p + 24 + nameLen;
            if (q + 4 > size) continue;
            const std::uint32_t descLen = ReadU32(data + q);
            if ((descLen & 1u) != 0 || descLen > 600 || q + 4 + descLen > size) continue;

            std::string desc;
            if (descLen > 2)
            {
                bool ok = false;
                if (!DecodeUtf16Le(data + q + 4, descLen, desc, ok) || !ok) continue;
            }

            const std::size_t r = q + 4 + descLen;
            if (r + 10 > size) continue;

            const double scale = static_cast<double>(ReadF32(data + r));
            if (!(scale >= 0.0 && scale <= 1000.0)) continue;   // 同时挡掉 NaN

            const std::uint32_t n = ReadU32(data + r + 4);
            if (n > 64) continue;
            if (r + 12 + static_cast<std::size_t>(n) * 4 > size) continue;

            // n 后面可能有一个 2 字节字段，再往后才是节拍数组。这个字段的取值**不固定**：
            //   12.5/15/16/17/18 大图里是 0xFFFF，工程图文档1.exb 里是 0x5555 / 0x5FFF，
            // 所以不能拿 0xFFFF 当判据（早先就是这么写的，旧代样本整条读成非规格化浮点）。
            // n = 0 时该字段一定不存在：ByLayer 的 n 后面直接就是英文名长度字段。
            //
            // 改为对两个候选起点打分：哪种切法能让「n 个节拍都像长度 + 英文名长度合法」
            // 就选哪种。节拍合法 = 0（画点）或落在 1e-4 … 1e6 的正常量级。
            auto dashOk = [](float v)
            {
                const double d = static_cast<double>(v);
                if (d == 0.0) return true;
                const double a = std::fabs(d);
                return a >= 1.0e-4 && a < 1.0e6;
            };
            auto altScore = [data, size, n](std::size_t q2) -> int
            {
                const std::size_t s = q2 + static_cast<std::size_t>(n) * 4;
                if (s + 4 > size) return -1000;
                const std::uint32_t len = ReadU32(data + s);
                if ((len & 1u) != 0 || len > 128 || s + 4 + len > size) return -1000;
                if (len <= 2) return 1;                     // 空串也合法
                std::string tmp;
                bool ok = false;
                if (!DecodeUtf16Le(data + s + 4, len, tmp, ok) || !ok) return -1000;
                return 2;
            };

            std::size_t bestQ = r + 8;
            int bestScore = -100000;
            for (std::size_t cand : { r + 8, r + 10 })
            {
                if (cand + static_cast<std::size_t>(n) * 4 > size) continue;
                int score = 0;
                for (std::uint32_t i = 0; i < n; ++i)
                    if (dashOk(ReadF32(data + cand + i * 4))) ++score;
                    else score -= 4;
                // n = 0 时没有这个字段，直接定在 r+8，避免两候选同分时误选
                if (n == 0 && cand != r + 8) continue;
                score += altScore(cand);
                if (score > bestScore) { bestScore = score; bestQ = cand; }
            }

            ExbLineTypeDef def;
            def.Id          = id;
            def.Name        = std::move(name);
            def.Description = std::move(desc);
            def.Scale       = scale > 0.0 ? scale : 1.0;
            def.Dashes.reserve(n);
            for (std::uint32_t i = 0; i < n; ++i)
                def.Dashes.push_back(static_cast<double>(ReadF32(data + bestQ + i * 4)));

            // 英文别名（Continuous / Dash Dot / 空）
            const std::size_t s = bestQ + static_cast<std::size_t>(n) * 4;
            if (s + 4 <= size)
            {
                const std::uint32_t altLen = ReadU32(data + s);
                if ((altLen & 1u) == 0 && altLen <= 128 && s + 4 + altLen <= size && altLen > 2)
                {
                    bool ok = false;
                    DecodeUtf16Le(data + s + 4, altLen, def.Alt, ok);
                    if (!ok) def.Alt.clear();
                }
            }

            if (std::none_of(lineTypes.begin(), lineTypes.end(),
                             [&](const ExbLineTypeDef& e) { return e.Id == def.Id; }))
                lineTypes.push_back(std::move(def));
        }
    }

    // ── 图层 ID 解析 ──────────────────────────────────────────────────────
    //
    // 解析出真实图层表之后这里只需要精确匹配：大图的负句柄（−20..−27）与正数持久
    // ID（45..53）都在样式流的记录头里，逐条对得上。
    //
    // 旧实现有一张 `kKnownLayerIds` 硬编码表，把 45..53 按「实体数量 + 渲染形态」
    // 猜成 Hatch-line / Dash line / Hiden / Solid / Dimension / Dash dot / Thick Solid，
    // **全部猜错**。真值（样式流里的中文名）是：
    //   45 缝纫线 46 折线 47 轮廓线 48 美工 49 6文字层 50 轮廓虚线层 51 Defpoints
    //   52 _改_车头 53 摊开
    // 猜错的原因见 ParseStyleStream 顶部注释（0x5C42 是「层」字的码点）。
    int ResolveLayerIndex(std::int32_t layerId, const std::vector<ExbLayerDef>& layers)
    {
        if (layers.empty()) return -1;

        // ① ID 精确匹配
        for (std::size_t i = 0; i < layers.size(); ++i)
            if (layers[i].Handle != 0 && layers[i].Handle == layerId)
                return static_cast<int>(i);

        // ② 旧代连续句柄：`句柄 = 首句柄 + 定义序号`
        //    （lay6 是 −20..−27，pl3 是 −26..−33，pl3x 的线 −31 即第 6 层 Dimension）
        std::int32_t first = 0;
        bool haveFirst = false;
        for (const ExbLayerDef& d : layers)
            if (d.Handle < 0) { first = d.Handle; haveFirst = true; break; }
        if (haveFirst)
        {
            const std::int32_t n = static_cast<std::int32_t>(layers.size());
            if (layerId <= first && layerId > first - n)
                return static_cast<int>(first - layerId);
        }
        return -1;
    }
}
