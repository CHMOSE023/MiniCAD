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

    struct TchRect
    {
        double X = 0, Y = 0;            // 中心
        double Rotation = 0;
        double Length = 0, Width = 0;
        int    Flag = 0;
    };

    namespace Detail
    {
        inline bool DecodeRectCipher(const MiniDWG::UnknownEntity& entity, TchRect& rect, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 3) return fail("unsupported native rectangle version");
            f.ReadKey();
            TchRect t;
            t.Flag = f.Short();
            const auto center = f.Point3();
            t.X = center.X; t.Y = center.Y;
            t.Rotation = f.Double();
            t.Length = f.Double();
            t.Width = f.Double();
            f.Double();
            const auto normal = f.Point3();
            f.Byte();
            if (!f.Exact()) return fail("native rectangle fields do not consume the payload exactly");
            if (std::abs(normal.X) > 1e-9 || std::abs(normal.Y) > 1e-9 || normal.Z <= 0) return fail("rectangle is not in the XY plane");
            for (double v : {t.X, t.Y, t.Rotation, t.Length, t.Width})
                if (!Finite(v)) return fail("invalid native rectangle settings");
            rect = t;
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    inline bool DecodeRect(const MiniDWG::UnknownEntity& entity, TchRect& rect, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_RECT") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeRectCipher(entity, rect, diagnostic);
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_RECT", r, base)) return fail("unsupported Tianzheng base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 2) return fail("unsupported native rectangle version");
        TchRect t;
        t.Flag = r.ReadBitShort();
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

    inline TchGraphics BuildRect(const TchRect& t)
    {
        TchGraphics g;
        const double hl = t.Length / 2, hw = t.Width / 2;
        TchGraphics::Path p;
        for (auto [x, y] : {std::pair{-hl, -hw}, {hl, -hw}, {hl, hw}, {-hl, hw}, {-hl, -hw}})
            p.Points.push_back({Detail::Local(t.X, t.Y, t.Rotation, x, y)});
        g.Paths.push_back(std::move(p));
        auto diagonal = [&](double x0, double y0, double x1, double y1) {
            TchGraphics::Path d;
            d.Points = {{Detail::Local(t.X, t.Y, t.Rotation, x0, y0)}, {Detail::Local(t.X, t.Y, t.Rotation, x1, y1)}};
            g.Paths.push_back(std::move(d));
        };
        if (t.Flag == 1 || t.Flag == 3) diagonal(-hl, -hw, hl, hw);
        if (t.Flag == 2 || t.Flag == 3) diagonal(-hl, hw, hl, -hw);
        return g;
    }
    namespace Detail
    {
        struct PathVertex { double X = 0, Y = 0, Bulge = 0; };

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

        inline std::vector<PathVertex> OffsetPathEdges(const std::vector<PathVertex>& pts, bool closed, const std::vector<double>& d)
        {
            const std::size_t n = pts.size(), segs = closed ? n : n - 1;
            std::vector<PathVertex> out;
            if (n < 2 || d.size() < segs) return out;
            auto dir = [&](std::size_t i, bool atStart) {
                const auto& a = pts[i];
                const auto& b = pts[(i + 1) % n];
                double dx = b.X - a.X, dy = b.Y - a.Y;
                const double L = std::hypot(dx, dy);
                if (L < 1e-12) return std::pair{1.0, 0.0};
                dx /= L; dy /= L;
                if (std::abs(a.Bulge) > 1e-12)
                {
                    const double t = 2 * std::atan(a.Bulge) * (atStart ? 1 : -1);
                    const double c = std::cos(t), s = std::sin(t);
                    return std::pair{dx * c - dy * s, dx * s + dy * c};
                }
                return std::pair{dx, dy};
            };
            for (std::size_t i = 0; i < n; ++i)
            {
                const bool hasPrev = closed || i > 0, hasNext = closed || i + 1 < n;
                const std::size_t prev = (i + n - 1) % n;
                auto t1 = hasPrev ? dir(prev, false) : dir(i, true);
                auto t2 = hasNext ? dir(i, true) : t1;
                const double d1 = hasPrev ? d[prev] : d[i], d2 = hasNext ? d[i] : d1;
                const double ax = pts[i].X - t1.second * d1, ay = pts[i].Y + t1.first * d1;
                const double bx = pts[i].X - t2.second * d2, by = pts[i].Y + t2.first * d2;
                const double cross = t1.first * t2.second - t1.second * t2.first;
                if (std::abs(cross) < 0.05)
                {
                    out.push_back({bx, by, pts[i].Bulge});
                    continue;
                }
                const double s = ((bx - ax) * t2.second - (by - ay) * t2.first) / cross;
                out.push_back({ax + t1.first * s, ay + t1.second * s, pts[i].Bulge});
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

        inline bool ReadPathPoly(MiniDWG::DwgBitReader& r, std::vector<PathVertex>& out, bool& closed)
        {
            closed = r.ReadByte() != 0;
            const int count = r.ReadBitShort();
            if (count < 0 || count > 100000) return false;
            for (int i = 0; i < count && !r.Failed(); ++i)
            {
                PathVertex v;
                v.X = r.ReadRawDouble(); v.Y = r.ReadRawDouble(); v.Bulge = r.ReadBitDouble();
                if (!Finite(v.X) || !Finite(v.Y) || !Finite(v.Bulge)) return false;
                out.push_back(v);
            }
            return !r.Failed();
        }
    }

    struct TchSlab
    {
        std::vector<Detail::PathVertex> Outline;
        std::vector<std::vector<Detail::PathVertex>> Holes;
    };

    namespace Detail
    {
        template <class R>
        const char* ReadSlabFields(R& r, TchSlab& s, int version)
        {
            r.D(); r.BL(); r.D();
            bool closed = false;
            if (!ReadPathPoly(r.Main(), s.Outline, closed)) return "invalid slab outline";
            const int holes = r.BS();
            if (holes < 0 || holes > 10000) return "invalid slab hole count";
            for (int i = 0; i < holes && r.Ok(); ++i)
            {
                s.Holes.emplace_back();
                if (!ReadPathPoly(r.Main(), s.Holes.back(), closed)) return "invalid slab hole";
            }
            if (version > 1) r.P3();
            return nullptr;
        }
    }

    inline bool DecodeSlab(const MiniDWG::UnknownEntity& entity, TchSlab& slab, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_SLAB") return false;
        TchSlab s;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 3) return fail("unsupported native slab version");
            f.ReadKey();
            Detail::CipherFields r{f};
            if (const char* why = Detail::ReadSlabFields(r, s, 3)) return fail(why);
            if (!f.Exact()) return fail("native slab fields do not consume the payload exactly");
        }
        else
        {
            DwgBitReader rr(data.Main, data.Version, Codec::CodePage::Gbk);
            TchBase base;
            if (!Detail::ShapeHead(entity, "TCH_SLAB", rr, base)) return fail("unsupported Tianzheng base layout");
            const int version = rr.ReadByte();
            if (version < 1 || version > 2) return fail("unsupported native slab version");
            Detail::PlainFields r{rr};
            if (const char* why = Detail::ReadSlabFields(r, s, version)) return fail(why);
            if (rr.Failed() || rr.PositionInBits() != data.MainBits) return fail("native slab fields do not consume the payload exactly");
        }
        if (s.Outline.size() < 3) return fail("slab outline is degenerate");
        slab = std::move(s);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildSlab(const TchSlab& s)
    {
        TchGraphics g;
        g.Paths.push_back(Detail::ToPath(s.Outline, true));
        for (const auto& h : s.Holes)
            if (h.size() >= 2) g.Paths.push_back(Detail::ToPath(h, true));
        return g;
    }

    struct TchHandRail
    {
        std::vector<Detail::PathVertex> Path;
        double Width = 60;
        int Align = 0;                              // 0 居中，1 路径在左侧，2 路径在右侧
        bool Draw2d = true;
    };

    namespace Detail
    {
        template <class R>
        const char* ReadHandRailFields(R& r, TchHandRail& h, int version)
        {
            if (r.RC() == 100) r.H();
            r.RC();
            const int count = r.BS();
            if (count < 2 || count > 100000) return "invalid handrail vertex count";
            for (int i = 0; i < count && r.Ok(); ++i)
            {
                PathVertex v;
                const auto p = r.P3();
                v.X = p.X; v.Y = p.Y;
                v.Bulge = r.D();
                if (!Finite(v.X) || !Finite(v.Y) || !Finite(v.Bulge)) return "invalid handrail vertex";
                h.Path.push_back(v);
            }
            h.Width = r.D();
            if (version > 1) r.D();
            r.D();
            h.Align = r.RC();
            h.Draw2d = (r.RC() & 1) != 0;
            return nullptr;
        }
    }

    inline bool DecodeHandRail(const MiniDWG::UnknownEntity& entity, TchHandRail& rail, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_HANDRAIL") return false;
        TchHandRail h;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!Detail::ReadCipherPathHead(f)) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 3) return fail("unsupported native handrail version");
            f.ReadKey();
            Detail::CipherFields r{f};
            if (const char* why = Detail::ReadHandRailFields(r, h, 3)) return fail(why);
            if (!f.Exact()) return fail("native handrail fields do not consume the payload exactly");
        }
        else
        {
            DwgBitReader rr(data.Main, data.Version, Codec::CodePage::Gbk);
            TchBase base;
            if (!Detail::ShapeHead(entity, "TCH_HANDRAIL", rr, base)) return fail("unsupported Tianzheng base layout");
            const int stairBase = rr.ReadByte();
            if (stairBase < 1 || stairBase > 2) return fail("unsupported stair base version");
            const int version = rr.ReadByte();
            if (version < 1 || version > 2) return fail("unsupported native handrail version");
            Detail::PlainFields r{rr};
            if (const char* why = Detail::ReadHandRailFields(r, h, version)) return fail(why);
            if (rr.Failed() || rr.PositionInBits() != data.MainBits) return fail("native handrail fields do not consume the payload exactly");
        }
        if (!(h.Width > 0) || !Detail::Finite(h.Width)) return fail("invalid handrail width");
        rail = std::move(h);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildHandRail(const TchHandRail& h)
    {
        TchGraphics g;
        if (!h.Draw2d) return g;
        const double l = h.Align == 1 ? 0 : h.Align == 2 ? h.Width : h.Width / 2;
        const auto left = Detail::OffsetPath(h.Path, false, l), right = Detail::OffsetPath(h.Path, false, l - h.Width);
        g.Paths.push_back(Detail::ToPath(left, false));
        g.Paths.push_back(Detail::ToPath(right, false));
        g.Lines.push_back({{left.front().X, left.front().Y}, {right.front().X, right.front().Y}});
        g.Lines.push_back({{left.back().X, left.back().Y}, {right.back().X, right.back().Y}});
        return g;
    }

    struct TchApron
    {
        std::vector<Detail::PathVertex> Path;      // 散水内侧（外墙外皮）
        bool Closed = true;
        std::vector<double> Widths;                 // 各段散水宽
    };

    namespace Detail
    {
        inline bool DecodeApronCipher(const MiniDWG::UnknownEntity& entity, TchApron& apron, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng base layout");
            std::vector<CipherPathVertex> path;
            TchApron a;
            MiniDWG::XYZ normal;
            if (!ReadCipherPolyCurve(f, path, a.Closed, normal)) return fail("unsupported path base layout");
            if (path.size() < 2) return fail("invalid apron vertex count");
            for (const auto& v : path)
            {
                if (!Finite(v.Point.X) || !Finite(v.Point.Y) || !Finite(v.Value)) return fail("invalid apron vertex");
                a.Path.push_back({v.Point.X, v.Point.Y, v.Value});
            }
            if (f.Main.ReadByte() != 2) return fail("unsupported native apron version");
            f.ReadKey();
            f.Double(); f.Double();
            const int widths = f.Short();
            for (int i = 0; i < widths && f.Ok(); ++i) a.Widths.push_back(f.Double());
            f.Long();
            if (!f.Exact()) return fail("native apron fields do not consume the payload exactly");
            for (double w : a.Widths)
                if (!Finite(w) || w < 0) return fail("invalid apron width");
            apron = std::move(a);
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    inline bool DecodeApron(const MiniDWG::UnknownEntity& entity, TchApron& apron, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_APRON") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeApronCipher(entity, apron, diagnostic);
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
    struct TchCornerWindow
    {
        double Scale = 100;
        double X = 0, Y = 0;                    // 墙内侧的转角点
        double Length1 = 0, Length2 = 0;        // 两段窗长
        double Angle1 = 0, Angle2 = 0;          // 两段自转角点出发的方向
        double Thickness = 240;                 // 第一段墙厚：向第一段方向的右侧偏移（窗线自内侧向外排 4 条）
        double Thickness2 = -240;               // 第二段墙厚：向第二段方向的右侧偏移（第二段在逆时针侧时为负）
        double LabelX = 0, LabelY = 0, LabelHeight = 3.5;
        std::string Label;
        MiniDWG::Handle Style = MiniDWG::kNullHandle;
    };

    namespace Detail
    {
        template <class R>
        const char* ReadCornerWindowFields(R& r, TchCornerWindow& w, int version)
        {
            const int flags = r.BL();
            const auto p = r.P3(); w.X = p.X; w.Y = p.Y;
            r.H(); r.H();
            w.Length1 = r.D(); w.Length2 = r.D();
            r.D();
            w.Angle1 = r.D(); w.Angle2 = r.D();
            w.Thickness = r.D();
            w.Thickness2 = r.D();
            for (int i = 0; i < 2; ++i) r.D();
            w.Label = r.T();
            const auto l = r.P2(); w.LabelX = l.X; w.LabelY = l.Y;
            w.Style = r.H(); r.H();
            r.D();
            w.LabelHeight = r.D();
            if (flags & 1) for (int i = 0; i < 4; ++i) r.D();
            r.BS();
            r.H(); r.H(); r.H();
            for (int i = 0; i < 3; ++i) r.D();
            if (flags & 4)
            {
                const int segs = r.BS();
                if (segs < 0 || segs > 10000) return "invalid corner window segment count";
                for (int i = 0; i < segs && r.Ok(); ++i) { r.P2(); r.P2(); r.D(); }
            }
            if (version > 1) { r.BS(); r.B(); r.T(); for (int i = 0; i < 4; ++i) r.D(); }
            if (version > 2) r.D();
            if (version > 3 && (flags & 1)) r.D();
            return nullptr;
        }
    }
     
    inline bool DecodeCornerWindow(const MiniDWG::UnknownEntity& entity, TchCornerWindow& window, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_CORNER_WINDOW") return false;
        TchCornerWindow w;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 5) return fail("unsupported native corner window version");
            f.ReadKey();
            w.Scale = f.Scale;
            Detail::CipherFields r{f};
            if (const char* why = Detail::ReadCornerWindowFields(r, w, 5)) return fail(why);
            if (!f.Exact()) return fail("native corner window fields do not consume the payload exactly");
        }
        else
        {
            DwgBitReader rr(data.Main, data.Version, Codec::CodePage::Gbk);
            // 编号在主数据流中的位置未核对：R2004 及更早（字符串嵌在主数据流）不支持
            if (data.Version < CadVersion::AC1021) return fail("unsupported DWG version for corner window");
            TchBase base;
            if (!Detail::ShapeHead(entity, "TCH_CORNER_WINDOW", rr, base)) return fail("unsupported Tianzheng base layout");
            const int version = rr.ReadByte();
            if (version < 1 || version > 4) return fail("unsupported native corner window version");
            w.Scale = base.Scale;
            Detail::PlainFields r{rr};
            if (const char* why = Detail::ReadCornerWindowFields(r, w, version)) return fail(why);
            if (rr.Failed() || rr.PositionInBits() != data.MainBits) return fail("native corner window fields do not consume the payload exactly");
            Detail::TextStream stream(data, rr);
            for (int i = 0; i < base.TextCount; ++i) stream.Next();
            w.Label = stream.Next();
            DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
            for (int i = 0; i < base.HandleCount + 2; ++i) handles.ReadHandle(entity.ObjectHandle);   // 两道墙
            w.Style = handles.ReadHandle(entity.ObjectHandle);
        }
        for (double v : {w.X, w.Y, w.Length1, w.Length2, w.Angle1, w.Angle2, w.Thickness, w.Thickness2, w.LabelX, w.LabelY, w.LabelHeight})
            if (!Detail::Finite(v)) return fail("invalid native corner window settings");
        window = std::move(w);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildCornerWindow(const TchCornerWindow& w)
    {
        TchGraphics g;
        const double d1x = std::cos(w.Angle1), d1y = std::sin(w.Angle1), d2x = std::cos(w.Angle2), d2y = std::sin(w.Angle2);
        const double cross = d1x * d2y - d1y * d2x;
        auto at = [&](double f, int which) -> TchGraphics::Point {
            const double a = w.Thickness * f, b = w.Thickness2 * f;
            const double ax = w.X + d1y * a, ay = w.Y - d1x * a, bx = w.X + d2y * b, by = w.Y - d2x * b;
            if (which == 1) return {ax + d1x * w.Length1, ay + d1y * w.Length1};
            if (which == 2) return {bx + d2x * w.Length2, by + d2y * w.Length2};
            if (std::abs(cross) < 1e-9) return {ax, ay};
            const double s = ((bx - ax) * d2y - (by - ay) * d2x) / cross;
            return {ax + d1x * s, ay + d1y * s};
        };
        for (int k = 0; k <= 3; ++k)
        {
            TchGraphics::Path p;
            p.Points = {{at(k / 3.0, 1)}, {at(k / 3.0, 0)}, {at(k / 3.0, 2)}};
            g.Paths.push_back(std::move(p));
        }
        g.Lines.push_back({at(0, 1), at(1, 1)});
        g.Lines.push_back({at(0, 2), at(1, 2)});
        if (!w.Label.empty())
        {
            const double nominal = w.LabelHeight * w.Scale;
            Detail::AddRuns(g, w.Label, nominal, w.LabelX, w.LabelY - 0.425 * nominal, 0, 1);
        }
        return g;
    }
    namespace Detail
    {
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

        inline bool ReadCipherFlightBase(CipherReader& f, TchFlightBase& b)
        {
            if (!f.ReadBase() || f.Main.ReadByte() != 4) return false;
            f.ReadKey();
            b.Base.Scale = f.Scale;
            b.Steps = f.Short();
            b.StepHeight = f.Double();
            b.BreakMode = f.Byte();
            b.BreakStart = f.Double();
            b.BreakEnd = f.Double();
            f.Long(); f.Double(); f.Double(); f.Double();
            f.Handle(); f.Handle();
            return f.Ok();
        }

        inline int BreakKind(int mode)
        {
            switch (mode & 15) { case 2: return 1; case 3: return 2; case 4: return 3; default: return 0; }
        }
    }

    struct TchLineStair
    {
        Detail::TchFlightBase Flight;
        double Tread = 270, Width = 1200;
        double X = 0, Y = 0, Rotation = 0;      // 起步边左端点；梯段沿局部 +Y 展开
    };

    namespace Detail
    {
        inline const char* CheckLineStair(const TchLineStair& s)
        {
            for (double v : {s.Tread, s.Width, s.X, s.Y, s.Rotation, s.Flight.BreakStart, s.Flight.BreakEnd})
                if (!Finite(v)) return "invalid native line stair settings";
            if (s.Flight.Steps < 2 || s.Flight.Steps > 1000 || !(s.Tread > 0) || !(s.Width > 0)) return "invalid native line stair size";
            return nullptr;
        }

        // 新版 TCH_LINESTAIR 版本 3
        inline bool DecodeLineStairCipher(const MiniDWG::UnknownEntity& entity, TchLineStair& stair, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            TchLineStair s;
            if (!ReadCipherFlightBase(f, s.Flight)) return fail("unsupported stair flight base layout");
            if (f.Main.ReadByte() != 3) return fail("unsupported native line stair version");
            f.ReadKey();
            s.Tread = f.Double(); s.Width = f.Double();
            const auto p = f.Point3();
            s.X = p.X; s.Y = p.Y;
            s.Rotation = f.Double();
            f.Double();
            if (!f.Exact()) return fail("native line stair fields do not consume the payload exactly");
            if (const char* why = CheckLineStair(s)) return fail(why);
            stair = s;
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    inline bool DecodeLineStair(const MiniDWG::UnknownEntity& entity, TchLineStair& stair, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_LINESTAIR") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeLineStairCipher(entity, stair, diagnostic);
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
        if (const char* why = Detail::CheckLineStair(s)) return fail(why);
        stair = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }

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

    struct TchArcStair
    {
        Detail::TchFlightBase Flight;
        double X = 0, Y = 0;                    // 圆心
        double InnerRadius = 800, OuterRadius = 2000;
        double StartAngle = 0, Sweep = std::numbers::pi;
    };

    namespace Detail
    {
        inline const char* CheckArcStair(const TchArcStair& s)
        {
            for (double v : {s.X, s.Y, s.InnerRadius, s.OuterRadius, s.StartAngle, s.Sweep})
                if (!Finite(v)) return "invalid native arc stair settings";
            if (s.Flight.Steps < 2 || s.Flight.Steps > 1000 || !(s.InnerRadius >= 0) || !(s.OuterRadius > s.InnerRadius) || s.Sweep == 0)
                return "invalid native arc stair size";
            return nullptr;
        }

        // 新版 TCH_ARCSTAIR 版本 3
        inline bool DecodeArcStairCipher(const MiniDWG::UnknownEntity& entity, TchArcStair& stair, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            TchArcStair s;
            if (!ReadCipherFlightBase(f, s.Flight)) return fail("unsupported stair flight base layout");
            if (f.Main.ReadByte() != 3) return fail("unsupported native arc stair version");
            f.ReadKey();
            const auto c = f.Point3();
            s.X = c.X; s.Y = c.Y;
            s.InnerRadius = f.Double(); s.OuterRadius = f.Double();
            s.StartAngle = f.Double(); s.Sweep = f.Double();
            for (int k = 0; k < 2; ++k)
            {
                f.Main.ReadByte();
                const int n = f.Main.ReadBitShort();
                if (n < 0 || n > 100000) return fail("invalid arc stair outline");
                for (int i = 0; i < n && f.Ok(); ++i) { f.Main.ReadRawDouble(); f.Main.ReadRawDouble(); f.Main.ReadBitDouble(); }
            }
            if (!f.Exact()) return fail("native arc stair fields do not consume the payload exactly");
            if (const char* why = CheckArcStair(s)) return fail(why);
            stair = s;
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    inline bool DecodeArcStair(const MiniDWG::UnknownEntity& entity, TchArcStair& stair, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_ARCSTAIR") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeArcStairCipher(entity, stair, diagnostic);
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
        if (const char* why = Detail::CheckArcStair(s)) return fail(why);
        stair = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }

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

    struct TchAscent
    {
        double X = 0, Y = 0, Rotation = 0;      // 坡顶边中点；坡道沿局部 −Y 方向
        bool   Slant = true;                    // 示坡线（横线）
        double Length = 2100, Width = 3000, Height = 450, Indent = 300;
        double Scale = 100;
    };

    namespace Detail
    {
        template <class R>
        void ReadAscentFields(R& r, TchAscent& a)
        {
            const auto p = r.P3(); a.X = p.X; a.Y = p.Y;
            a.Rotation = r.D();
            a.Slant = r.RC() != 0;
            a.Length = r.D(); a.Width = r.D(); a.Height = r.D(); a.Indent = r.D();
        }
    }

  
    inline bool DecodeAscent(const MiniDWG::UnknownEntity& entity, TchAscent& ascent, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_ASCENT") return false;
        TchAscent a;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 2) return fail("unsupported native ascent version");
            f.ReadKey();
            a.Scale = f.Scale;
            Detail::CipherFields r{f};
            Detail::ReadAscentFields(r, a);
            if (!f.Exact()) return fail("native ascent fields do not consume the payload exactly");
        }
        else
        {
            DwgBitReader rr(data.Main, data.Version, Codec::CodePage::Gbk);
            TchBase base;
            if (!Detail::ShapeHead(entity, "TCH_ASCENT", rr, base)) return fail("unsupported Tianzheng base layout");
            if (rr.ReadByte() != 1) return fail("unsupported native ascent version");
            a.Scale = base.Scale;
            Detail::PlainFields r{rr};
            Detail::ReadAscentFields(r, a);
            if (rr.Failed() || rr.PositionInBits() != data.MainBits) return fail("native ascent fields do not consume the payload exactly");
        }
        for (double v : {a.X, a.Y, a.Rotation, a.Length, a.Width, a.Indent})
            if (!Detail::Finite(v)) return fail("invalid native ascent settings");
        if (!(a.Length > 0) || !(a.Width > 0) || !(a.Scale > 0)) return fail("invalid native ascent size");
        ascent = a;
        if (diagnostic) diagnostic->clear();
        return true;
    }

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

    struct TchBalcony
    {
        std::vector<Detail::PathVertex> Outline;    // 阳台外缘（开口两端在墙上）
        bool Closed = false;
        double RailWidth = 100;                     // 栏板宽
        std::uint32_t NoRailEdges = 0;              // 位 i：第 i 段没有栏板（靠墙）
    };

    namespace Detail
    {
        inline bool DecodeBalconyCipher(const MiniDWG::UnknownEntity& entity, TchBalcony& balcony, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            if (!ReadCipherPathHead(f)) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 5) return fail("unsupported native balcony version");
            f.ReadKey();
            TchBalcony b;
            b.NoRailEdges = static_cast<std::uint32_t>(f.Long());
            f.Double();
            if (!ReadPathPoly(f.Main, b.Outline, b.Closed) || b.Outline.size() < 2) return fail("invalid balcony vertex count");
            b.RailWidth = f.Double();
            f.Double(); f.Double(); f.Double();
            f.Handle();
            f.Long();
            f.Handle(); f.Handle();
            f.Double(); f.Double(); f.Double();
            f.Bool();
            for (int k = 0; k < 2; ++k)
            {
                const int n = f.Long();
                if (n < 0 || n > 100000) return fail("invalid balcony handle count");
                for (int i = 0; i < n && f.Ok(); ++i) f.Handle();
                if (k == 0) f.Handle();
            }
            if (!f.Exact()) return fail("native balcony fields do not consume the payload exactly");
            if (!Finite(b.RailWidth) || b.RailWidth < 0) return fail("invalid balcony rail width");
            balcony = std::move(b);
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    inline bool DecodeBalcony(const MiniDWG::UnknownEntity& entity, TchBalcony& balcony, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_BALCONY") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeBalconyCipher(entity, balcony, diagnostic);
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!Detail::ShapeHead(entity, "TCH_BALCONY", r, base)) return fail("unsupported Tianzheng base layout");
        if (r.ReadByte() != 2) return fail("unsupported native balcony version");
        r.ReadByte();
        TchBalcony b;
        b.NoRailEdges = static_cast<std::uint32_t>(r.ReadBitLong());
        r.ReadBitDouble();
        if (!Detail::ReadPathPoly(r, b.Outline, b.Closed) || b.Outline.size() < 2) return fail("invalid balcony vertex count");
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

    inline TchGraphics BuildBalcony(const TchBalcony& b)
    {
        TchGraphics g;
        const std::size_t n = b.Outline.size(), segs = b.Closed ? n : n - 1;
        auto rail = [&](std::size_t i) { return i >= 32 || (b.NoRailEdges >> i & 1) == 0; };
        const double inward = Detail::SignedArea(b.Outline) > 0 ? 1.0 : -1.0;     // 逆时针时内侧在左
        std::vector<double> d(segs);
        std::size_t start = 0;
        bool all = true;
        for (std::size_t i = 0; i < segs; ++i)
        {
            d[i] = rail(i) ? inward * b.RailWidth : 0;
            if (!rail(i) && all) { all = false; start = b.Closed ? i + 1 : 0; }
        }
        if (all && b.Closed)
        {
            g.Paths.push_back(Detail::ToPath(b.Outline, true));
            if (b.RailWidth > 0) g.Paths.push_back(Detail::ToPath(Detail::OffsetPath(b.Outline, true, inward * b.RailWidth), true));
            return g;
        }
        const auto inner = Detail::OffsetPathEdges(b.Outline, b.Closed, d);
        std::vector<std::size_t> run;
        auto flush = [&] {
            if (run.size() >= 2)
                for (const auto* pts : {&b.Outline, &inner})
                {
                    if (pts == &inner && !(b.RailWidth > 0)) continue;
                    TchGraphics::Path p;
                    for (std::size_t k = 0; k < run.size(); ++k)
                        p.Points.push_back({{(*pts)[run[k]].X, (*pts)[run[k]].Y}, k + 1 < run.size() ? (*pts)[run[k]].Bulge : 0});
                    g.Paths.push_back(std::move(p));
                }
            run.clear();
        };
        for (std::size_t k = 0; k < segs; ++k)
        {
            const std::size_t i = (start + k) % n;
            if (!rail(i)) { flush(); continue; }
            if (run.empty()) run.push_back(i);
            run.push_back((i + 1) % n);
        }
        flush();
        return g;
    }

    struct TchCurveStair
    {
        Detail::TchFlightBase Flight;
        struct Edge { double X1 = 0, Y1 = 0, X2 = 0, Y2 = 0, Bulge = 0; };
        Edge Left, Right;                       // 两侧边（起步端 → 末端）
    };

    namespace Detail
    {
        inline const char* CheckCurveStair(const TchCurveStair& s)
        {
            for (const auto* e : {&s.Left, &s.Right})
                for (double v : {e->X1, e->Y1, e->X2, e->Y2, e->Bulge})
                    if (!Finite(v)) return "invalid native curve stair edge";
            if (s.Flight.Steps < 2 || s.Flight.Steps > 1000) return "invalid native curve stair steps";
            return nullptr;
        }

        inline bool DecodeCurveStairCipher(const MiniDWG::UnknownEntity& entity, TchCurveStair& stair, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            TchCurveStair s;
            if (!ReadCipherFlightBase(f, s.Flight)) return fail("unsupported stair flight base layout");
            if (f.Main.ReadByte() != 2) return fail("unsupported native curve stair version");
            f.ReadKey();
            f.Double();
            for (auto* e : {&s.Left, &s.Right})
            {
                const auto a = f.Point2(), b = f.Point2();
                e->X1 = a.X; e->Y1 = a.Y; e->X2 = b.X; e->Y2 = b.Y;
                e->Bulge = f.Double();
            }
            if (!f.Exact()) return fail("native curve stair fields do not consume the payload exactly");
            if (const char* why = CheckCurveStair(s)) return fail(why);
            stair = s;
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    inline bool DecodeCurveStair(const MiniDWG::UnknownEntity& entity, TchCurveStair& stair, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_CURVESTAIR") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeCurveStairCipher(entity, stair, diagnostic);
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
        if (const char* why = Detail::CheckCurveStair(s)) return fail(why);
        stair = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    namespace Detail
    {
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

    inline TchGraphics BuildCurveStair(const TchCurveStair& s)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
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

    // ── TCH_STEP 台阶（版本 1 为明文分支）──
    struct TchStep
    {
        std::vector<Detail::PathVertex> Outline;    // 平台轮廓（踏步自轮廓边向外排）
        bool     Closed = true;
        unsigned NoStepMask = 0;                    // 第 i 位：第 i 条边（点 i → 点 i+1）没有踏步
        int      Lines = 3;                         // 踏步线条数（含平台边）
        double   Tread = 300, Riser = 150;
    };

    namespace Detail
    {
        // 新版 TCH_STEP 版本 2 与明文版本 1 字段相同（轮廓为明文）
        template <class R>
        const char* ReadStepFields(R& r, TchStep& s)
        {
            s.NoStepMask = static_cast<unsigned>(r.BL());
            r.D();
            if (!ReadPathPoly(r.Main(), s.Outline, s.Closed) || s.Outline.size() < 2 || s.Outline.size() > 10000) return "invalid step outline";
            s.Lines = r.BS();
            s.Tread = r.D();
            s.Riser = r.D();
            return nullptr;
        }
    }

    inline bool DecodeStep(const MiniDWG::UnknownEntity& entity, TchStep& step, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_STEP") return false;
        TchStep s;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 2) return fail("unsupported native step version");
            f.ReadKey();
            Detail::CipherFields r{f};
            if (const char* why = Detail::ReadStepFields(r, s)) return fail(why);
            if (!f.Exact()) return fail("native step fields do not consume the payload exactly");
        }
        else
        {
            DwgBitReader rr(data.Main, data.Version, Codec::CodePage::Gbk);
            TchBase base;
            if (!Detail::ShapeHead(entity, "TCH_STEP", rr, base)) return fail("unsupported Tianzheng base layout");
            if (rr.ReadByte() != 1) return fail("unsupported native step version");
            Detail::PlainFields r{rr};
            if (const char* why = Detail::ReadStepFields(r, s)) return fail(why);
            if (rr.Failed() || rr.PositionInBits() != data.MainBits) return fail("native step fields do not consume the payload exactly");
        }
        if (s.Lines < 1 || s.Lines > 1000 || !Detail::Finite(s.Tread) || s.Tread <= 0) return fail("invalid native step settings");
        step = std::move(s);
        if (diagnostic) diagnostic->clear();
        return true;
    }
     
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

    // ── TCH_WALL_PATCH 墙体造型（版本 ≤ 4）──
    struct TchWallPatch
    {
        std::vector<Detail::PathVertex> Outline;
        bool Closed = true;
        std::vector<MiniDWG::Handle> Walls;         // 所附着的墙
    };

    namespace Detail
    {
        // 新版 TCH_WALL_PATCH 版本 1-4 不加密，字段与明文相同
        inline const char* ReadWallPatchFields(MiniDWG::DwgBitReader& r, TchWallPatch& w, int version, int& walls)
        {
            const int segs = r.ReadBitShort();
            if (segs < 0 || segs > 10000) return "invalid wall patch segment count";
            for (int i = 0; i < segs && !r.Failed(); ++i) for (int k = 0; k < 7; ++k) r.ReadBitDouble();
            r.ReadBitDouble();
            if (!ReadPathPoly(r, w.Outline, w.Closed) || w.Outline.size() < 2 || w.Outline.size() > 10000) return "invalid wall patch outline";
            r.ReadBitDouble();
            walls = r.ReadByte();
            r.ReadByte();
            if (version > 2) { r.ReadByte(); r.ReadByte(); }
            if (version > 3) { r.ReadBitLong(); r.ReadBitDouble(); }
            return nullptr;
        }
    }
     
    inline bool DecodeWallPatch(const MiniDWG::UnknownEntity& entity, TchWallPatch& patch, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_WALL_PATCH") return false;
        TchWallPatch w;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng base layout");
            const int version = f.Main.ReadByte();
            if (version < 1 || version > 4) return fail("unsupported native wall patch version");
            int walls = 0;
            if (const char* why = Detail::ReadWallPatchFields(f.Main, w, version, walls)) return fail(why);
            if (!f.Exact()) return fail("native wall patch fields do not consume the payload exactly");
            for (int i = 0; i < walls && f.Ok(); ++i) w.Walls.push_back(f.Handle());
        }
        else
        {
            DwgBitReader rr(data.Main, data.Version, Codec::CodePage::Gbk);
            TchBase base;
            if (!Detail::ShapeHead(entity, "TCH_WALL_PATCH", rr, base)) return fail("unsupported Tianzheng base layout");
            const int version = rr.ReadByte();
            if (version < 1 || version > 4) return fail("unsupported native wall patch version");
            int walls = 0;
            if (const char* why = Detail::ReadWallPatchFields(rr, w, version, walls)) return fail(why);
            if (rr.Failed() || rr.PositionInBits() != data.MainBits) return fail("native wall patch fields do not consume the payload exactly");
            DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
            for (int i = 0; i < base.HandleCount; ++i) handles.ReadHandle(entity.ObjectHandle);
            for (int i = 0; i < walls && !handles.Failed(); ++i) w.Walls.push_back(handles.ReadHandle(entity.ObjectHandle));
        }
        patch = std::move(w);
        if (diagnostic) diagnostic->clear();
        return true;
    }
     
    inline TchGraphics BuildWallPatch(const TchWallPatch& w, const std::vector<std::array<TchGraphics::Point, 4>>& wallBodies)
    {
        using P = TchGraphics::Point;
        TchGraphics g; 
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
     
    struct TchAutoStair
    {
        double X = 0, Y = 0, Rotation = 0;      // 上端外角点；扶梯沿局部 −Y 方向（向下端）展开
        double Tread = 300, StepWidth = 800;    // 踏步宽、梯级宽
        double Landing = 2200;                  // 两端平台长
        double CombOffset = 900;                // 梳齿板线距踏步起点
        double Rise = 3000, AngleDeg = 30;      // 提升高度、倾角
        double BreakStart = 0.4, BreakEnd = 0.6;
        int    Floor = 1;                       // StairFlag：0 首层、1 中间层、2 顶层
        std::uint32_t Flags = 0x400;            // 位 10：画箭头
        double TextHeight = 3.5, Scale = 100;
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle, ArrowLayer = MiniDWG::kNullHandle;
    };
 
    namespace Detail
    {
        template <class R>
        void ReadAutoStairFields(R& r, TchAutoStair& s)
        {
            const auto p = r.P3(); s.X = p.X; s.Y = p.Y;
            s.Rotation = r.D();
            r.D();
            s.Tread = r.D();
            s.StepWidth = r.D();
            s.Landing = r.D();
            r.D(); r.D();
            s.Floor = r.RC();
            r.D();
            s.BreakStart = r.D();
            s.BreakEnd = r.D();
            s.CombOffset = r.D();
            s.TextHeight = r.D();
            s.Rise = r.D();
            s.AngleDeg = r.D();
            r.D(); r.D();
            r.RC();
            s.Flags = static_cast<std::uint32_t>(r.BL());
            r.D();
            r.H(); r.H();
            s.TextStyle = r.H();
            s.ArrowLayer = r.H();
            r.RC();
        }
    }

    inline bool DecodeAutoStair(const MiniDWG::UnknownEntity& entity, TchAutoStair& stair, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_AUTOSTAIR") return false;
        TchAutoStair s;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!Detail::ReadCipherPathHead(f)) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 1) return fail("unsupported native escalator version");
            f.ReadKey();
            s.Scale = f.Scale;
            Detail::CipherFields r{f};
            Detail::ReadAutoStairFields(r, s);
            if (!f.Exact()) return fail("native escalator fields do not consume the payload exactly");
        }
        else
        {
            DwgBitReader rr(data.Main, data.Version, Codec::CodePage::Gbk);
            TchBase base;
            if (!Detail::ShapeHead(entity, "TCH_AUTOSTAIR", rr, base)) return fail("unsupported Tianzheng base layout");
            const int stairBase = rr.ReadByte();
            if (stairBase < 1 || stairBase > 2) return fail("unsupported stair base version");
            if (rr.ReadByte() != 0) return fail("unsupported native escalator version");
            s.Scale = base.Scale;
            Detail::PlainFields r{rr};
            Detail::ReadAutoStairFields(r, s);
            if (rr.Failed() || rr.PositionInBits() != data.MainBits) return fail("native escalator fields do not consume the payload exactly");
            DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
            for (int i = 0; i < base.HandleCount + 1 + 2; ++i) handles.ReadHandle(entity.ObjectHandle);
            s.TextStyle = handles.ReadHandle(entity.ObjectHandle);
            s.ArrowLayer = handles.ReadHandle(entity.ObjectHandle);
        }
        for (double v : {s.X, s.Y, s.Rotation, s.Tread, s.StepWidth, s.Landing, s.CombOffset, s.Rise, s.AngleDeg, s.TextHeight})
            if (!Detail::Finite(v)) return fail("invalid native escalator settings");
        if (s.Tread <= 0 || s.StepWidth <= 0 || s.Landing < 0 || s.Rise <= 0 || s.AngleDeg <= 1 || s.AngleDeg >= 89)
            return fail("invalid native escalator settings");
        stair = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }
     
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
        if (!(s.Flags & 0x400)) return g;
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

        inline bool ReadCipherRoofBase(CipherReader& f)
        {
            CipherLayer layer(f);
            if (layer.Version(3, 2) < 2) return false;
            for (int i = 0; i < 4; ++i) f.Handle();
            f.Double(); f.Double(); f.Double();
            f.Byte();
            const int polys = f.Long();
            if (polys < 0 || polys > 10000) return false;
            for (int i = 0; i < polys && f.Ok(); ++i)
            {
                f.Main.ReadByte();
                const int n = f.Main.ReadBitShort();
                if (n < 0 || n > 100000) return false;
                for (int j = 0; j < n && f.Ok(); ++j) { f.Main.ReadRawDouble(); f.Main.ReadRawDouble(); f.Main.ReadBitDouble(); }
            }
            const int handles = f.Long();
            if (handles < 0 || handles > 100000) return false;
            for (int i = 0; i < handles && f.Ok(); ++i) f.Handle();
            return f.Ok();
        }

        inline bool ReadRoofBase(PlainFields& r) { return ReadRoofBase(r.R); }
        inline bool ReadRoofBase(CipherFields& r) { return ReadCipherRoofBase(r.F); }

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

    struct TchMountRoof
    {
        double X = 0, Y = 0, Rotation = 0;     // 墙外皮矩形的第一个角点；长度方向为转角方向
        double Length = 6000, Width = 4000;
        double EndSlope = std::numbers::pi / 4; // 两端坡角（属性 SlopeAng）
        double Eaves = 600;                    // 长边方向出檐
        double RoofHeight = 3000, MountHeight = 1100;
    };

    namespace Detail
    {
        template <class R>
        void ReadMountRoofFields(R& r, TchMountRoof& m)
        {
            const auto p = r.P3(); m.X = p.X; m.Y = p.Y;
            m.Rotation = r.D();
            m.Length = r.D();
            m.Width = r.D();
            m.EndSlope = r.D();
            m.Eaves = r.D();
            r.D();
            m.RoofHeight = r.D();
            m.MountHeight = r.D();
            r.B();
        }
    }

    inline bool DecodeMountRoof(const MiniDWG::UnknownEntity& entity, TchMountRoof& roof, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_MOUNTROOF") return false;
        TchMountRoof m;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!Detail::ReadCipherPathHead(f)) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 2) return fail("unsupported native mount roof version");
            if (!Detail::ReadCipherRoofBase(f)) return fail("unsupported Tianzheng roof base layout");
            f.ReadKey();
            Detail::CipherFields r{f};
            Detail::ReadMountRoofFields(r, m);
            if (!f.Exact()) return fail("native mount roof fields do not consume the payload exactly");
        }
        else
        {
            DwgBitReader rr(data.Main, data.Version, Codec::CodePage::Gbk);
            int version = 0;
            if (!Detail::RoofHead(entity, "TCH_MOUNTROOF", rr, version)) return fail("unsupported Tianzheng roof base layout");
            if (version > 2) return fail("unsupported native mount roof version");
            if (version > 1 && rr.ReadByte() >= 16) return fail("encrypted native mount roof is not supported");
            Detail::PlainFields r{rr};
            Detail::ReadMountRoofFields(r, m);
            if (rr.Failed() || rr.PositionInBits() != data.MainBits) return fail("native mount roof fields do not consume the payload exactly");
        }
        for (double v : {m.X, m.Y, m.Rotation, m.Length, m.Width, m.EndSlope, m.Eaves, m.RoofHeight, m.MountHeight})
            if (!Detail::Finite(v)) return fail("invalid native mount roof settings");
        if (m.Length <= 0 || m.Width <= 0 || m.RoofHeight <= 0 || m.EndSlope <= 0.01 || m.EndSlope >= std::numbers::pi / 2 - 0.01)
            return fail("invalid native mount roof settings");
        roof = m;
        if (diagnostic) diagnostic->clear();
        return true;
    }
 
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

    struct TchCuspRoof
    {
        double X = 0, Y = 0, Rotation = 0;     // 中心、第一个顶点的方向
        double Height = 3000, Radius = 2000, Eaves = 600;
        int    Sides = 6;
    };

    namespace Detail
    {
        template <class R>
        void ReadCuspRoofFields(R& r, TchCuspRoof& c, int version)
        {
            const auto p = r.P3(); c.X = p.X; c.Y = p.Y;
            c.Rotation = r.D();
            c.Height = r.D();
            c.Radius = r.D();
            c.Sides = r.RC();
            c.Eaves = version > 1 ? r.D() : 0.0;
        }
    }

    inline bool DecodeCuspRoof(const MiniDWG::UnknownEntity& entity, TchCuspRoof& roof, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_CUSPROOF") return false;
        TchCuspRoof c;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!Detail::ReadCipherPathHead(f)) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 3) return fail("unsupported native cusp roof version");
            if (!Detail::ReadCipherRoofBase(f)) return fail("unsupported Tianzheng roof base layout");
            f.ReadKey();
            Detail::CipherFields r{f};
            Detail::ReadCuspRoofFields(r, c, 3);
            if (!f.Exact()) return fail("native cusp roof fields do not consume the payload exactly");
        }
        else
        {
            DwgBitReader rr(data.Main, data.Version, Codec::CodePage::Gbk);
            int version = 0;
            if (!Detail::RoofHead(entity, "TCH_CUSPROOF", rr, version)) return fail("unsupported Tianzheng roof base layout");
            if (version > 3) return fail("unsupported native cusp roof version");
            if (version > 2 && rr.ReadByte() >= 16) return fail("encrypted native cusp roof is not supported");
            Detail::PlainFields r{rr};
            Detail::ReadCuspRoofFields(r, c, version);
            if (rr.Failed() || rr.PositionInBits() != data.MainBits) return fail("native cusp roof fields do not consume the payload exactly");
        }
        for (double v : {c.X, c.Y, c.Rotation, c.Height, c.Radius, c.Eaves})
            if (!Detail::Finite(v)) return fail("invalid native cusp roof settings");
        if (c.Sides < 3 || c.Sides > 64 || c.Radius <= 0) return fail("invalid native cusp roof settings");
        roof = c;
        if (diagnostic) diagnostic->clear();
        return true;
    }
     
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

    struct TchSlopeRoof
    {
        struct Edge { double X = 0, Y = 0, Slope = 0; };    // 轮廓顶点（边的起点）与该边的坡角
        std::vector<Edge> Edges;
    };

    namespace Detail
    {
        template <class R>
        const char* ReadSlopeRoofFields(R& r, TchSlopeRoof& s, int version)
        {
            r.D();
            const int count = r.BS();
            if (count < 3 || count > 10000) return "invalid slope roof edge count";
            for (int i = 0; i < count && r.Ok(); ++i)
            {
                TchSlopeRoof::Edge e;
                const auto p = r.P3(); e.X = p.X; e.Y = p.Y;
                e.Slope = r.D();
                if (!Finite(e.X) || !Finite(e.Y) || !Finite(e.Slope)) return "invalid slope roof edge";
                s.Edges.push_back(e);
            }
            if (version > 1)
            {
                if (!ReadRoofBase(r)) return "unsupported Tianzheng roof base layout";
                r.D();
            }
            return nullptr;
        }
    }

    inline bool DecodeSlopeRoof(const MiniDWG::UnknownEntity& entity, TchSlopeRoof& roof, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_SLOPEROOF") return false;
        TchSlopeRoof s;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!Detail::ReadCipherPathHead(f)) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 3) return fail("unsupported native slope roof version");
            f.ReadKey();
            Detail::CipherFields r{f};
            if (const char* why = Detail::ReadSlopeRoofFields(r, s, 3)) return fail(why);
            if (!f.Exact()) return fail("native slope roof fields do not consume the payload exactly");
        }
        else
        {
            DwgBitReader rr(data.Main, data.Version, Codec::CodePage::Gbk);
            TchBase base;
            if (!Detail::ShapeHead(entity, "TCH_SLOPEROOF", rr, base)) return fail("unsupported Tianzheng base layout");
            const int pathVersion = rr.ReadByte();
            if (pathVersion > 2) return fail("unsupported Tianzheng path base layout");
            const int version = rr.ReadByte();
            if (version > 2) return fail("unsupported native slope roof version");
            Detail::PlainFields r{rr};
            if (const char* why = Detail::ReadSlopeRoofFields(r, s, version)) return fail(why);
            if (rr.Failed() || rr.PositionInBits() != data.MainBits) return fail("native slope roof fields do not consume the payload exactly");
        }
        roof = std::move(s);
        if (diagnostic) diagnostic->clear();
        return true;
    }
     
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
        struct Plane { double A, B, C; };
        std::vector<Plane> planes(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            const P& a = poly[i]; const P& b = poly[(i + 1) % n];
            const double dx = b.X - a.X, dy = b.Y - a.Y, len = std::max(std::hypot(dx, dy), 1e-12);
            const double inx = -dy / len * orient, iny = dx / len * orient, t = std::tan(s.Edges[i].Slope);
            planes[i] = {inx * t, iny * t, -(inx * a.X + iny * a.Y) * t};
        }
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

    struct TchDualRoof
    {
        std::vector<Detail::PathVertex> Outline;
        double X1 = 0, Y1 = 0, X2 = 0, Y2 = 0;            // 屋脊两端
    };

    namespace Detail
    {
        template <class R>
        const char* ReadDualRoofFields(R& r, TchDualRoof& d, int version)
        {
            r.D();
            auto& m = r.Main();
            m.ReadByte();
            const int count = m.ReadBitShort();
            if (count < 2 || count > 100000) return "invalid dual-slope roof outline";
            for (int i = 0; i < count && !m.Failed(); ++i)
            {
                PathVertex v;
                v.X = m.ReadRawDouble(); v.Y = m.ReadRawDouble(); v.Bulge = m.ReadBitDouble();
                d.Outline.push_back(v);
            }
            const auto a = r.P2(), b = r.P2();
            d.X1 = a.X; d.Y1 = a.Y; d.X2 = b.X; d.Y2 = b.Y;
            r.D(); r.D();
            r.B();
            if (version > 1) { r.B(); r.D(); }
            return nullptr;
        }
    }
     
    inline bool DecodeDualRoof(const MiniDWG::UnknownEntity& entity, TchDualRoof& roof, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_DUALROOF") return false;
        TchDualRoof d;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!Detail::ReadCipherPathHead(f)) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 3) return fail("unsupported native dual-slope roof version");
            if (!Detail::ReadCipherRoofBase(f)) return fail("unsupported Tianzheng roof base layout");
            f.ReadKey();
            Detail::CipherFields r{f};
            if (const char* why = Detail::ReadDualRoofFields(r, d, 3)) return fail(why);
            if (!f.Exact()) return fail("native dual-slope roof fields do not consume the payload exactly");
        }
        else
        {
            DwgBitReader rr(data.Main, data.Version, Codec::CodePage::Gbk);
            int version = 0;
            if (!Detail::RoofHead(entity, "TCH_DUALROOF", rr, version)) return fail("unsupported Tianzheng roof base layout");
            if (version > 2) return fail("unsupported native dual-slope roof version");
            Detail::PlainFields r{rr};
            if (const char* why = Detail::ReadDualRoofFields(r, d, version)) return fail(why);
            if (rr.Failed() || rr.PositionInBits() != data.MainBits) return fail("native dual-slope roof fields do not consume the payload exactly");
        }
        for (const auto& v : d.Outline)
            if (!Detail::Finite(v.X) || !Detail::Finite(v.Y) || !Detail::Finite(v.Bulge)) return fail("invalid dual-slope roof outline");
        for (double v : {d.X1, d.Y1, d.X2, d.Y2})
            if (!Detail::Finite(v)) return fail("invalid dual-slope roof ridge");
        roof = std::move(d);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildDualRoof(const TchDualRoof& d)
    {
        TchGraphics g;
        g.Paths.push_back(Detail::ToPath(d.Outline, true));
        g.Lines.push_back({{d.X1, d.Y1}, {d.X2, d.Y2}});
        return g;
    }

    // ── TCH_OPENINGSLOT 门窗套（版本 < 3 为明文分支）──
    struct TchOpeningSlot
    {
        MiniDWG::Handle Opening = MiniDWG::kNullHandle;   // 所属门窗
        int    Sides = 1;                                  // 位 0：门窗方向左侧墙面，位 1：右侧（样例为 1，即室内一侧在左）
        double ProfileWidth = 60, ProfileDepth = 15;       // 门窗套截面：沿墙方向宽、伸出墙面厚
    };

    namespace Detail
    {
        template <class R>
        const char* ReadOpeningSlotFields(R& r, TchOpeningSlot& s, int version)
        {
            r.BS();
            s.Opening = r.H();
            r.B();
            s.Sides = r.BS();
            r.BS(); r.BS();
            r.BS();
            const int count = r.BS();
            if (count < 0 || count > 10000) return "invalid opening slot profile";
            double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
            for (int i = 0; i < count && r.Ok(); ++i)
            {
                const auto p = r.P3();
                r.D();
                if (i == 0) { x0 = x1 = p.X; y0 = y1 = p.Y; }
                x0 = std::min(x0, p.X); x1 = std::max(x1, p.X); y0 = std::min(y0, p.Y); y1 = std::max(y1, p.Y);
            }
            for (int i = 0; i < 11; ++i) r.D();
            r.BS();
            if (version > 1) { r.H(); r.H(); }
            s.ProfileWidth = x1 - x0;
            s.ProfileDepth = y1 - y0;
            return nullptr;
        }
    }

    inline bool DecodeOpeningSlot(const MiniDWG::UnknownEntity& entity, TchOpeningSlot& slot, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_OPENINGSLOT") return false;
        TchOpeningSlot s;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 3) return fail("unsupported native opening slot version");
            f.ReadKey();
            Detail::CipherFields r{f};
            if (const char* why = Detail::ReadOpeningSlotFields(r, s, 3)) return fail(why);
            if (!f.Exact()) return fail("native opening slot fields do not consume the payload exactly");
        }
        else
        {
            DwgBitReader rr(data.Main, data.Version, Codec::CodePage::Gbk);
            TchBase base;
            if (!Detail::ShapeHead(entity, "TCH_OPENINGSLOT", rr, base)) return fail("unsupported Tianzheng base layout");
            const int version = rr.ReadByte();
            if (version > 2) return fail("unsupported native opening slot version");
            Detail::PlainFields r{rr};
            if (const char* why = Detail::ReadOpeningSlotFields(r, s, version)) return fail(why);
            if (rr.Failed() || rr.PositionInBits() != data.MainBits) return fail("native opening slot fields do not consume the payload exactly");
            DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
            for (int i = 0; i < base.HandleCount; ++i) handles.ReadHandle(entity.ObjectHandle);
            s.Opening = handles.ReadHandle(entity.ObjectHandle);
            if (handles.Failed()) return fail("opening slot owner is missing");
        }
        if (!Detail::Finite(s.ProfileWidth) || !Detail::Finite(s.ProfileDepth)) return fail("invalid opening slot profile");
        if (s.Opening == MiniDWG::kNullHandle) return fail("opening slot owner is missing");
        slot = s;
        if (diagnostic) diagnostic->clear();
        return true;
    }

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
