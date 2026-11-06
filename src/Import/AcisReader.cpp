#include "Import/AcisReader.h"
#include "Core/Math/Mat4.hpp"
#include "Core/Math/MathUtils.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

// 格式要点见 docs/Solid/PLAN.md 第 16 节
namespace MiniCAD::Import
{
    namespace
    {
        using Math::Point3;
        using Math::Vec3;
        using Solid::NurbsCurve;

        enum class Kind : std::uint8_t { Ptr, Int, Num, Word, Str, Bool, Enum, Open, Close };

        struct Tok
        {
            Kind        K = Kind::Int;
            double      N = 0.0;
            long long   I = 0;
            std::string S;
        };

        struct Rec
        {
            std::string      Type;
            std::vector<Tok> T;
        };

        struct File
        {
            int                                  Version = 0;
            double                               Res     = 1e-6;
            std::vector<Rec>                     Recs;
            std::vector<std::pair<int, size_t>>  Subtypes;   // ref N → (记录, '{' 的下标)
        };

        bool IsEndMarker(const std::string& s) { return s.rfind("End-of-", 0) == 0; }

        // ── SAB ───────────────────────────────────────────────────────────
        bool ReadSab(const std::uint8_t* d, size_t n, File& f)
        {
            if (n < 31)
                return false;
            std::int32_t hdr[4];
            std::memcpy(hdr, d + 15, sizeof hdr);
            f.Version = hdr[0];
            size_t p = 31;
            auto take = [&](void* v, size_t k) {
                if (p + k > n)
                    return false;
                std::memcpy(v, d + p, k);
                p += k;
                return true;
            };
            auto takeStr = [&](size_t len, std::string& s) {
                if (p + len > n)
                    return false;
                s.assign(reinterpret_cast<const char*>(d + p), len);
                p += len;
                return true;
            };

            Rec cur;
            std::string prefix;
            std::vector<double> headerNums;
            bool first = true;
            while (p < n)
            {
                const std::uint8_t t = d[p++];
                Tok tok;
                bool push = true;
                switch (t)
                {
                case 0x04: { std::int32_t v; if (!take(&v, 4)) return false; tok.K = Kind::Int; tok.I = v; break; }
                case 0x15: { std::int32_t v; if (!take(&v, 4)) return false; tok.K = Kind::Enum; tok.I = v; break; }
                case 0x16: { std::int64_t v; if (!take(&v, 8)) return false; tok.K = Kind::Int; tok.I = v; break; }
                case 0x06: case 0x17: { double v; if (!take(&v, 8)) return false; tok.K = Kind::Num; tok.N = v; break; }
                case 0x07: case 0x0D: case 0x0E:
                {
                    if (p >= n || !takeStr(d[p++], tok.S))
                        return false;
                    tok.K = t == 0x07 ? Kind::Str : Kind::Word;
                    if (t == 0x0E)
                    {
                        prefix += tok.S + "-";
                        push = false;
                    }
                    else if (t == 0x0D)
                    {
                        tok.S = prefix + tok.S;
                        prefix.clear();
                    }
                    break;
                }
                case 0x08: { std::uint16_t len; if (!take(&len, 2) || !takeStr(len, tok.S)) return false; tok.K = Kind::Str; break; }
                case 0x12: { std::uint32_t len; if (!take(&len, 4) || !takeStr(len, tok.S)) return false; tok.K = Kind::Str; break; }
                case 0x0A: tok.K = Kind::Bool; tok.I = 1; break;
                case 0x0B: tok.K = Kind::Bool; tok.I = 0; break;
                case 0x0C: { std::int32_t v; if (!take(&v, 4)) return false; tok.K = Kind::Ptr; tok.I = v; break; }
                case 0x0F: tok.K = Kind::Open; break;
                case 0x10: tok.K = Kind::Close; break;
                case 0x13: case 0x14:
                {
                    double v[3];
                    if (!take(v, sizeof v))
                        return false;
                    for (double x : v)
                    {
                        Tok c;
                        c.K = Kind::Num;
                        c.N = x;
                        if (cur.Type.empty())
                            headerNums.push_back(x);
                        else
                            cur.T.push_back(c);
                    }
                    push = false;
                    break;
                }
                case 0x11:
                    if (!cur.Type.empty())
                        f.Recs.push_back(std::move(cur));
                    cur   = Rec{};
                    first = false;
                    push  = false;
                    break;
                default:
                    return !f.Recs.empty();
                }
                if (!push)
                    continue;
                if (cur.Type.empty())
                {
                    if (tok.K == Kind::Word)
                    {
                        if (IsEndMarker(tok.S))
                            break;
                        cur.Type = tok.S;
                        if (first && headerNums.size() >= 2)
                            f.Res = headerNums[1];
                    }
                    else if (tok.K == Kind::Num && first)
                        headerNums.push_back(tok.N);
                    continue;
                }
                cur.T.push_back(std::move(tok));
            }
            return !f.Recs.empty();
        }

