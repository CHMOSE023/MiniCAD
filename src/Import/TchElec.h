#pragma once
#include "Import/TchDimension.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <string>
#include <vector>
namespace MiniCAD::Tch
{
    namespace Detail
    { 
        inline bool ReadXDataMap(MiniDWG::DwgBitReader& r, TextStream& text, TchXDataMap& map, int& handles)
        {
            int guard = 0;
            for (std::string s = text.Next(); s != "TDBXDATAMAP_BLOCK_BEGIN_"; s = text.Next())
                if (++guard > 64 || r.Failed() || text.Reader.Failed()) return false;
            for (std::string key = text.Next(); key != "TDBXDATAMAP_BLOCK_END_"; key = text.Next())
            {
                if (++guard > 4096 || r.Failed() || text.Reader.Failed()) return false;
                TchXDataMap::Value v;
                v.Type = r.ReadBitLong();
                switch (v.Type)
                {
                case 3: v.Number = r.ReadBitLong(); break;
                case 5: v.Number = r.ReadBitDouble(); break;
                case 8: case 0x4009: v.Text = text.Next(); break;
                case 0x13: case 0x15: v.HandleIndex = handles++; break;
                case 0x4005: case 0x4006: case 0x4007: case 0x4008: v.Point = r.Read3BitDouble(); break;
                default: return false;
                }
                const auto at = key.find("$vector3d");
                if (at != std::string::npos) key.erase(at);
                map.Items[key] = std::move(v);
            }
            return !r.Failed() && !text.Reader.Failed();
        }
         
        inline bool ReadInterfacesObj(const MiniDWG::RawDwgData& data, MiniDWG::DwgBitReader& r, TextStream*& text,
                                      std::optional<TextStream>& storage, TchBase& base, int& handles)
        {
            if (!ReadTchBase(r, base, &data)) return false;
            storage.emplace(data, r);
            text = &*storage;
            for (int i = 0; i < base.TextCount; ++i) text->Next();
            handles = base.HandleCount;
            const int version = r.ReadBitShort();
            if (version > 1) return false;
            if (version == 1) handles += 2;
            return !r.Failed();
        }

        inline MiniDWG::Handle HandleAt(const MiniDWG::UnknownEntity& entity, int index)
        {
            using namespace MiniDWG;
            if (index < 0) return kNullHandle;
            DwgBitReader handles(entity.Raw.Dwg.Handles, entity.Raw.Dwg.Version, Codec::CodePage::Gbk);
            Handle h = kNullHandle;
            for (int i = 0; i <= index; ++i) h = handles.ReadHandle(entity.ObjectHandle);
            return handles.Failed() ? kNullHandle : h;
        }

        inline MiniDWG::Handle HandleAt(const MiniDWG::UnknownEntity& entity, const TchXDataMap::Value& v)
        {
            return v.HandleIndex < 0 ? v.Ref : HandleAt(entity, v.HandleIndex);
        }

        inline bool FinitePoint(const MiniDWG::XYZ& p) { return Finite(p.X) && Finite(p.Y); }

        // 新版管线接口列表 版本 2～3（版本为 BS）
        inline bool ReadCipherInterfacesObj(CipherReader& f)
        {
            if (!f.ReadBase()) return false;
            const int version = f.Main.ReadBitShort();
            if (version < 2 || version > 3) return false;
            f.ReadKey();
            f.Handle(); f.Handle();
            if (version > 2) { f.Byte(); f.Short(); }
            return f.Ok();
        }

        // 边界与连接列表新旧版相同（明文）
        inline bool ReadCipherEdgesAndLinks(CipherReader& f)
        {
            const int edges = f.Long();
            if (edges < 0 || edges > 1024) return false;
            for (int i = 0; i < edges && f.Ok(); ++i)
            {
                f.Handle();
                const int segments = f.Main.ReadBitLong();
                if (segments < 0 || segments > 4096) return false;
                for (int k = 0; k < segments && f.Ok(); ++k)
                {
                    for (int j = 0; j < 3; ++j) f.Main.Read3BitDouble();
                    f.Main.ReadBitShort(); f.Main.ReadBitLong(); f.Main.ReadBitDouble();
                }
            }
            const int links = f.Long();
            if (links < 0 || links > 4096) return false;
            for (int i = 0; i < links && f.Ok(); ++i) f.Handle();
            return f.Ok();
        }
         
