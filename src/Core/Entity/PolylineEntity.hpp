#pragma once
#include "../GeomKernel/Polyline.hpp"
#include "../Math/Point3.hpp"
#include "../Math/Color4.hpp"
#include "Entity.hpp"
#include "ICurveEntity.hpp"
#include "../Draw/CurveSampling.hpp"
#include "../GeomKernel/Curves.hpp"
#include <algorithm>

namespace MiniCAD
{
    class PolylineEntity : public Entity, public ICurveEntity
    {
    public:
        // ── Construction ─────────────────────────────────────────────────────
        PolylineEntity(ObjectID id, Polyline polyline)
            : Entity(id)
            , m_polyline(std::move(polyline))
        {}

        // Convenience: pure-line polyline from a point list
        PolylineEntity(ObjectID id, std::vector<Math::Point3> pts)
            : Entity(id)
            , m_polyline(std::move(pts))
        {}

        // Polyline with mixed line/arc segments
        PolylineEntity(ObjectID id, std::vector<Math::Point3> pts, std::vector<double>  bulges)
            : Entity(id)
            , m_polyline(std::move(pts), std::move(bulges))
        {}

        // ── Accessors ─────────────────────────────────────────────────────────
        void             SetPolyline(Polyline pl) { m_polyline = std::move(pl); SyncSegmentWidths(); }
        const Polyline&  GetPolyline()      const { return m_polyline; }

        // 多段线恒定宽度(DXF LWPOLYLINE 组码 43,模型单位)。>= 1 时按粗线填充绘制。
        void   SetWidth(double w) { m_width = w; }
        double GetWidth() const   { return m_width; }

        // ── 每段起止宽度（锥形段）──────────────────────────────────────────────
        // 与 DXF 顶点组码 40/41 同义：顶点 i 的起/止宽度作用于「顶点 i → 顶点 i+1」这一段，
        // 于是相邻两段可以在同一顶点处取不同宽度（首尾不一致）。段数 = Points.size()-1；
        // 闭合多段线（末点与起点重合）的最后一段就是闭合段。
        //   某端为 0 → 该端沿用恒定宽度 GetWidth()（DXF 的「0 = 用默认值」）。
        // 数组为空 = 整条多段线只有恒定宽度（此时绘制路径与旧行为完全一致）。
        bool HasSegmentWidths() const
        {
            for (double w : m_segStartWidth) if (w != 0.0) return true;
            for (double w : m_segEndWidth)   if (w != 0.0) return true;
            return false;
        }

        const std::vector<double>& GetSegmentStartWidths() const { return m_segStartWidth; }
        const std::vector<double>& GetSegmentEndWidths()   const { return m_segEndWidth; }

        // 第 seg 段起点端的实际宽度（显式值为 0 时回退到恒定宽度）
        double SegmentStartWidth(int seg) const
        {
            const double w = (seg >= 0 && seg < static_cast<int>(m_segStartWidth.size())) ? m_segStartWidth[seg] : 0.0;
            return w != 0.0 ? w : m_width;
        }
        // 第 seg 段终点端的实际宽度
        double SegmentEndWidth(int seg) const
        {
            const double w = (seg >= 0 && seg < static_cast<int>(m_segEndWidth.size())) ? m_segEndWidth[seg] : 0.0;
            return w != 0.0 ? w : m_width;
        }

        // 逐段写入起止宽度（原始值，0 保留为「用恒定宽度」）。
        // 两数组都会按段数补齐 / 截断，长度不一致时短的一方补 0。
        void SetSegmentWidths(std::vector<double> startW, std::vector<double> endW)
        {
            const size_t segs = m_polyline.IsValid() ? static_cast<size_t>(m_polyline.SegCount()) : 0;
            startW.resize(segs, 0.0);
            endW.resize(segs, 0.0);
            m_segStartWidth = std::move(startW);
            m_segEndWidth   = std::move(endW);
        }

