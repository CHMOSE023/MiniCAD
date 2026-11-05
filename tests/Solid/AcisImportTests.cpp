// ACIS（SAT / SAB）读取与 B-rep 建实体：手写的小样例，不用真实图纸数据
#include "Import/AcisReader.h"
#include "Import/CadExchange.h"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Scene/Scene.h"
#include "Solid/SMLib/SMLibKernel.h"
#include "Database/CadDatabase.h"
#include "Database/UnknownObjects.h"
#include "Dwg/Write/DwgBitWriter.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

using namespace MiniCAD;
using Math::Point3;

namespace
{
    int g_failed = 0;
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        g_failed += ok ? 0 : 1;
    }

    constexpr double kPi = std::numbers::pi;

    bool Near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }
    bool NearPoint(const Point3& a, const Point3& b, double tol = 1e-6) { return Near(a.x, b.x, tol) && Near(a.y, b.y, tol) && Near(a.z, b.z, tol); }

    const char* kHeader700 = "700 0 1 0\n@16 Autodesk Test 20 ASM 225.1.0.65535 NT 24 Thu Jan  1 00:00:00 2026\n1 9.9999999999999995e-07 1e-10\n";
    const char* kHeader400 = "400 0 1 0\r\n16 Autodesk Test 20 ASM 225.1.0.65535 NT 24 Thu Jan  1 00:00:00 2026\r\n1 9.9999999999999995e-07 1e-10\r\n";

    // 半径 5、高 10 的圆柱，平移到 (100, 0, 0)
    const char* kCylinder700 =
        "body $-1 -1 $-1 $1 $-1 $2 #\n"
        "lump $-1 -1 $-1 $-1 $3 $0 #\n"
        "transform $-1 -1 $-1 1 0 0 0 1 0 0 0 1 100 0 0 1 no_rotate no_reflect no_shear #\n"
        "shell $-1 -1 $-1 $-1 $-1 $4 $-1 $1 #\n"
        "face $-1 -1 $-1 $5 $7 $3 $-1 $10 forward single #\n"
        "face $-1 -1 $-1 $6 $8 $3 $-1 $11 forward single #\n"
        "face $-1 -1 $-1 $-1 $9 $3 $-1 $12 forward single #\n"
        "loop $-1 -1 $-1 $13 $14 $4 #\n"
        "loop $-1 -1 $-1 $-1 $15 $5 #\n"
        "loop $-1 -1 $-1 $-1 $16 $6 #\n"
        "cone-surface $-1 -1 $-1 0 0 0 0 0 1 5 0 0 1 I I 0 1 5 forward I I I I #\n"
        "plane-surface $-1 -1 $-1 0 0 10 0 0 1 1 0 0 forward_v I I I I #\n"
        "plane-surface $-1 -1 $-1 0 0 0 0 0 -1 1 0 0 forward_v I I I I #\n"
        "loop $-1 -1 $-1 $-1 $17 $4 #\n"
        "coedge $-1 -1 $-1 $14 $14 $16 $18 reversed $7 $-1 #\n"
        "coedge $-1 -1 $-1 $15 $15 $17 $19 forward $8 $-1 #\n"
        "coedge $-1 -1 $-1 $16 $16 $14 $18 forward $9 $-1 #\n"
        "coedge $-1 -1 $-1 $17 $17 $15 $19 reversed $13 $-1 #\n"
        "edge $-1 -1 $-1 $20 0 $20 6.283185307179586 $16 $22 forward @7 unknown #\n"
        "edge $-1 -1 $-1 $21 0 $21 6.283185307179586 $15 $23 forward @7 unknown #\n"
        "vertex $-1 -1 $-1 $18 $24 #\n"
        "vertex $-1 -1 $-1 $19 $25 #\n"
        "ellipse-curve $-1 -1 $-1 0 0 0 0 0 -1 5 0 0 1 I I #\n"
        "ellipse-curve $-1 -1 $-1 0 0 10 0 0 1 5 0 0 1 I I #\n"
        "point $-1 -1 $-1 5 0 0 #\n"
        "point $-1 -1 $-1 5 0 10 #\n"
        "End-of-ACIS-data\n";

    // 旧版本（400）：没有历史指针、边没有参数；半径 3、高 4 的圆锥，锥顶是退化的边
    const char* kCone400 =
        "body $-1 $1 $-1 $-1 #\r\n"
        "lump $-1 $-1 $2 $0 #\r\n"
        "shell $-1 $-1 $-1 $3 $-1 $1 #\r\n"
        "face $-1 $4 $5 $2 $-1 $6 forward single #\r\n"
        "face $-1 $-1 $7 $2 $-1 $8 forward single #\r\n"
        "loop $-1 $9 $10 $3 #\r\n"
        "cone-surface $-1 0 0 0 0 0 1 3 0 0 1 I I -0.6 0.8 3 forward I I I I #\r\n"
        "loop $-1 $-1 $11 $4 #\r\n"
        "plane-surface $-1 0 0 0 0 0 -1 1 0 0 forward_v I I I I #\r\n"
        "loop $-1 $-1 $12 $3 #\r\n"
        "coedge $-1 $10 $10 $11 $13 reversed $5 $-1 #\r\n"
        "coedge $-1 $11 $11 $10 $13 forward $7 $-1 #\r\n"
        "coedge $-1 $12 $12 $-1 $14 forward $9 $-1 #\r\n"
        "edge $-1 $15 $15 $10 $17 forward #\r\n"
        "edge $-1 $16 $16 $12 $-1 forward #\r\n"
        "vertex $-1 $13 $18 #\r\n"
        "vertex $-1 $14 $19 #\r\n"
        "ellipse-curve $-1 0 0 0 0 0 -1 3 0 0 1 I I #\r\n"
        "point $-1 3 0 0 #\r\n"
        "point $-1 0 0 4 #\r\n";

    const char* kSphere400 =
        "body $-1 $1 $-1 $-1 #\r\n"
        "lump $-1 $-1 $2 $0 #\r\n"
        "shell $-1 $-1 $-1 $3 $-1 $1 #\r\n"
        "face $-1 $-1 $-1 $2 $-1 $4 forward single #\r\n"
        "sphere-surface $-1 1 2 3 5 1 0 0 0 0 1 forward_v I I I I #\r\n";

    // 一张双线性样条曲面（z 随 x、y 变化），一条边是 intcurve（经 ref 引用，参数区间只取中间一段），其余是直线
    const char* kSplineSheet700 =
        "body $-1 -1 $-1 $1 $-1 $-1 #\n"
        "lump $-1 -1 $-1 $-1 $2 $0 #\n"
        "shell $-1 -1 $-1 $-1 $-1 $3 $-1 $1 #\n"
        "face $-1 -1 $-1 $-1 $4 $2 $-1 $5 forward double out #\n"
        "loop $-1 -1 $-1 $-1 $6 $3 #\n"
        "spline-surface $-1 -1 $-1 forward { exactsur nubs 1 1 open open none none 2 2 0 1 10 1 0 1 20 1 "
        "0 0 0 10 0 1 0 20 2 10 20 3 0 } I I I I #\n"
        "coedge $-1 -1 $-1 $7 $9 $-1 $10 forward $4 $-1 #\n"
        "coedge $-1 -1 $-1 $8 $6 $-1 $11 forward $4 $-1 #\n"
        "coedge $-1 -1 $-1 $9 $7 $-1 $12 forward $4 $-1 #\n"
        "coedge $-1 -1 $-1 $6 $8 $-1 $13 forward $4 $-1 #\n"
        "edge $-1 -1 $-1 $14 0 $15 10 $6 $18 forward @7 unknown #\n"
        "edge $-1 -1 $-1 $15 0 $16 20.1 $7 $19 forward @7 unknown #\n"
        "edge $-1 -1 $-1 $16 0 $17 10.05 $8 $20 forward @7 unknown #\n"
        "edge $-1 -1 $-1 $17 0 $14 20.1 $9 $21 forward @7 unknown #\n"
        "vertex $-1 -1 $-1 $10 $22 #\n"
        "vertex $-1 -1 $-1 $11 $23 #\n"
        "vertex $-1 -1 $-1 $12 $24 #\n"
        "vertex $-1 -1 $-1 $13 $25 #\n"
        "intcurve-curve $-1 -1 $-1 forward { ref 1 } I I #\n"
        "straight-curve $-1 -1 $-1 10 0 1 0 1 0 I I #\n"
        "straight-curve $-1 -1 $-1 10 20 3 -1 0 0 I I #\n"
        "straight-curve $-1 -1 $-1 0 20 2 0 -1 0 I I #\n"
        "point $-1 -1 $-1 0 0 0 #\n"
        "point $-1 -1 $-1 10 0 1 #\n"
        "point $-1 -1 $-1 10 20 3 #\n"
        "point $-1 -1 $-1 0 20 2 #\n"
        "intcurve-curve $-1 -1 $-1 forward { exactcur nubs 1 open 2 -10 1 20 1 -10 0 -1 20 0 2 0 } I I #\n";

    // SAT 文本 → SAB（只为测试分词）：类型名拆成 0x0E 段 + 0x0D，记录尾 0x11
    std::vector<std::uint8_t> ToSab(const std::string& body, int version)
    {
        std::vector<std::uint8_t> out;
        auto bytes = [&](const void* p, size_t n) { out.insert(out.end(), static_cast<const std::uint8_t*>(p), static_cast<const std::uint8_t*>(p) + n); };
        auto i32 = [&](std::int32_t v) { bytes(&v, 4); };
        auto f64 = [&](double v) { bytes(&v, 8); };
        auto str = [&](std::uint8_t tag, const std::string& s) { out.push_back(tag); out.push_back(static_cast<std::uint8_t>(s.size())); bytes(s.data(), s.size()); };
        bytes("ACIS BinaryFile", 15);
        for (std::int32_t v : { version, 0, 1, 0 })
            i32(v);
        str(0x07, "Test");
        str(0x07, "ASM 225");
        str(0x07, "date");
        for (double v : { 1.0, 1e-6, 1e-10 })
        {
            out.push_back(0x06);
            f64(v);
        }
        std::istringstream in(body);
        std::string w;
        bool start = true;
        while (in >> w)
        {
            if (w == "#")
            {
                out.push_back(0x11);
                start = true;
                continue;
            }
            if (start)
            {
                size_t p = 0, dash;
                while ((dash = w.find('-', p)) != std::string::npos)
                {
                    str(0x0E, w.substr(p, dash - p));
                    p = dash + 1;
                }
                str(0x0D, w.substr(p));
                start = false;
                continue;
            }
            if (w[0] == '$')
            {
                out.push_back(0x0C);
                i32(std::atoi(w.c_str() + 1));
            }
            else if (w[0] == '@')
            {
                std::string s;
                in >> s;
                str(0x07, s);
            }
            else if (w == "{")
                out.push_back(0x0F);
            else if (w == "}")
                out.push_back(0x10);
            else if (w == "forward" || w == "reversed")
                out.push_back(w == "reversed" ? 0x0A : 0x0B);
            else if (w == "I")
                out.push_back(0x0B);
            else if (std::isdigit(static_cast<unsigned char>(w[0])) || w[0] == '-')
            {
                if (w.find_first_of(".e") == std::string::npos)
                {
                    out.push_back(0x04);
                    i32(std::atoi(w.c_str()));
                }
                else
                {
                    out.push_back(0x06);
                    f64(std::atof(w.c_str()));
                }
            }
            else
                str(0x0D, w);
        }
        return out;
    }

    bool Read(const std::string& text, Import::AcisResult& r)
    {
        return Import::ReadAcis(reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), r);
    }

    double Volume(Solid::ISolidKernel& k, const Solid::BrepModel& m, bool* solid = nullptr)
    {
        auto body = k.FromBrep(m, 1e-6);
        if (solid)
            *solid = body && k.IsSolid(body);
        return body ? k.Volume(body) : -1.0;
    }

    void TestCylinder(Solid::ISolidKernel& k)
    {
        for (int pass = 0; pass < 2; ++pass)
        {
            Import::AcisResult r;
            bool ok;
            if (pass == 0)
                ok = Read(std::string(kHeader700) + kCylinder700, r);
            else
            {
                std::string body = kCylinder700;
                body.resize(body.find("End-of-ACIS-data"));
                std::vector<std::uint8_t> sab = ToSab(body, 21500);
                const std::uint8_t tail[] = { 0x0E, 3, 'E', 'n', 'd', 0x0E, 2, 'o', 'f', 0x0E, 4, 'A', 'C', 'I', 'S', 0x0D, 4, 'd', 'a', 't', 'a' };
                sab.insert(sab.end(), std::begin(tail), std::end(tail));
                sab.insert(sab.end(), 32, 0xAB);   // 结束标记后的数据不读
                ok = Import::ReadAcis(sab.data(), sab.size(), r);
            }
            const char* name = pass == 0 ? "SAT 700" : "SAB";
            std::printf("  %s\n", name);
            Check(ok && r.Brep.Faces.size() == 3 && r.FailedFaces == 0, "cylinder: 3 faces");
            Check(r.Wire.size() == 2, "cylinder: 2 circle edges in the wire");
            if (r.Brep.Faces.size() != 3)
                continue;
            const auto& side = r.Brep.Faces[0];
            Check(side.Surface.Type == Solid::BrepSurfaceType::Cone && Near(side.Surface.Radius, 5) && side.Loops.size() == 2,
                  "side face is a cylinder with two circular loops");
            Check(NearPoint(side.Surface.Origin, { 100, 0, 0 }), "body transform moves the surface");
            bool onCircle = true;
            for (const auto& p : r.Wire[0])
                onCircle = onCircle && Near(std::hypot(p.x - 100, p.y), 5, 1e-9);
            Check(onCircle, "wire points lie on the moved circle");
            bool solid = false;
            Check(Near(Volume(k, r.Brep, &solid), kPi * 25 * 10, 1e-3) && solid, "builds a closed solid with volume 250π");
        }
    }

    void TestOldVersion(Solid::ISolidKernel& k)
    {
        Import::AcisResult r;
        Check(Read(std::string(kHeader400) + kCone400, r) && r.Version == 400, "SAT 400 cone reads");
        Check(r.Brep.Faces.size() == 2 && r.Brep.Faces[0].PointLoops.size() == 1 && NearPoint(r.Brep.Faces[0].PointLoops[0], { 0, 0, 4 }),
              "cone apex becomes a point loop");
        Check(r.Brep.Faces.size() == 2 && r.Brep.Faces[0].Loops.size() == 1 && r.Brep.Faces[0].Loops[0].size() == 1
                  && Near(r.Brep.Faces[0].Loops[0][0].Length(), 2 * kPi * 3, 1e-2),
              "edge without parameters: full circle from the vertex projection");
        bool solid = false;
        Check(Near(Volume(k, r.Brep, &solid), kPi * 9 * 4 / 3, 1e-3) && solid, "cone solid volume 12π");

        Import::AcisResult s;
        Check(Read(std::string(kHeader400) + kSphere400, s) && s.Brep.Faces.size() == 1 && s.Brep.Faces[0].Loops.empty(),
              "sphere face without loops");
        Check(s.Brep.Faces.size() == 1 && Near(Volume(k, s.Brep), 4.0 / 3 * kPi * 125, 1e-2), "sphere volume");
    }

    void TestSpline(Solid::ISolidKernel& k)
    {
        Import::AcisResult r;
        Check(Read(std::string(kHeader700) + kSplineSheet700, r) && r.Brep.Faces.size() == 1, "spline sheet reads");
        if (r.Brep.Faces.size() != 1)
            return;
        const auto& f = r.Brep.Faces[0];
        const auto& n = f.Surface.Nurbs;
        Check(f.Surface.Type == Solid::BrepSurfaceType::Nurbs && n.IsValid() && n.CountU == 2 && n.CountV == 2,
              "bs3 surface: 2 × 2 control points");
        Check(NearPoint(n.PointAt(10, 0), { 10, 0, 1 }) && NearPoint(n.PointAt(0, 20), { 0, 20, 2 }),
              "ACIS points (u fastest) map to u * CountV + v");
        Check(f.Loops.size() == 1 && f.Loops[0].size() == 4, "one loop of 4 edges");
        if (f.Loops.size() == 1 && f.Loops[0].size() == 4)
        {
            const auto& c = f.Loops[0][0];
            Check(NearPoint(c.PointAt(c.StartParam()), { 0, 0, 0 }) && NearPoint(c.PointAt(c.EndParam()), { 10, 0, 1 }),
                  "intcurve via ref, clipped to the edge parameters");
        }
        auto body = k.FromBrep(r.Brep, 1e-6);
        Check(body && !k.Tessellate(body).Indices.empty(), "sheet body builds and tessellates");
    }

    void TestGarbage()
    {
        Import::AcisResult r;
        const std::uint8_t junk[] = { 'A', 'C', 'I', 'S', ' ', 'B', 'i', 'n', 'a', 'r', 'y', 'F', 'i', 'l', 'e', 1, 2, 3 };
        Check(!Import::ReadAcis(junk, sizeof junk, r), "truncated SAB fails cleanly");
        Check(!Read("garbage", r), "random text fails cleanly");
    }

    std::string Encrypt(const std::string& s)
    {
        std::string r = s;
        for (char& c : r)
            if (static_cast<unsigned char>(c) > 32)
                c = static_cast<char>(159 - static_cast<unsigned char>(c));
        return r;
    }

    std::vector<std::uint8_t> SabCylinder()
    {
        std::string body = kCylinder700;
        body.resize(body.find("End-of-ACIS-data"));
        return ToSab(body, 21500);
    }

    // MiniDWG：实体自己的数据（R2013 前）与 AcDs 段（R2013 起）里取 ACIS
    void TestDwgExtraction()
    {
        namespace Dwg = MiniDWG;
        const std::string sat = std::string(kHeader700) + kCylinder700;
        {
            Dwg::UnknownEntity e;
            e.Raw.DxfName     = "3DSOLID";
            e.Raw.Dwg.Version = Dwg::CadVersion::AC1015;
            Dwg::DwgBitWriter w(Dwg::CadVersion::AC1015, Dwg::Codec::CodePage::Utf8);
            w.WriteBit(false);
            w.WriteBit(true);
            w.WriteBitShort(1);
            const std::string enc = Encrypt(sat);
            for (size_t p = 0; p < enc.size(); p += 300)
            {
                const std::string block = enc.substr(p, 300);
                w.WriteBitLong(static_cast<std::int32_t>(block.size()));
                w.WriteBytes({ reinterpret_cast<const std::uint8_t*>(block.data()), block.size() });
            }
            w.WriteBitLong(0);
            w.WriteBit(true);
            e.Raw.Dwg.MainBits = w.PositionInBits();
            e.Raw.Dwg.Main     = w.Take();
            std::vector<std::uint8_t> out;
            Check(Dwg::DecodeAcisData(e, {}, out) && std::string(out.begin(), out.end()) == sat, "DWG R2000: encrypted SAT blocks");
        }
        const std::vector<std::uint8_t> sab = SabCylinder();
        {
            Dwg::UnknownEntity e;
            e.Raw.DxfName     = "REGION";
            e.Raw.Dwg.Version = Dwg::CadVersion::AC1024;
            Dwg::DwgBitWriter w(Dwg::CadVersion::AC1024, Dwg::Codec::CodePage::Utf8);
            w.WriteBit(false);
            w.WriteBit(false);
            w.WriteBitShort(2);
            w.WriteBytes(sab);
            e.Raw.Dwg.MainBits = w.PositionInBits();
            e.Raw.Dwg.Main     = w.Take();
            std::vector<std::uint8_t> out;
            Import::AcisResult r;
            Check(Dwg::DecodeAcisData(e, {}, out) && Import::ReadAcis(out.data(), out.size(), r) && r.Brep.Faces.size() == 3,
                  "DWG R2010: SAB right after the version");
        }
        {
            // 最小的 AcDs：段索引、datidx 段、数据段，一条记录
            std::vector<std::uint8_t> ds(0x480 + 4 + sab.size(), 0);
            auto put32 = [&](size_t p, std::uint32_t v) { std::memcpy(ds.data() + p, &v, 4); };
            auto put64 = [&](size_t p, std::uint64_t v) { std::memcpy(ds.data() + p, &v, 8); };
            put32(0x18, 0x100);
            put32(0x20, 2);
            put32(0x28, 0);
            put64(0x100 + 48, 0x200);
            put64(0x100 + 48 + 12, 0x400);
            put32(0x200 + 48, 1);
            put32(0x200 + 56, 1);
            put32(0x200 + 60, 0);
            put32(0x400 + 36, 0x8);
            const std::uint32_t entry[] = { 0x14, 1, 0x2AB, 0, 0 };
            for (size_t i = 0; i < 5; ++i)
                put32(0x400 + 48 + 4 * i, entry[i]);
            put32(0x480, static_cast<std::uint32_t>(sab.size()));
            std::memcpy(ds.data() + 0x484, sab.data(), sab.size());
            Dwg::CadDatabase db;
            db.RawSections.push_back(Dwg::RawSection{ Dwg::CadVersion::AC1032, "AcDb:AcDsPrototype_1b", ds });
            const Dwg::AcisDataStore store = Dwg::ReadAcisDataStore(db);
            Check(store.size() == 1 && store.count(0x2AB) == 1, "AcDs: one ACIS record by handle");

            Dwg::UnknownEntity e;
            e.Raw.DxfName     = "LOFTEDSURFACE";
            e.ObjectHandle    = 0x2AB;
            e.Raw.Dwg.Version = Dwg::CadVersion::AC1032;
            Dwg::DwgBitWriter w(Dwg::CadVersion::AC1032, Dwg::Codec::CodePage::Utf8);
            w.WriteBit(true);
            w.WriteBit(false);
            e.Raw.Dwg.MainBits = w.PositionInBits();
            e.Raw.Dwg.Main     = w.Take();
            std::vector<std::uint8_t> out;
            Check(Dwg::DecodeAcisData(e, store, out) && out == sab, "R2013+: data comes from the store");
            e.ObjectHandle = 0x2AC;
            Check(!Dwg::DecodeAcisData(e, store, out), "missing store record fails");
        }
    }

    // 整条路线：DXF（R2000，组码 1 加密的 SAT）→ ImportCad → Solid3DEntity
    std::string CylinderDxf()
    {
        std::string dxf =
            "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n0\nENDSEC\n"
            "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n1\n"
            "0\nLAYER\n2\n0\n70\n0\n62\n7\n6\nCONTINUOUS\n0\nENDTAB\n0\nENDSEC\n"
            "0\nSECTION\n2\nENTITIES\n"
            "0\n3DSOLID\n5\nA1\n100\nAcDbEntity\n8\n0\n100\nAcDbModelerGeometry\n70\n1\n";
        std::istringstream in(std::string(kHeader700) + kCylinder700);
        std::string line;
        while (std::getline(in, line))
        {
            std::string esc;
            for (char c : Encrypt(line))
                esc += c == '^' ? std::string("^ ") : std::string(1, c);
            dxf += "1\n" + esc + "\n";
        }
        return dxf + "0\nENDSEC\n0\nEOF\n";
    }

    const Solid3DEntity* OnlySolid(const Scene& scene)
    {
        const Solid3DEntity* found = nullptr;
        int count = 0;
        for (Object::ObjectID id : scene.GetAllIDs())
            if (const Object* e = scene.GetEntity(id); e && e->IsKindOf<Solid3DEntity>())
            {
                found = static_cast<const Solid3DEntity*>(e);
                ++count;
            }
        return count == 1 ? found : nullptr;
    }

    void TestImportCad(Solid::ISolidKernel& k)
    {
        const std::string dxf = CylinderDxf();
        const std::vector<std::uint8_t> bytes(dxf.begin(), dxf.end());
        {
            SetCadImportSolidKernel(nullptr);
            Scene scene;
            Check(ImportCad(bytes, scene), "DXF with a 3DSOLID imports (no kernel)");
            const Solid3DEntity* s = OnlySolid(scene);
            Check(s && s->GetBytes().empty() && s->GetLocalWire().size() == 2, "no kernel: wire-only solid with the 2 circles");
        }
        {
            SetCadImportSolidKernel(&k);
            Scene scene;
            Check(ImportCad(bytes, scene), "DXF with a 3DSOLID imports (kernel)");
            const Solid3DEntity* s = OnlySolid(scene);
            Check(s && !s->GetBytes().empty() && !s->GetLocalMesh().Empty(), "kernel: solid with shaded mesh");
            if (s)
            {
                const AABB b = s->GetBoundingBox();
                Check(Near(b.Min.x, 95, 1e-6) && Near(b.Max.x, 105, 1e-6) && Near(b.Max.z, 10, 1e-6), "bounds of the moved cylinder");
                auto body = s->GetBody(k);
                Check(body && Near(k.Volume(body), kPi * 250, 1e-3), "kernel body volume 250π");
            }
            SetCadImportSolidKernel(nullptr);
        }
    }

    // 开发用：MINICAD_ACIS_DIR 下的 .sat / .sab 逐个读、建实体，只打印统计
    void ScanDir(Solid::ISolidKernel& k)
    {
        const char* dir = std::getenv("MINICAD_ACIS_DIR");
        if (!dir)
            return;
        for (const auto& e : std::filesystem::directory_iterator(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(dir)))))
        {
            const auto ext = e.path().extension().u8string();
            std::ifstream in(e.path(), std::ios::binary);
            std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (ext == u8".dwg" || ext == u8".dxf")
            {
                SetCadImportSolidKernel(&k);
                Scene scene;
                const auto t0 = std::chrono::steady_clock::now();
                const bool ok = ImportCad(data, scene);
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                int shaded = 0, wireOnly = 0;
                for (Object::ObjectID id : scene.GetAllIDs())
                    if (const Object* o = scene.GetEntity(id); o && o->IsKindOf<Solid3DEntity>())
                        ++(static_cast<const Solid3DEntity*>(o)->GetBytes().empty() ? wireOnly : shaded);
                std::printf("  %s: import=%d entities=%d solids shaded=%d wire-only=%d %.0f ms\n",
                            reinterpret_cast<const char*>(e.path().filename().u8string().c_str()), ok, scene.EntityCount(), shaded,
                            wireOnly, ms);
                SetCadImportSolidKernel(nullptr);
                continue;
            }
            if (ext != u8".sat" && ext != u8".sab")
                continue;
            Import::AcisResult r;
            std::string err;
            const bool ok = Import::ReadAcis(data.data(), data.size(), r, &err);
            int skipped = 0;
            auto body = ok && !r.Brep.Faces.empty() ? k.FromBrep(r.Brep, std::max(1e-6, r.Resolution * 10), &skipped) : nullptr;
            std::printf("  %s: v%d faces=%zu failed=%d wire=%zu body=%s solid=%d skipped=%d vol=%.4f area=%.4f %s\n",
                        reinterpret_cast<const char*>(e.path().filename().u8string().c_str()), r.Version, r.Brep.Faces.size(),
                        r.FailedFaces, r.Wire.size(), body ? "yes" : "no", body ? k.IsSolid(body) : 0, skipped,
                        body ? k.Volume(body) : 0.0, body ? k.Area(body) : 0.0, ok ? "" : err.c_str());
            if (skipped > 0 && std::getenv("MINICAD_ACIS_FACES"))
                for (size_t i = 0; i < r.Brep.Faces.size(); ++i)
                {
                    Solid::BrepModel one;
                    one.Faces.push_back(r.Brep.Faces[i]);
                    int s = 0;
                    k.FromBrep(one, std::max(1e-6, r.Resolution * 10), &s);
                    const auto& f = r.Brep.Faces[i];
                    if (s > 0)
                    {
                        std::printf("    face %zu type=%d rev=%d loops=%zu points=%zu: %s\n", i, static_cast<int>(f.Surface.Type), f.Reversed,
                                    f.Loops.size(), f.PointLoops.size(), k.LastError().c_str());
                        const auto& sf = f.Surface;
                        std::printf("      surf o=(%g %g %g) n=(%g %g %g) x=(%g %g %g) r=%g sin=%g cos=%g\n", sf.Origin.x, sf.Origin.y, sf.Origin.z,
                                    sf.Normal.x, sf.Normal.y, sf.Normal.z, sf.XAxis.x, sf.XAxis.y, sf.XAxis.z, sf.Radius, sf.SinHalfAngle, sf.CosHalfAngle);
                        for (const auto& loop : f.Loops)
                            for (const auto& c : loop)
                            {
                                const auto a = c.PointAt(c.StartParam()), m = c.PointAt((c.StartParam() + c.EndParam()) / 2), b = c.PointAt(c.EndParam());
                                std::printf("      curve deg=%d n=%zu [%g,%g] (%g %g %g) (%g %g %g) (%g %g %g)\n", c.Degree, c.ControlPoints.size(),
                                            c.StartParam(), c.EndParam(), a.x, a.y, a.z, m.x, m.y, m.z, b.x, b.y, b.z);
                            }
                    }
                }
        }
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    auto kernel = Solid::CreateSMLibKernel();
    TestCylinder(*kernel);
    TestOldVersion(*kernel);
    TestSpline(*kernel);
    TestGarbage();
    TestDwgExtraction();
    TestImportCad(*kernel);
    ScanDir(*kernel);
    std::printf("%d failed\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