        inline bool ReadCipherPipeBase(CipherReader& f, TchXDataMap& map)
        {
            if (!ReadCipherInterfacesObj(f)) return false;
            const int version = f.Main.ReadBitShort();
            if (version < 3 || version > 4) return false;
            f.ReadKey();
            if (!ReadCipherEdgesAndLinks(f)) return false;
            map = f.XData;
            if (version > 3)
                for (const char* key : {"Start", "End"})
                {
                    TchXDataMap::Value v;
                    v.Type = 0x4005;
                    v.Point = f.Point3();
                    map.Items[key] = v;
                }
            if (version > 3) { f.Point3(); f.Double(); f.Short(); }
            return f.Ok();
        }
         
        inline bool ReadCipherFittingBase(CipherReader& f, TchXDataMap& map)
        {
            if (!ReadCipherInterfacesObj(f) || f.Main.ReadBitShort() != 3) return false;
            f.ReadKey();
            f.Handle();
            CipherLayer layer(f);
            f.ReadKey();
            if (!ReadCipherEdgesAndLinks(f)) return false;
            map = f.XData;
            return f.Ok();
        }
    }

     
    struct TchCableStroke
    {
        double EdgeWidth = 0, CenterWidth = 0;
        bool CenterLine = false;
    };

    namespace Detail
    {
        inline TchCableStroke CableStroke(const TchXDataMap& map, double scale)
        {
            TchCableStroke s;
            const double k = scale > 0 && Finite(scale) ? scale : 1;
            if (const auto w = map.Number("EdageWidth"); w && Finite(*w) && *w > 0) s.EdgeWidth = *w * k;
            if (const auto w = map.Number("CenLineWidth"); w && Finite(*w) && *w > 0) s.CenterWidth = *w * k;
            if (const auto c = map.Number("HasCenLine")) s.CenterLine = *c != 0;
            return s;
        }

        inline void AddCableCenterLine(TchGraphics& g, const TchCableStroke& s, TchGraphics::Point a, TchGraphics::Point b)
        {
            if (!s.CenterLine) return;
            TchGraphics::Path p;
            p.Layer = 1;
            p.Width = s.CenterWidth;
            p.Points = {{a}, {b}};
            g.Paths.push_back(std::move(p));
        }
    }
     
    struct TchCableTray
    {
        MiniDWG::XYZ Start, End;
        double Width = 0;
        TchCableStroke Stroke;
    };
     