        // ── SAT ───────────────────────────────────────────────────────────
        bool ReadSat(const char* s, size_t n, File& f)
        {
            size_t p = 0;
            f.Version = std::atoi(std::string(s, std::min<size_t>(n, 16)).c_str());
            for (int line = 0; line < 3 && p < n; ++line)
            {
                const size_t start = p;
                while (p < n && s[p] != '\n')
                    ++p;
                if (line == 2)
                {
                    const std::string l(s + start, p - start);
                    char* end = nullptr;
                    std::strtod(l.c_str(), &end);
                    const double res = std::strtod(end, nullptr);
                    if (res > 0.0)
                        f.Res = res;
                }
                ++p;
            }

            Rec cur;
            bool expectSeq = true;
            while (p < n)
            {
                while (p < n && static_cast<unsigned char>(s[p]) <= ' ')
                    ++p;
                if (p >= n)
                    break;
                size_t e = p;
                while (e < n && static_cast<unsigned char>(s[e]) > ' ')
                    ++e;
                std::string w(s + p, e - p);
                p = e;
                bool endRec = false;
                if (w.size() > 1 && w.back() == '#')
                {
                    w.pop_back();
                    endRec = true;
                }
                if (w == "#")
                {
                    if (!cur.Type.empty())
                        f.Recs.push_back(std::move(cur));
                    cur       = Rec{};
                    expectSeq = true;
                    continue;
                }

                Tok tok;
                if (w[0] == '@' && w.size() > 1 && std::isdigit(static_cast<unsigned char>(w[1])))
                {
                    const size_t len = std::strtoul(w.c_str() + 1, nullptr, 10);
                    if (p < n)
                        ++p;
                    const size_t take = std::min(len, n - p);
                    tok.K = Kind::Str;
                    tok.S.assign(s + p, take);
                    p += take;
                }
                else if (w[0] == '$')
                {
                    tok.K = Kind::Ptr;
                    tok.I = std::strtoll(w.c_str() + 1, nullptr, 10);
                }
                else if (w == "{")
                    tok.K = Kind::Open;
                else if (w == "}")
                    tok.K = Kind::Close;
                else
                {
                    char* end = nullptr;
                    const double v = std::strtod(w.c_str(), &end);
                    const bool numeric = end == w.c_str() + w.size()
                        && (std::isdigit(static_cast<unsigned char>(w[0])) || w[0] == '-' || w[0] == '+' || w[0] == '.');
                    if (numeric && w.find_first_of(".eE") == std::string::npos)
                    {
                        tok.K = Kind::Int;
                        tok.I = std::strtoll(w.c_str(), nullptr, 10);
                        tok.N = v;
                    }
                    else if (numeric)
                    {
                        tok.K = Kind::Num;
                        tok.N = v;
                    }
                    else
                    {
                        tok.K = Kind::Word;
                        tok.S = std::move(w);
                    }
                }

                if (cur.Type.empty())
                {
                    if (tok.K == Kind::Word)
                    {
                        if (IsEndMarker(tok.S))
                            break;
                        cur.Type = tok.S;
                    }
                    else if (!(expectSeq && tok.K == Kind::Int))
                        cur.Type = "?";
                    expectSeq = false;
                }
                else
                    cur.T.push_back(std::move(tok));
                if (endRec)
                {
                    f.Recs.push_back(std::move(cur));
                    cur       = Rec{};
                    expectSeq = true;
                }
            }
            return !f.Recs.empty();
        }

        // ── 记录读取 ─────────────────────────────────────────────────────
        struct Cur
        {
            const Rec* R  = nullptr;
            size_t     P  = 0;
            bool       Ok = true;

            const Tok* Peek() const { return R && P < R->T.size() ? &R->T[P] : nullptr; }
            bool Is(Kind k) const { const Tok* t = Peek(); return t && t->K == k; }
            bool IsNum() const { const Tok* t = Peek(); return t && (t->K == Kind::Num || t->K == Kind::Int); }
            bool IsWord(const char* w) const { const Tok* t = Peek(); return t && t->K == Kind::Word && t->S == w; }