        void SetSegmentWidth(int seg, double startW, double endW)
        {
            const size_t segs = m_polyline.IsValid() ? static_cast<size_t>(m_polyline.SegCount()) : 0;
            if (m_segStartWidth.size() != segs) m_segStartWidth.resize(segs, 0.0);
            if (m_segEndWidth.size()   != segs) m_segEndWidth.resize(segs, 0.0);
            if (seg < 0 || seg >= static_cast<int>(segs)) return;
            m_segStartWidth[seg] = startW;
            m_segEndWidth[seg]   = endW;
        }

        void ClearSegmentWidths() { m_segStartWidth.clear(); m_segEndWidth.clear(); }

        // 整条多段线上最大的实际宽度（用于粗线判定的阈值与包围盒扩张）
        double MaxEffectiveWidth() const
        {
            if (!HasSegmentWidths()) return m_width;
            double m = 0.0;
            for (int i = 0; i < m_polyline.SegCount(); ++i)
                m = std::max(m, std::max(SegmentStartWidth(i), SegmentEndWidth(i)));
            return m;
        }

        // ── Point editing helpers ─────────────────────────────────────────────
        void AddPoint(const Math::Point3& pt, double bulge = 0.0)
        {
            if (!m_polyline.Points.empty())
                m_polyline.Bulges.push_back(bulge);
            m_polyline.Points.push_back(pt);

            // 新增一段：起止宽度补 0（= 该段沿用恒定宽度）。
            // 按 DXF 语义，宽度属于「顶点 i → i+1」这一段，所以新段对应新顶点自身的宽度，
            // 而它此前没有值。
            SyncSegmentWidths();
        }

        // Replace the last bulge value (e.g. to switch a segment from line→arc
        // after the endpoint has already been placed).
        void SetLastBulge(double bulge)
        {
            if (!m_polyline.Bulges.empty())
                m_polyline.Bulges.back() = bulge;
        }

        void SetPoint(int i, const Math::Point3& pt)
        {
            m_polyline.Points[i] = pt;
        }

        void SetBulge(int seg, double bulge)
        {
            if (seg >= 0 && seg < static_cast<int>(m_polyline.Bulges.size()))
                m_polyline.Bulges[seg] = bulge;
        }

        void RemoveLastPoint()
        {
            if (m_polyline.Points.empty())
                return;
            m_polyline.Points.pop_back();

            if (!m_polyline.Bulges.empty())
                m_polyline.Bulges.pop_back();

            SyncSegmentWidths();   // 少一段
        }

        int PointCount() const
        {
            return static_cast<int>(m_polyline.Points.size());
        }

        // ── Entity interface ──────────────────────────────────────────────────
        virtual AABB GetBoundingBox() const override
        {
            AABB box = m_polyline.GetBounds();

            // 按粗线绘制时，包围盒需向外扩张半宽（与填充几何范围一致）。
            // 有锥形段时取所有段里最大的实际宽度，否则包围盒会切掉较宽的那几段。
            const double lineWidth = MaxEffectiveWidth();
            if (DrawsAsBand())
            {
                const double hw = lineWidth * 0.5;
                box.Min.x -= hw; box.Min.y -= hw;
                box.Max.x += hw; box.Max.y += hw;
            }
            return box;
        }

