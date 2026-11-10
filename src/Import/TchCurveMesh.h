#pragma once
#include "Import/TchShape.h"
#include <cmath>
#include <vector>

namespace MiniCAD::Tch
{
    struct TchCurveMesh
    {
        std::vector<MiniDWG::XYZ> Points;
        std::vector<double> Bulges;   // 与 Points 等长：顶点凸度（曲线网格的圆弧段）
        bool Closed = false;
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

        // 旧版（明文）TCH_CURVEMESH2：主数据流里直接是 BitDouble 的 (X,Y,Z) 点数组，
        // 没有点数前缀，只能盲扫出最长的一段合理三元组。
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
                    auto plausible = [](double v) { return v == 0 || (std::isfinite(v) && std::abs(v) >= 1e-3 && std::abs(v) < 1e6); };
                    const bool ok = plausible(x) && plausible(y) && plausible(z) &&
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
        if (Detail::IsCipherObject(data))
        {
            // 新版（加密）TCH_CURVEMESH2 = 天正曲线 版本 1：
            //   ReadBase → version(1) → key → count(BS) → count × [Point3 + bulge(BD)] → closed(B) → normal(P3)
            // 盲扫会把加密的 Point3（Key>=17 时分量交换）读成 (X,0,Z) 的错位三元组，
            // 画到远离图纸主体的空区域，表现为"未显示"。这里按真实布局结构化解析。
            Detail::CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng curve mesh base layout");
            if (f.Main.ReadByte() != 1) return fail("unsupported native curve mesh version");
            f.ReadKey();
            const int count = f.Short();
            if (count < 3 || count > 100000) return fail("invalid native curve mesh vertex count");
            m.Version = 1;
            for (int i = 0; i < count && f.Ok(); ++i)
            {
                const auto p = f.Point3();
                m.Points.push_back(p);
                m.Bulges.push_back(f.Double());
            }
            m.Closed = f.Bool();
            f.Point3();   // normal
            if (!f.Ok()) return fail("native curve mesh vertices are not recognized");
        }
        else
        {
            if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for curve mesh");
            DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
            TchBase base;
            if (!ReadTchBase(r, base, &data)) return fail("unsupported Tianzheng curve mesh base layout");
            m.Version = r.ReadByte();
            const auto scanFrom = r.PositionInBits();
            if (scanFrom + 192 > data.MainBits) return fail("curve mesh payload is incomplete");
            m.Points = Detail::ScanMeshVertices(data, scanFrom, 640);
        }
        if (!Detail::CheckCurveMesh(m)) return fail("invalid native curve mesh");
        if (m.Bulges.empty()) m.Bulges.assign(m.Points.size(), 0.0);
        mesh = std::move(m);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildCurveMesh(const TchCurveMesh& m)
    {
        TchGraphics g;
        TchGraphics::Path p;
        for (std::size_t i = 0; i < m.Points.size(); ++i)
        {
            const auto& pt = m.Points[i];
            p.Points.push_back({{pt.X, pt.Y}, i < m.Bulges.size() ? m.Bulges[i] : 0.0});
        }
        // PolylineEntity 靠"首尾点重复"闭合：末尾追加首点（ bulge 用第一段或 0）。
        // 曲线网格是墙/山墙的闭合轮廓，Closed 或首尾重合时都要闭合。
        if (p.Points.size() >= 3)
        {
            const auto& a = p.Points.front().P;
            const auto& b = p.Points.back().P;
            const bool coincide = std::abs(a.X - b.X) < 1e-6 && std::abs(a.Y - b.Y) < 1e-6;
            if (coincide) p.Points.pop_back();   // 去掉数据里已有的重复末点，再统一补
            if (m.Closed || coincide || p.Points.size() >= 3)
                p.Points.push_back({{a.X, a.Y}, 0.0});
        }
        if (p.Points.size() >= 2) g.Paths.push_back(std::move(p));
        return g;
    }
}