            double Num()
            {
                const Tok* t = Peek();
                if (!t || (t->K != Kind::Num && t->K != Kind::Int && t->K != Kind::Enum))
                {
                    Ok = false;
                    return 0.0;
                }
                ++P;
                return t->K == Kind::Num ? t->N : static_cast<double>(t->I);
            }
            long long Int()
            {
                const Tok* t = Peek();
                if (!t || (t->K != Kind::Int && t->K != Kind::Enum && t->K != Kind::Num))
                {
                    Ok = false;
                    return 0;
                }
                ++P;
                return t->K == Kind::Num ? std::llround(t->N) : t->I;
            }
            int Ptr()
            {
                while (Is(Kind::Int) || Is(Kind::Enum))
                    ++P;
                if (!Is(Kind::Ptr))
                {
                    Ok = false;
                    return -1;
                }
                return static_cast<int>(R->T[P++].I);
            }
            Point3 Pt() { const double x = Num(), y = Num(), z = Num(); return { x, y, z }; }
            Vec3   V()  { const double x = Num(), y = Num(), z = Num(); return { x, y, z }; }
            bool Reversed()
            {
                const Tok* t = Peek();
                if (!t)
                {
                    Ok = false;
                    return false;
                }
                ++P;
                if (t->K == Kind::Bool || t->K == Kind::Enum)
                    return t->I != 0;
                return t->K == Kind::Word && t->S == "reversed";
            }
            std::optional<double> Bound()
            {
                const Tok* t = Peek();
                if (!t)
                    return std::nullopt;
                if (t->K == Kind::Word && t->S == "I")
                {
                    ++P;
                    return std::nullopt;
                }
                if (t->K == Kind::Bool && t->I == 0)
                {
                    ++P;
                    return std::nullopt;
                }
                if ((t->K == Kind::Word && (t->S == "F" || t->S == "T")) || t->K == Kind::Bool)
                    ++P;
                if (!IsNum())
                    return std::nullopt;
                return Num();
            }
            void Skip(size_t k = 1) { P = std::min(P + k, R ? R->T.size() : 0); }
        };

        // ── NURBS 工具 ────────────────────────────────────────────────────
        NurbsCurve LineCurve(const Point3& a, const Point3& b)
        {
            NurbsCurve c;
            c.Degree        = 1;
            c.ControlPoints = { a, b };
            c.Knots         = { 0.0, 0.0, 1.0, 1.0 };
            return c;
        }

        // 椭圆弧 center + major·cos t + minor·sin t，t ∈ [a, b]：单位圆弧（每段不超过 90°）的仿射像
        NurbsCurve EllipseArc(const Point3& center, const Vec3& major, const Vec3& minor, double a, double b)
        {
            NurbsCurve c;
            c.Degree = 2;
            const int    pieces = std::max(1, static_cast<int>(std::ceil((b - a) / (std::numbers::pi / 2) - 1e-9)));
            const double step   = (b - a) / pieces;
            auto map = [&](double x, double y) { return center + major * x + minor * y; };
            c.ControlPoints.push_back(map(std::cos(a), std::sin(a)));
            c.Weights.push_back(1.0);
            c.Knots = { a, a, a };
            for (int k = 0; k < pieces; ++k)
            {
                const double t0 = a + step * k, t1 = k == pieces - 1 ? b : t0 + step, tm = (t0 + t1) / 2, h = std::cos(step / 2);
                c.ControlPoints.push_back(map(std::cos(tm) / h, std::sin(tm) / h));
                c.Weights.push_back(h);
                c.ControlPoints.push_back(map(std::cos(t1), std::sin(t1)));
                c.Weights.push_back(1.0);
                c.Knots.push_back(t1);
                c.Knots.push_back(t1);
            }
            c.Knots.push_back(b);
            return c;
        }

        // ── 拓扑与几何 ────────────────────────────────────────────────────
        class Builder
        {
        public:
            Builder(const File& f, AcisResult& out) : m_f(f), m_out(out) {}

            void Run()
            {
                for (int i = 0; i < static_cast<int>(m_f.Recs.size()); ++i)
                    if (m_f.Recs[i].Type == "body")
                        Body(i);
            }

        private:
            struct EdgeGeom
            {
                bool         Degenerate = false;
                bool         Ok         = false;
                NurbsCurve   Curve;      // 方向 v0 → v1
                Point3       A, B;
            };

            const Rec* R(int i) const { return i >= 0 && i < static_cast<int>(m_f.Recs.size()) ? &m_f.Recs[i] : nullptr; }
            bool TypeIs(int i, const char* t) const { const Rec* r = R(i); return r && r->Type == t; }
            static bool EndsWith(const std::string& s, const char* suffix)
            {
                const size_t n = std::strlen(suffix);
                return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
            }

            Cur Open(int i) const
            {
                Cur c{ R(i) };
                if (!c.R)
                {
                    c.Ok = false;
                    return c;
                }
                if (c.Is(Kind::Ptr))
                    c.Skip();
                if (m_f.Version >= 700)
                {
                    if (c.Is(Kind::Int))
                        c.Skip();
                    if (c.Is(Kind::Ptr))
                        c.Skip();
                }
                return c;
            }

            std::optional<Point3> VertexPoint(int v) const
            {
                if (!TypeIs(v, "vertex"))
                    return std::nullopt;
                Cur c = Open(v);
                c.Ptr();
                const int pt = c.Ptr();
                if (!c.Ok || !TypeIs(pt, "point"))
                    return std::nullopt;
                Cur q = Open(pt);
                const Point3 p = q.Pt();
                return q.Ok ? std::optional<Point3>(p) : std::nullopt;
            }

