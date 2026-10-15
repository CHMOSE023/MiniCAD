#pragma once
#include "Import/TchAnnotation.h"
#include "Import/TchOpening.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <utility>
#include <string>
#include <vector>

namespace MiniCAD::Tch
{
    namespace Detail
    {
        // 天正自定义的双精度编码（读取函数 0x6c8760）：两位标志 00 为 RD、01 为 1、10 为 0
        inline double ReadTchDouble(MiniDWG::DwgBitReader& r)
        {
            const int code = (r.ReadBit() ? 2 : 0) + (r.ReadBit() ? 1 : 0);
            return code == 0 ? r.ReadRawDouble() : code == 1 ? 1.0 : 0.0;
        }

        inline bool ShapeHead(const MiniDWG::UnknownEntity& entity, const char* dxf, MiniDWG::DwgBitReader& r, TchBase& base)
        {
            using namespace MiniDWG;
            const auto& data = entity.Raw.Dwg;
            if (entity.Raw.DxfName != dxf || !SupportedVersion(data)) return false;
            return ReadTchBase(r, base);
        }

        // 以 (cx, cy) 为中心、转 rot 的局部坐标点
        inline TchGraphics::Point Local(double cx, double cy, double rot, double x, double y)
        {
            const double c = std::cos(rot), s = std::sin(rot);
            return {cx + x * c - y * s, cy + x * s + y * c};
        }
    }

    // ── TCH_RECT 天正矩形（CADReader TDbRect 读取函数 0x6e7c80，版本 < 3 为明文分支）──
    struct TchRect
    {
        double X = 0, Y = 0;            // 中心
        double Rotation = 0;
        double Length = 0, Width = 0;
    };