        // 是否按「有宽度的带子」绘制。
        //
        // ⚠ 判据不是「宽度 >= 1」，而是「宽度 > 0」：
        //   旧实现（v0.2.0）用 `m_width >= 1.0`，把「宽度」当成了**线宽**（Lineweight 那种 1px 起算的
        //   屏显线宽）的近似。但 LWPOLYLINE 的组码 40/41/43 是**实打实的模型单位宽度** ——
        //   `sample_AC1032.dwg` 句柄 35B 的段宽是 0.2 → 0.1、末顶点 0.1 → 0.5，全部 < 1，
        //   于是数据虽然保住了，画面依旧是一条细线，用户看到的「不同线段宽度不同」完全没有体现。
        //   1.0 这个门槛对 0.2 这种真实宽度是错的：0.2 个模型单位在图上就是 0.2 个模型单位宽。
        //
        // 恒宽多段线的默认值是 1.0（`m_width = 1.0`），它同样会走进带子路径 ——
        // 这与旧实现一致（旧实现 `1.0 >= 1.0` 也走带子），所以默认新建的多段线外观不变。
        bool DrawsAsBand() const
        {
            if (HasSegmentWidths())
                return MaxEffectiveWidth() > kWidthEpsilon;   // 有逐段宽度：只要不是全 0 就画带子
            return m_width > kWidthEpsilon;                   // 恒宽：> 0 即画带子
        }

        // ── ICurveEntity：作为参数曲线参与求交 / 修剪 / 延伸 / 打断 ───────────
        std::unique_ptr<ICurve> MakeCurve() const override { return std::make_unique<PolylineCurve>(m_polyline); }
        ICurveEntity*           AsCurveEntity()       override { return this; }
        const ICurveEntity*     AsCurveEntity() const override { return this; }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<PolylineEntity>(newId, m_polyline);
            e->SetAttr(GetAttr());
            e->m_width = m_width;
            e->m_segStartWidth = m_segStartWidth;
            e->m_segEndWidth   = m_segEndWidth;
            return e;
        }
         
        virtual void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            if (!m_polyline.IsValid()) return;

            const auto& attr  = GetAttr();
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor : isHovered ? IDrawSink::kHoverColor : ResolveDrawColor(sink);
            constexpr double kAngleTol = Math::PI / 36.0;   // 5°

            // 有宽度：实体由填充三角形拼接而成（折点处用 miter 尖角补缺口）。
            // 宽度为 0：直接绘制直线，开销最低。
            if (DrawsAsBand())
            {
                // 虚线必须逐段按线型铺设，不能整体填充 —— 否则线型被完全忽略。
                // 连续线仍走整体填充，以保留折点处的斜接尖角。
                if (sink.IsCurrentLinetypeDashed())
                {
                    for (int i = 0; i < m_polyline.SegCount(); ++i)
                    {
                        const auto& A = m_polyline.SegStart(i);
                        const auto& B = m_polyline.SegEnd(i);
                        if (m_polyline.SegIsLine(i))
                        {
                            sink.DrawStyledLine(A, B, color, std::max(SegmentStartWidth(i), SegmentEndWidth(i)));
                        }
                        else
                        {
                            // 圆弧细分点上宽度按弧长线性插值，端点用本段的起止宽度
                            const double ws = SegmentStartWidth(i), we = SegmentEndWidth(i);
                            if (!SampleArcSegment(sink, A, B, m_polyline.SegBulge(i), kAngleTol,
                                                  [&](const Math::Point3& p, const Math::Point3& q, double t0, double t1)
                                                  {
                                                      sink.DrawStyledLine(p, q, color, std::max(ws + (we - ws) * t0, ws + (we - ws) * t1));
                                                  }, 0.5 * std::max(ws, we)))
                                sink.DrawStyledLine(A, B, color, std::max(ws, we));
                        }
                    }
                    return;
                }

                const std::vector<Node> nodes = BuildNodes(sink, kAngleTol);
                EmitThickPolyline(sink, nodes, color);
                return;
            }

