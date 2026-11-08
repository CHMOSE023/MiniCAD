#pragma once
#include "Import/TchShape.h"
#include <cmath>
#include <vector>

namespace MiniCAD::Tch
{
    struct TchCurveMesh
    {
        std::vector<MiniDWG::XYZ> Points;
        int Version = 0;
    };

    namespace Detail
    {
        inline bool CheckCurveMesh(const TchCurveMesh& m)
        {
            if (m.Points.size() < 3 || m.Points.size() > 100000) return false;
            for (const auto& p : m.Points)
                if (!std::isfinite(p.X) || !std::isfinite(p.Y) || !std::isfinite(p.Z) ||
                    std::abs(p.X) > 1e9 || std::abs(p.Y) > 1e9 || std::abs(p.Z) > 1e9) return false;
            return true;
        }

        inline std::vector<MiniDWG::XYZ> ScanMeshVertices(const MiniDWG::RawDwgData& data,
                                                          unsigned long long scanFrom,
                                                          unsigned long long scanWindow)
        {
            std::vector<MiniDWG::XYZ> best;
            for (unsigned long long t = scanFrom; t < scanFrom + scanWindow && t < data.MainBits; ++t)
            {
                MiniDWG::DwgBitReader r(data.Main, data.Version, MiniDWG::Codec::CodePage::Gbk);
                r.SetPositionInBits(t);
                std::vector<MiniDWG::XYZ> pts;
                while (r.PositionInBits() + 6 <= data.MainBits)
                {
                    const auto before = r.PositionInBits();
                    const double x = r.ReadBitDouble();
                    const double y = r.ReadBitDouble();
                    const double z = r.ReadBitDouble();
                    if (r.Failed()) break;
                    const bool ok = std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
                                    std::abs(x) < 1e6 && std::abs(y) < 1e6 && std::abs(z) < 1e6 &&
                                    (std::abs(x) > 1000 || std::abs(y) > 1000);
                    if (!ok)
                    {
                        r.SetPositionInBits(before);
                        break;
                    }
                    pts.push_back({x, y, z});
                }
                if (pts.size() > best.size()) best = std::move(pts);
            }
            return best;
        }
    }

    inline bool DecodeCurveMesh(const MiniDWG::UnknownEntity& entity, TchCurveMesh& mesh, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_CURVEMESH2") return false;
        if (data.Main.empty() || data.Main.size() < (data.MainBits + 7) / 8)
            return fail("unsupported TCH_CURVEMESH2 layout");
        TchCurveMesh m;
        unsigned long long scanFrom = 0;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng curve mesh base layout");
            m.Version = f.Main.ReadByte();
            f.ReadKey();
            scanFrom = f.Main.PositionInBits();
        }
        else
        {
            if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for curve mesh");
            DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
            TchBase base;
            if (!ReadTchBase(r, base, &data)) return fail("unsupported Tianzheng curve mesh base layout");
            m.Version = r.ReadByte();
            scanFrom = r.PositionInBits();
        }
        if (scanFrom + 192 > data.MainBits) return fail("curve mesh payload is incomplete");
        auto pts = Detail::ScanMeshVertices(data, scanFrom, 640);
        if (pts.size() < 3) return fail("native curve mesh vertices are not recognized");
        m.Points = std::move(pts);
        if (!Detail::CheckCurveMesh(m)) return fail("invalid native curve mesh");
        mesh = std::move(m);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildCurveMesh(const TchCurveMesh& m)
    {
        TchGraphics g;
        TchGraphics::Path p;
        for (const auto& pt : m.Points) p.Points.push_back({pt.X, pt.Y});
        if (p.Points.size() >= 2) g.Paths.push_back(std::move(p));
        return g;
    }
}
