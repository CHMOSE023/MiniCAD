#pragma once
#include "Import/TchOpening.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <numbers>
#include <tuple>
#include <vector>

namespace MiniCAD::Tch
{
    // TCH_DIMENSION2 尺寸标注（逐点标注、角度 / 弧长标注共用）
    struct TchDimension
    {
        MiniDWG::XYZ        Position;           // 第一个被标注点
        double              Angle = 0;          // 标注方向（弧度）；弧形标注时为第一个点处的切线方向
        double              Offset = 0;         // 尺寸线偏移（图面单位，乘以 Scale），正值在方向的左侧
        std::vector<double> Spans;              // 各段长度；弧形标注时为各段圆心角（弧度）
        int                 Flags = 0;
        double              ArcRadius = 0;      // 标志 0x10：尺寸线圆弧半径
        double              MeasuredRadius = 0; // 标志 0x10：被标注点所在半径（Position 在此半径上）
        std::map<int, std::string> TextOverrides;   // 标志 0x04：段号 → 替代文字
        double              Scale = 1;
        MiniDWG::Handle     DimStyle = MiniDWG::kNullHandle;

        bool IsArc() const { return (Flags & 0x10) != 0; }
    };

    // TCH_AXIS_LABEL 轴号：一组平行轴线两端的引出线、圆圈和编号
    struct TchAxisLabel
    {
        struct Axis { double Spacing = 0; std::string Label; };   // Spacing 为到下一根轴线的距离

        MiniDWG::XYZ      Start, End;           // 第一根轴线的两端
        std::vector<Axis> Axes;
        double            StartExtension = 0;   // 起点端引出线长度（图面单位）
        double            EndExtension = 0;     // 终点端引出线长度（图面单位）
        double            Radius = 0;           // 轴号圆半径（图面单位）
        double            Scale = 1;
        MiniDWG::Handle   TextStyle = MiniDWG::kNullHandle;
    };

    namespace Detail
    {
        inline bool Finite(double v) { return std::isfinite(v) && std::abs(v) < 1e12; }

        // 文字流在基类之后依次存放派生类的字符串
        struct TextStream
        {
            MiniDWG::DwgBitReader Reader;
            explicit TextStream(const MiniDWG::RawDwgData& data) : Reader(data.Text, data.Version, MiniDWG::Codec::CodePage::Gbk) {}
            std::string Next() { return Reader.ReadVariableText(); }
        };
    }