            for (int i = 0; i < m_polyline.SegCount(); ++i)
            {
                const auto& A = m_polyline.SegStart(i);
                const auto& B = m_polyline.SegEnd(i);

                if (m_polyline.SegIsLine(i))
                {
                    sink.DrawLine(A, B, color, false);
                }
                else if (!SampleArcSegment(sink, A, B, m_polyline.SegBulge(i), kAngleTol,
                                           [&](const Math::Point3& p, const Math::Point3& q, double, double) { sink.DrawLine(p, q, color, false); }))
                {
                    sink.DrawLine(A, B, color, false);
                }
            }
        }

        DECLARE_RUNTIME_TYPE(PolylineEntity, Entity)

    private:
        // 圆弧段 A→B 按参数 t ∈ [0, 1] 细分，取点与 Polyline::TessellateArc 相同（每 angleTol 一段，末点就是 B）；
        // 深度放大时 sink 要求在视图附近加密（见 SampleCurveSpans；halfWidth：带宽度时的半宽，外边缘比中心线伸出这么多）。
        // emit(p, q, tp, tq)。半径太小（当直线段画）时返回 false
        template <class Emit>
        static bool SampleArcSegment(const IDrawSink& sink, const Math::Point3& A, const Math::Point3& B, double bulge, double angleTol, Emit&& emit,
                                     double halfWidth = 0.0)
        {
            const ArcGeom arc = Polyline::ComputeArc(A, B, bulge);
            if (arc.Radius < Math::LengthEPS)
                return false;
            const int segs = std::max(2, static_cast<int>(std::ceil(std::abs(arc.SweepAngle) / angleTol)));
            SampleCurveSpans(sink, 0.0, 1.0, segs, (arc.Radius + halfWidth) * arc.SweepAngle * arc.SweepAngle,
                             [&](double t)
                             {
                                 if (t == 1.0) return B;
                                 const double angle = arc.StartAngle + t * arc.SweepAngle;
                                 return Math::Point3{ arc.Center.x + arc.Radius * std::cos(angle), arc.Center.y + arc.Radius * std::sin(angle),
                                                      A.z + t * (B.z - A.z) };
                             },
                             emit, halfWidth);
            return true;
        }

        // ── 粗线（宽度 >= 1）三角形拼接 ──────────────────────────────────────
        // 把中心线展开为带宽度的填充几何。每段两端的半宽可以不同（DXF 顶点组码 40/41
        // 的锥形段，例如 `sample_AC1032.dwg` 句柄 35B：顶点 0 起止宽度 0.2 → 0.1，
        // 末顶点 0.1 → 0.5），因此：
        //   · 每段 → 一个梯形（两端半宽相同时退化为矩形）
        //   · 每个内部折点 → 斜接尖角，两侧偏移边按相邻两段各自的半宽求交点
        // 恒宽多段线走同一条代码路径，几何与旧实现完全一致（矩形 + miter 尖角）。

        // 中心线上的采样点。关键：折点处「进入段的终点宽度」与「离开段的起点宽度」是两个
        // 独立的值（这正是首尾宽度不一致的来源），所以必须分别记录：
        //   hIn  = 到达本点的那一段在本点处的半宽
        //   hOut = 从本点出发的那一段在本点处的半宽
        // 段内插值点有 hIn == hOut。DXF 的宽度对称于中心线，故不区分左右侧。
        struct Node
        {
            Math::Point3 P;
            double       hIn  = 0.0;
            double       hOut = 0.0;
        };

        // 把多段线按角度容差离散成带半宽的节点序列（含所有顶点与圆弧插值点）。
        // 圆弧段上的半宽按弧长线性插值——宽度沿段均匀变化，与 AutoCAD 一致。
        std::vector<Node> BuildNodes(const IDrawSink& sink, double angleTol) const
        {
            std::vector<Node> nodes;
            if (!m_polyline.IsValid()) return nodes;

            const int segs = m_polyline.SegCount();
            auto halfOf = [](double w) { return w * 0.5; };

            for (int i = 0; i < segs; ++i)
            {
                const Math::Point3& A = m_polyline.SegStart(i);
                const Math::Point3& B = m_polyline.SegEnd(i);
                const double h0 = halfOf(SegmentStartWidth(i));   // 段起点端半宽
                const double h1 = halfOf(SegmentEndWidth(i));     // 段终点端半宽

                // 本段的起点 A：要么是整条线的首点，要么是上一段的终点（已存在）。
                if (nodes.empty())
                    nodes.push_back({ A, h0, h0 });
                // 折点处：上一段的 hIn 保留（那是到达它的宽度），这里只补上离开它的宽度。
                nodes.back().hOut = h0;

                if (m_polyline.SegIsLine(i))
                {
                    // B 的 hIn = 本段终点宽度；hOut 待下一段覆盖
                    nodes.push_back({ B, h1, h1 });
                }
                else
                {
                    // 每段的终点进节点（起点 A 已是上面的折点：再压一次会复制成两个重合节点，
                    // 使「上一段 → 本弧」接缝处的斜接被 l0==0 判掉而留下缺口）；最后一段的终点就是 B，下面单独压
                    SampleArcSegment(sink, A, B, m_polyline.SegBulge(i), angleTol,
                                     [&](const Math::Point3&, const Math::Point3& q, double, double t)
                                     {
                                         if (t < 1.0)
                                         {
                                             const double h = h0 + (h1 - h0) * t;
                                             nodes.push_back({ q, h, h });
                                         }
                                     }, std::max(h0, h1));
                    nodes.push_back({ B, h1, h1 });       // 弧的终点
                }
            }
            return nodes;
        }

        static void EmitThickPolyline(IDrawSink& sink,
                                      const std::vector<Node>& nodes,
                                      const Math::Color4& color)
        {
            const size_t n = nodes.size();
            if (n < 2) return;

            // 每段梯形（两三角形）。两端半宽相同时就是矩形。
            // 相邻两节点一定属于同一段：起点用它的 hOut，终点用它的 hIn。
            for (size_t i = 0; i + 1 < n; ++i)
            {
                const Node& a = nodes[i];
                const Node& b = nodes[i + 1];

                double dx = b.P.x - a.P.x, dy = b.P.y - a.P.y;
                double len = std::sqrt(dx * dx + dy * dy);
                if (len < 1e-12) continue;

                const double ux = -dy / len, uy = dx / len;   // 单位左法线

                const Math::Point3 AL{ a.P.x + ux * a.hOut, a.P.y + uy * a.hOut, a.P.z };
                const Math::Point3 AR{ a.P.x - ux * a.hOut, a.P.y - uy * a.hOut, a.P.z };
                const Math::Point3 BR{ b.P.x - ux * b.hIn,  b.P.y - uy * b.hIn,  b.P.z };
                const Math::Point3 BL{ b.P.x + ux * b.hIn,  b.P.y + uy * b.hIn,  b.P.z };

                sink.FillTriangle(AL, AR, BR, color);
                sink.FillTriangle(AL, BR, BL, color);
                // 某一端半宽为 0 时梯形退化成三角形，两三角形仍恰好覆盖该区域。
            }

            // 内部折点斜接尖角。段内插值点（hIn == hOut 且三点共线）会自然早退。
            for (size_t i = 1; i + 1 < n; ++i)
                EmitMiterJoin(sink, nodes[i - 1], nodes[i], nodes[i + 1], color);

            // 叠加中心线：缩小到线宽不足 1 像素时，细长三角形盖不住像素中心会断续发花，
            // 硬件线至少画 1 像素，线条保持连续；放大后被填充覆盖，看不出来。
            for (size_t i = 0; i + 1 < n; ++i)
                sink.DrawLine(nodes[i].P, nodes[i + 1].P, color, false);
        }

        // 在折点 v 处（来自 a、去往 b）用斜接（miter）三角形补外侧缺口。
        // 进入段在 v 处半宽 hIn、离开段 hOut（两者可不同）。外侧两条偏移边交于 miter 尖点 M，
        // 填充 (v,P0,M) 与 (v,M,P1)。尖角过长时退化为平接，避免无限尖刺。
        static void EmitMiterJoin(IDrawSink& sink,
                                  const Node& a, const Node& v, const Node& b,
                                  const Math::Color4& color)
        {
            const double hIn  = v.hIn;
            const double hOut = v.hOut;
            if (hIn <= 0.0 && hOut <= 0.0) return;

            double d0x = v.P.x - a.P.x, d0y = v.P.y - a.P.y;
            double d1x = b.P.x - v.P.x, d1y = b.P.y - v.P.y;
            double l0 = std::sqrt(d0x * d0x + d0y * d0y);
            double l1 = std::sqrt(d1x * d1x + d1y * d1y);
            if (l0 < 1e-12 || l1 < 1e-12) return;
            d0x /= l0; d0y /= l0;
            d1x /= l1; d1y /= l1;

            const double cross = d0x * d1y - d0y * d1x;   // >0 左转，<0 右转
            if (std::abs(cross) < 1e-9) return;           // 共线，无缺口

            // 外侧法线：左转时缺口在右侧（-左法线），右转时在左侧（+左法线）
            const bool leftSide = (cross < 0.0);
            const double o0x = leftSide ? -d0y :  d0y;    // 进入段外侧单位法线
            const double o0y = leftSide ?  d0x : -d0x;
            const double o1x = leftSide ? -d1y :  d1y;    // 离开段外侧单位法线
            const double o1y = leftSide ?  d1x : -d1x;

            // 外侧两条偏移边各自的端点
            const Math::Point3 P0{ v.P.x + o0x * hIn,  v.P.y + o0y * hIn,  v.P.z };
            const Math::Point3 P1{ v.P.x + o1x * hOut, v.P.y + o1y * hOut, v.P.z };

            // M = 两条偏移边的交点：P0 + s·d0 = P1 + t·d1
            // 半宽相等时它落在角平分线上，与旧的 miterLen = h/cosφ 等价。
            const double denom = cross;
            Math::Point3 M = P0;
            bool haveM = false;
            if (std::abs(denom) > 1e-12)
            {
                const double ex = P1.x - P0.x, ey = P1.y - P0.y;
                const double s  = (ex * d1y - ey * d1x) / denom;
                M = { P0.x + s * d0x, P0.y + s * d0y, v.P.z };
                haveM = true;
            }

            const double hMax = std::max(hIn, hOut);
            if (haveM && hMax > 0.0)
            {
                const double mdx = M.x - v.P.x, mdy = M.y - v.P.y;
                const double miterLen = std::sqrt(mdx * mdx + mdy * mdy);
                constexpr double kMiterLimit = 8.0;   // 斜接长度上限（半宽的倍数）
                if (miterLen <= kMiterLimit * hMax)
                {
                    sink.FillTriangle(v.P, P0, M, color);
                    sink.FillTriangle(v.P, M, P1, color);
                    return;
                }
            }

            // 超出斜接限制（接近反向折回）或求交失败：平接，避免无限尖刺
            sink.FillTriangle(v.P, P0, P1, color);
        }

        // 线段数变化后把逐段宽度数组对齐到当前段数（多补 0、少截断）。
        void SyncSegmentWidths()
        {
            const size_t segs = m_polyline.IsValid() ? static_cast<size_t>(m_polyline.SegCount()) : 0;
            if (m_segStartWidth.empty() && m_segEndWidth.empty()) return;   // 恒宽多段线不额外分配
            m_segStartWidth.resize(segs, 0.0);
            m_segEndWidth.resize(segs, 0.0);
        }

        Polyline m_polyline;
        double   m_width = 1.0;   // 恒定线宽(模型单位),默认 1.0 与旧行为一致

        // 宽度小于这个值就当作「无宽度」（亚纳米量级，只用来挡掉浮点噪声与 0）
        static constexpr double kWidthEpsilon = 1e-9;

        // 逐段起止宽度（原始 DXF 值，0 = 用恒定宽度）。
        // 为空表示整条多段线只有恒定宽度。
        std::vector<double> m_segStartWidth;
        std::vector<double> m_segEndWidth;
    };

}   
