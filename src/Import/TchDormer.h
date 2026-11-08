#pragma once
#include "Import/TchShape.h"
#include <array>
#include <cmath>

namespace MiniCAD::Tch
{
    struct TchDormer
    {
        double X = 0, Y = 0, Rotation = 0;
        double Z = 0;
        double NX = 0, NY = 0, NZ = 1;
        double Width = 0, Depth = 0;
        double Params[10] = {};
        int Version = 0;
    };

    namespace Detail
    {
        inline bool CheckDormer(const TchDormer& d)
        {
            const std::array<double, 9> values{d.X, d.Y, d.Z, d.NX, d.NY, d.NZ, d.Rotation, d.Width, d.Depth};
            for (double v : values)
                if (!std::isfinite(v) || std::abs(v) > 1e9) return false;
            for (double v : d.Params)
                if (!std::isfinite(v) || std::abs(v) > 1e9) return false;
            return d.Width > 0 && d.Depth > 0;
        }

        inline void FinishDormer(TchDormer& d)
        {
            d.Width = d.Params[7];
            d.Depth = d.Params[9];
            const double len = std::hypot(d.NX, d.NY);
            d.Rotation = len > 1e-9 ? std::atan2(d.NY, d.NX) : 0.0;
        }
    }

    inline bool DecodeDormer(const MiniDWG::UnknownEntity& entity, TchDormer& dormer, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_DORMER") return false;
        if (data.Main.empty() || data.Main.size() < (data.MainBits + 7) / 8) return fail("dormer payload is incomplete");
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng dormer base layout");
            TchDormer d;
            d.Version = f.Main.ReadByte();
            f.ReadKey();
            const double flag = f.Double();
            if (!std::isfinite(flag) || flag < 0 || flag > 8) return fail("unsupported native dormer version");
            for (int i = 0; i < 10 && f.Ok(); ++i) d.Params[i] = f.Double();
            const auto pos = f.Point3();
            const auto nrm = f.Point3();
            d.X = pos.X; d.Y = pos.Y; d.Z = pos.Z;
            d.NX = nrm.X; d.NY = nrm.Y; d.NZ = nrm.Z;
            if (!f.Ok()) return fail("native dormer fields are incomplete");
            Detail::FinishDormer(d);
            if (!Detail::CheckDormer(d)) return fail("invalid native dormer settings");
            dormer = std::move(d);
            if (diagnostic) diagnostic->clear();
            return true;
        }
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for native dormer");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base, &data)) return fail("unsupported Tianzheng dormer base layout");
        TchDormer d;
        d.Version = r.ReadByte();
        const int flag = r.ReadBitShort();
        if (flag < 0 || flag > 8) return fail("unsupported native dormer version");
        for (int i = 0; i < 10 && !r.Failed(); ++i) d.Params[i] = r.ReadBitDouble();
        d.X = r.ReadBitDouble(); d.Y = r.ReadBitDouble(); d.Z = r.ReadBitDouble();
        d.NX = r.ReadBitDouble(); d.NY = r.ReadBitDouble(); d.NZ = r.ReadBitDouble();
        if (r.Failed()) return fail("native dormer fields are incomplete");
        Detail::FinishDormer(d);
        if (!Detail::CheckDormer(d)) return fail("invalid native dormer settings");
        dormer = std::move(d);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildDormer(const TchDormer& d)
    {
        TchGraphics g;
        const double hw = d.Width / 2;
        const double hd = d.Depth / 2;
        auto at = [&](double u, double v) { return Detail::Local(d.X, d.Y, d.Rotation, u, v); };
        TchGraphics::Path outline;
        for (auto [u, v] : {std::pair{-hd, -hw}, {hd, -hw}, {hd, hw}, {-hd, hw}, {-hd, -hw}})
            outline.Points.push_back({at(u, v)});
        g.Paths.push_back(std::move(outline));
        const double slope = d.Params[2] > 0.01 && d.Params[2] < 10.0 ? d.Params[2] : 0.5;
        const double reach = hd * (1.0 + slope);
        auto line = [&](double u0, double v0, double u1, double v1) {
            TchGraphics::Path p;
            p.Points.push_back({at(u0, v0)});
            p.Points.push_back({at(u1, v1)});
            g.Paths.push_back(std::move(p));
        };
        line(-hd, 0, reach, 0);
        line(hd, -hw, reach, 0);
        line(hd, hw, reach, 0);
        return g;
    }
}