    // 天正基类、RC 版本、BS 基点类型、3BD 中心、转角、长、宽、高（自定义编码 BD）、3BD 法向；版本 ≥ 2：RC
    inline bool DecodeRect(const MiniDWG::UnknownEntity& entity, TchRect& rect, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_RECT") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_RECT", r, base)) return fail("unsupported Tianzheng base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 2) return fail("unsupported native rectangle version");
        TchRect t;
        r.ReadBitShort();
        t.X = r.ReadBitDouble(); t.Y = r.ReadBitDouble(); r.ReadBitDouble();
        t.Rotation = Detail::ReadTchDouble(r);
        t.Length = Detail::ReadTchDouble(r);
        t.Width = Detail::ReadTchDouble(r);
        Detail::ReadTchDouble(r);
        const double nx = r.ReadBitDouble(), ny = r.ReadBitDouble(), nz = r.ReadBitDouble();
        if (version > 1) r.ReadByte();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native rectangle fields do not consume the payload exactly");
        if (std::abs(nx) > 1e-9 || std::abs(ny) > 1e-9 || nz <= 0) return fail("rectangle is not in the XY plane");
        for (double v : {t.X, t.Y, t.Rotation, t.Length, t.Width})
            if (!Detail::Finite(v)) return fail("invalid native rectangle settings");
        rect = t;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解为一条闭合多段线（2 个变体核对）
    inline TchGraphics BuildRect(const TchRect& t)
    {
        TchGraphics g;
        const double hl = t.Length / 2, hw = t.Width / 2;
        TchGraphics::Path p;
        for (auto [x, y] : {std::pair{-hl, -hw}, {hl, -hw}, {hl, hw}, {-hl, hw}, {-hl, -hw}})
            p.Points.push_back({Detail::Local(t.X, t.Y, t.Rotation, x, y)});
        g.Paths.push_back(std::move(p));
        return g;
    }
    namespace Detail
    {
        struct PathVertex { double X = 0, Y = 0, Bulge = 0; };

        // 多段线（只按直线段处理凸度为 0 的段；圆弧段的偏移保持凸度）向左偏移 d（负值向右），转角取两段偏移线的交点
        inline std::vector<PathVertex> OffsetPath(const std::vector<PathVertex>& pts, bool closed, double d)
        {
            const std::size_t n = pts.size();
            std::vector<PathVertex> out;
            if (n < 2) return out;
            auto dir = [&](std::size_t i, bool atStart) {
                const auto& a = pts[i];
                const auto& b = pts[(i + 1) % n];
                double dx = b.X - a.X, dy = b.Y - a.Y;
                const double L = std::hypot(dx, dy);
                if (L < 1e-12) return std::pair{1.0, 0.0};
                dx /= L; dy /= L;
                if (std::abs(a.Bulge) > 1e-12)
                {
                    // 圆弧在起点 / 终点的切线：弦方向转 ±2·atan(bulge)
                    const double t = 2 * std::atan(a.Bulge) * (atStart ? 1 : -1);
                    const double c = std::cos(t), s = std::sin(t);
                    return std::pair{dx * c - dy * s, dx * s + dy * c};
                }
                return std::pair{dx, dy};
            };
            const std::size_t segs = closed ? n : n - 1;
            for (std::size_t i = 0; i < n; ++i)
            {
                const bool hasPrev = closed || i > 0, hasNext = closed || i + 1 < n;
                std::pair<double, double> t1{0, 0}, t2{0, 0};
                if (hasPrev) t1 = dir((i + n - 1) % n, false);
                if (hasNext && i < segs + (closed ? 0 : 0)) t2 = dir(i, true);
                if (!hasPrev) t1 = t2;
                if (!hasNext) t2 = t1;
                const double n1x = -t1.second, n1y = t1.first, n2x = -t2.second, n2y = t2.first;
                double mx = n1x + n2x, my = n1y + n2y;
                const double ml = std::hypot(mx, my);
                double k = d;
                if (ml > 1e-9)
                {
                    mx /= ml; my /= ml;
                    const double cosHalf = mx * n1x + my * n1y;
                    k = cosHalf > 0.05 ? d / cosHalf : d;
                }
                else { mx = n1x; my = n1y; }
                out.push_back({pts[i].X + mx * k, pts[i].Y + my * k, pts[i].Bulge});
            }
            return out;
        }

        inline TchGraphics::Path ToPath(const std::vector<PathVertex>& pts, bool closed)
        {
            TchGraphics::Path p;
            for (const auto& v : pts) p.Points.push_back({{v.X, v.Y}, v.Bulge});
            if (closed && !pts.empty()) { p.Points.back().Bulge = pts.back().Bulge; p.Points.push_back({{pts.front().X, pts.front().Y}}); }
            else if (!p.Points.empty()) p.Points.back().Bulge = 0;
            return p;
        }

        inline double SignedArea(const std::vector<PathVertex>& pts)
        {
            double a = 0;
            for (std::size_t i = 0; i < pts.size(); ++i)
            {
                const auto& p = pts[i];
                const auto& q = pts[(i + 1) % pts.size()];
                a += p.X * q.Y - q.X * p.Y;
            }
            return a / 2;
        }
    }

    // ── TCH_SLAB 平板 / 楼板（CADReader TDbSlab 读取函数 0x6eb010，版本 < 3 为明文分支）──
    struct TchSlab
    {
        std::vector<Detail::PathVertex> Outline;
        std::vector<std::vector<Detail::PathVertex>> Holes;
    };

    // 天正基类、RC 版本、BD 板厚、BL、BD、RC、BS 点数与各点 2RD + BD 凸度（外轮廓）、BS 洞数、
    // 每个洞 RC、BS 点数与各点 2RD + BD；版本 ≥ 2：3BD 法向
    inline bool DecodeSlab(const MiniDWG::UnknownEntity& entity, TchSlab& slab, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_SLAB") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_SLAB", r, base)) return fail("unsupported Tianzheng base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 2) return fail("unsupported native slab version");
        r.ReadBitDouble(); r.ReadBitLong(); r.ReadBitDouble(); r.ReadByte();
        auto ring = [&](std::vector<Detail::PathVertex>& out) {
            const int count = r.ReadBitShort();
            if (count < 0 || count > 100000) return false;
            for (int i = 0; i < count && !r.Failed(); ++i)
            {
                Detail::PathVertex v;
                v.X = r.ReadRawDouble(); v.Y = r.ReadRawDouble(); v.Bulge = r.ReadBitDouble();
                if (!Detail::Finite(v.X) || !Detail::Finite(v.Y) || !Detail::Finite(v.Bulge)) return false;
                out.push_back(v);
            }
            return true;
        };
        TchSlab s;
        if (!ring(s.Outline)) return fail("invalid slab outline");
        const int holes = r.ReadBitShort();
        if (holes < 0 || holes > 10000) return fail("invalid slab hole count");
        for (int i = 0; i < holes && !r.Failed(); ++i)
        {
            r.ReadByte();
            s.Holes.emplace_back();
            if (!ring(s.Holes.back())) return fail("invalid slab hole");
        }
        if (version > 1) { r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble(); }
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native slab fields do not consume the payload exactly");
        if (s.Outline.size() < 3) return fail("slab outline is degenerate");
        slab = std::move(s);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解为外轮廓（与洞口）的线段（2 个样例核对）
    inline TchGraphics BuildSlab(const TchSlab& s)
    {
        TchGraphics g;
        g.Paths.push_back(Detail::ToPath(s.Outline, true));
        for (const auto& h : s.Holes)
            if (h.size() >= 2) g.Paths.push_back(Detail::ToPath(h, true));
        return g;
    }

    // ── TCH_HANDRAIL 扶手（CADReader TDbHandRail 读取函数 0x6cfe70，楼梯基类 0x6e6cb0，版本 < 3 为明文分支）──
    struct TchHandRail
    {
        std::vector<Detail::PathVertex> Path;
        double Width = 60;
    };

    // 天正基类、楼梯基类 RC 版本（1～2）、RC 版本、RC 方式（100 时句柄流多一个句柄）、RC、BS 点数与各点 3BD + BD 凸度、
    // BD 宽；版本 ≥ 2：BD；BD 顶面高、RC、RC
    inline bool DecodeHandRail(const MiniDWG::UnknownEntity& entity, TchHandRail& rail, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_HANDRAIL") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_HANDRAIL", r, base)) return fail("unsupported Tianzheng base layout");
        const int stairBase = r.ReadByte();
        if (stairBase < 1 || stairBase > 2) return fail("unsupported stair base version");
        const int version = r.ReadByte();
        if (version < 1 || version > 2) return fail("unsupported native handrail version");
        r.ReadByte(); r.ReadByte();
        const int count = r.ReadBitShort();
        if (count < 2 || count > 100000) return fail("invalid handrail vertex count");
        TchHandRail h;
        for (int i = 0; i < count && !r.Failed(); ++i)
        {
            Detail::PathVertex v;
            v.X = r.ReadBitDouble(); v.Y = r.ReadBitDouble(); r.ReadBitDouble();
            v.Bulge = r.ReadBitDouble();
            if (!Detail::Finite(v.X) || !Detail::Finite(v.Y) || !Detail::Finite(v.Bulge)) return fail("invalid handrail vertex");
            h.Path.push_back(v);
        }
        h.Width = r.ReadBitDouble();
        if (version > 1) r.ReadBitDouble();
        r.ReadBitDouble(); r.ReadByte(); r.ReadByte();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native handrail fields do not consume the payload exactly");
        if (!(h.Width > 0) || !Detail::Finite(h.Width)) return fail("invalid handrail width");
        rail = std::move(h);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解为路径两侧各偏移半宽的线与两端封口（1 个样例核对）
    inline TchGraphics BuildHandRail(const TchHandRail& h)
    {
        TchGraphics g;
        const auto left = Detail::OffsetPath(h.Path, false, h.Width / 2), right = Detail::OffsetPath(h.Path, false, -h.Width / 2);
        g.Paths.push_back(Detail::ToPath(left, false));
        g.Paths.push_back(Detail::ToPath(right, false));
        g.Lines.push_back({{left.front().X, left.front().Y}, {right.front().X, right.front().Y}});
        g.Lines.push_back({{left.back().X, left.back().Y}, {right.back().X, right.back().Y}});
        return g;
    }

    // ── TCH_APRON 散水（CADReader TDbApron 读取函数 0x6c3b30，路径基类 0x6e64d0，版本 < 2 为明文分支）──
    struct TchApron
    {
        std::vector<Detail::PathVertex> Path;      // 散水内侧（外墙外皮）
        bool Closed = true;
        std::vector<double> Widths;                 // 各段散水宽
    };

    // 路径基类（同线图案）、RC 版本、BD、BD、BS 段数与各段 BD 宽、BL
    inline bool DecodeApron(const MiniDWG::UnknownEntity& entity, TchApron& apron, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_APRON") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_APRON", r, base)) return fail("unsupported Tianzheng base layout");
        if (r.ReadByte() != 0) return fail("unsupported path base version");
        const int count = r.ReadBitShort();
        if (count < 2 || count > 100000) return fail("invalid apron vertex count");
        TchApron a;
        for (int i = 0; i < count && !r.Failed(); ++i)
        {
            Detail::PathVertex v;
            v.X = r.ReadBitDouble(); v.Y = r.ReadBitDouble(); r.ReadBitDouble();
            v.Bulge = r.ReadBitDouble();
            if (!Detail::Finite(v.X) || !Detail::Finite(v.Y) || !Detail::Finite(v.Bulge)) return fail("invalid apron vertex");
            a.Path.push_back(v);
        }
        a.Closed = r.ReadBit();
        r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
        if (r.ReadByte() != 1) return fail("unsupported native apron version");
        r.ReadBitDouble(); r.ReadBitDouble();
        const int widths = r.ReadBitShort();
        if (widths < 0 || widths > 100000) return fail("invalid apron width count");
        for (int i = 0; i < widths && !r.Failed(); ++i) a.Widths.push_back(r.ReadBitDouble());
        r.ReadBitLong();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native apron fields do not consume the payload exactly");
        for (double w : a.Widths)
            if (!Detail::Finite(w) || w < 0) return fail("invalid apron width");
        apron = std::move(a);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（1 个样例核对）：内侧轮廓、向外偏移散水宽的外轮廓、两者对应顶点间的斜线
    inline TchGraphics BuildApron(const TchApron& a)
    {
        TchGraphics g;
        g.Paths.push_back(Detail::ToPath(a.Path, a.Closed));
        double w = 0;
        for (double x : a.Widths) w = std::max(w, x);
        if (!(w > 0)) return g;
        const double outward = a.Closed && Detail::SignedArea(a.Path) < 0 ? 1.0 : -1.0;   // 逆时针轮廓的外侧在右
        const auto outer = Detail::OffsetPath(a.Path, a.Closed, outward * w);
        g.Paths.push_back(Detail::ToPath(outer, a.Closed));
        for (std::size_t i = 0; i < a.Path.size(); ++i)
            g.Lines.push_back({{a.Path[i].X, a.Path[i].Y}, {outer[i].X, outer[i].Y}});
        return g;
    }
    // ── TCH_CORNER_WINDOW 转角窗（CADReader TDbCornerWin 读取函数 0x6cb740，版本 < 5 为明文分支）──
    struct TchCornerWindow
    {
        double Scale = 100;
        double X = 0, Y = 0;                    // 墙内侧的转角点
        double Length1 = 0, Length2 = 0;        // 两段窗长
        double Angle1 = 0, Angle2 = 0;          // 两段自转角点出发的方向
        double Thickness = 240;                 // 墙厚（窗线自内侧向外排 4 条）
        double LabelX = 0, LabelY = 0, LabelHeight = 3.5;
        std::string Label;
        MiniDWG::Handle Style = MiniDWG::kNullHandle;
    };

    // 天正基类、RC 版本、BL 标志、3BD 转角点、两个句柄（墙）、9 个 BD（两段长、窗高、两个方向、墙厚、−墙厚、两个偏移）、
    // 编号文字、2RD 编号位置、两个句柄（文字样式等）、BD、BD 编号字高；标志位 0：4 个 BD；BS、3 个句柄、3 个 BD；
    // 标志位 2：BS 段数与各段 2RD、2RD、BD；版本 ≥ 2：BS、B、文字、4 个 BD；版本 ≥ 3：BD
    inline bool DecodeCornerWindow(const MiniDWG::UnknownEntity& entity, TchCornerWindow& window, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_CORNER_WINDOW") return false;
        // 编号在主数据流中的位置未核对：R2004 及更早（字符串嵌在主数据流）不支持
        if (data.Version < CadVersion::AC1021) return fail("unsupported DWG version for corner window");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_CORNER_WINDOW", r, base)) return fail("unsupported Tianzheng base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 4) return fail("unsupported native corner window version");
        TchCornerWindow w;
        w.Scale = base.Scale;
        const int flags = r.ReadBitLong();
        w.X = r.ReadBitDouble(); w.Y = r.ReadBitDouble(); r.ReadBitDouble();
        w.Length1 = r.ReadBitDouble(); w.Length2 = r.ReadBitDouble();
        r.ReadBitDouble();                                  // 窗高
        w.Angle1 = r.ReadBitDouble(); w.Angle2 = r.ReadBitDouble();
        w.Thickness = r.ReadBitDouble();
        for (int i = 0; i < 3; ++i) r.ReadBitDouble();
        w.LabelX = r.ReadRawDouble(); w.LabelY = r.ReadRawDouble();
        r.ReadBitDouble();
        w.LabelHeight = r.ReadBitDouble();
        if (flags & 1) for (int i = 0; i < 4; ++i) r.ReadBitDouble();
        r.ReadBitShort();
        for (int i = 0; i < 3; ++i) r.ReadBitDouble();
        if (flags & 4)
        {
            const int segs = r.ReadBitShort();
            if (segs < 0 || segs > 10000) return fail("invalid corner window segment count");
            for (int i = 0; i < segs && !r.Failed(); ++i) { for (int k = 0; k < 4; ++k) r.ReadRawDouble(); r.ReadBitDouble(); }
        }
        int texts = 1;
        if (version > 1) { r.ReadBitShort(); r.ReadBit(); ++texts; for (int i = 0; i < 4; ++i) r.ReadBitDouble(); }
        if (version > 2) r.ReadBitDouble();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native corner window fields do not consume the payload exactly");
        for (double v : {w.X, w.Y, w.Length1, w.Length2, w.Angle1, w.Angle2, w.Thickness, w.LabelX, w.LabelY, w.LabelHeight})
            if (!Detail::Finite(v)) return fail("invalid native corner window settings");
        Detail::TextStream stream(data, r);
        for (int i = 0; i < base.TextCount; ++i) stream.Next();
        w.Label = stream.Next();
        (void)texts;
        DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
        for (int i = 0; i < base.HandleCount + 2; ++i) handles.ReadHandle(entity.ObjectHandle);   // 两道墙
        w.Style = handles.ReadHandle(entity.ObjectHandle);
        window = std::move(w);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（1 个样例核对）：自内侧转角点起 4 条窗线（间距墙厚 / 3），每条依次为第一段端点、转角、第二段端点，
    // 每段向另一段的反方向平移；两端封口；编号文字以编号位置为中心
    inline TchGraphics BuildCornerWindow(const TchCornerWindow& w)
    {
        TchGraphics g;
        const double d1x = std::cos(w.Angle1), d1y = std::sin(w.Angle1), d2x = std::cos(w.Angle2), d2y = std::sin(w.Angle2);
        auto at = [&](double t, int which) -> TchGraphics::Point {
            const double ox = -(d1x + d2x) * t, oy = -(d1y + d2y) * t;
            if (which == 1) return {w.X + d1x * w.Length1 - d2x * t, w.Y + d1y * w.Length1 - d2y * t};
            if (which == 2) return {w.X + d2x * w.Length2 - d1x * t, w.Y + d2y * w.Length2 - d1y * t};
            return {w.X + ox, w.Y + oy};
        };
        for (int k = 0; k <= 3; ++k)
        {
            const double t = w.Thickness * k / 3;
            TchGraphics::Path p;
            p.Points = {{at(t, 1)}, {at(t, 0)}, {at(t, 2)}};
            g.Paths.push_back(std::move(p));
        }
        g.Lines.push_back({at(0, 1), at(w.Thickness, 1)});
        g.Lines.push_back({at(0, 2), at(w.Thickness, 2)});
        if (!w.Label.empty())
        {
            const double nominal = w.LabelHeight * w.Scale;
            Detail::AddRuns(g, w.Label, nominal, w.LabelX, w.LabelY - 0.425 * nominal, 0, 1);
        }
        return g;
    }
    namespace Detail
    {
        // 梯段剖断线（与双跑楼梯中间层相同的画法，按天正 2014 分解结果核对）：剖断中线自 a 到 b，
        // 双线时两侧各偏 50；锯齿两尖点在中点为圆心、半径 135 的圆上（中线方向坐标系 115° 与 −75°），两侧斜段方向 75°。
        // kind：2 双剖断（剪掉两线之间的部分），1 下剖断（保留中线靠 a→b 右侧的部分），3 上剖断（保留左侧）
        struct FlightBreak
        {
            using P = TchGraphics::Point;
            int Kind = 0;
            P A, B, C;                  // 两端与中点
            double Ux = 1, Uy = 0;      // 中线方向
            std::vector<std::vector<P>> Lines;   // 剖断线（世界坐标）

            std::function<bool(const P&)> Inside;      // 梯段范围（剖断线画到梯段边为止）
            std::function<double(const P&)> Progress;  // 点沿梯段的进度（0 起步端、1 末端），单剖断按它判断哪一侧剪掉
            double From = 0, To = 1;                   // 剖断线两端的进度

            FlightBreak(int kind, P a, P b, std::function<bool(const P&)> inside, std::function<double(const P&)> progress, double from, double to)
                : Kind(kind), A(a), B(b), Inside(std::move(inside)), Progress(std::move(progress)), From(std::min(from, to)), To(std::max(from, to))
            {
                C = {(a.X + b.X) / 2, (a.Y + b.Y) / 2};
                const double L = std::hypot(b.X - a.X, b.Y - a.Y);
                if (L > 1e-9) { Ux = (b.X - a.X) / L; Uy = (b.Y - a.Y) / L; }
                constexpr double deg = std::numbers::pi / 180;
                const P pb{135 * std::cos(115 * deg), 135 * std::sin(115 * deg)};
                const P pc{135 * std::cos(-75 * deg), 135 * std::sin(-75 * deg)};
                auto along = [&](const P& p, double n) { const double t = (n - p.Y) / std::sin(75 * deg); return P{p.X + t * std::cos(75 * deg), n}; };
                const double h = L / 2 + 300;       // 两端略延长（端点本在梯段边上），画时再截到梯段范围内
                if (kind == 2)
                {
                    const P pa = along(pb, -50), x2l = along(pb, 50), pd = along(pc, 50), x2r = along(pc, -50);
                    Lines = {{W(-h, 50), W(x2l.X, x2l.Y)},
                             {W(-h, -50), W(pa.X, pa.Y), W(pb.X, pb.Y), W(pc.X, pc.Y), W(pd.X, pd.Y), W(h, 50)},
                             {W(x2r.X, x2r.Y), W(h, -50)}};
                }
                else if (kind == 1 || kind == 3)
                {
                    const P a0 = along(pb, 0), d0 = along(pc, 0);
                    Lines = {{W(-h, 0), W(a0.X, a0.Y), W(pb.X, pb.Y), W(pc.X, pc.Y), W(d0.X, d0.Y), W(h, 0)}};
                }
                Clip();
            }
            std::vector<std::pair<P, P>> Visible;    // 截到梯段范围内的剖断线段
            double Umin = 0, Umax = 0;               // 可见剖断线沿中线方向的范围

            void Clip()
            {
                auto clip = [&](P inPt, P outPt) {
                    for (int k = 0; k < 50; ++k)
                    {
                        const P m{(inPt.X + outPt.X) / 2, (inPt.Y + outPt.Y) / 2};
                        (Inside(m) ? inPt : outPt) = m;
                    }
                    return inPt;
                };
                const double h = std::hypot(B.X - A.X, B.Y - A.Y) / 2;
                bool first = true;
                for (const auto& l : Lines)
                    for (std::size_t i = 0; i + 1 < l.size(); ++i)
                    {
                        P a = l[i], b = l[i + 1];
                        // 只取靠近剖断中线两端的那一段（圆弧梯段的延长线会穿过内圆再进入梯段另一侧）
                        auto u = [&](const P& p) { return (p.X - C.X) * Ux + (p.Y - C.Y) * Uy; };
                        if (std::abs(u(a)) > h + 600 && std::abs(u(b)) > h + 600) continue;
                        const bool ia = Inside(a), ib = Inside(b);
                        if (!ia && !ib) continue;
                        if (!ia) a = clip(b, a);
                        if (!ib) b = clip(a, b);
                        Visible.push_back({a, b});
                        for (double v : {u(a), u(b)})
                        {
                            Umin = first ? v : std::min(Umin, v);
                            Umax = first ? v : std::max(Umax, v);
                            first = false;
                        }
                    }
            }
            P W(double u, double n) const { return {C.X + u * Ux - n * Uy, C.Y + u * Uy + n * Ux}; }
            double Normal(const P& p) const { return -(p.X - C.X) * Uy + (p.Y - C.Y) * Ux; }
            // 点是否被剪掉
            bool Removed(const P& p) const
            {
                const double n = Normal(p);
                if (Kind == 1) return n > 0 && Progress(p) >= From - 1e-9;      // 下剖断：剪掉剖断线以后的部分
                if (Kind == 3) return n < 0 && Progress(p) <= To + 1e-9;        // 上剖断：剪掉剖断线以前的部分
                const double u = (p.X - C.X) * Ux + (p.Y - C.Y) * Uy;
                if (u < Umin - 1e-6 || u > Umax + 1e-6) return false;
                return Kind == 2 && std::abs(n) < 50;
            }

            void AddLine(TchGraphics& g, P a, P b) const
            {
                if (Kind == 0) { g.Lines.push_back({a, b}); return; }
                // 与剖断线（含锯齿）的交点把线段分段，保留中点未被剪掉的段
                std::vector<double> ts{0, 1};
                for (const auto& [p0, p1] : Visible)
                    {
                        const double dx = b.X - a.X, dy = b.Y - a.Y, ex = p1.X - p0.X, ey = p1.Y - p0.Y;
                        const double den = dx * ey - dy * ex;
                        if (std::abs(den) < 1e-12) continue;
                        const double ox = p0.X - a.X, oy = p0.Y - a.Y;
                        const double t = (ox * ey - oy * ex) / den, u = (ox * dy - oy * dx) / den;
                        if (t > 0 && t < 1 && u >= -1e-9 && u <= 1 + 1e-9) ts.push_back(t);
                    }
                std::sort(ts.begin(), ts.end());
                auto at = [&](double t) { return P{a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t}; };
                for (std::size_t i = 0; i + 1 < ts.size(); ++i)
                    if (ts[i + 1] - ts[i] > 1e-9 && !Removed(at((ts[i] + ts[i + 1]) / 2))) g.Lines.push_back({at(ts[i]), at(ts[i + 1])});
            }

            // 圆弧（逆时针 a0 → a1）：按与剖断中线两侧偏移线（无限长）的交点分段
            void AddArc(TchGraphics& g, P c, double r, double a0, double a1) const
            {
                if (Kind == 0) { g.Arcs.push_back({c, r, a0, a1}); return; }
                while (a1 < a0) a1 += 2 * std::numbers::pi;
                std::vector<double> as{a0, a1};
                for (double off : (Kind == 2 ? std::vector<double>{-50, 50} : std::vector<double>{0}))
                {
                    // 直线 n(p) = off：-(x-Cx)·Uy + (y-Cy)·Ux = off，代入 x = cx + r cosθ, y = cy + r sinθ
                    const double k = off + (c.X - C.X) * Uy - (c.Y - C.Y) * Ux;   // r(-cosθ Uy + sinθ Ux) = k
                    if (std::abs(k) > r) continue;
                    const double phi = std::atan2(-Uy, Ux), base = std::asin(k / r);
                    for (double th : {base - phi, std::numbers::pi - base - phi})
                    {
                        double t = th;
                        while (t < a0) t += 2 * std::numbers::pi;
                        while (t > a0 + 2 * std::numbers::pi) t -= 2 * std::numbers::pi;
                        if (t > a0 && t < a1) as.push_back(t);
                    }
                }
                std::sort(as.begin(), as.end());
                for (std::size_t i = 0; i + 1 < as.size(); ++i)
                {
                    const double m = (as[i] + as[i + 1]) / 2;
                    if (as[i + 1] - as[i] > 1e-9 && !Removed({c.X + r * std::cos(m), c.Y + r * std::sin(m)})) g.Arcs.push_back({c, r, as[i], as[i + 1]});
                }
            }

            void AddSymbol(TchGraphics& g) const
            {
                for (const auto& [a, b] : Visible) g.Lines.push_back({a, b});
            }
        };

        // 梯段基类（0x6ece20）：天正基类、RC 版本（< 4 为明文分支）、BS 踏步数、BD 踏步高、RC 剖断方式；
        // 版本 ≥ 2：BD、BD 剖断线两端位置（占梯段长的比例）；版本 ≥ 3：BL、3 个 BD、两个句柄
        struct TchFlightBase
        {
            TchBase Base;
            int    Steps = 0;
            double StepHeight = 150;
            int    BreakMode = 0;
            double BreakStart = 0.4, BreakEnd = 0.6;
            int    HandleCount = 0;
        };

        inline bool ReadFlightBase(MiniDWG::DwgBitReader& r, TchFlightBase& f)
        {
            if (!ReadTchBase(r, f.Base)) return false;
            const int version = r.ReadByte();
            if (version < 1 || version > 3) return false;
            f.Steps = r.ReadBitShort();
            f.StepHeight = r.ReadBitDouble();
            f.BreakMode = r.ReadByte();
            if (version > 1) { f.BreakStart = r.ReadBitDouble(); f.BreakEnd = r.ReadBitDouble(); }
            f.HandleCount = f.Base.HandleCount;
            if (version > 2) { r.ReadBitLong(); r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble(); f.HandleCount += 2; }
            return !r.Failed();
        }

        // 剖断方式（4 个变体核对）：低 4 位 1 无剖断、2 下剖断、3 双剖断、4 上剖断（高位 16 含义未明）
        inline int BreakKind(int mode)
        {
            switch (mode & 15) { case 2: return 1; case 3: return 2; case 4: return 3; default: return 0; }
        }
    }

    // ── TCH_LINESTAIR 直线梯段（CADReader TDbLineStair 读取函数 0x6d0210，版本 < 3 为明文分支）──
    struct TchLineStair
    {
        Detail::TchFlightBase Flight;
        double Tread = 270, Width = 1200;
        double X = 0, Y = 0, Rotation = 0;      // 起步边左端点；梯段沿局部 +Y 展开
    };

    inline bool DecodeLineStair(const MiniDWG::UnknownEntity& entity, TchLineStair& stair, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_LINESTAIR") return false;
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for line stair");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchLineStair s;
        if (!Detail::ReadFlightBase(r, s.Flight)) return fail("unsupported stair flight base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 2) return fail("unsupported native line stair version");
        s.Tread = r.ReadBitDouble(); s.Width = r.ReadBitDouble();
        s.X = r.ReadBitDouble(); s.Y = r.ReadBitDouble(); r.ReadBitDouble();
        s.Rotation = r.ReadBitDouble();
        if (version > 1) r.ReadBitDouble();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native line stair fields do not consume the payload exactly");
        for (double v : {s.Tread, s.Width, s.X, s.Y, s.Rotation, s.Flight.BreakStart, s.Flight.BreakEnd})
            if (!Detail::Finite(v)) return fail("invalid native line stair settings");
        if (s.Flight.Steps < 2 || s.Flight.Steps > 1000 || !(s.Tread > 0) || !(s.Width > 0)) return fail("invalid native line stair size");
        stair = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（1 个样例核对）：两侧边、踏步线（踏步数个，间距踏步宽）；剖断线自起步端 BreakStart × 梯段长的左边
    // 到 BreakEnd × 梯段长的右边
    inline TchGraphics BuildLineStair(const TchLineStair& s)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double L = (s.Flight.Steps - 1) * s.Tread, W = s.Width;
        auto at = [&](double x, double y) { return Detail::Local(s.X, s.Y, s.Rotation, x, y); };
        auto inside = [&](const P& p) {
            const double dx = p.X - s.X, dy = p.Y - s.Y, c = std::cos(s.Rotation), sn = std::sin(s.Rotation);
            const double x = dx * c + dy * sn;
            return x >= -1e-9 && x <= W + 1e-9;
        };
        auto progress = [&](const P& p) {
            const double dx = p.X - s.X, dy = p.Y - s.Y, c = std::cos(s.Rotation), sn = std::sin(s.Rotation);
            return (-dx * sn + dy * c) / L;
        };
        const Detail::FlightBreak brk(Detail::BreakKind(s.Flight.BreakMode), at(0, s.Flight.BreakStart * L), at(W, s.Flight.BreakEnd * L), inside,
                                      progress, s.Flight.BreakStart, s.Flight.BreakEnd);
        brk.AddLine(g, at(0, 0), at(0, L));
        brk.AddLine(g, at(W, 0), at(W, L));
        for (int i = 0; i < s.Flight.Steps; ++i) brk.AddLine(g, at(0, i * s.Tread), at(W, i * s.Tread));
        brk.AddSymbol(g);
        (void)sizeof(P);
        return g;
    }

    // ── TCH_ARCSTAIR 圆弧梯段（CADReader TDbArcStair 读取函数 0x6c3fc0，版本 < 3 为明文分支）──
    struct TchArcStair
    {
        Detail::TchFlightBase Flight;
        double X = 0, Y = 0;                    // 圆心
        double InnerRadius = 800, OuterRadius = 2000;
        double StartAngle = 0, Sweep = std::numbers::pi;
    };

    // 梯段基类、RC 版本、3BD 圆心、BD 内径、BD 外径、BD 起始角、BD 圆心角；版本 ≥ 2：RC、BS 点数与各点 2RD + BD、RC、BS 点数与各点 2RD + BD
    inline bool DecodeArcStair(const MiniDWG::UnknownEntity& entity, TchArcStair& stair, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_ARCSTAIR") return false;
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for arc stair");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchArcStair s;
        if (!Detail::ReadFlightBase(r, s.Flight)) return fail("unsupported stair flight base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 2) return fail("unsupported native arc stair version");
        s.X = r.ReadBitDouble(); s.Y = r.ReadBitDouble(); r.ReadBitDouble();
        s.InnerRadius = r.ReadBitDouble(); s.OuterRadius = r.ReadBitDouble();
        s.StartAngle = r.ReadBitDouble(); s.Sweep = r.ReadBitDouble();
        if (version > 1)
            for (int k = 0; k < 2; ++k)
            {
                r.ReadByte();
                const int n = r.ReadBitShort();
                if (n < 0 || n > 100000) return fail("invalid arc stair outline");
                for (int i = 0; i < n && !r.Failed(); ++i) { r.ReadRawDouble(); r.ReadRawDouble(); r.ReadBitDouble(); }
            }
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native arc stair fields do not consume the payload exactly");
        for (double v : {s.X, s.Y, s.InnerRadius, s.OuterRadius, s.StartAngle, s.Sweep})
            if (!Detail::Finite(v)) return fail("invalid native arc stair settings");
        if (s.Flight.Steps < 2 || s.Flight.Steps > 1000 || !(s.InnerRadius >= 0) || !(s.OuterRadius > s.InnerRadius) || s.Sweep == 0)
            return fail("invalid native arc stair size");
        stair = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（1 个样例核对）：内外圆弧、均分圆心角的径向踏步线（踏步数个）；剖断线自内弧 BreakStart × 圆心角处
    // 到外弧 BreakEnd × 圆心角处
    inline TchGraphics BuildArcStair(const TchArcStair& s)
    {
        TchGraphics g;
        const TchGraphics::Point c{s.X, s.Y};
        auto at = [&](double r, double a) { return TchGraphics::Point{s.X + r * std::cos(a), s.Y + r * std::sin(a)}; };
        const double a0 = s.StartAngle, sweep = s.Sweep;
        auto inside = [&](const TchGraphics::Point& p) {
            const double r = std::hypot(p.X - s.X, p.Y - s.Y);
            return r >= s.InnerRadius - 1e-9 && r <= s.OuterRadius + 1e-9;
        };
        auto progress = [&](const TchGraphics::Point& p) {
            double a = std::atan2(p.Y - s.Y, p.X - s.X) - a0;
            if (sweep > 0) { while (a < -1e-9) a += 2 * std::numbers::pi; }
            else { while (a > 1e-9) a -= 2 * std::numbers::pi; }
            return a / sweep;
        };
        const Detail::FlightBreak brk(Detail::BreakKind(s.Flight.BreakMode), at(s.InnerRadius, a0 + s.Flight.BreakStart * sweep),
                                      at(s.OuterRadius, a0 + s.Flight.BreakEnd * sweep), inside, progress, s.Flight.BreakStart, s.Flight.BreakEnd);
        const double lo = std::min(a0, a0 + sweep), hi = std::max(a0, a0 + sweep);
        if (s.InnerRadius > 0) brk.AddArc(g, c, s.InnerRadius, lo, hi);
        brk.AddArc(g, c, s.OuterRadius, lo, hi);
        for (int i = 0; i < s.Flight.Steps; ++i)
        {
            const double a = a0 + sweep * i / (s.Flight.Steps - 1);
            brk.AddLine(g, at(s.InnerRadius, a), at(s.OuterRadius, a));
        }
        brk.AddSymbol(g);
        return g;
    }

    // ── TCH_ASCENT 坡道（CADReader TDbAscent 读取函数 0x6c44d0，版本 < 2 为明文分支）──
    struct TchAscent
    {
        double X = 0, Y = 0, Rotation = 0;      // 坡顶边中点；坡道沿局部 −Y 方向
        bool   Slant = true;                    // 示坡线（横线）
        double Length = 2100, Width = 3000, Height = 450, Indent = 300;
        double Scale = 100;
    };

    // 天正基类、RC 版本、3BD 坡顶中点、BD 转角、RC 边坡、BD 长、BD 宽、BD 高、BD 边坡宽
    inline bool DecodeAscent(const MiniDWG::UnknownEntity& entity, TchAscent& ascent, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_ASCENT") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_ASCENT", r, base)) return fail("unsupported Tianzheng base layout");
        if (r.ReadByte() != 1) return fail("unsupported native ascent version");
        TchAscent a;
        a.Scale = base.Scale;
        a.X = r.ReadBitDouble(); a.Y = r.ReadBitDouble(); r.ReadBitDouble();
        a.Rotation = r.ReadBitDouble();
        a.Slant = r.ReadByte() != 0;
        a.Length = r.ReadBitDouble(); a.Width = r.ReadBitDouble(); a.Height = r.ReadBitDouble(); a.Indent = r.ReadBitDouble();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native ascent fields do not consume the payload exactly");
        for (double v : {a.X, a.Y, a.Rotation, a.Length, a.Width, a.Indent})
            if (!Detail::Finite(v)) return fail("invalid native ascent settings");
        if (!(a.Length > 0) || !(a.Width > 0) || !(a.Scale > 0)) return fail("invalid native ascent size");
        ascent = a;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（3 个变体核对）：矩形外框；边坡斜线自坡顶内缩边坡宽处到坡底角点；有示坡线时斜线之间每隔 1 × 比例一条横线
    inline TchGraphics BuildAscent(const TchAscent& a)
    {
        TchGraphics g;
        const double hw = a.Width / 2, L = a.Length, in = a.Indent;
        auto at = [&](double x, double y) { return Detail::Local(a.X, a.Y, a.Rotation, x, y); };
        g.Lines.push_back({at(-hw, -L), at(hw, -L)});
        g.Lines.push_back({at(-hw, -L), at(-hw, 0)});
        g.Lines.push_back({at(hw, -L), at(hw, 0)});
        g.Lines.push_back({at(-hw, 0), at(hw, 0)});
        g.Lines.push_back({at(-hw + in, 0), at(-hw, -L)});
        g.Lines.push_back({at(hw - in, 0), at(hw, -L)});
        if (!a.Slant) return g;
        const double step = a.Scale;
        for (int i = 1; i * step < L - 1e-6 && i < 10000; ++i)
        {
            const double y = -i * step, inset = in * (1 - i * step / L);
            g.Lines.push_back({at(-hw + inset, y), at(hw - inset, y)});
        }
        return g;
    }
    // ── TCH_BALCONY 阳台（CADReader 无此类；按样例与天正 2014 分解结果确定）──
    struct TchBalcony
    {
        std::vector<Detail::PathVertex> Outline;    // 阳台外缘（开口两端在墙上）
        double RailWidth = 100;                     // 栏板宽
    };

    // 天正基类、RC 版本（2）、RC、BS、BD 标高、RC、BS 点数与各点 2RD + BD 凸度、8 个 BD（梁高、栏板高、栏板宽、板厚等）、5 位
    inline bool DecodeBalcony(const MiniDWG::UnknownEntity& entity, TchBalcony& balcony, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_BALCONY") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_BALCONY", r, base)) return fail("unsupported Tianzheng base layout");
        if (r.ReadByte() != 2) return fail("unsupported native balcony version");
        r.ReadByte(); r.ReadBitShort(); r.ReadBitDouble(); r.ReadByte();
        const int count = r.ReadBitShort();
        if (count < 2 || count > 100000) return fail("invalid balcony vertex count");
        TchBalcony b;
        for (int i = 0; i < count && !r.Failed(); ++i)
        {
            Detail::PathVertex v;
            v.X = r.ReadRawDouble(); v.Y = r.ReadRawDouble(); v.Bulge = r.ReadBitDouble();
            if (!Detail::Finite(v.X) || !Detail::Finite(v.Y) || !Detail::Finite(v.Bulge)) return fail("invalid balcony vertex");
            b.Outline.push_back(v);
        }
        double values[8];
        for (double& v : values) v = r.ReadBitDouble();
        b.RailWidth = values[2];
        for (int i = 0; i < 5; ++i) r.ReadBit();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native balcony fields do not consume the payload exactly");
        if (!Detail::Finite(b.RailWidth) || b.RailWidth < 0) return fail("invalid balcony rail width");
        balcony = std::move(b);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（1 个样例核对）：外缘线与向内偏移栏板宽的内缘线（开口端不封口）
    inline TchGraphics BuildBalcony(const TchBalcony& b)
    {
        TchGraphics g;
        g.Paths.push_back(Detail::ToPath(b.Outline, false));
        if (b.RailWidth > 0)
        {
            const double inward = Detail::SignedArea(b.Outline) > 0 ? 1.0 : -1.0;     // 逆时针时内侧在左
            g.Paths.push_back(Detail::ToPath(Detail::OffsetPath(b.Outline, false, inward * b.RailWidth), false));
        }
        return g;
    }
    // ── TCH_CURVESTAIR 任意梯段（CADReader 无此类；按样例与天正 2014 分解结果确定）──
    struct TchCurveStair
    {
        Detail::TchFlightBase Flight;
        struct Edge { double X1 = 0, Y1 = 0, X2 = 0, Y2 = 0, Bulge = 0; };
        Edge Left, Right;                       // 两侧边（起步端 → 末端）
    };

    // 梯段基类、RC 版本（1）、BD、两侧边：每条 4RD（起点、终点）与 BD 凸度（样例均为直线，凸度位置未核对）
    inline bool DecodeCurveStair(const MiniDWG::UnknownEntity& entity, TchCurveStair& stair, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_CURVESTAIR") return false;
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for curve stair");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchCurveStair s;
        if (!Detail::ReadFlightBase(r, s.Flight)) return fail("unsupported stair flight base layout");
        if (r.ReadByte() != 1) return fail("unsupported native curve stair version");
        r.ReadBitDouble();
        for (auto* e : {&s.Left, &s.Right})
        {
            e->X1 = r.ReadRawDouble(); e->Y1 = r.ReadRawDouble(); e->X2 = r.ReadRawDouble(); e->Y2 = r.ReadRawDouble();
            e->Bulge = r.ReadBitDouble();
        }
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native curve stair fields do not consume the payload exactly");
        for (const auto* e : {&s.Left, &s.Right})
            for (double v : {e->X1, e->Y1, e->X2, e->Y2, e->Bulge})
                if (!Detail::Finite(v)) return fail("invalid native curve stair edge");
        if (s.Flight.Steps < 2 || s.Flight.Steps > 1000) return fail("invalid native curve stair steps");
        stair = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    namespace Detail
    {
        // 边上参数 t（0～1，按弧长均分）处的点
        inline TchGraphics::Point EdgePoint(const TchCurveStair::Edge& e, double t)
        {
            if (std::abs(e.Bulge) < 1e-12) return {e.X1 + (e.X2 - e.X1) * t, e.Y1 + (e.Y2 - e.Y1) * t};
            const double dx = e.X2 - e.X1, dy = e.Y2 - e.Y1, L = std::hypot(dx, dy), th = 4 * std::atan(e.Bulge);
            const double r = L / 2 / std::sin(th / 2), h = r * std::cos(th / 2);
            const double cx = (e.X1 + e.X2) / 2 - dy / L * h, cy = (e.Y1 + e.Y2) / 2 + dx / L * h;
            const double a0 = std::atan2(e.Y1 - cy, e.X1 - cx), a = a0 + th * t;
            return {cx + std::abs(r) * std::cos(a), cy + std::abs(r) * std::sin(a)};
        }
    }

    // 天正 2014 分解结果（1 个样例核对）：两侧边；踏步线连接两侧边上同一参数的点（踏步数个）；
    // 剖断线自左边 BreakStart 处到右边 BreakEnd 处
    inline TchGraphics BuildCurveStair(const TchCurveStair& s)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        // 梯段范围：两侧边折线围成的多边形（每边取 16 段）
        std::vector<P> ring;
        for (int i = 0; i <= 16; ++i) ring.push_back(Detail::EdgePoint(s.Left, i / 16.0));
        for (int i = 16; i >= 0; --i) ring.push_back(Detail::EdgePoint(s.Right, i / 16.0));
        auto inside = [&](const P& p) {
            bool c = false;
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                const P& a = ring[i];
                const P& b = ring[(i + 1) % ring.size()];
                if ((a.Y > p.Y) != (b.Y > p.Y) && p.X < a.X + (p.Y - a.Y) * (b.X - a.X) / (b.Y - a.Y)) c = !c;
            }
            if (c) return true;
            // 边上的点也算在内
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                const P& a = ring[i];
                const P& b = ring[(i + 1) % ring.size()];
                const double L = std::hypot(b.X - a.X, b.Y - a.Y);
                if (L < 1e-9) continue;
                const double t = ((p.X - a.X) * (b.X - a.X) + (p.Y - a.Y) * (b.Y - a.Y)) / (L * L);
                if (t >= -1e-9 && t <= 1 + 1e-9 && std::abs((p.X - a.X) * (b.Y - a.Y) - (p.Y - a.Y) * (b.X - a.X)) / L < 1e-6) return true;
            }
            return false;
        };
        // 进度：投影到两边起点中点 → 终点中点的连线上
        const P m0{(s.Left.X1 + s.Right.X1) / 2, (s.Left.Y1 + s.Right.Y1) / 2}, m1{(s.Left.X2 + s.Right.X2) / 2, (s.Left.Y2 + s.Right.Y2) / 2};
        const double mx = m1.X - m0.X, my = m1.Y - m0.Y, ml2 = mx * mx + my * my;
        auto progress = [&](const P& p) { return ml2 > 0 ? ((p.X - m0.X) * mx + (p.Y - m0.Y) * my) / ml2 : 0.0; };
        const Detail::FlightBreak brk(Detail::BreakKind(s.Flight.BreakMode), Detail::EdgePoint(s.Left, s.Flight.BreakStart),
                                      Detail::EdgePoint(s.Right, s.Flight.BreakEnd), inside, progress, s.Flight.BreakStart, s.Flight.BreakEnd);
        for (const auto* e : {&s.Left, &s.Right})
        {
            if (std::abs(e->Bulge) < 1e-12) brk.AddLine(g, {e->X1, e->Y1}, {e->X2, e->Y2});
            else
                for (int i = 0; i < 32; ++i) brk.AddLine(g, Detail::EdgePoint(*e, i / 32.0), Detail::EdgePoint(*e, (i + 1) / 32.0));
        }
        for (int i = 0; i < s.Flight.Steps; ++i)
        {
            const double t = static_cast<double>(i) / (s.Flight.Steps - 1);
            brk.AddLine(g, Detail::EdgePoint(s.Left, t), Detail::EdgePoint(s.Right, t));
        }
        brk.AddSymbol(g);
        return g;
    }

    namespace Detail
    {
        // 把带凸度的轮廓展开为折线：圆弧段按 segs 等分（直线段保持一段）；Owner 记录每段来自原轮廓的第几条边
        struct FlatEdge { TchGraphics::Point A, B; int Owner = 0; };
        inline std::vector<FlatEdge> Flatten(const std::vector<PathVertex>& pts, bool closed, int segs = 16)
        {
            std::vector<FlatEdge> out;
            const std::size_t n = pts.size();
            const std::size_t count = closed ? n : (n ? n - 1 : 0);
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto& a = pts[i];
                const auto& b = pts[(i + 1) % n];
                if (std::abs(a.Bulge) < 1e-12) { out.push_back({{a.X, a.Y}, {b.X, b.Y}, static_cast<int>(i)}); continue; }
                const double dx = b.X - a.X, dy = b.Y - a.Y, L = std::hypot(dx, dy);
                if (L < 1e-12) continue;
                const double th = 4 * std::atan(a.Bulge), r = L / 2 / std::sin(th / 2), h = r * std::cos(th / 2);
                const double cx = (a.X + b.X) / 2 - dy / L * h, cy = (a.Y + b.Y) / 2 + dx / L * h;
                const double a0 = std::atan2(a.Y - cy, a.X - cx), R = std::abs(r);
                TchGraphics::Point prev{a.X, a.Y};
                for (int k = 1; k <= segs; ++k)
                {
                    const double t = a0 + th * k / segs;
                    const TchGraphics::Point p = k == segs ? TchGraphics::Point{b.X, b.Y} : TchGraphics::Point{cx + R * std::cos(t), cy + R * std::sin(t)};
                    out.push_back({prev, p, static_cast<int>(i)});
                    prev = p;
                }
            }
            return out;
        }

        // 直线 p + s·u 与 q + t·v 的交点；平行时返回 fallback
        inline TchGraphics::Point LineHit(TchGraphics::Point p, double ux, double uy, TchGraphics::Point q, double vx, double vy, TchGraphics::Point fallback)
        {
            const double den = ux * vy - uy * vx;
            if (std::abs(den) < 1e-12) return fallback;
            const double s = ((q.X - p.X) * vy - (q.Y - p.Y) * vx) / den;
            return {p.X + ux * s, p.Y + uy * s};
        }
    }

    // ── TCH_STEP 台阶（CADReader TDbStep 读取函数 0x6ed690，版本 1 为明文分支）──
    struct TchStep
    {
        std::vector<Detail::PathVertex> Outline;    // 平台轮廓（踏步自轮廓边向外排）
        bool     Closed = true;
        unsigned NoStepMask = 0;                    // 第 i 位：第 i 条边（点 i → 点 i+1）没有踏步
        int      Lines = 3;                         // 踏步线条数（含平台边）
        double   Tread = 300, Riser = 150;
    };

    // 天正基类、RC 版本（1）、BL 无踏步边掩码、BD 标高、RC 闭合、BS 点数与各点 2RD + BD 凸度、BS 踏步线数、BD 踏步宽、BD 踏步高
    inline bool DecodeStep(const MiniDWG::UnknownEntity& entity, TchStep& step, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_STEP") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_STEP", r, base)) return fail("unsupported Tianzheng base layout");
        if (r.ReadByte() != 1) return fail("unsupported native step version");
        TchStep s;
        s.NoStepMask = static_cast<unsigned>(r.ReadBitLong());
        r.ReadBitDouble();
        s.Closed = r.ReadByte() != 0;
        const int count = r.ReadBitShort();
        if (count < 2 || count > 10000) return fail("invalid step vertex count");
        for (int i = 0; i < count && !r.Failed(); ++i)
        {
            Detail::PathVertex v;
            v.X = r.ReadRawDouble(); v.Y = r.ReadRawDouble(); v.Bulge = r.ReadBitDouble();
            if (!Detail::Finite(v.X) || !Detail::Finite(v.Y) || !Detail::Finite(v.Bulge)) return fail("invalid step vertex");
            s.Outline.push_back(v);
        }
        s.Lines = r.ReadBitShort();
        s.Tread = r.ReadBitDouble();
        s.Riser = r.ReadBitDouble();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native step fields do not consume the payload exactly");
        if (s.Lines < 1 || s.Lines > 1000 || !Detail::Finite(s.Tread) || s.Tread <= 0) return fail("invalid native step settings");
        step = std::move(s);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（1 个样例核对）：有踏步的边每隔踏步宽向外偏移一次，共 Lines 条（第 0 条即平台边）；
    // 相邻有踏步边的偏移线在转角处相交，一串有踏步边的两端延长到相邻无踏步边（靠墙边）所在直线为止；无踏步边不画
    inline TchGraphics BuildStep(const TchStep& s)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const auto edges = Detail::Flatten(s.Outline, s.Closed, 8);
        const int n = static_cast<int>(edges.size());
        if (n == 0) return g;
        auto hasStep = [&](int e) { const int o = edges[e].Owner; return o >= 32 || !(s.NoStepMask & (1u << o)); };
        const double outward = s.Closed && Detail::SignedArea(s.Outline) < 0 ? -1.0 : 1.0;   // 逆时针时外侧在右
        struct Dir { double X, Y, Nx, Ny; };
        std::vector<Dir> dirs(n);
        for (int i = 0; i < n; ++i)
        {
            const double dx = edges[i].B.X - edges[i].A.X, dy = edges[i].B.Y - edges[i].A.Y, L = std::max(std::hypot(dx, dy), 1e-12);
            dirs[i] = {dx / L, dy / L, dy / L * outward, -dx / L * outward};
        }
        // 找出有踏步边的连续串（闭合时首尾相接）
        std::vector<std::pair<int, int>> chains;    // 起始边、边数
        bool all = true;
        for (int i = 0; i < n; ++i) all = all && hasStep(i);
        if (all && s.Closed) chains.push_back({0, n});
        else
            for (int i = 0; i < n; ++i)
            {
                if (!hasStep(i)) continue;
                const bool continues = s.Closed ? hasStep((i + n - 1) % n) : (i > 0 && hasStep(i - 1));
                if (continues) continue;                    // 不是一串的起点
                int len = 0;
                while (len < n && hasStep((i + len) % n) && (s.Closed || i + len < n)) ++len;
                chains.push_back({i, len});
            }
        for (int k = 0; k < s.Lines; ++k)
        {
            const double d = s.Tread * k;
            for (auto [first, len] : chains)
            {
                const bool ring = len == n && s.Closed;
                auto off = [&](int e) { return P{edges[e].A.X + dirs[e].Nx * d, edges[e].A.Y + dirs[e].Ny * d}; };
                TchGraphics::Path p;
                for (int j = 0; j <= len; ++j)
                {
                    const int e = (first + j) % n;                  // 第 j 个点在边 e 的起点（j == len 时为最后一条边的终点）
                    const int before = (first + j - 1 + n) % n;
                    P pt;
                    if (j > 0 && j < len)
                        pt = Detail::LineHit(off(before), dirs[before].X, dirs[before].Y, off(e), dirs[e].X, dirs[e].Y, off(e));
                    else if (ring)
                        pt = Detail::LineHit(off(before), dirs[before].X, dirs[before].Y, off(first), dirs[first].X, dirs[first].Y, off(first));
                    else if (j == 0)
                    {
                        const bool hasPrev = s.Closed || first > 0;
                        pt = hasPrev ? Detail::LineHit(off(e), dirs[e].X, dirs[e].Y, edges[before].A, dirs[before].X, dirs[before].Y, off(e)) : off(e);
                    }
                    else
                    {
                        const int last = before, next = (first + len) % n;
                        const P end{edges[last].B.X + dirs[last].Nx * d, edges[last].B.Y + dirs[last].Ny * d};
                        const bool hasNext = s.Closed || first + len < n;
                        pt = hasNext ? Detail::LineHit(off(last), dirs[last].X, dirs[last].Y, edges[next].A, dirs[next].X, dirs[next].Y, end) : end;
                    }
                    p.Points.push_back({pt});
                }
                if (ring) p.Points.back() = p.Points.front();
                g.Paths.push_back(std::move(p));
            }
        }
        return g;
    }

    // ── TCH_WALL_PATCH 墙体造型（CADReader TDbWallPatch 读取函数 0x6f7ad0，版本 ≤ 4）──
    struct TchWallPatch
    {
        std::vector<Detail::PathVertex> Outline;
        bool Closed = true;
        std::vector<MiniDWG::Handle> Walls;         // 所附着的墙
    };

    // 天正基类、RC 版本、BS 段数与各段 2 个 3BD + BD、BD、RC 闭合、BS 点数与各点 2RD + BD 凸度、BD 高、
    // RC 墙数（句柄流）、RC；版本 ≥ 3：RC 句柄数、RC；版本 ≥ 4：BL、BD。
    // 句柄流：基类句柄、各墙；版本 ≥ 2 一个句柄；版本 ≥ 3 若干句柄；版本 ≥ 4 一个句柄
    inline bool DecodeWallPatch(const MiniDWG::UnknownEntity& entity, TchWallPatch& patch, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_WALL_PATCH") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_WALL_PATCH", r, base)) return fail("unsupported Tianzheng base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 4) return fail("unsupported native wall patch version");
        const int segs = r.ReadBitShort();
        if (segs < 0 || segs > 10000) return fail("invalid wall patch segment count");
        for (int i = 0; i < segs && !r.Failed(); ++i) for (int k = 0; k < 7; ++k) r.ReadBitDouble();
        r.ReadBitDouble();
        TchWallPatch w;
        w.Closed = r.ReadByte() != 0;
        const int count = r.ReadBitShort();
        if (count < 2 || count > 10000) return fail("invalid wall patch vertex count");
        for (int i = 0; i < count && !r.Failed(); ++i)
        {
            Detail::PathVertex v;
            v.X = r.ReadRawDouble(); v.Y = r.ReadRawDouble(); v.Bulge = r.ReadBitDouble();
            if (!Detail::Finite(v.X) || !Detail::Finite(v.Y) || !Detail::Finite(v.Bulge)) return fail("invalid wall patch vertex");
            w.Outline.push_back(v);
        }
        r.ReadBitDouble();
        const int walls = r.ReadByte();
        r.ReadByte();
        if (version > 2) { r.ReadByte(); r.ReadByte(); }
        if (version > 3) { r.ReadBitLong(); r.ReadBitDouble(); }
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native wall patch fields do not consume the payload exactly");
        DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
        for (int i = 0; i < base.HandleCount; ++i) handles.ReadHandle(entity.ObjectHandle);
        for (int i = 0; i < walls && !handles.Failed(); ++i) w.Walls.push_back(handles.ReadHandle(entity.ObjectHandle));
        patch = std::move(w);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（1 个样例核对）：轮廓线去掉落在所附着墙体内部的部分（墙边上的部分保留）。
    // wallBodies：各墙的四边形（墙线两侧边围成），由调用方按墙的解码结果给出
    inline TchGraphics BuildWallPatch(const TchWallPatch& w, const std::vector<std::array<TchGraphics::Point, 4>>& wallBodies)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        // 线段 a→b 落在凸四边形内部的参数区间（Cyrus–Beck；只算严格内部，贴边的线段不剪）
        auto inside = [](const std::array<P, 4>& q, P a, P b, double& t0, double& t1) {
            double area = 0;
            for (int i = 0; i < 4; ++i) area += q[i].X * q[(i + 1) % 4].Y - q[(i + 1) % 4].X * q[i].Y;
            const double sgn = area >= 0 ? 1 : -1;
            t0 = 0; t1 = 1;
            for (int i = 0; i < 4; ++i)
            {
                const P& e0 = q[i];
                const P& e1 = q[(i + 1) % 4];
                const double ex = e1.X - e0.X, ey = e1.Y - e0.Y, el = std::hypot(ex, ey);
                if (el < 1e-9) continue;
                // 内侧距离 f(t) = f0 + t·df，需 > eps
                const double f0 = sgn * (ex * (a.Y - e0.Y) - ey * (a.X - e0.X)) / el;
                const double df = sgn * (ex * (b.Y - a.Y) - ey * (b.X - a.X)) / el;
                constexpr double eps = 1e-6;
                if (std::abs(df) < 1e-12) { if (f0 <= eps) return false; continue; }
                const double t = (eps - f0) / df;
                if (df > 0) t0 = std::max(t0, t); else t1 = std::min(t1, t);
            }
            return t1 - t0 > 1e-9;
        };
        for (const auto& e : Detail::Flatten(w.Outline, w.Closed))
        {
            std::vector<std::pair<double, double>> cut;
            for (const auto& q : wallBodies)
            {
                double t0, t1;
                if (inside(q, e.A, e.B, t0, t1)) cut.push_back({t0, t1});
            }
            std::sort(cut.begin(), cut.end());
            double t = 0;
            auto at = [&](double u) { return P{e.A.X + (e.B.X - e.A.X) * u, e.A.Y + (e.B.Y - e.A.Y) * u}; };
            for (auto [c0, c1] : cut)
            {
                if (c0 > t + 1e-9) g.Lines.push_back({at(t), at(c0)});
                t = std::max(t, c1);
            }
            if (t < 1 - 1e-9) g.Lines.push_back({at(t), at(1)});
        }
        return g;
    }

    // ── 带形窗（TCH_OPENING 类型 5）──
    // 天正 2014 分解结果（1 个样例核对）：沿墙 4 条窗线（墙两侧面及其间三等分处），不封口；
    // 编号文字以编号位置为中心。left / right 为所在墙在窗位置处左、右两侧的宽度（按墙的方向 wallAngle）
    inline TchGraphics BuildBandWindow(const TchOpening& o, double wallAngle, double left, double right)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double nx = -std::sin(wallAngle), ny = std::cos(wallAngle);
        auto offsets = [&](P a, P b, double bulge) {
            for (int k = 0; k <= 3; ++k)
            {
                const double t = -right + (left + right) * k / 3;
                if (std::abs(bulge) < 1e-12)
                {
                    g.Lines.push_back({{a.X + nx * t, a.Y + ny * t}, {b.X + nx * t, b.Y + ny * t}});
                    continue;
                }
                // 圆弧段：同心偏移（偏移方向按弦的左侧近似）
                const double dx = b.X - a.X, dy = b.Y - a.Y, L = std::hypot(dx, dy);
                if (L < 1e-9) continue;
                const double th = 4 * std::atan(bulge), r = L / 2 / std::sin(th / 2), h = r * std::cos(th / 2);
                const double cx = (a.X + b.X) / 2 - dy / L * h, cy = (a.Y + b.Y) / 2 + dx / L * h;
                const double R = std::abs(r) - (bulge > 0 ? t : -t);
                if (R <= 0) continue;
                double s0 = std::atan2(a.Y - cy, a.X - cx), s1 = std::atan2(b.Y - cy, b.X - cx);
                if (bulge < 0) std::swap(s0, s1);
                g.Arcs.push_back({{cx, cy}, R, s0, s1});
            }
        };
        if (o.Segments.empty())
        {
            const double ux = std::cos(o.Angle) * o.Width / 2, uy = std::sin(o.Angle) * o.Width / 2;
            offsets({o.Position.X - ux, o.Position.Y - uy}, {o.Position.X + ux, o.Position.Y + uy}, 0);
        }
        else
            for (const auto& s : o.Segments) offsets({s.X1, s.Y1}, {s.X2, s.Y2}, s.Bulge);
        if (!o.Label.empty())
        {
            const double nominal = o.LabelHeight * o.Scale;
            Detail::AddRuns(g, o.Label, nominal, o.LabelX, o.LabelY - 0.425 * nominal, 0, 1);
        }
        return g;
    }

    // ── TCH_AUTOSTAIR 自动扶梯（CADReader TDbAutoStair 读取函数 0x6c4a50，版本 0 为明文分支）──
    struct TchAutoStair
    {
        double X = 0, Y = 0, Rotation = 0;      // 上端外角点；扶梯沿局部 −Y 方向（向下端）展开
        double Tread = 300, StepWidth = 800;    // 踏步宽、梯级宽
        double Landing = 2200;                  // 两端平台长
        double CombOffset = 900;                // 梳齿板线距踏步起点
        double Rise = 3000, AngleDeg = 30;      // 提升高度、倾角
        double BreakStart = 0.4, BreakEnd = 0.6;
        int    Floor = 1;                       // StairFlag：0 首层、1 中间层、2 顶层
        double TextHeight = 3.5, Scale = 100;
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle, ArrowLayer = MiniDWG::kNullHandle;
    };

    // 天正基类、楼梯基类 RC（1～2）、RC 版本（0）、3BD 角点、BD 转角、BD、BD 踏步宽、BD 梯级宽、BD 平台长、BD、BD、RC 楼层、BD、
    // BD 剖断起、BD 剖断止、BD 梳齿板距、BD 字高、BD 提升高度、BD 倾角、BD、BD、RC、BL 标志、BD；
    // 句柄流：楼梯基类一个、四个（第 3 个为文字样式，第 4 个为箭头图层）；末尾 RC
    inline bool DecodeAutoStair(const MiniDWG::UnknownEntity& entity, TchAutoStair& stair, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_AUTOSTAIR") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_AUTOSTAIR", r, base)) return fail("unsupported Tianzheng base layout");
        const int stairBase = r.ReadByte();
        if (stairBase < 1 || stairBase > 2) return fail("unsupported stair base version");
        if (r.ReadByte() != 0) return fail("unsupported native escalator version");
        TchAutoStair s;
        s.Scale = base.Scale;
        s.X = r.ReadBitDouble(); s.Y = r.ReadBitDouble(); r.ReadBitDouble();
        s.Rotation = r.ReadBitDouble();
        r.ReadBitDouble();
        s.Tread = r.ReadBitDouble();
        s.StepWidth = r.ReadBitDouble();
        s.Landing = r.ReadBitDouble();
        r.ReadBitDouble(); r.ReadBitDouble();
        s.Floor = r.ReadByte();
        r.ReadBitDouble();
        s.BreakStart = r.ReadBitDouble();
        s.BreakEnd = r.ReadBitDouble();
        s.CombOffset = r.ReadBitDouble();
        s.TextHeight = r.ReadBitDouble();
        s.Rise = r.ReadBitDouble();
        s.AngleDeg = r.ReadBitDouble();
        r.ReadBitDouble(); r.ReadBitDouble();
        r.ReadByte();
        r.ReadBitLong();
        r.ReadBitDouble();
        r.ReadByte();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native escalator fields do not consume the payload exactly");
        for (double v : {s.X, s.Y, s.Rotation, s.Tread, s.StepWidth, s.Landing, s.CombOffset, s.Rise, s.AngleDeg, s.TextHeight})
            if (!Detail::Finite(v)) return fail("invalid native escalator settings");
        if (s.Tread <= 0 || s.StepWidth <= 0 || s.Landing < 0 || s.Rise <= 0 || s.AngleDeg <= 1 || s.AngleDeg >= 89)
            return fail("invalid native escalator settings");
        DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
        for (int i = 0; i < base.HandleCount + 1 + 2; ++i) handles.ReadHandle(entity.ObjectHandle);
        s.TextStyle = handles.ReadHandle(entity.ObjectHandle);
        s.ArrowLayer = handles.ReadHandle(entity.ObjectHandle);
        stair = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（中间层 / 首层 / 顶层与属性取不同值共 4 个样例核对；扶手各线距外边的横向偏移取样例值，沿长度方向的位置相对梳齿板线）：
    // 总长 = 2 × 平台长 + ⌊提升高度 / tan(倾角) / 踏步宽⌋ × 踏步宽，总宽 = 梯级宽 + 2 × 274；
    // 外框、两侧扶手带与裙板线、梳齿板线、踏步线；斜向双剖断线两端在踏步段（自下端起）的 BreakStart / BreakEnd 处；
    // 上、下两个箭头自两端扶手端头指向中部，文字在箭头起点外侧
    inline TchGraphics BuildAutoStair(const TchAutoStair& s)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double side = 274, W = s.StepWidth + 2 * side;
        const int steps = static_cast<int>(std::floor(s.Rise / std::tan(s.AngleDeg * std::numbers::pi / 180) / s.Tread + 1e-9));
        const double run = std::max(steps, 1) * s.Tread, L = 2 * s.Landing + run;
        const double comb = s.Landing - s.CombOffset;      // 梳齿板线距两端（扶手端头等沿长度方向的位置都相对它）
        // 局部坐标：x 横向 0～W，v 自上端向下 0～L（局部 y = −v）
        auto at = [](double x, double v) { return P{x, -v}; };
        auto progress = [&](const P& p) { return (L - s.Landing - (-p.Y)) / run; };     // 踏步段自下端起的进度
        auto inside = [&](const P& p) { return p.X >= -1e-6 && p.X <= W + 1e-6 && -p.Y >= -1e-6 && -p.Y <= L + 1e-6; };
        // 中间层双剖断；首层单剖断（只留下端）；顶层不剖断
        const int kind = s.Floor == 1 ? 2 : (s.Floor == 0 ? 1 : 0);
        const Detail::FlightBreak brk(kind, at(0, L - s.Landing - s.BreakStart * run), at(W, L - s.Landing - s.BreakEnd * run),
                                      inside, progress, s.BreakStart, s.BreakEnd);
        auto line = [&](double x0, double v0, double x1, double v1) { brk.AddLine(g, at(x0, v0), at(x1, v1)); };
        // 外框
        line(0, 0, W, 0); line(0, L, W, L); line(0, 0, 0, L); line(W, 0, W, L);
        // 扶手带、裙板线（左右对称、上下对称）
        for (const bool right : {false, true})
        {
            auto x = [&](double d) { return right ? W - d : d; };
            line(x(29), (comb - 119.5), x(29), L - (comb - 119.5));
            line(x(115), (comb - 655.5), x(115), L - (comb - 655.5));
            line(x(175), (comb - 655.5), x(175), L - (comb - 655.5));
            line(x(side), (comb - 132.75), x(side), L - (comb - 132.75));
            for (const bool bottom : {false, true})
            {
                auto v = [&](double d) { return bottom ? L - d : d; };
                line(x(115), v((comb - 655.5)), x(175), v((comb - 655.5)));
                line(x(29), v((comb - 119.5)), x(115), v((comb - 205.5)));
                line(x(175), v((comb - 205.5)), x(side), v((comb - 132.75)));
            }
        }
        // 梳齿板线与踏步线
        line(side, s.Landing - s.CombOffset, W - side, s.Landing - s.CombOffset);
        line(side, L - s.Landing + s.CombOffset, W - side, L - s.Landing + s.CombOffset);
        for (int k = 0; k <= steps; ++k) line(side, s.Landing + k * s.Tread, W - side, s.Landing + k * s.Tread);
        brk.AddSymbol(g);

        // 局部 → 世界
        const double cr = std::cos(s.Rotation), sr = std::sin(s.Rotation);
        auto world = [&](const P& q) { return P{s.X + q.X * cr - q.Y * sr, s.Y + q.X * sr + q.Y * cr}; };
        for (auto& l : g.Lines) { l.A = world(l.A); l.B = world(l.B); }
        // 箭头与文字（箭头线宽 0.8 × 比例渐变为 0，画为实心三角形）
        const double hn = s.TextHeight * s.Scale, half = 0.4 * s.Scale, head = 3 * s.Scale;
        struct Arrow { double From, To; const char* Text; bool Up; };
        // 上行箭头（首层、中间层）、下行箭头（中间层、顶层；顶层指到下端踏步起点以上一级）
        const double downTo = s.Floor == 2 ? L - s.Landing - s.Tread - head : L / 2 - 2 * head;
        const Arrow arrows[] = {{L - (comb - 655.5), L / 2 + 2 * head, "\xe4\xb8\x8a", true}, {(comb - 655.5), downTo, "\xe4\xb8\x8b", false}};    // UTF-8“上”“下”
        for (const auto& a : arrows)
        {
            if ((a.Up && s.Floor == 2) || (!a.Up && s.Floor == 0)) continue;
            const double dir = a.Up ? -1 : 1;           // v 方向
            TchGraphics::Path shaft;
            shaft.Layer = 1;
            shaft.Points = {{world(at(W / 2, a.From))}, {world(at(W / 2, a.To))}};
            g.Paths.push_back(std::move(shaft));
            const double tipV = a.To + dir * head;
            g.Arrows.push_back({world(at(W / 2 - half, a.To)), world(at(W / 2 + half, a.To)), world(at(W / 2, tipV)), 1});
            // 文字基线：向上箭头在起点外 1.3 × 字高，向下箭头在起点外 0.3 × 字高
            const double baseV = a.Up ? a.From + 1.3 * hn : a.From - 0.3 * hn;
            const P c = world(at(W / 2, baseV));
            Detail::AddRuns(g, a.Text, hn, c.X, c.Y, s.Rotation, 1, 7, 1);
        }
        return g;
    }

    namespace Detail
    {
        // 屋顶共用的头部（天正内核 TDbPolyPathEnt::dwgInFields 与 TRoofBase::Read）：天正基类、RC 路径版本（> 2 时另有 RC 密钥）、
        // RC 屋顶版本（由调用方读）之前的部分；TRoofBase：RC 版本，< 2 时 3 个 BD、RC、2 个 B，≥ 1 时 BL 轮廓数与各轮廓
        // （RC 闭合、BS 点数、各点 2RD + BD 凸度）、BL 句柄数；≥ 2 时先读 RC 密钥（只支持明文，即密钥 < 16），没有 2 个 B
        inline bool ReadRoofBase(MiniDWG::DwgBitReader& r)
        {
            const int version = r.ReadByte();
            if (version > 3) return false;
            if (version >= 2 && r.ReadByte() >= 16) return false;
            r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
            r.ReadByte();
            if (version < 2) { r.ReadBit(); r.ReadBit(); }
            if (version >= 1)
            {
                const int polys = r.ReadBitLong();
                if (polys < 0 || polys > 10000) return false;
                for (int i = 0; i < polys && !r.Failed(); ++i)
                {
                    r.ReadByte();
                    const int n = r.ReadBitShort();
                    if (n < 0 || n > 100000) return false;
                    for (int j = 0; j < n && !r.Failed(); ++j) { r.ReadRawDouble(); r.ReadRawDouble(); r.ReadBitDouble(); }
                }
                r.ReadBitLong();
            }
            return !r.Failed();
        }

        inline bool RoofHead(const MiniDWG::UnknownEntity& entity, const char* dxf, MiniDWG::DwgBitReader& r, int& version)
        {
            TchBase base;
            if (!ShapeHead(entity, dxf, r, base)) return false;
            const int pathVersion = r.ReadByte();
            if (pathVersion > 3) return false;
            if (pathVersion > 2 && r.ReadByte() >= 16) return false;
            version = r.ReadByte();
            return ReadRoofBase(r);
        }
    }

    // ── TCH_MOUNTROOF 矩形屋顶（歇山）（天正内核 TDbMountRoof::dwgInFields，版本 ≤ 2）──
    struct TchMountRoof
    {
        double X = 0, Y = 0, Rotation = 0;     // 墙外皮矩形的第一个角点；长度方向为转角方向
        double Length = 6000, Width = 4000;
        double EndSlope = std::numbers::pi / 4; // 两端坡角（属性 SlopeAng）
        double Eaves = 600;                    // 长边方向出檐
        double RoofHeight = 3000, MountHeight = 1100;
    };

    // 屋顶头部、RC 版本（2 时另有 RC 密钥）、3BD 基点、BD 转角、BD 长、BD 宽、BD 端坡角、BD 出檐、BD、BD 屋顶高、BD 歇山高、B
    inline bool DecodeMountRoof(const MiniDWG::UnknownEntity& entity, TchMountRoof& roof, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_MOUNTROOF") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        int version = 0;
        if (!Detail::RoofHead(entity, "TCH_MOUNTROOF", r, version)) return fail("unsupported Tianzheng roof base layout");
        if (version > 2) return fail("unsupported native mount roof version");
        if (version > 1 && r.ReadByte() >= 16) return fail("encrypted native mount roof is not supported");
        TchMountRoof m;
        m.X = r.ReadBitDouble(); m.Y = r.ReadBitDouble(); r.ReadBitDouble();
        m.Rotation = r.ReadBitDouble();
        m.Length = r.ReadBitDouble();
        m.Width = r.ReadBitDouble();
        m.EndSlope = r.ReadBitDouble();
        m.Eaves = r.ReadBitDouble();
        r.ReadBitDouble();
        m.RoofHeight = r.ReadBitDouble();
        m.MountHeight = r.ReadBitDouble();
        r.ReadBit();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native mount roof fields do not consume the payload exactly");
        for (double v : {m.X, m.Y, m.Rotation, m.Length, m.Width, m.EndSlope, m.Eaves, m.RoofHeight, m.MountHeight})
            if (!Detail::Finite(v)) return fail("invalid native mount roof settings");
        if (m.Length <= 0 || m.Width <= 0 || m.RoofHeight <= 0 || m.EndSlope <= 0.01 || m.EndSlope >= std::numbers::pi / 2 - 0.01)
            return fail("invalid native mount roof settings");
        roof = m;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（5 个样例核对）：主坡 tan = 屋顶高 / (宽 / 2)；长边出檐 Eaves，两端出檐 = Eaves·tan(主坡) / tan(端坡)；
    // 歇山山面距墙 (屋顶高 − 歇山高) / tan(端坡)、半宽 歇山高 / tan(主坡)；画外轮廓、屋脊、两个山面线与四条斜脊
    inline TchGraphics BuildMountRoof(const TchMountRoof& m)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double tanMain = m.RoofHeight / (m.Width / 2), tanEnd = std::tan(m.EndSlope);
        const double ex = m.Eaves * tanMain / tanEnd, ey = m.Eaves;
        const double xg = std::min((m.RoofHeight - std::max(m.MountHeight, 0.0)) / tanEnd, m.Length / 2);
        const double half = std::clamp(m.MountHeight / tanMain, 0.0, m.Width / 2), cy = m.Width / 2;
        auto at = [&](double x, double y) { return Detail::Local(m.X, m.Y, m.Rotation, x, y); };
        TchGraphics::Path outline;
        for (auto [x, y] : {std::pair{-ex, -ey}, {m.Length + ex, -ey}, {m.Length + ex, m.Width + ey}, {-ex, m.Width + ey}, {-ex, -ey}})
            outline.Points.push_back({at(x, y)});
        g.Paths.push_back(std::move(outline));
        const P a0 = at(xg, cy - half), a1 = at(xg, cy + half), b0 = at(m.Length - xg, cy - half), b1 = at(m.Length - xg, cy + half);
        g.Lines.push_back({at(xg, cy), at(m.Length - xg, cy)});
        if (half > 1e-9) { g.Lines.push_back({a0, a1}); g.Lines.push_back({b0, b1}); }
        g.Lines.push_back({at(-ex, -ey), a0});
        g.Lines.push_back({at(-ex, m.Width + ey), a1});
        g.Lines.push_back({at(m.Length + ex, -ey), b0});
        g.Lines.push_back({at(m.Length + ex, m.Width + ey), b1});
        return g;
    }

    // ── TCH_CUSPROOF 攒尖屋顶（天正内核 TDbCuspRoof::dwgInFields，版本 ≤ 3）──
    struct TchCuspRoof
    {
        double X = 0, Y = 0, Rotation = 0;     // 中心、第一个顶点的方向
        double Height = 3000, Radius = 2000, Eaves = 600;
        int    Sides = 6;
    };

    // 屋顶头部、RC 版本（3 时另有 RC 密钥）、3BD 中心、BD 转角、BD 高、BD 半径（墙多边形外接圆）、RC 边数；版本 ≥ 2：BD 出檐
    inline bool DecodeCuspRoof(const MiniDWG::UnknownEntity& entity, TchCuspRoof& roof, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_CUSPROOF") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        int version = 0;
        if (!Detail::RoofHead(entity, "TCH_CUSPROOF", r, version)) return fail("unsupported Tianzheng roof base layout");
        if (version > 3) return fail("unsupported native cusp roof version");
        if (version > 2 && r.ReadByte() >= 16) return fail("encrypted native cusp roof is not supported");
        TchCuspRoof c;
        c.X = r.ReadBitDouble(); c.Y = r.ReadBitDouble(); r.ReadBitDouble();
        c.Rotation = r.ReadBitDouble();
        c.Height = r.ReadBitDouble();
        c.Radius = r.ReadBitDouble();
        c.Sides = r.ReadByte();
        c.Eaves = version > 1 ? r.ReadBitDouble() : 0.0;
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native cusp roof fields do not consume the payload exactly");
        for (double v : {c.X, c.Y, c.Rotation, c.Height, c.Radius, c.Eaves})
            if (!Detail::Finite(v)) return fail("invalid native cusp roof settings");
        if (c.Sides < 3 || c.Sides > 64 || c.Radius <= 0) return fail("invalid native cusp roof settings");
        roof = c;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（2 个样例核对）：正多边形外轮廓（顶点半径 = 半径 + 出檐 / cos(π / 边数)，第一个顶点在转角方向），
    // 中心到各顶点的脊线
    inline TchGraphics BuildCuspRoof(const TchCuspRoof& c)
    {
        TchGraphics g;
        const double rv = c.Radius + c.Eaves / std::cos(std::numbers::pi / c.Sides);
        TchGraphics::Path outline;
        for (int i = 0; i <= c.Sides; ++i)
        {
            const double a = c.Rotation + 2 * std::numbers::pi * (i % c.Sides) / c.Sides;
            const TchGraphics::Point p{c.X + rv * std::cos(a), c.Y + rv * std::sin(a)};
            outline.Points.push_back({p});
            if (i < c.Sides) g.Lines.push_back({{c.X, c.Y}, p});
        }
        g.Paths.push_back(std::move(outline));
        return g;
    }

    // ── TCH_SLOPEROOF 任意坡顶（天正内核 TDbSlopeRoof::dwgInFields，版本 < 3 为明文分支）──
    struct TchSlopeRoof
    {
        struct Edge { double X = 0, Y = 0, Slope = 0; };    // 轮廓顶点（边的起点）与该边的坡角
        std::vector<Edge> Edges;
    };

    // 路径基类、RC 版本、BD 檐口标高、BS 边数与各边 3BD 起点 + BD 坡角；版本 ≥ 2：屋顶基类、BD
    inline bool DecodeSlopeRoof(const MiniDWG::UnknownEntity& entity, TchSlopeRoof& roof, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_SLOPEROOF") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_SLOPEROOF", r, base)) return fail("unsupported Tianzheng base layout");
        const int pathVersion = r.ReadByte();
        if (pathVersion > 2) return fail("unsupported Tianzheng path base layout");
        const int version = r.ReadByte();
        if (version > 2) return fail("unsupported native slope roof version");
        TchSlopeRoof s;
        r.ReadBitDouble();
        const int count = r.ReadBitShort();
        if (count < 3 || count > 10000) return fail("invalid slope roof edge count");
        for (int i = 0; i < count && !r.Failed(); ++i)
        {
            TchSlopeRoof::Edge e;
            e.X = r.ReadBitDouble(); e.Y = r.ReadBitDouble(); r.ReadBitDouble();
            e.Slope = r.ReadBitDouble();
            if (!Detail::Finite(e.X) || !Detail::Finite(e.Y) || !Detail::Finite(e.Slope)) return fail("invalid slope roof edge");
            s.Edges.push_back(e);
        }
        if (version > 1)
        {
            if (!Detail::ReadRoofBase(r)) return fail("unsupported Tianzheng roof base layout");
            r.ReadBitDouble();
        }
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native slope roof fields do not consume the payload exactly");
        roof = std::move(s);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（1 个样例核对：矩形、四边同坡）：轮廓各边与屋脊、斜脊。
    // 凸轮廓时坡屋面即各边坡面的下包络：每条边的坡面范围 = 轮廓内满足“本边坡面不高于其他各边坡面”的凸区域，
    // 画出这些区域的边界（凹轮廓的屋脊推算未实现，只画轮廓）
    inline TchGraphics BuildSlopeRoof(const TchSlopeRoof& s)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const std::size_t n = s.Edges.size();
        std::vector<P> poly;
        for (const auto& e : s.Edges) poly.push_back({e.X, e.Y});
        double area = 0;
        for (std::size_t i = 0; i < n; ++i) area += poly[i].X * poly[(i + 1) % n].Y - poly[(i + 1) % n].X * poly[i].Y;
        const double orient = area >= 0 ? 1.0 : -1.0;              // 逆时针时内侧在左
        bool convex = true;
        for (std::size_t i = 0; i < n; ++i)
        {
            const P& a = poly[i]; const P& b = poly[(i + 1) % n]; const P& c = poly[(i + 2) % n];
            if (orient * ((b.X - a.X) * (c.Y - b.Y) - (b.Y - a.Y) * (c.X - b.X)) < -1e-9) convex = false;
        }
        if (!convex || n < 3)
        {
            TchGraphics::Path p;
            for (const auto& q : poly) p.Points.push_back({q});
            p.Points.push_back({poly.front()});
            g.Paths.push_back(std::move(p));
            return g;
        }
        // 第 i 边坡面高度 h_i(p) = tan(坡角)·(p 到该边的内侧距离)，写成 A·x + B·y + C
        struct Plane { double A, B, C; };
        std::vector<Plane> planes(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            const P& a = poly[i]; const P& b = poly[(i + 1) % n];
            const double dx = b.X - a.X, dy = b.Y - a.Y, len = std::max(std::hypot(dx, dy), 1e-12);
            const double inx = -dy / len * orient, iny = dx / len * orient, t = std::tan(s.Edges[i].Slope);
            planes[i] = {inx * t, iny * t, -(inx * a.X + iny * a.Y) * t};
        }
        // 凸多边形被半平面 A·x + B·y + C ≤ 0 裁剪
        auto clip = [](const std::vector<P>& in, double A, double B, double C) {
            std::vector<P> out;
            for (std::size_t i = 0; i < in.size(); ++i)
            {
                const P& p = in[i]; const P& q = in[(i + 1) % in.size()];
                const double fp = A * p.X + B * p.Y + C, fq = A * q.X + B * q.Y + C;
                if (fp <= 1e-9) out.push_back(p);
                if ((fp < -1e-9 && fq > 1e-9) || (fp > 1e-9 && fq < -1e-9))
                {
                    const double t = fp / (fp - fq);
                    out.push_back({p.X + (q.X - p.X) * t, p.Y + (q.Y - p.Y) * t});
                }
            }
            return out;
        };
        std::vector<TchGraphics::Segment> edges;
        auto addUnique = [&](P a, P b) {
            if (std::hypot(b.X - a.X, b.Y - a.Y) < 1e-6) return;
            for (const auto& e : edges)
                if ((std::hypot(e.A.X - a.X, e.A.Y - a.Y) < 1e-6 && std::hypot(e.B.X - b.X, e.B.Y - b.Y) < 1e-6) ||
                    (std::hypot(e.A.X - b.X, e.A.Y - b.Y) < 1e-6 && std::hypot(e.B.X - a.X, e.B.Y - a.Y) < 1e-6)) return;
            edges.push_back({a, b});
        };
        for (std::size_t i = 0; i < n; ++i)
        {
            std::vector<P> face = poly;
            for (std::size_t j = 0; j < n && face.size() >= 3; ++j)
                if (j != i) face = clip(face, planes[i].A - planes[j].A, planes[i].B - planes[j].B, planes[i].C - planes[j].C);
            for (std::size_t k = 0; face.size() >= 2 && k < face.size(); ++k) addUnique(face[k], face[(k + 1) % face.size()]);
        }
        for (const auto& e : edges) g.Lines.push_back(e);
        return g;
    }

    // ── TCH_DUALROOF 人字坡顶（天正内核 TDbDualSlopeRoof::dwgInFields，版本 < 3 为明文分支）──
    struct TchDualRoof
    {
        std::vector<Detail::PathVertex> Outline;
        double X1 = 0, Y1 = 0, X2 = 0, Y2 = 0;            // 屋脊两端
    };

    // 路径基类、RC 版本、屋顶基类、BD 标高、轮廓（RC 闭合、BS 点数、各点 2RD + BD 凸度）、2RD 屋脊起点、2RD 屋脊终点、
    // BD、BD 两侧坡角、B；版本 ≥ 2：B、BD
    inline bool DecodeDualRoof(const MiniDWG::UnknownEntity& entity, TchDualRoof& roof, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_DUALROOF") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        int version = 0;
        if (!Detail::RoofHead(entity, "TCH_DUALROOF", r, version)) return fail("unsupported Tianzheng roof base layout");
        if (version > 2) return fail("unsupported native dual-slope roof version");
        TchDualRoof d;
        r.ReadBitDouble();
        r.ReadByte();
        const int count = r.ReadBitShort();
        if (count < 2 || count > 100000) return fail("invalid dual-slope roof outline");
        for (int i = 0; i < count && !r.Failed(); ++i)
        {
            Detail::PathVertex v;
            v.X = r.ReadRawDouble(); v.Y = r.ReadRawDouble(); v.Bulge = r.ReadBitDouble();
            d.Outline.push_back(v);
        }
        d.X1 = r.ReadRawDouble(); d.Y1 = r.ReadRawDouble();
        d.X2 = r.ReadRawDouble(); d.Y2 = r.ReadRawDouble();
        r.ReadBitDouble(); r.ReadBitDouble();
        r.ReadBit();
        if (version > 1) { r.ReadBit(); r.ReadBitDouble(); }
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native dual-slope roof fields do not consume the payload exactly");
        for (const auto& v : d.Outline)
            if (!Detail::Finite(v.X) || !Detail::Finite(v.Y) || !Detail::Finite(v.Bulge)) return fail("invalid dual-slope roof outline");
        for (double v : {d.X1, d.Y1, d.X2, d.Y2})
            if (!Detail::Finite(v)) return fail("invalid dual-slope roof ridge");
        roof = std::move(d);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（1 个样例核对）：闭合轮廓多段线与屋脊线
    inline TchGraphics BuildDualRoof(const TchDualRoof& d)
    {
        TchGraphics g;
        g.Paths.push_back(Detail::ToPath(d.Outline, true));
        g.Lines.push_back({{d.X1, d.Y1}, {d.X2, d.Y2}});
        return g;
    }

    // ── TCH_OPENINGSLOT 门窗套（天正内核 TDbOpeningSlot::dwgInFields，版本 < 3 为明文分支）──
    struct TchOpeningSlot
    {
        MiniDWG::Handle Opening = MiniDWG::kNullHandle;   // 所属门窗
        int    Sides = 1;                                  // 位 0：门窗方向左侧墙面，位 1：右侧（样例为 1，即室内一侧在左）
        double ProfileWidth = 60, ProfileDepth = 15;       // 门窗套截面：沿墙方向宽、伸出墙面厚
    };

    // 天正基类、RC 版本、BS、（句柄流：所属门窗）、B、3 个 BS（类型、样式、位置）、截面轮廓（BS 闭合、BS 点数、各点 3BD + BD）、
    // 11 个 BD、BS；版本 ≥ 2：句柄流 2 个句柄
    inline bool DecodeOpeningSlot(const MiniDWG::UnknownEntity& entity, TchOpeningSlot& slot, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_OPENINGSLOT") return false;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_OPENINGSLOT", r, base)) return fail("unsupported Tianzheng base layout");
        const int version = r.ReadByte();
        if (version > 2) return fail("unsupported native opening slot version");
        TchOpeningSlot s;
        r.ReadBitShort();
        r.ReadBit();
        s.Sides = r.ReadBitShort();
        r.ReadBitShort(); r.ReadBitShort();
        r.ReadBitShort();
        const int count = r.ReadBitShort();
        if (count < 0 || count > 10000) return fail("invalid opening slot profile");
        double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        for (int i = 0; i < count && !r.Failed(); ++i)
        {
            const double x = r.ReadBitDouble(), y = r.ReadBitDouble();
            r.ReadBitDouble(); r.ReadBitDouble();
            if (i == 0) { x0 = x1 = x; y0 = y1 = y; }
            x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
        }
        for (int i = 0; i < 11; ++i) r.ReadBitDouble();
        r.ReadBitShort();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native opening slot fields do not consume the payload exactly");
        s.ProfileWidth = x1 - x0;
        s.ProfileDepth = y1 - y0;
        if (!Detail::Finite(s.ProfileWidth) || !Detail::Finite(s.ProfileDepth)) return fail("invalid opening slot profile");
        DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
        for (int i = 0; i < base.HandleCount; ++i) handles.ReadHandle(entity.ObjectHandle);
        s.Opening = handles.ReadHandle(entity.ObjectHandle);
        if (handles.Failed() || s.Opening == kNullHandle) return fail("opening slot owner is missing");
        slot = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 天正 2014 分解结果（1 个样例核对）：门窗两端外侧、所选墙面上各一个截面矩形（沿墙宽 ProfileWidth、伸出墙面 ProfileDepth）。
    // faceLeft / faceRight 为门窗方向左、右两侧墙面到门窗中线的距离
    inline TchGraphics BuildOpeningSlot(const TchOpeningSlot& s, const TchOpening& o, double faceLeft, double faceRight)
    {
        TchGraphics g;
        const double ux = std::cos(o.Angle), uy = std::sin(o.Angle), nx = -uy, ny = ux;
        auto at = [&](double u, double v) { return TchGraphics::Point{o.Position.X + ux * u + nx * v, o.Position.Y + uy * u + ny * v}; };
        for (int side = 0; side < 2; ++side)
        {
            if (!(s.Sides & (1 << side))) continue;
            const double sign = side == 0 ? 1.0 : -1.0, face = side == 0 ? faceLeft : faceRight;
            for (const double end : {1.0, -1.0})
            {
                const double u0 = end * o.Width / 2, u1 = end * (o.Width / 2 + s.ProfileWidth);
                const double v0 = sign * face, v1 = sign * (face + s.ProfileDepth);
                TchGraphics::Path p;
                p.Points = {{at(u0, v0)}, {at(u1, v0)}, {at(u1, v1)}, {at(u0, v1)}, {at(u0, v0)}};
                g.Paths.push_back(std::move(p));
            }
        }
        return g;
    }
}