            void Body(int b)
            {
                Cur c = Open(b);
                const int lump = c.Ptr();
                c.Ptr();
                const int xf = c.Ptr();
                const size_t faceStart = m_out.Brep.Faces.size(), wireStart = m_out.Wire.size();
                std::unordered_set<int> seen;
                for (int l = lump; TypeIs(l, "lump") && seen.insert(l).second;)
                {
                    Cur lc = Open(l);
                    const int next = lc.Ptr(), shell = lc.Ptr();
                    for (int s = shell; TypeIs(s, "shell") && seen.insert(s).second;)
                    {
                        Cur sc = Open(s);
                        const int sNext = sc.Ptr();
                        sc.Ptr();
                        const int face = sc.Ptr();
                        for (int fi = face; TypeIs(fi, "face") && seen.insert(fi).second;)
                            fi = Face(fi);
                        s = sNext;
                    }
                    l = next;
                }
                if (TypeIs(xf, "transform"))
                    ApplyTransform(xf, faceStart, wireStart);
            }

            void ApplyTransform(int xf, size_t faceStart, size_t wireStart)
            {
                Cur c = Open(xf);
                const Vec3 r0 = c.V(), r1 = c.V(), r2 = c.V(), t = c.V();
                const double s = c.IsNum() ? c.Num() : 1.0;
                if (!c.Ok)
                    return;
                Math::Mat4 m = Math::Mat4::Identity();
                const Vec3 rows[3] = { r0 * s, r1 * s, r2 * s };
                for (int i = 0; i < 3; ++i)
                {
                    m.m[i * 4 + 0] = rows[i].x;
                    m.m[i * 4 + 1] = rows[i].y;
                    m.m[i * 4 + 2] = rows[i].z;
                }
                m.m[12] = t.x;
                m.m[13] = t.y;
                m.m[14] = t.z;
                const Math::Mat4 id = Math::Mat4::Identity();
                bool identity = true;
                for (int i = 0; i < 16; ++i)
                    identity = identity && std::abs(m.m[i] - id.m[i]) < 1e-15;
                if (identity)
                    return;
                const double det = Math::Dot(rows[0], Math::Cross(rows[1], rows[2]));
                const double scale = std::cbrt(std::abs(det));
                for (size_t i = faceStart; i < m_out.Brep.Faces.size(); ++i)
                {
                    auto& f = m_out.Brep.Faces[i];
                    auto& sf = f.Surface;
                    sf.Origin = m.TransformPoint(sf.Origin);
                    sf.Normal = m.TransformVector(sf.Normal).Normalized();
                    sf.XAxis  = m.TransformVector(sf.XAxis).Normalized();
                    sf.Radius *= scale;
                    sf.MinorRadius *= scale;
                    for (auto& q : sf.Nurbs.ControlPoints)
                        q = m.TransformPoint(q);
                    if (sf.Type == Solid::BrepSurfaceType::Nurbs && det < 0)
                        f.Reversed = !f.Reversed;
                    for (auto& loop : f.Loops)
                        for (auto& curve : loop)
                            curve.Transform(m);
                    for (auto& q : f.PointLoops)
                        q = m.TransformPoint(q);
                }
                for (size_t i = wireStart; i < m_out.Wire.size(); ++i)
                    for (auto& q : m_out.Wire[i])
                        q = m.TransformPoint(q);
            }

            // 返回下一个面
            int Face(int fi)
            {
                Cur c = Open(fi);
                const int next = c.Ptr(), loop = c.Ptr();
                c.Ptr();
                c.Ptr();
                const int surface = c.Ptr();
                const bool reversed = c.Reversed();
                if (!c.Ok)
                    return -1;

                Solid::BrepFace face;
                bool surfReversed = false;
                bool ok = Surface(surface, face.Surface, surfReversed);
                face.Reversed = reversed != surfReversed;

                std::unordered_set<int> seen;
                for (int l = loop; TypeIs(l, "loop") && seen.insert(l).second;)
                {
                    Cur lc = Open(l);
                    const int lNext = lc.Ptr(), first = lc.Ptr();
                    std::vector<NurbsCurve> curves;
                    std::optional<Point3> apex;
                    int guard = 0;
                    for (int ce = first; TypeIs(ce, "coedge") && guard++ < 100000;)
                    {
                        Cur cc = Open(ce);
                        const int ceNext = cc.Ptr();
                        cc.Ptr();
                        cc.Ptr();
                        const int edge = cc.Ptr();
                        const bool ceRev = cc.Reversed();
                        const EdgeGeom& g = Edge(edge);
                        if (g.Degenerate)
                            apex = g.A;
                        else if (!g.Ok)
                            ok = false;
                        else
                            curves.push_back(ceRev ? g.Curve.Reversed() : g.Curve);
                        if (ceNext == first || ceNext < 0)
                            break;
                        ce = ceNext;
                    }
                    if (!curves.empty())
                        face.Loops.push_back(std::move(curves));
                    else if (apex)
                        face.PointLoops.push_back(*apex);
                    l = lNext;
                }
                if (ok)
                    m_out.Brep.Faces.push_back(std::move(face));
                else
                    ++m_out.FailedFaces;
                return next;
            }

