#pragma once
#include "Import/TchDimension.h"
#include <cmath>
#include <numbers>
#include <vector>

namespace MiniCAD::Tch
{
    // TCH_PATH_ARRAY 线图案（CADReader TDbPolyPathArray 读取函数 0x6e6840，路径基类 0x6e64d0）：
    // 沿多段线路径重复排列一个图案块（样例为 $LinePat$45度线、$LinePat$波浪线）
    struct TchPathArray
    {
        struct Vertex { double X = 0, Y = 0, Angle = 0; };   // Angle：到下一点的圆弧圆心角（弧度，正为逆时针），不是凸度
        std::vector<Vertex> Vertices;
        bool   Closed = false;
        double Width = 120;                 // 图案宽度（垂直路径方向）
        double PatternScale = 1;            // 图案整体缩放（宽度与单元长度都乘以它）
        int    Flags = 8;                   // 位 0：单元不拉伸、居中排布；位 2：块底边在路径上；位 3：块居中；位 4：块上下翻转
        MiniDWG::Handle Block = MiniDWG::kNullHandle;
    };

    // 路径基类：天正基类、RC 版本（只有 0 为明文分支）、BS 顶点数、每点 3BD + BD 圆心角、B 闭合、3BD 法向；句柄流一个句柄。
    // 阵列：RC 版本（< 3 为明文分支）、BD 宽度、RC 标志、版本 ≥ 2 时 BD 图案缩放；句柄流为图案块。必须恰好读完主数据流
    inline bool DecodePathArray(const MiniDWG::UnknownEntity& entity, TchPathArray& array, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_PATH_ARRAY") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (data.Version < CadVersion::AC1021 || data.Main.size() < (data.MainBits + 7) / 8)
            return fail("unsupported DWG version for native path array");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        if (r.ReadByte() != 0) return fail("unsupported path base version");
        const int count = r.ReadBitShort();
        if (count < 2 || count > 100000) return fail("invalid path vertex count");
        TchPathArray a;
        for (int i = 0; i < count && !r.Failed(); ++i)
        {
            TchPathArray::Vertex v;
            v.X = r.ReadBitDouble(); v.Y = r.ReadBitDouble(); r.ReadBitDouble();
            v.Angle = r.ReadBitDouble();
            a.Vertices.push_back(v);
        }
        a.Closed = r.ReadBit();
        const double nx = r.ReadBitDouble(), ny = r.ReadBitDouble(), nz = r.ReadBitDouble();
        const int version = r.ReadByte();
        if (version < 1 || version > 2) return fail("unsupported native path array version");
        a.Width = r.ReadBitDouble();
        a.Flags = r.ReadByte();
        if (version >= 2) a.PatternScale = r.ReadBitDouble();
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native path array fields do not consume the payload exactly");
        if (std::abs(nx) > 1e-9 || std::abs(ny) > 1e-9 || nz <= 0) return fail("path array is not in the XY plane");
        for (const auto& v : a.Vertices)
            if (!Detail::Finite(v.X) || !Detail::Finite(v.Y) || !Detail::Finite(v.Angle) || std::abs(v.Angle) >= 2 * std::numbers::pi)
                return fail("invalid path vertex");
        if (!Detail::Finite(a.Width) || !Detail::Finite(a.PatternScale) || a.Width <= 0 || a.PatternScale <= 0)
            return fail("invalid path array settings");

        DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
        for (int i = 0; i < base.HandleCount + 1; ++i) handles.ReadHandle(entity.ObjectHandle);
        a.Block = handles.ReadHandle(entity.ObjectHandle);
        if (handles.Failed() || handles.PositionInBits() > data.HandleBits || a.Block == kNullHandle)
            return fail("path array handle stream is incomplete");
        array = std::move(a);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 一个图案单元：图案块按 (ScaleX, ScaleY) 缩放、转 Rotation 后插入到 (X, Y)
    struct TchPathArrayUnit { double X = 0, Y = 0, Rotation = 0, ScaleX = 1, ScaleY = 1; };

    // 按天正 2014 分解结果核对出的排布（块包围盒 [x0, x1] × [y0, y1]）：
    // - 横向缩放 sy = 宽度 × 图案缩放 / 块高，名义单元长 u = 块宽 × sy；
    // - 每段单元数 n = ⌊段长 / u⌋（不足一个单元的段不画），单元沿段拉伸为段长 / n；位 0 时不拉伸，n 个名义单元在段内居中；
    // - 圆弧段：段长与拉伸取路径左侧边（半径 R − 圆心角符号 × 半宽）的弧长与子弧弦长，单元中心在路径子弧中点、方向沿切线；
    // - 块的横向对齐：位 2 底边、位 3 中线、否则顶边在路径上；位 4 块在包围盒内上下翻转
    inline std::vector<TchPathArrayUnit> BuildPathArray(const TchPathArray& a, double x0, double y0, double x1, double y1)
    {
        std::vector<TchPathArrayUnit> units;
        const double bw = x1 - x0, bh = y1 - y0;
        if (!(bw > 1e-9) || !(bh > 1e-9)) return units;
        const double width = a.Width * a.PatternScale;
        const double sy = width / bh, u0 = bw * sy;
        const double xc = (x0 + x1) / 2;
        const double yc = (a.Flags & 4) ? y0 : (a.Flags & 8) ? (y0 + y1) / 2 : y1;
        const bool flip = (a.Flags & 16) != 0;
        constexpr std::size_t limit = 200000;
        auto add = [&](double mx, double my, double rot, double sx)
        {
            // 块点 (x, y) → M + R(rot)·((x − xc)·sx, (y′ − yc)·sy)，翻转时 y′ = y0 + y1 − y
            const double c = std::cos(rot), s = std::sin(rot);
            const double lx = -xc * sx, ly = (flip ? (y0 + y1 - yc) : -yc) * sy;
            units.push_back({mx + lx * c - ly * s, my + lx * s + ly * c, rot, sx, flip ? -sy : sy});
        };
        const std::size_t n = a.Vertices.size();
        for (std::size_t i = 0; i < (a.Closed ? n : n - 1) && units.size() < limit; ++i)
        {
            const auto& p = a.Vertices[i];
            const auto& q = a.Vertices[(i + 1) % n];
            const double L = std::hypot(q.X - p.X, q.Y - p.Y);
            if (L < 1e-9) continue;
            const double ux = (q.X - p.X) / L, uy = (q.Y - p.Y) / L;
            const double th = p.Angle;
            if (std::abs(th) < 1e-12)
            {
                const auto count = static_cast<std::size_t>(std::floor(L / u0 + 1e-9));
                if (count == 0 || count > limit) continue;
                const bool keep = (a.Flags & 1) != 0;
                const double step = keep ? u0 : L / static_cast<double>(count);
                const double lead = keep ? (L - static_cast<double>(count) * u0) / 2 : 0;
                for (std::size_t k = 0; k < count; ++k)
                {
                    const double t = lead + (static_cast<double>(k) + 0.5) * step;
                    add(p.X + ux * t, p.Y + uy * t, std::atan2(uy, ux), step / bw);
                }
                continue;
            }
            const double sign = th > 0 ? 1.0 : -1.0;
            const double R = L / 2 / std::sin(std::abs(th) / 2), d = R * std::cos(std::abs(th) / 2);
            const double cx = (p.X + q.X) / 2 - uy * d * sign, cy = (p.Y + q.Y) / 2 + ux * d * sign;
            const double a0 = std::atan2(p.Y - cy, p.X - cx);
            const double edge = R - sign * width / 2;
            if (edge <= 0) continue;
            const auto count = static_cast<std::size_t>(std::floor(edge * std::abs(th) / u0 + 1e-9));
            if (count == 0 || count > limit) continue;
            const double dl = th / static_cast<double>(count);
            const double sx = 2 * edge * std::sin(std::abs(dl) / 2) / bw;
            for (std::size_t k = 0; k < count; ++k)
            {
                const double am = a0 + dl * (static_cast<double>(k) + 0.5);
                add(cx + R * std::cos(am), cy + R * std::sin(am), am + sign * std::numbers::pi / 2, sx);
            }
        }
        return units;
    }
}
