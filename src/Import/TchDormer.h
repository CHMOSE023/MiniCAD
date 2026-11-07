#pragma once
#include "Import/TchShape.h"
#include <array>
#include <cmath>

namespace MiniCAD::Tch
{
    // 老虎窗（TCH_DORMER）：代理图形是空的（只有 8 字节头），几何必须从私有数据解。
    // 明文（基类 3～8）与 T30 加密（基类 9～14）两个家族的字段语义一致但编码不同：
    //   明文：ReadTchBase + version(2) + BS(1) + 10 个 BD 参数 + 6 个 BD
    //         （插入点 X/Y/Z，然后屋面法向 X/Y/Z）
    //   加密：ReadBase + version(3) + ReadKey + 11 个 Double（第 1 个=1，其余 10 个参数）
    //         + P3（插入点，按 Key 重排为 X/Y/Z）+ P3（屋面法向）
    // 各版本（t6～t20V9）这 10 个参数实测完全相同：
    //   200, 200, 0.5, 450, 200, 1500, 180, 1800, 1300, 1400；
    // 插入点 X=61689.23, Y=29421.88, Z=744.23；屋面法向 (-0.7656, 0, 0.6433)，模长 1。
    // 平面朝向取法向在 XY 平面的方位角 atan2(nrm.Y, nrm.X)——实测该值（180°）让老虎窗
    // 背靠最近的竖直墙（靠墙角距墙 134，背离侧 1666），与图纸布局自洽；
    // 以前把法向的 X 分量（-0.7656）当旋转角用，方向差约 136°。
    struct TchDormer
    {
        double X = 0, Y = 0, Rotation = 0;
        double Z = 0;                // 插入点标高
        double NX = 0, NY = 0, NZ = 1;  // 屋面法向（单位向量）
        double Width = 0, Depth = 0; // 取自主尺寸参数（1800 / 1400）
        double Params[10] = {};      // 10 个原始参数，保留供后续几何校准
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
            d.Width = d.Params[7];   // 1800
            d.Depth = d.Params[9];   // 1400
            // 平面朝向取屋面法向在 XY 平面的方位角；法向在 XY 上没有分量时退回 0
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
            const double flag = f.Double();                 // 1（与明文的 BS 对应）
            if (!std::isfinite(flag) || flag < 0 || flag > 8) return fail("unsupported native dormer version");
            for (int i = 0; i < 10 && f.Ok(); ++i) d.Params[i] = f.Double();
            const auto pos = f.Point3();                    // 插入点 X / Y / Z
            const auto nrm = f.Point3();                    // 屋面法向（单位向量）
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
        const int flag = r.ReadBitShort();                  // 1
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

    // 老虎窗的平面示意：局部坐标以插入点为原点，+u 为坡向（法向在 XY 的方位），
    // +v 为墙面方向。窗洞是矩形（宽 Width 沿 v、进深 Depth 沿 u），在背离墙的一侧
    // 伸出屋面板（由坡度 Params[2] 与屋面高 Params[5] 决定伸出量），两侧坡线收成脊。
    // 参数化近似；位置、朝向、尺寸都来自实测数据。
    inline TchGraphics BuildDormer(const TchDormer& d)
    {
        TchGraphics g;
        const double hw = d.Width / 2;              // 沿 v（墙面方向）的半宽
        const double hd = d.Depth / 2;              // 沿 u（坡向）的半深
        auto at = [&](double u, double v) { return Detail::Local(d.X, d.Y, d.Rotation, u, v); };
        // 窗洞矩形
        TchGraphics::Path outline;
        for (auto [u, v] : {std::pair{-hd, -hw}, {hd, -hw}, {hd, hw}, {-hd, hw}, {-hd, -hw}})
            outline.Points.push_back({at(u, v)});
        g.Paths.push_back(std::move(outline));
        // 伸出量：坡度（Params[2]）越大伸出越远；没有可用坡度时按半个进深
        const double slope = d.Params[2] > 0.01 && d.Params[2] < 10.0 ? d.Params[2] : 0.5;
        const double reach = hd * (1.0 + slope);
        auto line = [&](double u0, double v0, double u1, double v1) {
            TchGraphics::Path p;
            p.Points.push_back({at(u0, v0)});
            p.Points.push_back({at(u1, v1)});
            g.Paths.push_back(std::move(p));
        };
        // 中脊线（沿坡向）
        line(-hd, 0, reach, 0);
        // 两条坡屋面线：从窗洞两前角收向脊端
        line(hd, -hw, reach, 0);
        line(hd, hw, reach, 0);
        return g;
    }
}