            bool Surface(int si, Solid::BrepSurface& s, bool& reversed)
            {
                const Rec* r = R(si);
                if (!r)
                    return false;
                Cur c = Open(si);
                using T = Solid::BrepSurfaceType;
                if (r->Type == "plane-surface")
                {
                    s.Type   = T::Plane;
                    s.Origin = c.Pt();
                    s.Normal = c.V().Normalized();
                    s.XAxis  = c.V().Normalized();
                    if (s.XAxis.LengthSq() < 0.5 || std::abs(Math::Dot(s.XAxis, s.Normal)) > 1e-6)
                    {
                        const Vec3 a = std::abs(s.Normal.x) < 0.9 ? Vec3{ 1, 0, 0 } : Vec3{ 0, 1, 0 };
                        s.XAxis = Math::Cross(Math::Cross(s.Normal, a), s.Normal).Normalized();
                    }
                    return c.Ok && s.Normal.LengthSq() > 0.5;
                }
                if (r->Type == "cone-surface")
                {
                    s.Type   = T::Cone;
                    s.Origin = c.Pt();
                    s.Normal = c.V().Normalized();
                    const Vec3 major = c.V();
                    const double ratio = c.Num();
                    c.Bound();
                    c.Bound();
                    s.SinHalfAngle = c.Num();
                    s.CosHalfAngle = c.Num();
                    s.XAxis  = major.Normalized();
                    s.Radius = major.Length();
                    if (s.CosHalfAngle < 0)
                    {
                        reversed = true;
                        s.SinHalfAngle = -s.SinHalfAngle;
                        s.CosHalfAngle = -s.CosHalfAngle;
                    }
                    return c.Ok && std::abs(std::abs(ratio) - 1.0) < 1e-9 && s.Radius > 0 && s.CosHalfAngle > 1e-12;
                }
                if (r->Type == "sphere-surface")
                {
                    s.Type   = T::Sphere;
                    s.Origin = c.Pt();
                    s.Radius = c.Num();
                    s.XAxis  = c.V().Normalized();
                    s.Normal = c.V().Normalized();
                    if (s.Radius < 0)
                    {
                        reversed = true;
                        s.Radius = -s.Radius;
                    }
                    return c.Ok && s.Radius > 0;
                }
                if (r->Type == "torus-surface")
                {
                    s.Type        = T::Torus;
                    s.Origin      = c.Pt();
                    s.Normal      = c.V().Normalized();
                    s.Radius      = c.Num();
                    s.MinorRadius = c.Num();
                    s.XAxis       = c.V().Normalized();
                    if (s.MinorRadius < 0)
                    {
                        reversed = true;
                        s.MinorRadius = -s.MinorRadius;
                    }
                    return c.Ok && s.Radius > 0 && s.MinorRadius > 0;
                }
                if (r->Type == "spline-surface")
                {
                    s.Type   = T::Nurbs;
                    reversed = c.Reversed();
                    if (!c.Is(Kind::Open))
                        return false;
                    Cur at;
                    if (FindBs3(*r, c.P, at, 0) && Bs3Surface(at, s.Nurbs))
                        return true;
                    return SweptSurface(*r, c.P, s.Nurbs);
                }
                return false;
            }

            bool Resolve(const Rec*& r, size_t& open, int depth = 0) const
            {
                const size_t i = open + 1;
                if (i + 1 < r->T.size() && r->T[i].K == Kind::Word && r->T[i].S == "ref")
                {
                    const long long n = r->T[i + 1].K == Kind::Num ? std::llround(r->T[i + 1].N) : r->T[i + 1].I;
                    if (n < 0 || n >= static_cast<long long>(m_f.Subtypes.size()) || depth > 16)
                        return false;
                    r    = &m_f.Recs[m_f.Subtypes[static_cast<size_t>(n)].first];
                    open = m_f.Subtypes[static_cast<size_t>(n)].second;
                    return Resolve(r, open, depth + 1);
                }
                return true;
            }

            static void SkipBraces(Cur& c)
            {
                int nest = 0;
                while (const Tok* t = c.Peek())
                {
                    c.Skip();
                    if (t->K == Kind::Open)
                        ++nest;
                    else if (t->K == Kind::Close && --nest <= 0)
                        return;
                }
            }