    inline bool ReadCableTrayMap(const MiniDWG::UnknownEntity& entity, TchXDataMap& map, std::string* diagnostic = nullptr, TchBase* baseOut = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_CABLETRY" && entity.Raw.DxfName != "TCH_DBHVACDUCT") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for cable tray");
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!Detail::ReadCipherPipeBase(f, map)) return fail("unsupported cable tray base layout");
            if (f.Main.ReadBitShort() > (entity.Raw.DxfName == "TCH_DBHVACDUCT" ? 4 : 1)) return fail("unsupported cable tray version");
            if (!f.Exact()) return fail("native cable tray fields do not consume the payload exactly");
            if (baseOut) { *baseOut = {}; baseOut->Scale = f.Scale; }
            return true;
        }
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        Detail::TextStream* text = nullptr;
        std::optional<Detail::TextStream> storage;
        int handles = 0;
        if (!Detail::ReadInterfacesObj(data, r, text, storage, base, handles)) return fail("unsupported cable tray base layout");
        if (r.ReadBitShort() != 1) return fail("unsupported cable tray pipe version");
        if (!Detail::ReadXDataMap(r, *text, map, handles)) return fail("invalid cable tray property map");
        const int edges = r.ReadBitLong();
        if (edges < 0 || edges > 1024) return fail("invalid cable tray edge count");
        for (int i = 0; i < edges && !r.Failed(); ++i)
        {
            const int segments = r.ReadBitLong();
            if (segments < 0 || segments > 4096) return fail("invalid cable tray edge");
            for (int k = 0; k < segments && !r.Failed(); ++k)
            {
                for (int j = 0; j < 3; ++j) r.Read3BitDouble();
                r.ReadBitShort(); r.ReadBitLong(); r.ReadBitDouble();
            }
        }
        const int links = r.ReadBitLong();
        if (links < 0 || links > 4096) return fail("invalid cable tray link count");
        if (r.ReadBitShort() > 1) return fail("unsupported cable tray version");
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native cable tray fields do not consume the payload exactly");
        if (baseOut) *baseOut = base;
        return true;
    }

    inline bool DecodeCableTray(const MiniDWG::UnknownEntity& entity, TchCableTray& tray, std::string* diagnostic = nullptr)
    {
        if (entity.Raw.DxfName != "TCH_CABLETRY") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        TchXDataMap map;
        TchBase base;
        if (!ReadCableTrayMap(entity, map, diagnostic, &base)) return false;
        const auto start = map.Point("Start"), end = map.Point("End");
        const auto width = map.Number("B");
        if (!start || !end || !width || !Detail::FinitePoint(*start) || !Detail::FinitePoint(*end) || !(*width > 0) || !Detail::Finite(*width))
            return fail("invalid cable tray geometry");
        tray = {*start, *end, *width, Detail::CableStroke(map, base.Scale)};
        if (diagnostic) diagnostic->clear();
        return true;
    }
     
    inline TchGraphics BuildCableTray(const TchCableTray& t)
    {
        TchGraphics g;
        const double dx = t.End.X - t.Start.X, dy = t.End.Y - t.Start.Y, len = std::hypot(dx, dy);
        if (len < 1e-9) return g;
        const double nx = -dy / len * t.Width / 2, ny = dx / len * t.Width / 2;
        TchGraphics::Path p;
        p.Width = t.Stroke.EdgeWidth;
        for (const auto& q : {TchGraphics::Point{t.Start.X + nx, t.Start.Y + ny}, TchGraphics::Point{t.End.X + nx, t.End.Y + ny},
                              TchGraphics::Point{t.End.X - nx, t.End.Y - ny}, TchGraphics::Point{t.Start.X - nx, t.Start.Y - ny},
                              TchGraphics::Point{t.Start.X + nx, t.Start.Y + ny}})
            p.Points.push_back({q});
        g.Paths.push_back(std::move(p));
        Detail::AddCableCenterLine(g, t.Stroke, {t.Start.X, t.Start.Y}, {t.End.X, t.End.Y});
        return g;
    }
     
    struct TchCableFitting
    {
        enum class Kind { Elbow, Tee, Cross };
        Kind Type = Kind::Elbow;
        MiniDWG::XYZ Mid, Major, Minor;
        double Length = 0, Length2 = 0, Width = 0;
        int Elbow = 0;
        bool Round = false;
        TchCableStroke Stroke;
        MiniDWG::Handle Connected[2] = {MiniDWG::kNullHandle, MiniDWG::kNullHandle};
    };
     
    inline bool ReadCableFittingMap(const MiniDWG::UnknownEntity& entity, TchXDataMap& map, std::string* diagnostic = nullptr, TchBase* baseOut = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_EFITTING" && entity.Raw.DxfName != "TCH_DBHFITTING") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for cable tray fitting");
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!Detail::ReadCipherFittingBase(f, map)) return fail("unsupported cable tray fitting base layout");
            if (f.Main.ReadBitShort() > (entity.Raw.DxfName == "TCH_DBHFITTING" ? 2 : 1)) return fail("unsupported cable tray fitting class version");
            if (!f.Exact()) return fail("native cable tray fitting fields do not consume the payload exactly");
            if (baseOut) { *baseOut = {}; baseOut->Scale = f.Scale; }
            return true;
        }
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        Detail::TextStream* text = nullptr;
        std::optional<Detail::TextStream> storage;
        int handles = 0;
        if (!Detail::ReadInterfacesObj(data, r, text, storage, base, handles)) return fail("unsupported cable tray fitting base layout");
        if (r.ReadBitShort() != 1) return fail("unsupported cable tray fitting version");
        ++handles;                                                 
        if (!Detail::ReadXDataMap(r, *text, map, handles)) return fail("invalid cable tray fitting property map"); 
        const int list1 = r.ReadBitLong(), list2 = r.ReadBitLong();
        if (r.ReadBitShort() > 1) return fail("unsupported cable tray fitting class version");
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail(("native cable tray fitting fields do not consume the payload exactly (lists " + std::to_string(list1) + ", " +
                         std::to_string(list2) + ", bit " + std::to_string(r.PositionInBits()) + " of " + std::to_string(data.MainBits) + ")").c_str());
        if (baseOut) *baseOut = base;
        return true;
    }

    inline bool DecodeCableFitting(const MiniDWG::UnknownEntity& entity, TchCableFitting& fitting, std::string* diagnostic = nullptr)
    {
        if (entity.Raw.DxfName != "TCH_EFITTING") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        TchXDataMap map;
        TchBase base;
        if (!ReadCableFittingMap(entity, map, diagnostic, &base)) return false;
        if (const auto v = map.Number("bTransFit"); v && *v != 0) return fail("reducing cable tray tees and crosses are not supported");
        if (const auto v = map.Number("InclineFit"); v && *v != 0) return fail("inclined cable tray fittings are not supported");
        TchCableFitting f;
        const auto mid = map.Point("Mid");
        const auto width = map.Number("B"), corner = map.Number("Corner");
        if (!mid || !width) return fail("cable tray fitting properties are missing");
        f.Mid = *mid;
        f.Width = *width;
        f.Round = corner && *corner != 0;
        f.Stroke = Detail::CableStroke(map, base.Scale);
        auto direction = [](const MiniDWG::XYZ& d) { return Detail::FinitePoint(d) && std::hypot(d.X, d.Y) > 1e-6; };
        if (map.Find("Elbow"))
        {
            const auto major = map.Point("MajBr"), minor = map.Point("MinBr");
            const auto length = map.Number("L"), elbow = map.Number("Elbow");
            if (!major || !minor || !length || !elbow) return fail("cable tray elbow properties are missing");
            f.Type = TchCableFitting::Kind::Elbow;
            f.Major = *major; f.Minor = *minor;
            f.Length = *length; f.Elbow = static_cast<int>(*elbow);
            if (f.Elbow != 1 && f.Elbow != 2) return fail("only 90 and 45 degree cable tray elbows are supported");
            const char* keys[] = {"IdCNT1", "IdCNT2"};
            for (int i = 0; i < 2; ++i)
                if (const auto* v = map.Find(keys[i])) f.Connected[i] = Detail::HandleAt(entity, *v);
        }
        else if (map.Find("Tee"))
        {
            const auto major = map.Point("MajBr"), minor = map.Point("MinBr");
            const auto length = map.Number("L1"), length2 = map.Number("L2");
            if (!major || !minor || !length || !length2) return fail("cable tray tee properties are missing");
            f.Type = TchCableFitting::Kind::Tee;
            f.Major = *major; f.Minor = *minor;
            f.Length = *length; f.Length2 = *length2;
            if (!(f.Length2 > 0) || !Detail::Finite(f.Length2)) return fail("invalid cable tray tee geometry");
        }
        else if (map.Find("Cross"))
        {
            const auto dir = map.Point("Mndir");
            const auto length = map.Number("L");
            if (!dir || !length) return fail("cable tray cross properties are missing");
            f.Type = TchCableFitting::Kind::Cross;
            f.Major = *dir;
            f.Minor = {-dir->Y, dir->X, 0};
            f.Length = *length;
        }
        else
            return fail("unsupported cable tray fitting type");
        if (!Detail::FinitePoint(f.Mid) || !direction(f.Major) || !direction(f.Minor) ||
            !(f.Length > 0) || !(f.Width > 0) || !Detail::Finite(f.Length) || !Detail::Finite(f.Width))
            return fail("invalid cable tray fitting geometry");
        if (f.Type != TchCableFitting::Kind::Elbow)
        {
            const double cosine = (f.Major.X * f.Minor.X + f.Major.Y * f.Minor.Y) / std::hypot(f.Major.X, f.Major.Y) / std::hypot(f.Minor.X, f.Minor.Y);
            if (std::abs(cosine) > 1e-3) return fail("only right-angle cable tray tees and crosses are supported");   
        }
        fitting = f;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    namespace Detail
    {  
        inline void AddFittingCorner(TchGraphics::Path& p, TchGraphics::Point a, TchGraphics::Point b, TchGraphics::Point mid, bool round, double turn)
        {
            double bulge = 0;
            if (round)
            {
                const double side = (b.X - a.X) * (mid.Y - a.Y) - (b.Y - a.Y) * (mid.X - a.X);
                bulge = std::tan(turn / 4) * (side < 0 ? 1 : -1);    
            }
            p.Points.push_back({a, bulge});
            p.Points.push_back({b});
        }
    }
     
    inline TchGraphics BuildCableElbow(const TchCableFitting& f, const std::vector<TchGraphics::Segment>& trays)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double w = f.Width / 2;
        const double arm = f.Elbow == 1 ? f.Length - w : f.Length + w * std::tan(std::numbers::pi / 8);
        auto unit = [](const MiniDWG::XYZ& d) { const double n = std::hypot(d.X, d.Y); return P{d.X / n, d.Y / n}; };
        auto distance = [](P p, const TchGraphics::Segment& s) {
            const double dx = s.B.X - s.A.X, dy = s.B.Y - s.A.Y, l2 = dx * dx + dy * dy;
            const double t = l2 > 0 ? std::clamp(((p.X - s.A.X) * dx + (p.Y - s.A.Y) * dy) / l2, 0.0, 1.0) : 0.0;
            return std::hypot(p.X - s.A.X - t * dx, p.Y - s.A.Y - t * dy);
        };
        auto orient = [&](P d, double fallback) {
            const P plus{f.Mid.X + d.X * arm, f.Mid.Y + d.Y * arm}, minus{f.Mid.X - d.X * arm, f.Mid.Y - d.Y * arm};
            double best = 1e300, sign = fallback;
            for (const auto& s : trays)       
            {
                const double a = distance(plus, s), b = distance(minus, s);
                if (std::min(a, b) < best && std::min(a, b) < f.Width) { best = std::min(a, b); sign = a <= b ? 1.0 : -1.0; }
            }
            return P{d.X * sign, d.Y * sign};
        };
        
        const P u = orient(unit(f.Major), f.Elbow == 1 ? -1.0 : 1.0), v = orient(unit(f.Minor), 1.0);
        const double cosine = u.X * v.X + u.Y * v.Y;
        if (std::abs(cosine) > 1 - 1e-6) return g;
        auto normal = [](P a, P b) {         
            const double d = b.X * a.X + b.Y * a.Y;
            const double nx = b.X - d * a.X, ny = b.Y - d * a.Y, n = std::hypot(nx, ny);
            return P{nx / n, ny / n};
        };
        const P nu = normal(u, v), nv = normal(v, u);
        const P m{f.Mid.X, f.Mid.Y};
        auto at = [&](P d, double t, P n, double s) { return P{m.X + d.X * t + n.X * s, m.Y + d.Y * t + n.Y * s}; };
        auto meet = [&](double s) {          
            const P a = at(u, 0, nu, s), b = at(v, 0, nv, s);
            const double den = u.X * v.Y - u.Y * v.X;
            const double t = ((b.X - a.X) * v.Y - (b.Y - a.Y) * v.X) / den;
            return P{a.X + u.X * t, a.Y + u.Y * t};
        };
        TchGraphics::Path p;
        p.Width = f.Stroke.EdgeWidth;
        for (const auto& q : {at(u, arm, nu, -w), meet(-w), at(v, arm, nv, -w), at(v, arm, nv, w)}) p.Points.push_back({q});
        if (f.Elbow == 1)
            Detail::AddFittingCorner(p, at(v, f.Length / 2, nv, w), at(u, f.Length / 2, nu, w), m, f.Round, std::numbers::pi - std::acos(cosine));
        else
            p.Points.push_back({meet(w)});
        p.Points.push_back({at(u, arm, nu, w)});
        p.Points.push_back({p.Points.front().P});
        g.Paths.push_back(std::move(p));
        Detail::AddCableCenterLine(g, f.Stroke, at(u, arm, nu, 0), m);
        Detail::AddCableCenterLine(g, f.Stroke, m, at(v, arm, nv, 0));
        return g;
    }

    inline TchGraphics BuildCableTee(const TchCableFitting& f)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double w = f.Width / 2, h = f.Length / 2, c = f.Length2 / 2;
        const double nu0 = std::hypot(f.Major.X, f.Major.Y), nv0 = std::hypot(f.Minor.X, f.Minor.Y);
        const P u{f.Major.X / nu0, f.Major.Y / nu0};
        const double side = (u.X * f.Minor.Y - u.Y * f.Minor.X) / nv0 < 0 ? -1.0 : 1.0;
        const P v{-u.Y * side, u.X * side};  // 支路取主路的垂线（MinBr 一侧）
        const P m{f.Mid.X, f.Mid.Y};
        auto at = [&](P d, double t, P n, double s) { return P{m.X + d.X * t + n.X * s, m.Y + d.Y * t + n.Y * s}; };
        const P nu = v, nv = u;              // 两路垂直：主路朝支路一侧的法向即支路方向，反之亦然
        const P mu{-u.X, -u.Y};
        TchGraphics::Path p;
        p.Width = f.Stroke.EdgeWidth;
        for (const auto& q : {at(u, -h, nu, -w), at(u, h, nu, -w), at(u, h, nu, w)}) p.Points.push_back({q});
        Detail::AddFittingCorner(p, at(u, c, nu, w), at(v, c, nv, w), m, f.Round, std::numbers::pi / 2);
        p.Points.push_back({at(v, h, nv, w)});
        p.Points.push_back({at(v, h, mu, w)});
        Detail::AddFittingCorner(p, at(v, c, mu, w), at(u, -c, nu, w), m, f.Round, std::numbers::pi / 2);
        p.Points.push_back({at(u, -h, nu, w)});
        p.Points.push_back({p.Points.front().P});
        g.Paths.push_back(std::move(p));
        Detail::AddCableCenterLine(g, f.Stroke, at(u, -h, nu, 0), at(u, h, nu, 0));
        Detail::AddCableCenterLine(g, f.Stroke, m, at(v, h, nv, 0));
        return g;
    }

    // 四通：沿 Mndir 及其垂直方向四路各伸出 L/2；四个内角的切点距 Mid 为 (L/2 + B/2) / 2（Corner 非 0 时为圆弧）
    inline TchGraphics BuildCableCross(const TchCableFitting& f)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double w = f.Width / 2, h = f.Length / 2, c = (h + w) / 2;
        const double n0 = std::hypot(f.Major.X, f.Major.Y);
        const P m{f.Mid.X, f.Mid.Y};
        P d{f.Major.X / n0, f.Major.Y / n0};
        auto at = [&](P dir, double t, P n, double s) { return P{m.X + dir.X * t + n.X * s, m.Y + dir.Y * t + n.Y * s}; };
        TchGraphics::Path p;
        p.Width = f.Stroke.EdgeWidth;
        for (int k = 0; k < 4; ++k)
        {
            const P n{-d.Y, d.X};            // 左侧法向，也是下一路的方向
            p.Points.push_back({at(d, h, n, -w)});
            p.Points.push_back({at(d, h, n, w)});
            Detail::AddFittingCorner(p, at(d, c, n, w), at(n, c, d, w), m, f.Round, std::numbers::pi / 2);
            d = n;
        }
        p.Points.push_back({p.Points.front().P});
        g.Paths.push_back(std::move(p));
        const P e{-d.Y, d.X};
        Detail::AddCableCenterLine(g, f.Stroke, at(d, -h, e, 0), at(d, h, e, 0));
        Detail::AddCableCenterLine(g, f.Stroke, at(e, -h, d, 0), at(e, h, d, 0));
        return g;
    }

    inline TchGraphics BuildCableFitting(const TchCableFitting& f, const std::vector<TchGraphics::Segment>& trays)
    {
        switch (f.Type)
        {
        case TchCableFitting::Kind::Tee: return BuildCableTee(f);
        case TchCableFitting::Kind::Cross: return BuildCableCross(f);
        default: return BuildCableElbow(f, trays);
        }
    }

    
    struct TchRupture
    {
        double X1 = 0, Y1 = 0, X2 = 0, Y2 = 0, Scale = 100;
        int Count = 1;
    };

    namespace Detail
    {
        inline bool CheckRupture(const TchRupture& t)
        {
            return Finite(t.X1) && Finite(t.Y1) && Finite(t.X2) && Finite(t.Y2) && t.Scale > 0 && std::hypot(t.X2 - t.X1, t.Y2 - t.Y1) >= 1e-9;
        }
         
        inline bool DecodeRuptureCipher(const MiniDWG::UnknownEntity& entity, TchRupture& rupture, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 2) return fail("unsupported native rupture line version");
            f.ReadKey();
            TchRupture t;
            t.Scale = f.Scale;
            const auto a = f.Point2(), b = f.Point2();
            t.X1 = a.X; t.Y1 = a.Y; t.X2 = b.X; t.Y2 = b.Y;
            t.Count = f.Byte();
            if (!f.Exact()) return fail("native rupture line fields do not consume the payload exactly");
            if (!CheckRupture(t)) return fail("invalid native rupture line geometry");
            rupture = t;
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    inline bool DecodeRupture(const MiniDWG::UnknownEntity& entity, TchRupture& rupture, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_RUPTURE") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeRuptureCipher(entity, rupture, diagnostic);
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for rupture line");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base, &data)) return fail("unsupported Tianzheng base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 2) return fail("unsupported native rupture line version");
        if (version == 2 && r.ReadByte() >= 16) return fail("encrypted native rupture line");
        TchRupture t;
        t.Scale = base.Scale;
        t.X1 = r.ReadRawDouble(); t.Y1 = r.ReadRawDouble();
        t.X2 = r.ReadRawDouble(); t.Y2 = r.ReadRawDouble();
        t.Count = r.ReadByte();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native rupture line fields do not consume the payload exactly");
        if (!Detail::CheckRupture(t)) return fail("invalid native rupture line geometry");
        rupture = t;
        if (diagnostic) diagnostic->clear();
        return true;
    }
 
    inline TchGraphics BuildRupture(const TchRupture& t)
    {
        TchGraphics g;
        const double dx = t.X2 - t.X1, dy = t.Y2 - t.Y1, total = std::hypot(dx, dy);
        const double ux = dx / total, uy = dy / total, nx = -uy, ny = ux, s = t.Scale, h = 2.1 * s;
        const double len = total / std::max(t.Count, 1);
        for (int i = 0; i < t.Count; ++i)
        {
            const double x0 = t.X1 + ux * len * i, y0 = t.Y1 + uy * len * i;
            auto at = [&](double along, double side) { return TchGraphics::Point{x0 + ux * along + nx * side, y0 + uy * along + ny * side}; };
            const double a = len / 2 - 1.125 * s;
            if (a < 0)
            {
                g.Lines.push_back({at(0, 0), at(len, 0)});
                continue;
            }
            const double peak = a + h / std::tan(70 * std::numbers::pi / 180);
            const double valley = peak + 2 * h / std::tan(80 * std::numbers::pi / 180);
            TchGraphics::Path p;
            for (const auto& q : {at(0, 0), at(a, 0), at(peak, h), at(valley, -h), at(len / 2 + 1.125 * s, 0), at(len, 0)}) p.Points.push_back({q});
            g.Paths.push_back(std::move(p));
        }
        return g;
    }
}
