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
        std::map<int, std::pair<double, double>> TextPositions;   // 标志 0x02：段号 → 文字另行指定的位置
        double              Scale = 1;
        MiniDWG::Handle     DimStyle = MiniDWG::kNullHandle;

        bool IsArc() const { return (Flags & 0x10) != 0; }
    };

    // TCH_AXIS_LABEL 轴号：一组平行轴线两端的引出线、圆圈和编号
    struct TchAxisLabel
    {
        // 每根轴的标志（内核 TDbAxisLabelSet::ExplodeOrDraw2d / GetAxis / GetLabelCenter）：
        // 0x02 / 0x04 不标起点端 / 终点端；0x08 / 0x10 起点端轴号圆斜向错开（+135° / −135°），0x20 / 0x40 终点端同样；
        // 0x80 / 0x100 起点端 / 终点端引出长度另设（图面单位）；0x400 / 0x800 起点 / 终点沿轴线向内缩进（世界单位）
        struct Axis
        {
            double Spacing = 0;                 // 到下一根轴线的距离
            int    Flags = 0;
            double StartLeader = 0, EndLeader = 0, StartTrim = 0, EndTrim = 0;
            std::string Label;
        };
        enum : int { HideStart = 0x04, HideEnd = 0x08, SkipFirst = 0x40 };   // Mode 中影响显示的位（0x80 只是不自动排序）

        MiniDWG::XYZ      Start, End;           // 第一根轴线的两端
        std::vector<Axis> Axes;
        int               Mode = 0;
        double            StartExtension = 0;   // 起点端引出线长度（图面单位）
        double            EndExtension = 0;     // 终点端引出线长度（图面单位）
        double            Radius = 0;           // 轴号圆半径（图面单位）
        double            Scale = 1;
        bool              ShowText = true;      // 末尾 BS 的第 0 位为 0 时隐藏全部编号
        MiniDWG::Handle   TextStyle = MiniDWG::kNullHandle;
        MiniDWG::Handle   TextLayer = MiniDWG::kNullHandle;     // 编号文字所在的图层（天正默认 AXIS_TEXT），编号随层
    };

    namespace Detail
    {
        inline bool Finite(double v) { return std::isfinite(v) && std::abs(v) < 1e12; }
    }

    // 按 TDbDimension2 的私有布局版本 1 的明文分支解析，
    // 必须恰好读完主数据流。版本 2 起数值以编码形式存放，按不支持处理
    inline bool DecodeDimension(const MiniDWG::UnknownEntity& entity, TchDimension& dimension, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_DIMENSION2") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for native dimension");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        if (r.ReadByte() != 1) return fail("unsupported native dimension version");
        Detail::TextStream text(data, r);
        for (int i = 0; i < base.TextCount; ++i) text.Next();

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
        if (d.Flags & 2)                                // 各段文字位置：每项 BS 段号、RD x、RD y
        {
            const int n = r.ReadBitShort();
            if (n < 0 || n > 4096) return fail("invalid dimension text position count");
            for (int i = 0; i < n && !r.Failed(); ++i)
            {
                const int index = r.ReadBitShort();
                const double x = r.ReadRawDouble(), y = r.ReadRawDouble();
                d.TextPositions[index] = {x, y};
            }
        }
        if (d.Flags & 4)                                // 替代文字：每项 BS 段号、字符串
        {
            const int n = r.ReadBitShort();
            if (n < 0 || n > 4096) return fail("invalid dimension text override count");
            for (int i = 0; i < n && !r.Failed(); ++i)
            {
                const int index = r.ReadBitShort();
                d.TextOverrides[index] = text.Next();
            }
            if (text.Reader.Failed()) return fail("dimension text stream is incomplete");
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

        DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
        for (int i = 0; i < base.HandleCount; ++i) handles.ReadHandle(entity.ObjectHandle);
        d.DimStyle = handles.ReadHandle(entity.ObjectHandle);
        if (handles.Failed() || handles.PositionInBits() > data.HandleBits)
            return fail("dimension handle stream is incomplete");
        dimension = std::move(d);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 按 TDbAxisLabelSet 的私有布局版本 < 7 解析（CADReader 0x6c5af0，内核 TDbAxisLabelSet::dwgInFields），
    // 必须恰好读完主数据流。模式位 0x01（弧形轴网）与第一根轴为圆弧（凸度不为 0）的情况不支持
    inline bool DecodeAxisLabel(const MiniDWG::UnknownEntity& entity, TchAxisLabel& label, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_AXIS_LABEL") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for native axis label");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 6) return fail("unsupported native axis label version");
        Detail::TextStream text(data, r);
        for (int i = 0; i < base.TextCount; ++i) text.Next();
        const int mode = version < 3 ? r.ReadByte() : r.ReadBitShort();

        TchAxisLabel a;
        a.Scale = base.Scale;
        // 句柄流：文字样式
        const double x0 = r.ReadRawDouble(), y0 = r.ReadRawDouble();
        const double x1 = r.ReadRawDouble(), y1 = r.ReadRawDouble();
        a.Start = {x0, y0, 0};
        a.End = {x1, y1, 0};
        const double bulge = r.ReadBitDouble();     // 第一根轴线的凸度
        const int count = version == 6 ? r.ReadBitShort() : r.ReadByte();
        if (count < 1 || count > 4096) return fail("invalid axis count");
        a.Axes.resize(static_cast<std::size_t>(count));
        for (auto& axis : a.Axes)
        {
            const int flags = version < 3 ? r.ReadByte() : r.ReadBitShort();
            axis.Flags = flags;
            axis.Label = text.Next();                   // 编号
            axis.Spacing = r.ReadBitDouble();
            if (version > 2)
            {
                if (flags & 0x80) axis.StartLeader = r.ReadBitDouble();
                if (flags & 0x100) axis.EndLeader = r.ReadBitDouble();
            }
            else axis.Flags &= ~0x180;
            if (version > 4)
            {
                if (flags & 0x400) axis.StartTrim = r.ReadBitDouble();
                if (flags & 0x800) axis.EndTrim = r.ReadBitDouble();
            }
            else axis.Flags &= ~0xc00;
        }
        r.ReadBitDouble();
        a.StartExtension = r.ReadBitDouble();
        a.EndExtension = version > 2 ? r.ReadBitDouble() : a.StartExtension;
        a.Radius = r.ReadBitDouble();
        // 版本 > 1：句柄流中多一个句柄（编号文字的图层，见下）
        if (version > 3) r.ReadBitShort();
        if (version > 4) a.ShowText = (r.ReadBitShort() & 1) != 0;
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native axis label fields do not consume the payload exactly");
        a.Mode = mode;
        if (mode & 1) return fail("arc axis net labels are not supported");
        if (mode & ~0xcc) return fail("unsupported axis label mode");
        if (std::abs(bulge) >= 1e-6) return fail("arc axis labels are not supported");

        const double values[] = {x0, y0, x1, y1, a.StartExtension, a.EndExtension, a.Radius, a.Scale};
        for (double v : values)
            if (!Detail::Finite(v)) return fail("nonfinite axis label geometry");
        for (const auto& axis : a.Axes)
            if (!Detail::Finite(axis.Spacing) || !Detail::Finite(axis.StartLeader) || !Detail::Finite(axis.EndLeader) ||
                !Detail::Finite(axis.StartTrim) || !Detail::Finite(axis.EndTrim))
                return fail("nonfinite axis spacing");
        if (a.Scale <= 0 || a.Radius < 0 || std::hypot(x1 - x0, y1 - y0) < 1e-9) return fail("invalid axis label geometry");

        if (text.Reader.Failed()) return fail("axis label text stream is incomplete");
        DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
        for (int i = 0; i < base.HandleCount; ++i) handles.ReadHandle(entity.ObjectHandle);
        a.TextStyle = handles.ReadHandle(entity.ObjectHandle);
        // 编号文字的图层（内核 TDbAxisLabelSet::GetTextLayerId；版本 < 2 时为默认的 AXIS_TEXT 图层，不存句柄）
        if (version > 1) a.TextLayer = handles.ReadHandle(entity.ObjectHandle);
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
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for native radius dimension");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        if (r.ReadByte() != 1) return fail("unsupported native radius dimension version");
        Detail::TextStream text(data, r);
        for (int i = 0; i < base.TextCount; ++i) text.Next();

        TchRadiusDim d;
        d.Scale = base.Scale;
        d.Flags = r.ReadBitShort();
        // 句柄流：标注样式
        d.Leader = r.ReadBitDouble();
        d.Center = {r.ReadBitDouble(), r.ReadBitDouble(), r.ReadBitDouble()};
        d.TipX = r.ReadRawDouble();
        d.TipY = r.ReadRawDouble();
        if (d.Flags & 4) d.TextOverride = text.Next();  // 替代文字
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
        if (text.Reader.Failed()) return fail("radius dimension text stream is incomplete");
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
        // 文字颜色 DIMCLRT（ACI；0 随块、256 随层）：8#楼-A平面图 的 _TCH_ARCH 为随层，天正 + AutoCAD 里尺寸数字与尺寸线同色
        int    TextColor = 7;
        // 角度去零 DIMAZIN：1 去掉整数部分的 0，2 去掉小数末尾的零（8#楼 为 2：90.00° 显示为 90°，100.55° 不变）
        int    AngularZeros = 0;
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
        // Color 为 ACI 颜色号（0 随块、256 随层；默认 7 号色）。尺寸标注的取标注样式的 DIMCLRT，轴号编号随文字图层；
        // 索引、引出、箭头、剖切、指北针、楼梯的文字在天正里就是 7 号色（改了它们的文字图层颜色也不变，8#楼-A平面图 核对）
        // WidthFactor：大于 0 时按此宽度因子显示（天正把混排文字拆成西文、汉字两段，西文宽度因子 0.6 / 0.85）
        struct Text { Point Center; double Height = 0, Rotation = 0; std::string Value; int Attachment = 5; int Style = 0; int Color = 7; int Layer = 0; double WidthFactor = 0; };

        std::vector<Segment>  Lines;
        std::vector<Tick>     Ticks;
        std::vector<Arc>      Arcs;
        std::vector<Circle>   Circles;
        std::vector<Triangle> Arrows;
        std::vector<Path>     Paths;
        std::vector<Text>     Texts;
        int LineColor = 256;        // Lines 的 ACI 颜色，256 为随层（天正给排水井标注的引线固定为 6 号色）
    };

    namespace Detail
    {
        inline std::string FormatNumber(double v, int decimals)
        {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.*f", std::clamp(decimals, 0, 8), v);
            return buf;
        }

        // 按 DIMAZIN / DIMZIN 的位去零：2 去掉小数末尾的零（全是零时连小数点一起），1 去掉整数部分的 0（"0.50" → ".50"）
        inline std::string SuppressZeros(std::string s, int zeros)
        {
            if ((zeros & 2) && s.find('.') != std::string::npos)
            {
                while (s.back() == '0') s.pop_back();
                if (s.back() == '.') s.pop_back();
            }
            if ((zeros & 1) && s.size() > 1 && s[0] == '0' && s[1] == '.') s.erase(0, 1);
            return s;
        }

        // 文字方向朝左时转 180°，保持可读（样例都朝右，转向规则未核对）
        inline bool Upright(double& rotation)
        {
            constexpr double pi = std::numbers::pi;
            rotation = std::remainder(rotation, 2 * pi);
            // 竖直方向带浮点误差时一律按自下而上读（8#楼-A平面图 的竖向尺寸 93F105 方向比 π/2 大 2e-9，天正仍自下而上，文字在左侧）
            if (rotation > pi / 2 + 1e-6 || rotation <= -pi / 2 + 1e-6)
            {
                rotation = std::remainder(rotation + pi, 2 * pi);
                return true;
            }
            return false;
        }
    }

    // 第 i 段文字的中心：标志 0x02 另行指定了位置（天正里拖动过的文字，如挤不下时移到尺寸线外侧、上一行或下方）时取该点，
    // 否则为按样式算出的 fallback（8#楼-A平面图 的电梯井尺寸：未移动的文字恰在尺寸线上方 (DIMTXT / 2 + DIMGAP) × 比例处，
    // 移动过的在其上一行、尺寸线下方对称处或界线外侧）
    inline TchGraphics::Point TextCenter(const TchDimension& d, std::size_t i, TchGraphics::Point fallback)
    {
        const auto it = d.TextPositions.find(static_cast<int>(i));
        return it == d.TextPositions.end() ? fallback : TchGraphics::Point{it->second.first, it->second.second};
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
                const P center = TextCenter(d, i, at((stations[i] + stations[i + 1]) / 2, off + up * lift));
                g.Texts.push_back({center, style.TextHeight * 0.85 * s, rotation, std::move(value), 5, 0, style.TextColor});
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
                ? it->second
                : Detail::SuppressZeros(Detail::FormatNumber(std::abs(d.Spans[i]) * 180 / std::numbers::pi, style.AngularDecimals),
                                        style.AngularZeros) + "\xC2\xB0";
            g.Texts.push_back({TextCenter(d, i, polar(mid, r - up * (textHeight / 2 + style.Gap * s))), textHeight, rotation, std::move(value),
                               5, 0, style.TextColor});
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
            g.Texts.push_back({{d.TextX, d.TextY}, height, rotation, std::move(value), 5, 0, style.TextColor});
        else
        {
            const double lift = (style.TextHeight / 2 + style.Gap) * s;
            g.Texts.push_back({at(length - style.Gap * s, flipped ? -lift : lift), height, rotation, std::move(value), flipped ? 4 : 6, 0,
                               style.TextColor});
        }
        return g;
    }

    // 轴号（内核 TDbAxisLabelSet::ExplodeOrDraw2d）：第 i 根轴为第一根轴向右法线平移前 i 个间距之和，标志 0x400 / 0x800 再把两端向内缩进；
    // 每个标注端从轴线端点向外引出（引出长度 × 比例），轴号圆在引出线外侧相切、编号居中；
    // 斜向错开时圆心在引出线末端沿“向内方向 ± 135°”的 √2 × 半径处，另画从圆边到引出线末端的短线（起点端引出长度为 0 时不画）。
    // 8#楼立剖面、墙身大样共 16 个轴号（含模式 0x08、0x88，标志 0x800 等）的线与圆和天正 2014 分解结果一致
    inline TchGraphics BuildAxisLabel(const TchAxisLabel& a)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double s = a.Scale;
        const double len = std::hypot(a.End.X - a.Start.X, a.End.Y - a.Start.Y);
        const double dx = (a.End.X - a.Start.X) / len, dy = (a.End.Y - a.Start.Y) / len;
        const double rx = dy, ry = -dx;                     // 右法线：轴线依次排列的方向
        const double radius = a.Radius * s;
        constexpr double diagonal = 3 * std::numbers::pi / 4;
        double offset = 0;
        for (std::size_t i = 0; i < a.Axes.size(); ++i)
        {
            const auto& axis = a.Axes[i];
            const double here = offset;
            offset += axis.Spacing;
            if (i == 0 && (a.Mode & TchAxisLabel::SkipFirst)) continue;
            P p0{a.Start.X + rx * here, a.Start.Y + ry * here};
            P p1{a.End.X + rx * here, a.End.Y + ry * here};
            if ((axis.Flags & 0x400) && axis.StartTrim < len + 1e-6) p0 = {p0.X + dx * axis.StartTrim, p0.Y + dy * axis.StartTrim};
            if ((axis.Flags & 0x800) && axis.EndTrim < len + 1e-6) p1 = {p1.X - dx * axis.EndTrim, p1.Y - dy * axis.EndTrim};
            struct Side { P Base; double InX, InY; bool Hidden; double Leader; int Plus, Minus; };
            const Side sides[] = {
                {p0, dx, dy, (a.Mode & TchAxisLabel::HideStart) || (axis.Flags & 0x02),
                 (axis.Flags & 0x80) ? axis.StartLeader * s : a.StartExtension * s, 0x08, 0x10},
                {p1, -dx, -dy, (a.Mode & TchAxisLabel::HideEnd) || (axis.Flags & 0x04),
                 (axis.Flags & 0x100) ? axis.EndLeader * s : a.EndExtension * s, 0x20, 0x40}};
            for (const auto& side : sides)
            {
                if (side.Hidden) continue;
                const P q{side.Base.X - side.InX * side.Leader, side.Base.Y - side.InY * side.Leader};
                if (std::abs(side.Leader) > 1e-9) g.Lines.push_back({side.Base, q});
                P c{q.X - side.InX * radius, q.Y - side.InY * radius};
                if (axis.Flags & (side.Plus | side.Minus))
                {
                    const double angle = std::atan2(side.InY, side.InX) + ((axis.Flags & side.Plus) ? diagonal : -diagonal);
                    const double ux = std::cos(angle), uy = std::sin(angle);
                    c = {q.X + ux * std::numbers::sqrt2 * radius, q.Y + uy * std::numbers::sqrt2 * radius};
                    if (a.StartExtension > 1e-6) g.Lines.push_back({{c.X - ux * radius, c.Y - uy * radius}, q});
                }
                g.Circles.push_back({c, radius});
                if (!a.ShowText || axis.Label.empty()) continue;
                // 附加轴号“分子/分母”（内核按 '/' 拆成上下两段）：圆内画水平直径，两段居中在圆心上下 0.457 × 半径，
                // 字高约 0.62 × 半径（8#楼墙身大样的分解结果核对）
                // 编号放在文字图层（图层 2：TchAxisLabel::TextLayer）上、随层（8#楼-A平面图 在天正 + AutoCAD 里与轴号圆同色，不是白色）
                if (const auto slash = axis.Label.find('/'); slash != std::string::npos)
                {
                    g.Lines.push_back({{c.X - radius, c.Y}, {c.X + radius, c.Y}});
                    g.Texts.push_back({{c.X, c.Y + 0.457 * radius}, 0.62 * radius, 0, axis.Label.substr(0, slash), 5, 0, 256, 2});
                    g.Texts.push_back({{c.X, c.Y - 0.457 * radius}, 0.62 * radius, 0, axis.Label.substr(slash + 1), 5, 0, 256, 2});
                }
                else
                {
                    // 天正用 Fit 对齐，字高随字数变化（8#楼各图的分解结果：单字约 1.24 × 半径，两字约 0.97 × 半径）
                    std::size_t chars = 0;
                    for (unsigned char ch : axis.Label) chars += (ch & 0xc0) != 0x80;
                    const double height = chars <= 1 ? 1.24 * radius : 1.94 * radius / static_cast<double>(chars);
                    g.Texts.push_back({c, height, 0, axis.Label, 5, 0, 256, 2});
                }
            }
        }
        return g;
    }
}