            // 子类型里内嵌的 "intcurve sense { … }"：取其 bs3，方向按 sense 调整
            bool NestedCurve(Cur& c, NurbsCurve& out) const
            {
                if (!c.IsWord("intcurve"))
                    return false;
                c.Skip();
                const bool rev = c.Reversed();
                if (!c.Is(Kind::Open))
                    return false;
                Cur at;
                const bool ok = FindBs3(*c.R, c.P, at, 0) && Bs3Curve(at, out);
                SkipBraces(c);
                c.Bound();
                c.Bound();
                if (ok && rev)
                    out = out.Reversed();
                return ok;
            }

            // 没有近似 bs3 的旋转面 / 拉伸面（旧版本）：由轮廓线自己构造
            bool SweptSurface(const Rec& rec, size_t open, Solid::NurbsSurface& out) const
            {
                const Rec* r = &rec;
                if (!Resolve(r, open))
                    return false;
                Cur c{ r, open + 1 };
                const Tok* name = c.Peek();
                if (!name || name->K != Kind::Word)
                    return false;
                const std::string kind = name->S;
                c.Skip();
                while (c.Is(Kind::Int) || c.Is(Kind::Enum))
                    c.Skip();
                NurbsCurve prof;
                if (!NestedCurve(c, prof))
                    return false;
                std::vector<Point3> pts = prof.ControlPoints;
                std::vector<double> w   = prof.Weights;
                if (w.empty())
                    w.assign(pts.size(), 1.0);
                out = Solid::NurbsSurface{};
                out.DegreeU = prof.Degree;
                out.CountU  = static_cast<int>(pts.size());
                out.KnotsU  = prof.Knots;

                if (kind == "rotsur" || kind == "rot_spl_sur")
                {
                    const Point3 root = c.Pt();
                    const Vec3   dir  = c.V().Normalized();
                    std::optional<double> b[4];
                    for (auto& x : b)
                        x = c.Bound();
                    if (!c.Ok || dir.LengthSq() < 0.5)
                        return false;
                    const double a0 = b[2].value_or(0.0), a1 = b[3].value_or(a0 + 2 * std::numbers::pi);
                    if (a1 - a0 <= 1e-9)
                        return false;
                    const int    pieces = std::max(1, static_cast<int>(std::ceil((a1 - a0) / (std::numbers::pi / 2) - 1e-9)));
                    const double step   = (a1 - a0) / pieces, h = std::cos(step / 2);
                    out.DegreeV = 2;
                    out.CountV  = 2 * pieces + 1;
                    out.KnotsV  = { a0, a0, a0 };
                    for (int k = 1; k <= pieces; ++k)
                    {
                        const double t = k == pieces ? a1 : a0 + step * k;
                        out.KnotsV.insert(out.KnotsV.end(), k == pieces ? 3 : 2, t);
                    }
                    for (size_t i = 0; i < pts.size(); ++i)
                    {
                        const Point3 o = root + dir * Math::Dot(pts[i] - root, dir);
                        const Vec3   x = pts[i] - o, y = Math::Cross(dir, x);
                        for (int j = 0; j < out.CountV; ++j)
                        {
                            const double t  = a0 + step * j / 2;
                            const bool   mid = j % 2 == 1;
                            const double s  = mid ? 1.0 / h : 1.0;
                            out.ControlPoints.push_back(o + x * (std::cos(t) * s) + y * (std::sin(t) * s));
                            out.Weights.push_back(w[i] * (mid ? h : 1.0));
                        }
                    }
                    return out.IsValid();
                }
                if ((kind == "sumsur" || kind == "sum_spl_sur") && c.IsWord("straight"))
                {
                    c.Skip();
                    const Point3 root = c.Pt();
                    const Vec3   dir  = c.V();
                    c.Bound();
                    c.Bound();
                    const Point3 base = c.Pt();
                    std::optional<double> b[4];
                    for (auto& x : b)
                        x = c.Bound();
                    if (!c.Ok || !b[2] || !b[3] || *b[3] - *b[2] <= 1e-12)
                        return false;
                    out.DegreeV = 1;
                    out.CountV  = 2;
                    out.KnotsV  = { *b[2], *b[2], *b[3], *b[3] };
                    for (size_t i = 0; i < pts.size(); ++i)
                        for (double v : { *b[2], *b[3] })
                        {
                            out.ControlPoints.push_back(pts[i] + (root - base) + dir * v);
                            out.Weights.push_back(w[i]);
                        }
                    if (prof.Weights.empty())
                        out.Weights.clear();
                    return out.IsValid();
                }
                return false;
            }