    // 按 TDbDimension2 的私有布局版本 1 的明文分支解析，
    // 必须恰好读完主数据流。版本 2 起数值以编码形式存放，按不支持处理
    inline bool DecodeDimension(const MiniDWG::UnknownEntity& entity, TchDimension& dimension, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_DIMENSION2") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (data.Version < CadVersion::AC1021 || data.Main.size() < (data.MainBits + 7) / 8)
            return fail("unsupported DWG version for native dimension");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        if (r.ReadByte() != 1) return fail("unsupported native dimension version");

        TchDimension d;
        d.Scale = base.Scale;
        d.Position = {r.ReadBitDouble(), r.ReadBitDouble(), r.ReadBitDouble()};
        d.Angle = r.ReadBitDouble();
        d.Offset = r.ReadBitDouble();
        // 句柄流：标注样式
        const int spans = r.ReadBitShort();
        if (spans < 1 || spans > 4096) return fail("invalid dimension span count");
        for (int i = 0; i < spans && !r.Failed(); ++i) d.Spans.push_back(r.ReadBitDouble());
        d.Flags = r.ReadBitShort();
        if (d.Flags & 2)                                // 各段文字位置（尚未用于显示）
        {
            const int n = r.ReadBitShort();
            if (n < 0 || n > 4096) return fail("invalid dimension text position count");
            for (int i = 0; i < n && !r.Failed(); ++i) { r.ReadBitShort(); r.ReadRawDouble(); r.ReadRawDouble(); }
        }
        std::vector<int> overrides;
        if (d.Flags & 4)                                // 替代文字：段号 + 文字流中的字符串
        {
            const int n = r.ReadBitShort();
            if (n < 0 || n > 4096) return fail("invalid dimension text override count");
            for (int i = 0; i < n && !r.Failed(); ++i) overrides.push_back(r.ReadBitShort());
        }
        // 标志 0x01：句柄流中多一个句柄
        if (d.Flags & 0x10)
        {
            d.ArcRadius = r.ReadBitDouble();
            d.MeasuredRadius = r.ReadBitDouble();
        }
        if (d.Flags & 0x80)
        {
            const int n = r.ReadBitShort();
            if (n < 0 || n > 4096) return fail("invalid dimension extension count");
            for (int i = 0; i < n && !r.Failed(); ++i) r.ReadBitDouble();
        }
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native dimension fields do not consume the payload exactly");

        const double values[] = {d.Position.X, d.Position.Y, d.Position.Z, d.Angle, d.Offset, d.Scale, d.ArcRadius, d.MeasuredRadius};
        for (double v : values)
            if (!Detail::Finite(v)) return fail("nonfinite dimension geometry");
        for (double v : d.Spans)
            if (!Detail::Finite(v)) return fail("nonfinite dimension span");
        if (d.Scale <= 0) return fail("invalid dimension scale");
        if (d.IsArc() && (d.ArcRadius <= 0 || d.MeasuredRadius <= 0)) return fail("invalid arc dimension radius");

        if (!overrides.empty())
        {
            Detail::TextStream text(data);
            for (int i = 0; i < base.TextCount; ++i) text.Next();
            for (int index : overrides) d.TextOverrides[index] = text.Next();
            if (text.Reader.Failed()) return fail("dimension text stream is incomplete");
        }
        DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
        for (int i = 0; i < base.HandleCount; ++i) handles.ReadHandle(entity.ObjectHandle);
        d.DimStyle = handles.ReadHandle(entity.ObjectHandle);
        if (handles.Failed() || handles.PositionInBits() > data.HandleBits)
            return fail("dimension handle stream is incomplete");
        dimension = std::move(d);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 按 TDbAxisLabelSet 的私有布局版本 < 7 解析，
    // 必须恰好读完主数据流。只接受模式 0（两端都标轴号，唯一核对过的样例）
    inline bool DecodeAxisLabel(const MiniDWG::UnknownEntity& entity, TchAxisLabel& label, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_AXIS_LABEL") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (data.Version < CadVersion::AC1021 || data.Main.size() < (data.MainBits + 7) / 8)
            return fail("unsupported DWG version for native axis label");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 6) return fail("unsupported native axis label version");
        const int mode = version < 3 ? r.ReadByte() : r.ReadBitShort();

        TchAxisLabel a;
        a.Scale = base.Scale;
        // 句柄流：文字样式
        const double x0 = r.ReadRawDouble(), y0 = r.ReadRawDouble();
        const double x1 = r.ReadRawDouble(), y1 = r.ReadRawDouble();
        a.Start = {x0, y0, 0};
        a.End = {x1, y1, 0};
        r.ReadBitDouble();
        const int count = version == 6 ? r.ReadBitShort() : r.ReadByte();
        if (count < 1 || count > 4096) return fail("invalid axis count");
        a.Axes.resize(static_cast<std::size_t>(count));
        for (auto& axis : a.Axes)
        {
            const int flags = version < 3 ? r.ReadByte() : r.ReadBitShort();
            // 文字流：编号
            axis.Spacing = r.ReadBitDouble();
            if (version > 2)
            {
                if (flags & 0x80) r.ReadBitDouble();
                if (flags & 0x100) r.ReadBitDouble();
            }
            if (version > 4)
            {
                if (flags & 0x400) r.ReadBitDouble();
                if (flags & 0x800) r.ReadBitDouble();
            }
        }
        r.ReadBitDouble();
        a.StartExtension = r.ReadBitDouble();
        a.EndExtension = version > 2 ? r.ReadBitDouble() : a.StartExtension;
        a.Radius = r.ReadBitDouble();
        // 版本 > 1：句柄流中多一个句柄（图层）
        if (version > 3) r.ReadBitShort();
        if (version > 4) r.ReadBitShort();
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native axis label fields do not consume the payload exactly");
        if (mode != 0) return fail("unsupported axis label mode");

        const double values[] = {x0, y0, x1, y1, a.StartExtension, a.EndExtension, a.Radius, a.Scale};
        for (double v : values)
            if (!Detail::Finite(v)) return fail("nonfinite axis label geometry");
        for (const auto& axis : a.Axes)
            if (!Detail::Finite(axis.Spacing)) return fail("nonfinite axis spacing");
        if (a.Scale <= 0 || a.Radius < 0 || std::hypot(x1 - x0, y1 - y0) < 1e-9) return fail("invalid axis label geometry");

        Detail::TextStream text(data);
        for (int i = 0; i < base.TextCount; ++i) text.Next();
        for (auto& axis : a.Axes) axis.Label = text.Next();
        if (text.Reader.Failed()) return fail("axis label text stream is incomplete");
        DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
        for (int i = 0; i < base.HandleCount; ++i) handles.ReadHandle(entity.ObjectHandle);
        a.TextStyle = handles.ReadHandle(entity.ObjectHandle);
        if (handles.Failed() || handles.PositionInBits() > data.HandleBits)
            return fail("axis label handle stream is incomplete");
        label = std::move(a);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // TCH_RADIUSDIM 半径标注：从圆上一点沿半径方向引出的箭头引线与 "R" 文字
    struct TchRadiusDim
    {
        MiniDWG::XYZ    Center;             // 圆心
        double          TipX = 0, TipY = 0; // 箭头尖：圆上的点
        double          Leader = 0;         // 引线长度（图面单位，乘以 Scale）；负值朝圆外，正值朝圆心
        int             Flags = 0;
        std::string     TextOverride;       // 标志 0x04
        bool            HasTextPosition = false;    // 标志 0x02：文字中心
        double          TextX = 0, TextY = 0;
        double          Scale = 1;
        MiniDWG::Handle DimStyle = MiniDWG::kNullHandle;
    };

    // 按 TDbRadiusDim 的私有布局版本 1 的明文分支解析，
    // 必须恰好读完主数据流。版本 2 为编码分支，按不支持处理
    inline bool DecodeRadiusDim(const MiniDWG::UnknownEntity& entity, TchRadiusDim& dimension, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_RADIUSDIM") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (data.Version < CadVersion::AC1021 || data.Main.size() < (data.MainBits + 7) / 8)
            return fail("unsupported DWG version for native radius dimension");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        if (r.ReadByte() != 1) return fail("unsupported native radius dimension version");

        TchRadiusDim d;
        d.Scale = base.Scale;
        d.Flags = r.ReadBitShort();
        // 句柄流：标注样式
        d.Leader = r.ReadBitDouble();
        d.Center = {r.ReadBitDouble(), r.ReadBitDouble(), r.ReadBitDouble()};
        d.TipX = r.ReadRawDouble();
        d.TipY = r.ReadRawDouble();
        // 标志 0x04：替代文字在文字流
        if (d.Flags & 2)
        {
            d.HasTextPosition = true;
            d.TextX = r.ReadRawDouble();
            d.TextY = r.ReadRawDouble();
        }
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native radius dimension fields do not consume the payload exactly");
        const double values[] = {d.Center.X, d.Center.Y, d.Center.Z, d.TipX, d.TipY, d.Leader, d.Scale, d.TextX, d.TextY};
        for (double v : values)
            if (!Detail::Finite(v)) return fail("nonfinite radius dimension geometry");
        if (d.Scale <= 0 || std::hypot(d.TipX - d.Center.X, d.TipY - d.Center.Y) < 1e-9)
            return fail("invalid radius dimension geometry");
        if (d.Flags & 4)
        {
            Detail::TextStream text(data);
            for (int i = 0; i < base.TextCount; ++i) text.Next();
            d.TextOverride = text.Next();
            if (text.Reader.Failed()) return fail("radius dimension text stream is incomplete");
        }
        DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
        for (int i = 0; i < base.HandleCount; ++i) handles.ReadHandle(entity.ObjectHandle);
        d.DimStyle = handles.ReadHandle(entity.ObjectHandle);
        if (handles.Failed() || handles.PositionInBits() > data.HandleBits)
            return fail("radius dimension handle stream is incomplete");
        dimension = std::move(d);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // ── 显示 ─────────────────────────────────────────────────────────────
    // 生成的图元都是世界坐标，与天正 2014 分解结果逐项核对（docs/Tch-compatibility.md）

    struct TchDimStyle      // 标注样式中用到的值（图面单位）
    {
        double ExtOffset = 3, ExtExtension = 2.5, ArrowSize = 1, TextHeight = 3.5, Gap = 1;
        int    Decimals = 0, AngularDecimals = 2;
    };

    struct TchGraphics
    {
        struct Point { double X = 0, Y = 0; };
        struct Segment { Point A, B; };
        struct Tick { Point A, B; double Width = 0; };                         // 有宽度的斜线
        struct Arc { Point Center; double Radius = 0, Start = 0, End = 0; };
        struct Circle { Point Center; double Radius = 0; };
        // Layer：0 为对象自身的图层，1 为对象另行引用的符号图层（如楼梯箭头所在的 DIM_SYMB）
        struct Triangle { Point A, B, C; int Layer = 0; };                      // 实心箭头
        struct PathPoint { Point P; double Bulge = 0; };                        // Bulge：到下一点的凸度
        struct Path { std::vector<PathPoint> Points; double Width = 0; int Layer = 0; };   // 开放多段线，Width 为全程线宽
        // Center 为对齐点；Attachment 同 MTEXT 的附着点（5 正中、4 左中、6 右中）
        // Style：0 为对象的主文字样式，1 为第二文字样式（如索引符号旁注用的标注文字样式）
        // Color 为 ACI 颜色号（天正分解出的文字多为 7 号色）
        struct Text { Point Center; double Height = 0, Rotation = 0; std::string Value; int Attachment = 5; int Style = 0; int Color = 7; int Layer = 0; };

        std::vector<Segment>  Lines;
        std::vector<Tick>     Ticks;
        std::vector<Arc>      Arcs;
        std::vector<Circle>   Circles;
        std::vector<Triangle> Arrows;
        std::vector<Path>     Paths;
        std::vector<Text>     Texts;
    };

    namespace Detail
    {
        inline std::string FormatNumber(double v, int decimals)
        {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.*f", std::clamp(decimals, 0, 8), v);
            return buf;
        }

        // 文字方向朝左时转 180°，保持可读（样例都朝右，转向规则未核对）
        inline bool Upright(double& rotation)
        {
            constexpr double pi = std::numbers::pi;
            rotation = std::remainder(rotation, 2 * pi);
            if (rotation > pi / 2 + 1e-9 || rotation <= -pi / 2 + 1e-9)
            {
                rotation = std::remainder(rotation + pi, 2 * pi);
                return true;
            }
            return false;
        }
    }

    inline TchGraphics BuildDimension(const TchDimension& d, const TchDimStyle& style)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double s = d.Scale;
        const double exo = style.ExtOffset * s, exe = style.ExtExtension * s, asz = style.ArrowSize * s;
        const double ux = std::cos(d.Angle), uy = std::sin(d.Angle);
        const double nx = -uy, ny = ux;                     // 左法线

        if (!d.IsArc())
        {
            const double off = d.Offset * s;
            const double side = off < 0 ? -1.0 : 1.0;
            std::vector<double> stations{0};
            for (double span : d.Spans) stations.push_back(stations.back() + span);
            auto at = [&](double t, double o) { return P{d.Position.X + ux * t + nx * o, d.Position.Y + uy * t + ny * o}; };
            for (double t : stations)
            {
                g.Lines.push_back({at(t, side * exo), at(t, off + side * exe)});
                const P c = at(t, off);
                const double hx = (ux + nx) * asz / 2, hy = (uy + ny) * asz / 2;   // 45° 斜线，边长为箭头大小
                g.Ticks.push_back({{c.X + hx, c.Y + hy}, {c.X - hx, c.Y - hy}, 0.4 * s});
            }
            g.Lines.push_back({at(0, off), at(stations.back(), off)});
            double rotation = d.Angle;
            const double up = Detail::Upright(rotation) ? -1.0 : 1.0;
            const double lift = style.TextHeight * s / 2 + style.Gap * s;
            for (std::size_t i = 0; i < d.Spans.size(); ++i)
            {
                const auto it = d.TextOverrides.find(static_cast<int>(i));
                std::string value = it != d.TextOverrides.end() ? it->second : Detail::FormatNumber(std::abs(d.Spans[i]), style.Decimals);
                g.Texts.push_back({at((stations[i] + stations[i + 1]) / 2, off + up * lift), style.TextHeight * 0.85 * s, rotation, std::move(value)});
            }
            return g;
        }

        // 弧形：圆心在第一个点的左法线方向 MeasuredRadius 处，各段按逆时针排列
        const P center{d.Position.X + nx * d.MeasuredRadius, d.Position.Y + ny * d.MeasuredRadius};
        const double r = d.ArcRadius;
        const double dir = r < d.MeasuredRadius ? -1.0 : 1.0;   // 从被标注点到尺寸线的径向
        std::vector<double> angles{d.Angle - std::numbers::pi / 2};
        for (double span : d.Spans) angles.push_back(angles.back() + span);
        auto polar = [&](double a, double radius) { return P{center.X + std::cos(a) * radius, center.Y + std::sin(a) * radius}; };
        // 偏移小于 DIMEXO 时界线仍从被标注点外 DIMEXO 处画起（越过尺寸线），与天正分解结果一致
        for (double a : angles)
            g.Lines.push_back({polar(a, d.MeasuredRadius + dir * exo), polar(a, r + dir * exe)});
        const double delta = asz < 2 * r ? 2 * std::asin(asz / (2 * r)) : 0;   // 箭头弦长为箭头大小
        const double textHeight = style.TextHeight * 0.85 * s;
        for (std::size_t i = 0; i < d.Spans.size(); ++i)
        {
            const double a0 = angles[i], a1 = angles[i + 1];
            g.Arcs.push_back({center, r, a0 + delta, a1 - delta});
            for (const auto [tip, toward] : {std::pair{a0, a0 + delta}, std::pair{a1, a1 - delta}})
            {
                const P t = polar(tip, r), b = polar(toward, r);
                const double len = std::hypot(b.X - t.X, b.Y - t.Y);
                if (len <= 0) continue;
                const double px = -(b.Y - t.Y) / len * asz / 6, py = (b.X - t.X) / len * asz / 6;
                g.Arrows.push_back({{b.X + px, b.Y + py}, {b.X - px, b.Y - py}, t});
            }
            const double mid = (a0 + a1) / 2;
            double rotation = mid + std::numbers::pi / 2;
            const double up = Detail::Upright(rotation) ? -1.0 : 1.0;
            const auto it = d.TextOverrides.find(static_cast<int>(i));
            std::string value = it != d.TextOverrides.end()
                ? it->second : Detail::FormatNumber(std::abs(d.Spans[i]) * 180 / std::numbers::pi, style.AngularDecimals) + "\xC2\xB0";
            g.Texts.push_back({polar(mid, r - up * (textHeight / 2 + style.Gap * s)), textHeight, rotation, std::move(value)});
        }
        return g;
    }

    // 半径标注（与天正 2014 对 7 个变体的分解结果核对）：引线从箭头尖沿半径方向画 |Leader| × 比例；
    // 箭头长 2 × DIMASZ × 比例、底宽为其 0.33；文字在读者上方 (DIMTXT / 2 + DIMGAP) × 比例，
    // 读者方向的末端距引线末端 DIMGAP × 比例；有文字位置（标志 0x02）时文字以该点为中心
    inline TchGraphics BuildRadiusDim(const TchRadiusDim& d, const TchDimStyle& style)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double s = d.Scale;
        const double radius = std::hypot(d.TipX - d.Center.X, d.TipY - d.Center.Y);
        double ux = (d.TipX - d.Center.X) / radius, uy = (d.TipY - d.Center.Y) / radius;
        if (d.Leader > 0) { ux = -ux; uy = -uy; }
        const double length = std::abs(d.Leader) * s;
        const P tip{d.TipX, d.TipY};
        auto at = [&](double t, double o) { return P{tip.X + ux * t - uy * o, tip.Y + uy * t + ux * o}; };
        const double arrow = std::min(2 * style.ArrowSize * s, length);
        if (length > arrow) g.Lines.push_back({at(arrow, 0), at(length, 0)});
        if (arrow > 0) g.Arrows.push_back({at(arrow, 0.165 * arrow), at(arrow, -0.165 * arrow), tip});

        std::string value = (d.Flags & 4) ? d.TextOverride : "R" + Detail::FormatNumber(radius, style.Decimals);
        double rotation = std::atan2(uy, ux);
        const bool flipped = Detail::Upright(rotation);
        const double height = style.TextHeight * 0.85 * s;
        if (d.HasTextPosition)
            g.Texts.push_back({{d.TextX, d.TextY}, height, rotation, std::move(value), 5});
        else
        {
            const double lift = (style.TextHeight / 2 + style.Gap) * s;
            g.Texts.push_back({at(length - style.Gap * s, flipped ? -lift : lift), height, rotation, std::move(value), flipped ? 4 : 6});
        }
        return g;
    }

    inline TchGraphics BuildAxisLabel(const TchAxisLabel& a)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double s = a.Scale;
        const double len = std::hypot(a.End.X - a.Start.X, a.End.Y - a.Start.Y);
        const double dx = (a.End.X - a.Start.X) / len, dy = (a.End.Y - a.Start.Y) / len;
        const double rx = dy, ry = -dx;                     // 右法线：轴线依次排列的方向
        const double radius = a.Radius * s;
        double offset = 0;
        for (const auto& axis : a.Axes)
        {
            const P p0{a.Start.X + rx * offset, a.Start.Y + ry * offset};
            const P p1{a.End.X + rx * offset, a.End.Y + ry * offset};
            for (const auto& [p, sign, ext] : {std::tuple{p0, -1.0, a.StartExtension * s}, std::tuple{p1, 1.0, a.EndExtension * s}})
            {
                const P q{p.X + sign * dx * ext, p.Y + sign * dy * ext};
                const P c{q.X + sign * dx * radius, q.Y + sign * dy * radius};
                g.Lines.push_back({p, q});
                g.Circles.push_back({c, radius});
                g.Texts.push_back({c, radius * 1.25, 0, axis.Label});
            }
            offset += axis.Spacing;
        }
        return g;
    }
}