            // 子类型里第一个顶层的 bs3（nubs / nurbs）；ref N 指向文件中第 N 个子类型
            bool FindBs3(const Rec& r, size_t open, Cur& at, int depth) const
            {
                size_t i = open + 1;
                if (i < r.T.size() && r.T[i].K == Kind::Word && r.T[i].S == "ref")
                {
                    if (i + 1 >= r.T.size() || depth > 16)
                        return false;
                    const long long n = r.T[i + 1].K == Kind::Num ? std::llround(r.T[i + 1].N) : r.T[i + 1].I;
                    if (n < 0 || n >= static_cast<long long>(m_f.Subtypes.size()))
                        return false;
                    const auto& [ri, pos] = m_f.Subtypes[static_cast<size_t>(n)];
                    return FindBs3(m_f.Recs[ri], pos, at, depth + 1);
                }
                int nest = 0;
                for (; i < r.T.size(); ++i)
                {
                    const Tok& t = r.T[i];
                    if (t.K == Kind::Open)
                        ++nest;
                    else if (t.K == Kind::Close)
                    {
                        if (nest == 0)
                            return false;
                        --nest;
                    }
                    else if (nest == 0 && t.K == Kind::Word && (t.S == "nubs" || t.S == "nurbs"))
                    {
                        at = Cur{ &r, i };
                        return true;
                    }
                }
                return false;
            }

            static bool Knots(Cur& c, int count, std::vector<double>& full)
            {
                for (int i = 0; i < count && c.Ok; ++i)
                {
                    const double v = c.Num();
                    const long long m = c.Int();
                    if (m < 1 || m > 64)
                        return false;
                    const long long reps = m + ((i == 0 || i == count - 1) ? 1 : 0);
                    full.insert(full.end(), static_cast<size_t>(reps), v);
                }
                return c.Ok;
            }

            static bool Bs3Curve(Cur c, NurbsCurve& out)
            {
                const bool rational = c.IsWord("nurbs");
                c.Skip();
                out = NurbsCurve{};
                out.Degree = static_cast<int>(c.Int());
                c.Skip();
                const int nk = static_cast<int>(c.Int());
                if (!c.Ok || out.Degree < 1 || out.Degree > 25 || nk < 2 || !Knots(c, nk, out.Knots))
                    return false;
                const int ncp = static_cast<int>(out.Knots.size()) - out.Degree - 1;
                if (ncp < out.Degree + 1)
                    return false;
                for (int i = 0; i < ncp && c.Ok; ++i)
                {
                    out.ControlPoints.push_back(c.Pt());
                    if (rational)
                        out.Weights.push_back(c.Num());
                }
                return c.Ok && out.IsValid();
            }

            static bool Bs3Surface(Cur c, Solid::NurbsSurface& out)
            {
                const bool rational = c.IsWord("nurbs");
                c.Skip();
                out = Solid::NurbsSurface{};
                out.DegreeU = static_cast<int>(c.Int());
                out.DegreeV = static_cast<int>(c.Int());
                if (rational && (c.IsWord("both") || c.IsWord("u") || c.IsWord("v")))
                    c.Skip();
                c.Skip(4);
                const int nu = static_cast<int>(c.Int()), nv = static_cast<int>(c.Int());
                if (!c.Ok || out.DegreeU < 1 || out.DegreeV < 1 || out.DegreeU > 25 || out.DegreeV > 25 || nu < 2 || nv < 2
                    || !Knots(c, nu, out.KnotsU) || !Knots(c, nv, out.KnotsV))
                    return false;
                out.CountU = static_cast<int>(out.KnotsU.size()) - out.DegreeU - 1;
                out.CountV = static_cast<int>(out.KnotsV.size()) - out.DegreeV - 1;
                if (out.CountU <= out.DegreeU || out.CountV <= out.DegreeV)
                    return false;
                const size_t total = static_cast<size_t>(out.CountU) * out.CountV;
                out.ControlPoints.resize(total);
                if (rational)
                    out.Weights.resize(total);
                for (int j = 0; j < out.CountV && c.Ok; ++j)
                    for (int i = 0; i < out.CountU && c.Ok; ++i)
                    {
                        const size_t k = static_cast<size_t>(i) * out.CountV + j;
                        out.ControlPoints[k] = c.Pt();
                        if (rational)
                            out.Weights[k] = c.Num();
                    }
                return c.Ok && out.IsValid();
            }

            const EdgeGeom& Edge(int ei)
            {
                auto [it, inserted] = m_edges.try_emplace(ei);
                EdgeGeom& g = it->second;
                if (!inserted)
                    return g;
                Cur c = Open(ei);
                const int v0 = c.Ptr();
                const std::optional<double> t0 = c.IsNum() ? std::optional<double>(c.Num()) : std::nullopt;
                const int v1 = c.Ptr();
                const std::optional<double> t1 = c.IsNum() ? std::optional<double>(c.Num()) : std::nullopt;
                c.Ptr();
                const int curve = c.Ptr();
                const bool forward = !c.Reversed();
                const auto pa = VertexPoint(v0), pb = VertexPoint(v1);
                if (!c.Ok || !pa || !pb)
                    return g;
                g.A = *pa;
                g.B = *pb;
                if (curve < 0)
                {
                    g.Degenerate = true;
                    return g;
                }
                std::optional<std::pair<double, double>> range;
                if (t0 && t1)
                    range = forward ? std::make_pair(*t0, *t1) : std::make_pair(-*t1, -*t0);
                g.Ok = Curve(curve, g, forward, range);
                const bool closed = (g.A - g.B).LengthSq() <= 1e-18;
                if (g.Ok)
                {
                    const auto pts = g.Curve.Sample(g.Curve.Degree == 1 ? 1 : 12);
                    if (pts.size() >= 2)
                        m_out.Wire.push_back(pts);
                }
                else if (!closed)
                    m_out.Wire.push_back({ g.A, g.B });
                return g;
            }

            // 曲线在 [range]（曲线自身的参数）上的一段，方向 g.A → g.B；没有参数时由端点投影求
            bool Curve(int ci, EdgeGeom& g, bool forward, std::optional<std::pair<double, double>> range)
            {
                const Rec* r = R(ci);
                if (!r)
                    return false;
                Cur c = Open(ci);
                const Point3 start = forward ? g.A : g.B, end = forward ? g.B : g.A;
                const bool closed = (g.A - g.B).LengthSq() <= 1e-18;
                if (r->Type == "straight-curve")
                {
                    if (closed)
                        return false;
                    g.Curve = LineCurve(g.A, g.B);
                    return true;
                }
                if (r->Type == "ellipse-curve")
                {
                    const Point3 center = c.Pt();
                    const Vec3   normal = c.V().Normalized();
                    const Vec3   major  = c.V();
                    const double ratio  = c.Num();
                    const Vec3   minor  = Math::Cross(normal, major) * ratio;
                    if (!c.Ok || major.LengthSq() <= 0 || minor.LengthSq() <= 0)
                        return false;
                    double a, b;
                    if (range)
                        std::tie(a, b) = *range;
                    else
                    {
                        auto angle = [&](const Point3& p) {
                            const Vec3 d = p - center;
                            return std::atan2(Math::Dot(d, minor) / minor.LengthSq(), Math::Dot(d, major) / major.LengthSq());
                        };
                        a = angle(start);
                        b = angle(end);
                        if (b <= a + 1e-12)
                            b += 2 * std::numbers::pi;
                    }
                    if (b - a <= 1e-12)
                        return false;
                    g.Curve = EllipseArc(center, major, minor, a, b);
                    if (!forward)
                        g.Curve = g.Curve.Reversed();
                    return true;
                }
                if (r->Type == "intcurve-curve")
                {
                    const bool icRev = c.Reversed();
                    Cur at;
                    NurbsCurve bs;
                    if (!c.Is(Kind::Open) || !FindBs3(*r, c.P, at, 0) || !Bs3Curve(at, bs))
                        return false;
                    const bool same = forward != icRev;
                    double lo, hi;
                    if (range)
                    {
                        lo = icRev ? -range->second : range->first;
                        hi = icRev ? -range->first : range->second;
                    }
                    else if (closed)
                    {
                        lo = bs.StartParam();
                        hi = bs.EndParam();
                    }
                    else
                    {
                        lo = bs.ClosestParam(same ? g.A : g.B);
                        hi = bs.ClosestParam(same ? g.B : g.A);
                    }
                    NurbsCurve piece = bs.Sub(lo, hi);
                    if (!piece.IsValid())
                        return false;
                    g.Curve = same ? std::move(piece) : piece.Reversed();
                    return true;
                }
                return false;
            }

            const File&                       m_f;
            AcisResult&                       m_out;
            std::unordered_map<int, EdgeGeom> m_edges;
        };
    }

    bool ReadAcis(const std::uint8_t* data, std::size_t size, AcisResult& out, std::string* error)
    {
        out = AcisResult{};
        File f;
        const bool binary = size >= 15
            && (std::memcmp(data, "ACIS BinaryFile", 15) == 0 || std::memcmp(data, "ASM BinaryFile", 14) == 0);
        const bool parsed = binary ? ReadSab(data, size, f) : ReadSat(reinterpret_cast<const char*>(data), size, f);
        if (!parsed)
        {
            if (error)
                *error = "no ACIS records";
            return false;
        }
        for (int ri = 0; ri < static_cast<int>(f.Recs.size()); ++ri)
        {
            const auto& t = f.Recs[ri].T;
            for (size_t i = 0; i + 1 < t.size(); ++i)
                if (t[i].K == Kind::Open && t[i + 1].K == Kind::Word && t[i + 1].S != "ref")
                    f.Subtypes.emplace_back(ri, i);
        }
        out.Version    = f.Version;
        out.Resolution = f.Res > 0 ? f.Res : 1e-6;
        Builder(f, out).Run();
        if (out.Brep.Faces.empty() && out.Wire.empty())
        {
            if (error)
                *error = out.FailedFaces > 0 ? "no supported faces" : "no body";
            return false;
        }
        return true;
    }
}
