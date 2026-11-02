#pragma once
#include "Import/TchDimension.h"
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <numbers>
#include <vector>

namespace MiniCAD::Tch
{
    // 天正符号类（坐标、箭头、索引等）的公共基类。
    // 天正基类之后：RC 版本；句柄（文字样式）；BD 文字高度（图面单位）；版本 ≥ 2：BD、句柄（图层）；版本 > 2：BS。只支持版本 1～3
    struct TchSymbolBase
    {
        TchBase Base;
        double  TextHeight = 3.5;
        int     HandleCount = 0;    // 天正基类与符号基类在句柄流中的句柄数
    };

    inline bool ReadTchSymbolBase(MiniDWG::DwgBitReader& r, TchSymbolBase& symbol)
    {
        symbol = {};
        if (!ReadTchBase(r, symbol.Base)) return false;
        const int version = r.ReadByte();
        if (version < 1 || version > 3) return false;
        symbol.TextHeight = r.ReadBitDouble();
        symbol.HandleCount = symbol.Base.HandleCount + 1;
        if (version >= 2) { r.ReadBitDouble(); ++symbol.HandleCount; }
        if (version > 2) r.ReadBitShort();
        return !r.Failed();
    }

    // TCH_COORD 坐标标注：引线 + 水平线 + 上下两行坐标文字
    struct TchCoord
    {
        double Scale = 1, TextHeight = 3.5;
        int    Format = 0;          // 第 4～7 位：小数位数；第 0 位：标签用 A= / B=（否则 X= / Y=）
        int    Side = 0;            // 0 水平线朝右，1 朝左
        double PointX = 0, PointY = 0;      // 被标注点
        double ValueX = 0, ValueY = 0;      // 坐标值（米）：显示时 X 取 ValueY、Y 取 ValueX（测量坐标）
        double LeaderX = 0, LeaderY = 0;    // 引线末端，水平线从这里画起
        double TextGap = -1000;     // 文字到水平线的距离（× 字高）；-1000 为自动（0.3）
        double LineLength = 100000; // 水平线长度（× 比例）；100000 为自动（按文字宽度）
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle;
    };

    // TCH_ARROW 箭头：多段线，最后一段末端为实心箭头，尾端或首段旁标注文字
    struct TchArrow
    {
        struct Vertex { double X = 0, Y = 0, Bulge = 0; };
        double Scale = 1, TextHeight = 3.5;
        int    Mode = 1;            // 1：第一个文字放在尾端外侧；2：两个文字在首段中点上下；3：第一个文字沿首段、从尾端起写在线上方
        std::vector<Vertex> Vertices;   // 少于 2 个顶点或闭合时天正不显示
        double ArrowLength = 3;     // 图面单位
        int    Head = 0;            // 箭头样式：0 有宽度的多段线、1 实心三角形（显示相同），4 及以上不画箭头
        bool   HasText2 = false;    // 为 false 时不画第二个文字
        std::string Text1, Text2;
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle;
    };

    // TCH_SYMB_SECTION 剖切符号。旧样式：剖切线各转折点处的粗短线、两端的投射方向线与编号；
    // 新国标样式（GB/T 50001-2017）：细剖切线，两端各一个内视符号（圆、朝投射方向的实心三角、圆内编号）
    struct TchSection
    {
        double Scale = 1, TextHeight = 5;
        int    Kind = 1;            // 1：剖面（两端画投射方向线）；0：断面（不画；新样式起点画一段粗线）
        double ViewAngle = 0;       // 投射方向（弧度）
        double EndViewAngle = 0;    // 终点的投射方向：IndexFlags 含 8（剖切线两端不垂直）时另存，否则同 ViewAngle
        std::vector<TchGraphics::Point> Points;
        std::string Label;          // 两端都用这个编号
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle;
        // 以下只有新版（T30 加密对象）有
        bool   NewStyle = false;    // 基类扩展数据表里的样式开关：非 0 为新国标样式
        std::string Label2;         // 索引图号：新样式且 IndexFlags 含 2 或 4 时写在圆内水平线下
        int    IndexFlags = 0;      // 位 1、2：起点 / 终点有索引图号；位 3：两端不垂直
        double Diameter = 10;       // 新样式的圆直径（图面单位）
        double TextRatio = 0;       // 新样式编号大小与半径之比，0 取图纸设置（同内视符号）
        int    TextMode = 2;        // 新样式编号大小的算法，2 取图纸设置（同内视符号）
    };

    // TCH_INDEXPOINTER 索引符号：引线、圆圈（编号 / 图号）与可选的上下旁注
    struct TchIndexPointer
    {
        double Scale = 1, TextHeight = 3.5;
        int    Kind = 0;            // 0：索引点画小圆；1：剖切索引，画粗线
        double PointX = 0, PointY = 0;      // 索引点
        double CornerX = 0, CornerY = 0;    // 引线转折点，水平段从这里画到圆圈
        double Radius = 5;          // 圆圈半径（图面单位）
        double Run = 0;             // 水平段长度（图面单位）
        double Mark = 0;            // 类型 0：小圆直径（世界单位）；类型 1：粗线长度（图面单位），负值在引线左侧
        std::string Number, Sheet, Above, Below;
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle;   // 圆圈内文字
        MiniDWG::Handle NoteStyle = MiniDWG::kNullHandle;   // 上下旁注（属性包中的“标注文字样式ID”）
    };

    // TCH_DRAWINGNAME 图名：图名文字、比例文字与图名下划线，整体以插入点居中
    struct TchDrawingName
    {
        double Scale = 1;
        double X = 0, Y = 0;                // 插入点：文字基线上、整体水平居中
        std::string Name, ScaleText;
        double NameHeight = 7, ScaleHeight = 5;     // 图面单位
        double LineWidth = 0.7;             // 下划线线宽（图面单位）
        double Gap = 0.6;                   // 图名与比例之间的距离（× 图名字高）
        bool   ShowScale = true;
        int    LineMode = 1;                // 0：双线（粗线下再加一条细线）；1、2：单下划线
        int    Color = 7;                   // 文字颜色（ACI）
        MiniDWG::Handle NameStyle = MiniDWG::kNullHandle, ScaleStyle = MiniDWG::kNullHandle;
    };

    // TCH_TDBINSIGHT 内视符号：圆、外切于圆的实心三角（编号多于一个时为菱形）、分隔线与圆内的编号
    struct TchInSight
    {
        double Scale = 1;
        int    Type = 0;            // 0：编号分布在圆内上下左右；非 0：一个三角、水平线上下两段文字（Number、Text2；未核对）
        double X = 0, Y = 0;        // 圆心
        double Radius = 5;          // 图面单位
        double Angle = -std::numbers::pi / 2;   // 三角朝向减 π/2；编号的位置随 Angle + π/2 绕圆心转，文字保持水平
        double TextRatio = 0;       // 编号大小与半径之比，绝对值小于 0.001 时取图纸设置（TchConfig::IndexTextRatio）
        int    TextMode = 2;        // 编号大小的算法：2 取图纸设置（TchConfig::IndexTextMode）；0 按圆内放得下的大小
        std::string Number, Bottom, Left, Right;    // 转角为 0 时在上、下、左、右的编号
        std::string Text2;
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle;
    };

    namespace Detail
    {
        // 新版天正（基类版本 9～14，见 TchCipher.h）的符号基类版本 4：RC 版本、RC 密钥；
        // 文字样式（句柄）、字高、数值、图层（句柄）、颜色
        inline bool ReadCipherSymbolBase(CipherReader& f, TchSymbolBase& symbol, MiniDWG::Handle& style)
        {
            symbol = {};
            if (!f.ReadBase() || f.Main.ReadByte() != 4) return false;
            f.ReadKey();
            symbol.Base.Version = 11;
            symbol.Base.Scale = f.Scale;
            style = f.Handle();
            symbol.TextHeight = f.Double();
            f.Double();
            f.Handle();
            f.Short();
            return f.Ok();
        }

        // 明文多段线：RC 闭合、BS 顶点数，每个顶点 2RD + BD 凸度
        template <class Vertex>
        inline bool ReadPoly2D(MiniDWG::DwgBitReader& r, bool& closed, std::vector<Vertex>* vertices)
        {
            closed = r.ReadByte() != 0;
            const int count = r.ReadBitShort();
            if (count < 0 || count > 4096) return false;
            for (int i = 0; i < count && !r.Failed(); ++i)
            {
                const double x = r.ReadRawDouble(), y = r.ReadRawDouble(), bulge = r.ReadBitDouble();
                if (vertices) vertices->push_back({x, y, bulge});
            }
            return !r.Failed();
        }

        inline bool ReadSymbolHandles(const MiniDWG::UnknownEntity& entity, const TchSymbolBase& symbol, MiniDWG::Handle& style)
        {
            using namespace MiniDWG;
            const auto& data = entity.Raw.Dwg;
            DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
            for (int i = 0; i < symbol.Base.HandleCount; ++i) handles.ReadHandle(entity.ObjectHandle);
            style = handles.ReadHandle(entity.ObjectHandle);
            return !handles.Failed() && handles.PositionInBits() <= data.HandleBits;
        }

        // 天正 _TCH_DIM（SIMPLEX）字符宽度，单位为字高的 1/21：除最后一个字符取前进量外，最后一个取墨迹宽度。
        // 数值由 AutoCAD textbox 逐字符测得，坐标文字 14 个变体的分解结果全部吻合；表外字符按数字估计
        inline double SimplexWidth(const std::string& text)
        {
            struct Metric { char C; int Advance, Ink; };
            static constexpr std::array<Metric, 18> table{{
                {'0', 20, 14}, {'1', 16, 5}, {'2', 20, 14}, {'3', 20, 14}, {'4', 20, 15}, {'5', 20, 14}, {'6', 20, 13},
                {'7', 20, 14}, {'8', 20, 14}, {'9', 20, 13}, {'X', 20, 14}, {'Y', 20, 16}, {'=', 26, 18}, {'-', 26, 18},
                {'.', 10, 2}, {'R', 21, 14}, {'A', 22, 16}, {'B', 21, 14}}};
            double units = 0;
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                Metric m{text[i], 20, 14};
                for (const auto& t : table)
                    if (t.C == text[i]) m = t;
                units += i + 1 < text.size() ? m.Advance : m.Ink;
            }
            return units / 21.0;
        }

        // AutoCAD txt.shx 的字符宽度，单位为字高的 1/6（前进量、墨迹宽度）；txt 不含汉字，汉字按“?”显示与计宽。
        // 由 AutoCAD textbox 测得，图名 11 个变体的文字宽度全部吻合
        inline double TxtWidth(const std::string& text)
        {
            double units = 0;
            int last = 0;
            for (std::size_t i = 0; i < text.size();)
            {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                const std::size_t len = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
                int advance = 6, ink = 4;                       // 字母、2～9、/、- 等
                if (c >= 0x80 || c == '?' || c == '0') { advance = 5; ink = 3; }
                else if (c == '1') { advance = 4; ink = 2; }
                else if (c == ':' || c == '.') { advance = 2; ink = 0; }
                else if (c == ' ') { advance = 6; ink = 0; }
                units += advance;
                last = advance - ink;
                i += len;
            }
            return text.empty() ? 0 : (units - last) / 6.0;
        }

        inline bool HasWideChar(const std::string& text)
        {
            for (char ch : text)
                if (static_cast<unsigned char>(ch) >= 0x80) return true;
            return false;
        }

        // GBENOR（西文）+ GBCBIG（汉字）的 textbox：宽度为墨迹右缘减左缘，Bottom 为墨迹最低点（相对基线），单位为字高。
        // 由 AutoCAD 逐字符测得（前进量、墨迹左右缘）；汉字取实测平均值，个别字形会差约 0.01 字高
        inline void GbenorExtent(const std::string& text, double& width, double& bottom)
        {
            struct Metric { char C; double Advance, Left, Right; };
            static constexpr std::array<Metric, 21> table{{
                {'0', 0.58333, 0.125, 0.45899}, {'1', 0.41667, 0.125, 0.29167}, {'2', 0.58333, 0.10384, 0.50838},
                {'3', 0.58333, 0.125, 0.49285}, {'4', 0.66667, 0.125, 0.54167}, {'5', 0.58333, 0.125, 0.49285},
                {'6', 0.58333, 0.07513, 0.49967}, {'7', 0.58333, 0.125, 0.45833}, {'8', 0.58333, 0.125, 0.45833},
                {'9', 0.58624, 0.09339, 0.51800}, {':', 0.25, 0.125, 0.125}, {'.', 0.25, 0.125, 0.125},
                {'?', 0.58333, 0.12238, 0.49195}, {'/', 0.58333, 0.125, 0.45833}, {'-', 0.58333, 0.125, 0.45833},
                {'A', 0.75, 0.125, 0.625}, {'B', 0.66667, 0.125, 0.54167}, {'C', 0.58333, 0.08617, 0.45833},
                {'a', 0.58333, 0.09911, 0.47917}, {'c', 0.5, 0.09480, 0.375}, {' ', 0.5, 0.0, 0.0}}};
            double cursor = 0, left = 0, right = 0;
            bool first = true, wide = false;
            for (std::size_t i = 0; i < text.size();)
            {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                const std::size_t len = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
                Metric m{static_cast<char>(c), 0.58333, 0.125, 0.45833};
                if (c >= 0x80) { m = {0, 0.72794, 0.05515, 0.67279}; wide = true; }
                else
                    for (const auto& t : table)
                        if (t.C == static_cast<char>(c)) m = t;
                if (first) left = cursor + m.Left;
                right = cursor + m.Right;
                cursor += m.Advance;
                first = false;
                i += len;
            }
            width = text.empty() ? 0 : right - left;
            bottom = wide ? -0.06618 : 0.0;
        }

        // 按字体估算 textbox（字高 1、宽度因子 1）：txt、simplex、gbenor 用实测表，其余按每字 0.8、汉字 1.0 粗估
        struct TextExtent { double Width = 0, Bottom = 0; };
        inline TextExtent EstimateExtent(const std::string& fontFile, const std::string& text)
        {
            std::string font;
            for (char ch : fontFile) font.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            TextExtent e;
            if (font == "txt" || font.starts_with("txt.")) e.Width = TxtWidth(text);
            else if (font.starts_with("simplex")) e.Width = SimplexWidth(text);
            else if (font.starts_with("gbenor")) GbenorExtent(text, e.Width, e.Bottom);
            else
                for (std::size_t i = 0; i < text.size();)
                {
                    const unsigned char c = static_cast<unsigned char>(text[i]);
                    e.Width += c < 0x80 ? 0.8 : 1.0;
                    i += c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
                }
            return e;
        }

        // MiniCAD 自己显示时的文字宽度（字高 1，按字形步进累加，与 TextLayoutEngine 相同）：
        // 程序只带 simplex、tssdeng、TSSDCHN，其余 SHX 主字体显示为 simplex，大字体显示为 TSSDCHN（FontEngine 的替代规则）。
        // tssdeng 与 simplex 字形相同、单位高 33 / 28；TSSDCHN 汉字步进为 1
        inline TextExtent DisplayExtent(const std::string& fontFile, const std::string& text)
        {
            static constexpr double simplex[95] = {
                0.67857, 0.46429, 0.53571, 0.67857, 0.71429, 0.78571, 0.92857, 0.32143, 0.5, 0.5, 0.57143, 0.92857, 0.35714, 0.92857,
                0.35714, 0.78571, 0.71429, 0.57143, 0.71429, 0.71429, 0.71429, 0.71429, 0.71429, 0.71429, 0.71429, 0.71429, 0.53571,
                0.5, 0.71429, 0.92857, 0.71429, 0.64286, 0.85714, 0.78571, 0.75, 0.75, 0.75, 0.67857, 0.64286, 0.75, 0.78571, 0.28571,
                0.57143, 0.75, 0.60714, 0.85714, 0.78571, 0.78571, 0.75, 0.78571, 0.75, 0.71429, 0.64286, 0.78571, 0.71429, 0.85714,
                0.71429, 0.71429, 0.71429, 0.53571, 0.78571, 0.42857, 0.71429, 0.85714, 0.32143, 0.67857, 0.67857, 0.64286, 0.67857,
                0.64286, 0.42857, 0.67857, 0.67857, 0.28571, 0.35714, 0.60714, 0.28571, 1.07143, 0.67857, 0.67857, 0.67857, 0.67857,
                0.46429, 0.60714, 0.5, 0.67857, 0.57143, 0.78571, 0.60714, 0.57143, 0.60714, 0.42857, 0.25, 0.39286, 0.92857};
            std::string font;
            for (char ch : fontFile) font.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            const double ascii = font.starts_with("tssdeng") ? 28.0 / 33.0 : 1.0;
            TextExtent e;
            bool wide = false;
            for (std::size_t i = 0; i < text.size();)
            {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                if (c >= 0x80) { e.Width += 1.0; wide = true; }
                else if (c >= 32 && c < 127) e.Width += simplex[c - 32] * ascii;
                i += c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
            }
            e.Bottom = wide ? -0.06618 : 0.0;
            return e;
        }
    }

    namespace Detail
    {
        inline const char* CheckCoord(const TchCoord& c)
        {
            const double values[] = {c.Scale, c.TextHeight, c.PointX, c.PointY, c.ValueX, c.ValueY, c.LeaderX, c.LeaderY, c.TextGap, c.LineLength};
            for (double v : values)
                if (!Finite(v)) return "nonfinite coordinate geometry";
            if (c.Scale <= 0 || c.TextHeight <= 0 || c.Side > 1) return "invalid coordinate settings";
            return nullptr;
        }

        // 新版天正的坐标标注版本 7：符号基类后 RC 版本、RC 密钥；格式、方向、被标注点、坐标值、引线末端（二维点）、
        // 字节、文字间距、水平线长、字节、二维点、字符串
        inline bool DecodeCoordCipher(const MiniDWG::UnknownEntity& entity, TchCoord& coord, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            TchSymbolBase symbol;
            TchCoord c;
            if (!ReadCipherSymbolBase(f, symbol, c.TextStyle)) return fail("unsupported Tianzheng symbol base layout");
            if (f.Main.ReadByte() != 7) return fail("unsupported native coordinate version");
            f.ReadKey();
            c.Scale = symbol.Base.Scale;
            c.TextHeight = symbol.TextHeight;
            c.Format = f.Byte();
            c.Side = f.Byte();
            const auto point = f.Point2(), value = f.Point2(), leader = f.Point2();
            c.PointX = point.X; c.PointY = point.Y;
            c.ValueX = value.X; c.ValueY = value.Y;
            c.LeaderX = leader.X; c.LeaderY = leader.Y;
            f.Byte();
            c.TextGap = f.Double();
            c.LineLength = f.Double();
            f.Byte();
            f.Point2();
            f.Text();
            if (!f.Exact()) return fail("native coordinate fields do not consume the payload exactly");
            if (const char* why = CheckCoord(c)) return fail(why);
            coord = c;
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    // 按坐标标注的私有布局版本 < 7 的明文分支解析，
    // 必须恰好读完主数据流。只接受版本 4～5（之后的版本多一段文字与点，未核对）
    inline bool DecodeCoord(const MiniDWG::UnknownEntity& entity, TchCoord& coord, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_COORD") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeCoordCipher(entity, coord, diagnostic);
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for native coordinate");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchSymbolBase symbol;
        if (!ReadTchSymbolBase(r, symbol)) return fail("unsupported Tianzheng symbol base layout");
        const int version = r.ReadByte();
        if (version < 4 || version > 5) return fail("unsupported native coordinate version");
        TchCoord c;
        c.Scale = symbol.Base.Scale;
        c.TextHeight = symbol.TextHeight;
        c.Format = r.ReadByte();
        c.Side = r.ReadByte();
        c.PointX = r.ReadRawDouble(); c.PointY = r.ReadRawDouble();
        c.ValueX = r.ReadRawDouble(); c.ValueY = r.ReadRawDouble();
        c.LeaderX = r.ReadRawDouble(); c.LeaderY = r.ReadRawDouble();
        r.ReadByte();
        c.TextGap = r.ReadBitDouble();
        if (version >= 5) c.LineLength = r.ReadBitDouble();
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native coordinate fields do not consume the payload exactly");
        if (const char* why = Detail::CheckCoord(c)) return fail(why);
        if (!Detail::ReadSymbolHandles(entity, symbol, c.TextStyle)) return fail("coordinate handle stream is incomplete");
        coord = c;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    namespace Detail
    {
        // 闭合或少于 2 个顶点时天正不显示；文字模式 0、4 与箭头样式 2、3 没有样例
        inline const char* FinishArrow(TchArrow& a, bool closed)
        {
            if (a.Vertices.size() < 2 || closed) a.Vertices.clear();
            if (a.Mode < 1 || a.Mode > 3) return "unsupported arrow text mode";
            if (a.Head == 2 || a.Head == 3) return "unsupported arrow head style";
            const double values[] = {a.Scale, a.TextHeight, a.ArrowLength};
            for (double v : values)
                if (!Finite(v)) return "nonfinite arrow settings";
            for (const auto& v : a.Vertices)
                if (!Finite(v.X) || !Finite(v.Y) || !Finite(v.Bulge) || std::abs(v.Bulge) > 1e6) return "nonfinite arrow vertex";
            if (a.Scale <= 0 || a.TextHeight <= 0 || a.ArrowLength < 0) return "invalid arrow settings";
            return nullptr;
        }

        // 新版天正的箭头引注版本 6：符号基类后 RC 版本、RC 密钥；文字模式、第一个文字、三维点、明文多段线、
        // 箭头长、箭头样式、数值、是否有第二个文字、第二个文字、三维点
        inline bool DecodeArrowCipher(const MiniDWG::UnknownEntity& entity, TchArrow& arrow, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            TchSymbolBase symbol;
            TchArrow a;
            if (!ReadCipherSymbolBase(f, symbol, a.TextStyle)) return fail("unsupported Tianzheng symbol base layout");
            if (f.Main.ReadByte() != 6) return fail("unsupported native arrow version");
            f.ReadKey();
            a.Scale = symbol.Base.Scale;
            a.TextHeight = symbol.TextHeight;
            a.Mode = f.Byte();
            a.Text1 = f.Text();
            f.Point3();
            bool closed = false;
            if (!ReadPoly2D(f.Main, closed, &a.Vertices)) return fail("invalid arrow vertex count");
            a.ArrowLength = f.Double();
            a.Head = f.Byte();
            f.Double();
            a.HasText2 = f.Bool();
            a.Text2 = f.Text();
            f.Point3();
            if (!f.Exact()) return fail("native arrow fields do not consume the payload exactly");
            if (const char* why = FinishArrow(a, closed)) return fail(why);
            arrow = std::move(a);
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    // 按箭头引注的私有布局版本 < 6 的明文分支解析，
    // 多段线布局（RC 闭合、BS 顶点数、每顶点 2RD + BD 凸度）。只接受版本 4～5、文字模式 1～3（模式 0、4 没有样例）
    inline bool DecodeArrow(const MiniDWG::UnknownEntity& entity, TchArrow& arrow, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_ARROW") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeArrowCipher(entity, arrow, diagnostic);
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for native arrow");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchSymbolBase symbol;
        if (!ReadTchSymbolBase(r, symbol)) return fail("unsupported Tianzheng symbol base layout");
        Detail::TextStream text(data, r);
        for (int i = 0; i < symbol.Base.TextCount; ++i) text.Next();
        const int version = r.ReadByte();
        if (version < 4 || version > 5) return fail("unsupported native arrow version");
        TchArrow a;
        a.Scale = symbol.Base.Scale;
        a.TextHeight = symbol.TextHeight;
        a.Mode = r.ReadByte();
        a.Text1 = text.Next();
        // 3BD 为文字位置（相对于按模式确定的局部坐标，空箭头中可能为 NaN），显示按模式的规则排布
        r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
        bool closed = false;
        if (!Detail::ReadPoly2D(r, closed, &a.Vertices)) return fail("invalid arrow vertex count");
        a.ArrowLength = r.ReadBitDouble();
        a.Head = r.ReadByte();
        r.ReadBitDouble();
        a.HasText2 = r.ReadBit() != 0;
        a.Text2 = text.Next();
        r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native arrow fields do not consume the payload exactly");
        if (const char* why = Detail::FinishArrow(a, closed)) return fail(why);
        if (text.Reader.Failed()) return fail("arrow text stream is incomplete");
        if (!Detail::ReadSymbolHandles(entity, symbol, a.TextStyle)) return fail("arrow handle stream is incomplete");
        arrow = std::move(a);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    namespace Detail
    {
        inline const char* CheckSection(const TchSection& s)
        {
            if (s.Points.size() < 2) return "section symbol needs at least two points";
            if (s.Kind != 0 && s.Kind != 1) return "unsupported section symbol kind";
            if (!Finite(s.Scale) || s.Scale <= 0 || !Finite(s.TextHeight) || !Finite(s.ViewAngle) || !Finite(s.EndViewAngle))
                return "invalid section symbol settings";
            if (s.NewStyle && (!Finite(s.Diameter) || s.Diameter <= 0 || !Finite(s.TextRatio))) return "invalid section symbol settings";
            for (const auto& p : s.Points)
                if (!Finite(p.X) || !Finite(p.Y)) return "nonfinite section symbol point";
            return nullptr;
        }

        // 新版天正的剖切符号版本 3：符号基类后 RC 版本、RC 密钥；类型、投射方向、点数与各点（二维点）、
        // 编号、2 个编号位置、索引图号、2 个索引位置、索引字高、索引标志、索引文字样式（句柄）、数值、终点投射方向、
        // 转角文字的点数与各点、数值、剖切线比例、圆直径、编号大小算法、编号比例
        inline bool DecodeSectionCipher(const MiniDWG::UnknownEntity& entity, TchSection& section, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            TchSymbolBase symbol;
            TchSection s;
            if (!ReadCipherSymbolBase(f, symbol, s.TextStyle)) return fail("unsupported Tianzheng symbol base layout");
            if (f.Main.ReadByte() != 3) return fail("unsupported native section symbol version");
            f.ReadKey();
            s.Scale = symbol.Base.Scale;
            s.TextHeight = symbol.TextHeight;
            s.Kind = f.Byte();
            s.ViewAngle = f.Double();
            for (int i = 0, n = f.Byte(); i < n && f.Ok(); ++i)
            {
                const auto p = f.Point2();
                s.Points.push_back({p.X, p.Y});
            }
            s.Label = f.Text();
            f.Point2(); f.Point2();
            s.Label2 = f.Text();
            f.Point2(); f.Point2();
            f.Double();
            s.IndexFlags = f.Short();
            f.Handle();
            f.Double();
            const double endView = f.Double();
            s.EndViewAngle = s.IndexFlags & 8 ? endView : s.ViewAngle;
            for (int i = 0, n = f.Byte(); i < n && f.Ok(); ++i) f.Point2();
            f.Short(); f.Double();
            s.Diameter = f.Double();
            s.TextMode = f.Byte();
            s.TextRatio = f.Double();
            const auto style = f.XData.find("T20d37");
            s.NewStyle = style != f.XData.end() && style->second != 0;
            if (!f.Exact()) return fail("native section symbol fields do not consume the payload exactly");
            if (const char* why = CheckSection(s)) return fail(why);
            section = std::move(s);
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    // 按版本 < 3 的明文分支解析，必须恰好读完主数据流
    inline bool DecodeSection(const MiniDWG::UnknownEntity& entity, TchSection& section, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_SYMB_SECTION") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeSectionCipher(entity, section, diagnostic);
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for native section symbol");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchSymbolBase symbol;
        if (!ReadTchSymbolBase(r, symbol)) return fail("unsupported Tianzheng symbol base layout");
        Detail::TextStream text(data, r);
        for (int i = 0; i < symbol.Base.TextCount; ++i) text.Next();
        const int version = r.ReadByte();
        if (version < 1 || version > 2) return fail("unsupported native section symbol version");
        TchSection s;
        s.Scale = symbol.Base.Scale;
        s.TextHeight = symbol.TextHeight;
        s.Kind = r.ReadByte();
        s.ViewAngle = s.EndViewAngle = r.ReadBitDouble();
        const int count = r.ReadByte();
        if (count < 2) return fail("section symbol needs at least two points");
        for (int i = 0; i < count && !r.Failed(); ++i) s.Points.push_back({r.ReadRawDouble(), r.ReadRawDouble()});
        s.Label = text.Next();                          // 编号
        for (int i = 0; i < 4; ++i) r.ReadRawDouble();
        if (version > 1)
        {
            s.Label2 = text.Next();                     // 索引图号（旧样式不显示）
            for (int i = 0; i < 4; ++i) r.ReadRawDouble();
            r.ReadBitDouble();
            s.IndexFlags = static_cast<std::uint16_t>(r.ReadBitShort());
            // 句柄流：一个句柄
            r.ReadBitDouble();
            const double endView = r.ReadBitDouble();
            if (s.IndexFlags & 8) s.EndViewAngle = endView;
        }
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native section symbol fields do not consume the payload exactly");
        if (const char* why = Detail::CheckSection(s)) return fail(why);
        if (text.Reader.Failed()) return fail("section symbol text stream is incomplete");
        if (!Detail::ReadSymbolHandles(entity, symbol, s.TextStyle)) return fail("section symbol handle stream is incomplete");
        section = std::move(s);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    namespace Detail
    {
        inline const char* CheckIndexPointer(const TchIndexPointer& p)
        {
            if (p.Kind != 0 && p.Kind != 1) return "unsupported index symbol kind";
            const double values[] = {p.Scale, p.TextHeight, p.PointX, p.PointY, p.CornerX, p.CornerY, p.Radius, p.Run, p.Mark};
            for (double v : values)
                if (!Finite(v)) return "nonfinite index symbol geometry";
            if (p.Scale <= 0 || p.Radius <= 0) return "invalid index symbol settings";
            return nullptr;
        }

        // 新版天正的索引符号版本 5：符号基类后 RC 版本、RC 密钥；类型、索引点、转折点（二维点）、圆半径、
        // 水平段长、索引点标记；编号、图号、上旁注、下旁注；两组个数与字符串；2 个数值、字节、明文多段线、数值、字符串、句柄。
        // 旁注样式同明文分支：基类属性包里的最后一个句柄，没有时与圆圈文字相同
        inline bool DecodeIndexPointerCipher(const MiniDWG::UnknownEntity& entity, TchIndexPointer& pointer, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            TchSymbolBase symbol;
            TchIndexPointer p;
            if (!ReadCipherSymbolBase(f, symbol, p.TextStyle)) return fail("unsupported Tianzheng symbol base layout");
            if (f.Main.ReadByte() != 5) return fail("unsupported native index symbol version");
            f.ReadKey();
            p.Scale = symbol.Base.Scale;
            p.TextHeight = symbol.TextHeight;
            p.Kind = f.Byte();
            const auto point = f.Point2(), corner = f.Point2();
            p.PointX = point.X; p.PointY = point.Y;
            p.CornerX = corner.X; p.CornerY = corner.Y;
            p.Radius = f.Double();
            p.Run = f.Double();
            p.Mark = f.Double();
            p.Number = f.Text(); p.Sheet = f.Text(); p.Above = f.Text(); p.Below = f.Text();
            for (int list = 0; list < 2; ++list)
                for (int i = 0, n = f.Byte(); i < n && f.Ok(); ++i) f.Text();
            f.Double(); f.Double(); f.Byte();
            bool closed = false;
            if (!ReadPoly2D<TchArrow::Vertex>(f.Main, closed, nullptr)) return fail("invalid index symbol polyline");
            f.Double();
            f.Text();
            f.Handle();
            if (!f.Exact()) return fail("native index symbol fields do not consume the payload exactly");
            if (const char* why = CheckIndexPointer(p)) return fail(why);
            p.NoteStyle = f.LastBaseHandle != MiniDWG::kNullHandle ? f.LastBaseHandle : p.TextStyle;
            pointer = std::move(p);
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    // 按版本 < 5 的明文分支解析，必须恰好读完主数据流。
    // 只接受版本 3～4（版本 2 及以前缺少后段，未核对）
    inline bool DecodeIndexPointer(const MiniDWG::UnknownEntity& entity, TchIndexPointer& pointer, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_INDEXPOINTER") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeIndexPointerCipher(entity, pointer, diagnostic);
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (data.Version < CadVersion::AC1021 || data.Main.size() < (data.MainBits + 7) / 8)
            return fail("unsupported DWG version for native index symbol");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchSymbolBase symbol;
        if (!ReadTchSymbolBase(r, symbol)) return fail("unsupported Tianzheng symbol base layout");
        const int version = r.ReadByte();
        if (version < 3 || version > 4) return fail("unsupported native index symbol version");
        TchIndexPointer p;
        p.Scale = symbol.Base.Scale;
        p.TextHeight = symbol.TextHeight;
        p.Kind = r.ReadByte();
        p.PointX = r.ReadRawDouble(); p.PointY = r.ReadRawDouble();
        p.CornerX = r.ReadRawDouble(); p.CornerY = r.ReadRawDouble();
        p.Radius = r.ReadBitDouble();
        p.Run = r.ReadBitDouble();
        p.Mark = r.ReadBitDouble();
        // 文字流：编号、图号、上旁注、下旁注
        const int list1 = r.ReadByte();
        const int list2 = r.ReadByte();
        r.ReadBitDouble(); r.ReadBitDouble(); r.ReadByte();
        r.ReadByte();                                           // 多段线：闭合标志
        const int vertices = r.ReadBitShort();
        if (vertices < 0 || vertices > 4096) return fail("invalid index symbol polyline");
        for (int i = 0; i < vertices && !r.Failed(); ++i) { r.ReadRawDouble(); r.ReadRawDouble(); r.ReadBitDouble(); }
        r.ReadBitDouble();
        // 文字流：一个字符串；版本 > 3 句柄流再一个句柄
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native index symbol fields do not consume the payload exactly");
        if (const char* why = Detail::CheckIndexPointer(p)) return fail(why);
        Detail::TextStream text(data, r);
        for (int i = 0; i < symbol.Base.TextCount; ++i) text.Next();
        p.Number = text.Next(); p.Sheet = text.Next(); p.Above = text.Next(); p.Below = text.Next();
        for (int i = 0; i < list1 + list2 + 1; ++i) text.Next();
        if (text.Reader.Failed()) return fail("index symbol text stream is incomplete");
        if (!Detail::ReadSymbolHandles(entity, symbol, p.TextStyle)) return fail("index symbol handle stream is incomplete");
        // 旁注样式：属性包中的句柄（排在天正基类句柄的最后）；没有时与圆圈文字相同
        p.NoteStyle = p.TextStyle;
        if (symbol.Base.HandleCount > 0)
        {
            DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
            Handle h = kNullHandle;
            for (int i = 0; i < symbol.Base.HandleCount; ++i) h = handles.ReadHandle(entity.ObjectHandle);
            if (!handles.Failed() && h != kNullHandle) p.NoteStyle = h;
        }
        pointer = std::move(p);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    namespace Detail
    {
        inline const char* CheckDrawingName(const TchDrawingName& d)
        {
            if (d.LineMode < 0 || d.LineMode > 2) return "unsupported drawing name underline mode";
            const double values[] = {d.Scale, d.X, d.Y, d.NameHeight, d.ScaleHeight, d.LineWidth, d.Gap};
            for (double v : values)
                if (!Finite(v)) return "nonfinite drawing name settings";
            if (d.Scale <= 0 || d.NameHeight <= 0 || d.Color < 0 || d.Color > 256) return "invalid drawing name settings";
            return nullptr;
        }

        // 新版天正的图名版本 4：BS 版本、RC 密钥；三维点、图名、比例文字、数值、图名字高、比例字高、
        // 下划线宽、间距系数；两个文字样式（句柄）；显示比例、下划线方式、文字颜色；插入点（三维点，以它为准）
        inline bool DecodeDrawingNameCipher(const MiniDWG::UnknownEntity& entity, TchDrawingName& name, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadBitShort() != 4) return fail("unsupported native drawing name version");
            f.ReadKey();
            TchDrawingName d;
            d.Scale = f.Scale;
            f.Point3();
            d.Name = f.Text();
            d.ScaleText = f.Text();
            f.Double();
            d.NameHeight = f.Double();
            d.ScaleHeight = f.Double();
            d.LineWidth = f.Double();
            d.Gap = f.Double();
            d.NameStyle = f.Handle();
            d.ScaleStyle = f.Handle();
            d.ShowScale = f.Bool();
            d.LineMode = f.Short();
            d.Color = f.Short();
            const auto p = f.Point3();
            d.X = p.X; d.Y = p.Y;
            if (!f.Exact()) return fail("native drawing name fields do not consume the payload exactly");
            if (const char* why = CheckDrawingName(d)) return fail(why);
            name = std::move(d);
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    // 按版本 < 4 的明文分支解析，必须恰好读完主数据流。
    // 天正基类后：BS 版本、3BD 插入点（版本 ≥ 2 时以末尾的 3BD 为准）、图名与比例（文字流）、BD、BD 图名字高、BD 比例字高、BD 下划线宽、
    // BD 间距系数、两个文字样式句柄、B 显示比例、BS 下划线方式、BS 文字颜色、版本 ≥ 2 的 3BD 插入点
    inline bool DecodeDrawingName(const MiniDWG::UnknownEntity& entity, TchDrawingName& name, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_DRAWINGNAME") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeDrawingNameCipher(entity, name, diagnostic);
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for native drawing name");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        Detail::TextStream text(data, r);
        for (int i = 0; i < base.TextCount; ++i) text.Next();
        const int version = r.ReadBitShort();
        if (version < 1 || version > 3) return fail("unsupported native drawing name version");
        TchDrawingName d;
        d.Scale = base.Scale;
        d.X = r.ReadBitDouble(); d.Y = r.ReadBitDouble(); r.ReadBitDouble();
        d.Name = text.Next();
        d.ScaleText = text.Next();
        r.ReadBitDouble();
        d.NameHeight = r.ReadBitDouble();
        d.ScaleHeight = r.ReadBitDouble();
        d.LineWidth = r.ReadBitDouble();
        d.Gap = r.ReadBitDouble();
        d.ShowScale = r.ReadBit();
        d.LineMode = r.ReadBitShort();
        d.Color = r.ReadBitShort();
        if (version >= 2) { d.X = r.ReadBitDouble(); d.Y = r.ReadBitDouble(); r.ReadBitDouble(); }
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native drawing name fields do not consume the payload exactly");
        if (const char* why = Detail::CheckDrawingName(d)) return fail(why);
        if (text.Reader.Failed()) return fail("drawing name text stream is incomplete");
        DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
        for (int i = 0; i < base.HandleCount; ++i) handles.ReadHandle(entity.ObjectHandle);
        d.NameStyle = handles.ReadHandle(entity.ObjectHandle);
        d.ScaleStyle = handles.ReadHandle(entity.ObjectHandle);
        if (handles.Failed() || handles.PositionInBits() > data.HandleBits) return fail("drawing name handle stream is incomplete");
        name = std::move(d);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 图名（与天正 2014 对 11 个变体的分解结果核对）：图名字高 NameHeight × 比例，比例字高 ScaleHeight × 比例，两者间隔 Gap × 图名字高；
    // 显示比例时“图名 + 间隔 + 比例”整体以插入点水平居中，否则“图名 + 0.2 × 图名字高”居中；文字基线在插入点。
    // 下划线线宽 LineWidth × 比例，位于图名墨迹最低点下 2 × 线宽，从图名起点画到图名末端外 0.2 × 图名字高。
    // 下划线方式 0（双线）：粗线下 1.4 × 比例
    // 再画一条细线；显示比例时两条线都画到比例文字末端外 0.2 × 比例字高。
    // 含汉字的文字天正把字高乘以 0.96454（大字体 GBCBIG 下实测）；宽度与最低点取 textbox，间隔与下划线外伸仍按名义字高。
    // extent(文字, 实际字高, 是否比例文字) 返回 textbox 宽度与最低点（世界单位）
    // TrueType 样式（nameTrueType / scaleTrueType，建筑详图样图“宋体”图名的分解结果核对）：字高 = 名义字高 × 0.75527，
    // 文字按中西文分段排布，步进汉字 = 字高、西文 = 0.5 × 字高（与天正单行文字相同）；排布用的宽度为墨迹宽度（步进之和减去首尾留白），
    // 含汉字时墨迹最低点为 −0.145 × 字高
    inline TchGraphics BuildDrawingName(const TchDrawingName& d,
                                        const std::function<Detail::TextExtent(const std::string&, double, bool)>& extent,
                                        bool nameTrueType = false, bool scaleTrueType = false)
    {
        TchGraphics g;
        const double s = d.Scale, h1 = d.NameHeight * s, h2 = d.ScaleHeight * s, line = d.LineWidth * s;
        const double a1 = nameTrueType ? 0.75527 * h1 : Detail::HasWideChar(d.Name) ? 0.96454 * h1 : h1;
        const double a2 = scaleTrueType ? 0.75527 * h2 : Detail::HasWideChar(d.ScaleText) ? 0.96454 * h2 : h2;
        const bool scale = d.ShowScale && !d.ScaleText.empty();
        // TrueType：按天正步进分段，返回总宽
        auto runs = [&](const std::string& text, double h, double x, int style, bool emit) {
            double cursor = 0;
            std::string run;
            bool runWide = false;
            double runStart = 0;
            auto flush = [&] {
                if (emit && run.find_first_not_of(' ') != std::string::npos)
                    g.Texts.push_back({{x + runStart, d.Y}, h, 0, run, 7, style, d.Color});
                run.clear();
            };
            for (std::size_t i = 0; i < text.size();)
            {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                const std::size_t len = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
                const bool wide = c >= 0x80;
                if (!run.empty() && wide != runWide) flush();
                if (run.empty()) { runWide = wide; runStart = cursor; }
                run.append(text, i, len);
                cursor += wide ? h : 0.5 * h;
                i += len;
            }
            flush();
            return cursor;
        };
        // 图名同样取墨迹宽度（建筑详图样图实测比步进之和少约 0.12 × 字高）
        Detail::TextExtent e1 = nameTrueType ? Detail::TextExtent{runs(d.Name, a1, 0, 0, false) - 0.12 * a1, Detail::HasWideChar(d.Name) ? -0.145 * a1 : 0.0}
                                             : extent(d.Name, a1, false);
        // 比例文字取墨迹宽度：TrueType 时为步进之和减去首尾字形留白（“1:50” 实测 0.162 × 字高）
        const double w2 = !scale ? 0.0 : scaleTrueType ? runs(d.ScaleText, a2, 0, 1, false) - 0.162 * a2 : extent(d.ScaleText, a2, true).Width;
        // 居中用的总长：双线方式且显示比例时也含比例文字末端外伸的 0.2 × 比例字高
        const double total = scale ? e1.Width + d.Gap * h1 + w2 + (d.LineMode == 0 ? 0.2 * h2 : 0.0) : e1.Width + 0.2 * h1;
        const double left = d.X - total / 2;
        if (!d.Name.empty())
        {
            if (nameTrueType) runs(d.Name, a1, left, 0, true);
            else g.Texts.push_back({{left, d.Y + a1 / 2}, a1, 0, d.Name, 4, 0, d.Color});
        }
        if (scale)
        {
            const double x2 = left + e1.Width + d.Gap * h1;
            if (scaleTrueType) runs(d.ScaleText, a2, x2, 1, true);
            else g.Texts.push_back({{x2, d.Y + a2 / 2}, a2, 0, d.ScaleText, 4, 1, d.Color});
        }
        TchGraphics::Path underline;
        underline.Width = line;
        const double y = d.Y + e1.Bottom - 2 * line;
        const double length = d.LineMode == 0 && scale ? e1.Width + d.Gap * h1 + w2 + 0.2 * h2 : e1.Width + 0.2 * h1;
        underline.Points = {{{left, y}}, {{left + length, y}}};
        g.Paths.push_back(std::move(underline));
        if (d.LineMode == 0) g.Lines.push_back({{left, y - 1.4 * s}, {left + length, y - 1.4 * s}});
        return g;
    }

    // 剖切符号的旧样式（与天正 2014 对 6 个变体的分解结果核对；新国标样式见 BuildSectionNewStyle）：每个点画线宽 0.5 × 比例的粗线，
    // 沿相邻段各 10 × 比例；剖面（类型 1）两端再沿投射方向（终点用 EndViewAngle）画 6 × 比例；编号中心在端点沿投射方向
    // （6 × 比例 + 名义字高）处，字高 0.85 × 名义字高
    inline TchGraphics BuildSection(const TchSection& s)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double stroke = 10 * s.Scale, leg = s.Kind == 1 ? 6 * s.Scale : 0.0, width = 0.5 * s.Scale;
        const double nominal = s.TextHeight * s.Scale;
        const P view{std::cos(s.ViewAngle), std::sin(s.ViewAngle)}, endView{std::cos(s.EndViewAngle), std::sin(s.EndViewAngle)};
        auto toward = [&](const P& from, const P& to) {
            const double d = std::hypot(to.X - from.X, to.Y - from.Y);
            const double t = d > 0 ? std::min(stroke, d) / d : 0;
            return P{from.X + (to.X - from.X) * t, from.Y + (to.Y - from.Y) * t};
        };
        const std::size_t n = s.Points.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            const P& p = s.Points[i];
            TchGraphics::Path path;
            path.Width = width;
            if (i == 0)
            {
                if (leg > 0) path.Points.push_back({{p.X + view.X * leg, p.Y + view.Y * leg}});
                path.Points.push_back({p});
                path.Points.push_back({toward(p, s.Points[1])});
            }
            else if (i + 1 == n)
            {
                path.Points.push_back({toward(p, s.Points[i - 1])});
                path.Points.push_back({p});
                if (leg > 0) path.Points.push_back({{p.X + endView.X * leg, p.Y + endView.Y * leg}});
            }
            else
            {
                path.Points.push_back({toward(p, s.Points[i - 1])});
                path.Points.push_back({p});
                path.Points.push_back({toward(p, s.Points[i + 1])});
            }
            g.Paths.push_back(std::move(path));
        }
        if (!s.Label.empty())
            for (const auto& [p, v] : {std::pair{s.Points.front(), view}, std::pair{s.Points.back(), endView}})
                g.Texts.push_back({{p.X + v.X * (leg + nominal), p.Y + v.Y * (leg + nominal)}, 0.85 * nominal, 0, s.Label, 5});
        return g;
    }

    // 索引符号（与天正 2014 对 10 个变体的分解结果核对）：引线从索引点到转折点，水平段长 Run × 比例到圆圈，圆圈半径 Radius × 比例，
    // 横穿圆心画直径。类型 0 在索引点画直径 Mark 的小圆（引线从圆边起）；类型 1 在引线一侧 1.5 × 比例处画长 |Mark| × 比例、
    // 线宽 0.5 × 比例的粗线。上下旁注与圆圈边缘对齐，距水平段 0.3 × 名义字高。圆圈内编号 / 图号天正用 Fit 对齐，这里取正中近似
    inline TchGraphics BuildIndexPointer(const TchIndexPointer& p)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double s = p.Scale, radius = p.Radius * s, nominal = p.TextHeight * s;
        // 圆圈在转折点哪一侧与引线方向无关（引线朝左时圆圈仍在右侧）；这里按水平段长度的符号取，负值未核对
        const double dir = p.Run >= 0 ? 1.0 : -1.0;
        const P point{p.PointX, p.PointY}, corner{p.CornerX, p.CornerY};
        const P edge{corner.X + p.Run * s, corner.Y}, center{edge.X + dir * radius, edge.Y};
        const double len = std::hypot(corner.X - point.X, corner.Y - point.Y);
        const P u = len > 0 ? P{(corner.X - point.X) / len, (corner.Y - point.Y) / len} : P{dir, 0};
        if (p.Kind == 0)
        {
            const double r = std::abs(p.Mark) / 2;
            if (r > 0) g.Circles.push_back({point, r});
            if (len > r) g.Lines.push_back({{point.X + u.X * r, point.Y + u.Y * r}, corner});
        }
        else
        {
            if (len > 0) g.Lines.push_back({point, corner});
            const double side = p.Mark < 0 ? 1.5 * s : -1.5 * s;
            const P a{point.X - u.Y * side, point.Y + u.X * side};
            TchGraphics::Path mark;
            mark.Width = 0.5 * s;
            mark.Points = {{a}, {{a.X + u.X * std::abs(p.Mark) * s, a.Y + u.Y * std::abs(p.Mark) * s}}};
            g.Paths.push_back(std::move(mark));
        }
        if (p.Run != 0) g.Lines.push_back({corner, edge});
        g.Circles.push_back({center, radius});
        g.Lines.push_back({{center.X + radius, center.Y}, {center.X - radius, center.Y}});
        auto inner = [&](const std::string& value, double sign) {
            if (value.empty()) return;
            const double h = (value.size() > 1 ? 0.46 : 0.535) * radius;
            g.Texts.push_back({{center.X, center.Y + sign * 0.45 * radius}, h, 0, value, 5});
        };
        inner(p.Number, 1);
        inner(p.Sheet, -1);
        const double height = 0.85 * nominal, gap = 0.3 * nominal;
        const double x = edge.X - dir * gap;
        const int attach = dir > 0 ? 6 : 4;
        if (!p.Above.empty()) g.Texts.push_back({{x, edge.Y + gap + height / 2}, height, 0, p.Above, attach, 1});
        if (!p.Below.empty()) g.Texts.push_back({{x, edge.Y - gap - height / 2}, height, 0, p.Below, attach, 1});
        return g;
    }

    // 坐标标注（与天正 2014 对 14 个变体的分解结果核对）：字高 = 0.85 × 名义字高（TextHeight × 比例），宽高比 0.706；
    // 水平线自动长度 = 1.1 × 较宽一行文字，至少 11 × 比例；两行文字与水平线末端对齐，X 行在上、Y 行在下，距水平线 0.3 × 名义字高
    inline TchGraphics BuildCoord(const TchCoord& c)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double nominal = c.TextHeight * c.Scale, height = 0.85 * nominal;
        const int decimals = (c.Format >> 4) & 0xf;
        const bool letters = (c.Format & 1) != 0;
        std::string top = std::string(letters ? "A=" : "X=") + Detail::FormatNumber(c.ValueY, decimals);
        std::string bottom = std::string(letters ? "B=" : "Y=") + Detail::FormatNumber(c.ValueX, decimals);
        const double charWidth = height * (0.6 / 0.85);
        const double width = std::max(Detail::SimplexWidth(top), Detail::SimplexWidth(bottom)) * charWidth;
        const double length = c.LineLength < 99999 ? c.LineLength * c.Scale : std::max(1.1 * width, 11 * c.Scale);
        const double gap = c.TextGap > -999 ? c.TextGap * nominal : 0.3 * nominal;
        const double dir = c.Side == 1 ? -1.0 : 1.0;
        const P leader{c.LeaderX, c.LeaderY}, end{c.LeaderX + dir * length, c.LeaderY};
        if (std::hypot(c.LeaderX - c.PointX, c.LeaderY - c.PointY) > 1e-9)
            g.Lines.push_back({{c.PointX, c.PointY}, leader});
        g.Lines.push_back({leader, end});
        const int attach = dir > 0 ? 6 : 4;     // 右中 / 左中
        g.Texts.push_back({{end.X, end.Y + gap + height / 2}, height, 0, std::move(top), attach});
        g.Texts.push_back({{end.X, end.Y - gap - height / 2}, height, 0, std::move(bottom), attach});
        return g;
    }

    namespace Detail
    {
        // 天正注释文字的显示：西文与汉字分段，西文字高 0.85 × 名义字高、宽度因子 0.6 / 0.85，汉字字高 0.96454 × 名义字高
        //（天正 2014 分解出的 TEXT 与此相同）。各段按 MiniCAD 实际显示的字宽（simplex / TSSDCHN）首尾相接，
        // align：0 左对齐、1 居中、2 右对齐，(x, y) 为该对齐位置的基线点
        // asciiScale / asciiWidth / tzAscii：西文段的字高系数、宽度因子（0 为样式自身）、天正排布用的每字宽度（× 名义字高）；
        // 注释符号为 0.85、0.6 / 0.85、0.5714，天正单行文字为 1、0（样式）、0.604
        // tianzhengAdvance：各段也按天正步进排布，每段画出的宽度同样取天正的——按 MiniCAD 实际显示的字体（simplex / TSSDCHN）
        // 反算宽度因子。天正的大字体 GBCBIG 汉字步进约 0.728 × 字高，替代的 TSSDCHN 为 1，不反算时汉字段压到下一段上、
        // 长句冲出表格（建筑平面样图的引出标注、平面小说明）；天正的显示也是各段占满自己的步进（DN100 每字 0.4 × 名义字高）。
        // 天正上标 ^U…^U（m^U2^U 为 m²）：字高为本段的一半、抬高 0.5 × 名义字高，每字步进 0.35 × 西文步进
        //（建筑平面样图“公卫（4.56m²）”“厨房（8.58m²）”的天正 2014 分解结果核对）
        inline void AddRuns(TchGraphics& g, const std::string& text, double nominal, double x, double y, double rotation, int align,
                            int color = 7, int layer = 0, int style = 0, double asciiScale = 0.85, double asciiWidth = 0.6 / 0.85,
                            double tzAscii = 0.5714, double tzWide = 0.69, bool tianzhengAdvance = false, double wideScale = 0.96454)
        {
            struct Run { std::string Value; bool Wide = false, Sup = false; double Width = 0, Tz = 0; };
            std::vector<Run> runs;
            bool sup = false;
            for (std::size_t i = 0; i < text.size();)
            {
                if (text.compare(i, 2, "^U") == 0) { sup = !sup; i += 2; continue; }
                const unsigned char c = static_cast<unsigned char>(text[i]);
                std::size_t len = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
                // 对齐按天正自己的字宽定起点（分解结果核对：西文每字 0.5714、句点 0.114、汉字约 0.69 × 名义字高），
                // 这样文字起点与天正一致，不会因 MiniCAD 字体宽窄不同越过引线转折点等
                double tz = c >= 0x80 ? tzWide : (c == '.' || c == ',') ? 0.2 * tzAscii : tzAscii;
                // AutoCAD 控制码 %%%、%%c / %%d / %%p、%%nnn 显示为一个字符
                if (c == '%' && i + 2 < text.size() && text[i + 1] == '%')
                {
                    len = 3;
                    while (len < 5 && i + len < text.size() && std::isdigit(static_cast<unsigned char>(text[i + len])) &&
                           std::isdigit(static_cast<unsigned char>(text[i + 2]))) ++len;
                    tz = tzAscii;
                }
                const bool wide = c >= 0x80;
                if (runs.empty() || runs.back().Wide != wide || runs.back().Sup != sup) runs.push_back({{}, wide, sup});
                runs.back().Value.append(text, i, len);
                runs.back().Tz += (sup ? 0.35 : 1.0) * tz * nominal;
                i += len;
            }
            double tz = 0;
            for (auto& r : runs)
            {
                r.Width = r.Wide ? DisplayExtent("", r.Value).Width * wideScale * nominal
                                 : DisplayExtent("simplex", r.Value).Width * asciiScale * (asciiWidth > 0 ? asciiWidth : 1.0) * nominal;
                if (r.Sup) r.Width *= 0.5;
                tz += r.Tz;
            }
            double cursor = align == 1 ? -tz / 2 : align == 2 ? -tz : 0;
            const double c = std::cos(rotation), s = std::sin(rotation);
            for (const auto& r : runs)
            {
                if (r.Value.find_first_not_of(' ') != std::string::npos)
                {
                    const double height = (r.Wide ? wideScale : asciiScale) * nominal * (r.Sup ? 0.5 : 1.0);
                    const double rise = r.Sup ? 0.5 * nominal : 0.0;
                    double width = r.Wide ? 0.0 : asciiWidth;
                    if (tianzhengAdvance)
                        if (const double natural = DisplayExtent(r.Wide ? "" : "simplex", r.Value).Width * height; natural > 0) width = r.Tz / natural;
                    g.Texts.push_back({{x + cursor * c - rise * s, y + cursor * s + rise * c}, height, rotation, r.Value, 7, style, color, layer, width});
                }
                cursor += tianzhengAdvance ? r.Tz : r.Width;
            }
        }
    }

    // 箭头（与天正 2014 对 8 个变体的分解结果核对）：最后一段末端为长 ArrowLength × 比例、底宽为其 1/3.75 的实心箭头；
    // 文字高 0.9645 × 名义字高。模式 1：第一个文字在尾端外侧 0.3 × 名义字高处、垂直居中；模式 2：两个文字沿首段、在首段中点上下，
    // 距线 0.3 × 名义字高；模式 3（墙身大样样图的分解结果核对）：第一个文字沿首段方向（朝左时转 180°），在线的上方 0.3 × 名义字高，
    // 从尾端起写（转 180° 时文字末端对齐尾端），字高 0.85 × 名义字高、宽度因子 0.6 / 0.85
    inline TchGraphics BuildArrow(const TchArrow& a)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const auto& v = a.Vertices;
        const std::size_t n = v.size();
        if (n < 2) return g;
        const P tip{v[n - 1].X, v[n - 1].Y}, before{v[n - 2].X, v[n - 2].Y};
        const double last = std::hypot(tip.X - before.X, tip.Y - before.Y);
        const double head = a.Head >= 4 ? 0.0 : std::min(a.ArrowLength * a.Scale, last);
        const P base = last > 0 ? P{tip.X + (before.X - tip.X) / last * head, tip.Y + (before.Y - tip.Y) / last * head} : tip;
        TchGraphics::Path path;
        for (std::size_t i = 0; i + 1 < n; ++i) path.Points.push_back({{v[i].X, v[i].Y}, i + 2 < n ? v[i].Bulge : 0});
        path.Points.push_back({base, 0});
        if (head < last) g.Paths.push_back(std::move(path));
        if (head > 0)
        {
            const double px = -(tip.Y - base.Y) / head * head / 7.5, py = (tip.X - base.X) / head * head / 7.5;
            g.Arrows.push_back({{base.X + px, base.Y + py}, {base.X - px, base.Y - py}, tip});
        }

        const double nominal = a.TextHeight * a.Scale, height = 0.96454 * nominal, gap = 0.3 * nominal;
        if (a.Mode == 1 && !a.Text1.empty())
        {
            const bool right = v[0].X >= v[1].X;
            g.Texts.push_back({{v[0].X + (right ? gap : -gap), v[0].Y}, height, 0, a.Text1, right ? 4 : 6});
        }
        else if (a.Mode == 2)
        {
            // 文字沿首段方向（朝左时转 180°）居中于首段中点：第一个文字基线在线上方 0.3 × 名义字高，第二个文字顶部在线下方同样距离
            //（墙身大样样图 62 个坡度箭头的分解结果核对：西文字高 0.85 × 名义字高、宽度因子 0.6 / 0.85）
            const P mid{(v[0].X + v[1].X) / 2, (v[0].Y + v[1].Y) / 2};
            double rotation = std::atan2(v[1].Y - v[0].Y, v[1].X - v[0].X);
            Detail::Upright(rotation);
            const double c = std::cos(rotation), s = std::sin(rotation);
            if (!a.Text1.empty()) Detail::AddRuns(g, a.Text1, nominal, mid.X - s * gap, mid.Y + c * gap, rotation, 1);
            if (!a.Text2.empty() && a.HasText2)
            {
                const double drop = gap + (Detail::HasWideChar(a.Text2) ? height : 0.85 * nominal);
                Detail::AddRuns(g, a.Text2, nominal, mid.X + s * drop, mid.Y - c * drop, rotation, 1);
            }
        }
        else if (a.Mode == 3 && !a.Text1.empty())
        {
            double rotation = std::atan2(v[1].Y - v[0].Y, v[1].X - v[0].X);
            const bool flipped = Detail::Upright(rotation);
            const double c = std::cos(rotation), s = std::sin(rotation);
            Detail::AddRuns(g, a.Text1, nominal, v[0].X - s * gap, v[0].Y + c * gap, rotation, flipped ? 2 : 0);
        }
        return g;
    }

    namespace Detail
    {
        inline const char* CheckInSight(const TchInSight& s)
        {
            if (!Finite(s.Scale) || s.Scale <= 0 || !Finite(s.Radius) || s.Radius <= 0 || !Finite(s.Angle) || !Finite(s.TextRatio))
                return "invalid insight symbol settings";
            if (!Finite(s.X) || !Finite(s.Y)) return "nonfinite insight symbol center";
            return nullptr;
        }

        // 内视符号在符号基类之后全是明文字段：BS 类型、2 个字符串（类型非 0 的第二段文字、编号）、
        // BD3 圆心、BD 半径、BD 角度、BD、BL（为 1 时后面有另外三个编号）、RC 编号大小算法、BD 编号比例；
        // BL 为 1 时 RC 版本（≤ 1）与下、左、右三个编号。这一层没有自己的版本号
        template <class NextText>
        inline bool ReadInSightFields(MiniDWG::DwgBitReader& r, NextText&& next, TchInSight& s)
        {
            s.Type = static_cast<std::uint16_t>(r.ReadBitShort());
            s.Text2 = next();
            s.Number = next();
            const auto center = r.Read3BitDouble();
            s.X = center.X;
            s.Y = center.Y;
            s.Radius = r.ReadBitDouble();
            s.Angle = r.ReadBitDouble();
            r.ReadBitDouble();
            const int more = r.ReadBitLong();
            s.TextMode = r.ReadByte();
            s.TextRatio = r.ReadBitDouble();
            if (more == 1)
            {
                if (r.ReadByte() > 1) return false;
                s.Bottom = next();
                s.Left = next();
                s.Right = next();
            }
            return !r.Failed();
        }

        // 新版天正（基类版本 9～14）：加密的符号基类之后同上
        inline bool DecodeInSightCipher(const MiniDWG::UnknownEntity& entity, TchInSight& inSight, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            TchSymbolBase symbol;
            TchInSight s;
            if (!ReadCipherSymbolBase(f, symbol, s.TextStyle)) return fail("unsupported Tianzheng symbol base layout");
            s.Scale = symbol.Base.Scale;
            if (!ReadInSightFields(f.Main, [&] { return f.Text(); }, s)) return fail("unsupported insight symbol number version");
            if (!f.Exact()) return fail("native insight symbol fields do not consume the payload exactly");
            if (const char* why = CheckInSight(s)) return fail(why);
            inSight = std::move(s);
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    // TCH_TDBINSIGHT：新版加密对象见 DecodeInSightCipher；旧版为明文符号基类加同样的字段（没有旧版样图核对），必须恰好读完主数据流
    inline bool DecodeInSight(const MiniDWG::UnknownEntity& entity, TchInSight& inSight, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_TDBINSIGHT") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeInSightCipher(entity, inSight, diagnostic);
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for native insight symbol");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchSymbolBase symbol;
        if (!ReadTchSymbolBase(r, symbol)) return fail("unsupported Tianzheng symbol base layout");
        Detail::TextStream text(data, r);
        for (int i = 0; i < symbol.Base.TextCount; ++i) text.Next();
        TchInSight s;
        s.Scale = symbol.Base.Scale;
        if (!Detail::ReadInSightFields(r, [&] { return text.Next(); }, s)) return fail("unsupported insight symbol number version");
        if (r.PositionInBits() != data.MainBits) return fail("native insight symbol fields do not consume the payload exactly");
        if (const char* why = Detail::CheckInSight(s)) return fail(why);
        if (text.Reader.Failed()) return fail("insight symbol text stream is incomplete");
        if (!Detail::ReadSymbolHandles(entity, symbol, s.TextStyle)) return fail("insight symbol handle stream is incomplete");
        inSight = std::move(s);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 内视符号（tianzheng2.dwg 的四向符号与其他看图软件的显示对照）。
    // R = 半径 × 比例，局部轴 x、y 为世界轴转 Angle。实心区域：P − Rx 经朝 +y 的半圆（凸度 −1）到 P + Rx，再到 P + √2R·x、
    // P + √2R·y、P − √2R·x 闭合（外切于圆的三角减去半圆）；类型 0 且编号多于一个时再加关于 x 轴对称的一块，合成菱形。
    // 分隔线（编号类型按非空编号算）：2（只有上、下）沿 y 轴，3（只有左、右）沿 x 轴，其余多个编号为
    // y 轴转 45°、135° 的两条；类型非 0 为水平直径。
    // 编号是 Fit 对齐的单行文字：先在不转的局部框里按编号大小算法定宽高与位置，类型 0 再把框中心绕圆心转 Angle + π/2，
    // 文字保持水平。textWidth 为字高 1、宽度因子 1 时的显示宽度，styleWidth 为文字样式的宽度因子（天正按它量宽高比）
    inline TchGraphics BuildInSight(const TchInSight& s, double configRatio, int configMode,
                                    const std::function<double(const std::string&)>& textWidth, double styleWidth = 1.0)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double r = s.Radius * s.Scale, d = std::sqrt(2.0) * r;
        const P c{s.X, s.Y}, x{std::cos(s.Angle), std::sin(s.Angle)}, y{-x.Y, x.X};
        auto at = [&](const P& dir, double k) { return P{c.X + dir.X * k, c.Y + dir.Y * k}; };
        const int count = !s.Number.empty() + !s.Bottom.empty() + !s.Left.empty() + !s.Right.empty();
        int kind = count == 0 ? 0 : count == 1 ? 1 : 4;
        if (count == 2 && s.Number.empty() && s.Bottom.empty()) kind = 3;
        else if (count == 2 && s.Left.empty() && s.Right.empty()) kind = 2;

        auto region = [&](double side) {
            TchGraphics::Path p;
            p.Points = {{at(x, -r), -side}, {at(x, r)}, {at(x, d)}, {at(y, side * d)}, {at(x, -d)}, {at(x, -r)}};
            g.Fills.push_back(p);
            g.Paths.push_back(std::move(p));
        };
        region(1);
        if (s.Type == 0 && kind > 1) region(-1);
        g.Circles.push_back({c, r});
        if (s.Type != 0) g.Lines.push_back({{c.X - r, c.Y}, {c.X + r, c.Y}});
        else if (kind == 2) g.Lines.push_back({at(y, -r), at(y, r)});
        else if (kind == 3) g.Lines.push_back({at(x, -r), at(x, r)});
        else if (kind == 4)
            for (const double a : {std::numbers::pi / 4, 3 * std::numbers::pi / 4})
            {
                const P dir{y.X * std::cos(a) - y.Y * std::sin(a), y.X * std::sin(a) + y.Y * std::cos(a)};
                g.Lines.push_back({at(dir, -r), at(dir, r)});
            }

        // 编号大小：base = 比例 × R。算法 0 按 textbox 宽高比 a（SHX 大写字母墨迹高 = 字高）把文字框的半对角线放进
        // base（类型非 0 或编号类型 2 另有系数），编号类型 4 固定宽 2·base / (2√2 + 1)；其他算法字高 2·base（编号类型 4 乘 0.8，
        // 类型非 0 或编号类型 2～4 再乘 0.5），宽按字形量，编号类型 1 宽高都减半（画时字高再加倍）
        const double ratio = std::abs(s.TextRatio) >= 0.001 ? s.TextRatio : configRatio;
        const int mode = s.TextMode == 2 ? configMode : s.TextMode;
        const double base = ratio * r;
        struct Box { double W = 0, H = 0; };
        auto extent = [&](const std::string& t) {
            const double natural = textWidth(t);
            const double a = (natural > 0 ? natural : 1.0) * (styleWidth > 0 ? styleWidth : 1.0);
            Box b;
            if (mode == 0)
            {
                b.H = s.Type == 0 && kind != 2 ? base / std::sqrt(a * a + 1) : base / std::sqrt(a * a * 0.25 + 1.21);
                b.W = b.H * a;
                if (kind == 4) { b.W = 2 * base / (2 * std::sqrt(2.0) + 1); b.H = b.W / a; }
            }
            else
            {
                b.H = 2 * base * (kind == 4 ? 0.8 : 1.0) * (s.Type != 0 || kind >= 2 ? 0.5 : 1.0);
                b.W = a * b.H;
                if (s.Type == 0 && kind == 1) { b.W *= 0.5; b.H *= 0.5; }
            }
            return b;
        };
        // 编号的局部框：中心相对圆心的偏移（未转）与宽高；Fit 对齐按显示宽度求宽度因子
        const double turn = s.Angle + std::numbers::pi / 2, ct = std::cos(turn), st = std::sin(turn);
        auto put = [&](const std::string& t, double dx, double dy, const Box& b, bool rotate) {
            if (t.empty() || b.W <= 0 || b.H <= 0) return;
            const P center = rotate ? P{c.X + dx * ct - dy * st, c.Y + dx * st + dy * ct} : P{c.X + dx, c.Y + dy};
            const double natural = textWidth(t) * b.H;
            g.Texts.push_back({center, b.H, 0, t, 5, 0, 7, 0, natural > 0 ? b.W / natural : 0.0});
        };
        // 编号到分隔线的距离：圆在文字宽度处余下的高度（算法 0；其他算法取 R）减去字高后的 0.4；比例大于 1.01 时为字高的 0.2
        auto gap = [&](const Box& b) {
            const double room = mode == 0 ? std::sqrt(std::max(0.0, r * r - b.W * b.W * 0.25)) : r;
            return ratio > 1.01 ? b.H / 5 : (room - b.H) * 0.4;
        };
        if (s.Type != 0 || kind == 2)
        {
            const std::string& lower = s.Type != 0 ? s.Text2 : s.Bottom;
            const Box top = extent(s.Number), bottom = extent(lower);
            put(s.Number, 0, gap(top) + top.H / 2, top, s.Type == 0);
            const double drop = ratio > 1.01 ? 1.2 * bottom.H : gap(bottom) + bottom.H;
            put(lower, 0, bottom.H / 2 - drop, bottom, s.Type == 0);
        }
        else if (kind == 1)
        {
            const std::string& t = !s.Number.empty() ? s.Number : !s.Bottom.empty() ? s.Bottom : !s.Left.empty() ? s.Left : s.Right;
            const Box b = extent(t);
            put(t, 0, 0, {2 * b.W, 2 * b.H}, false);
        }
        else if (kind == 3)
        {
            const Box left = extent(s.Left), right = extent(s.Right);
            const double off = gap(left);
            put(s.Left, -(off + left.W / 2), 0, left, true);
            put(s.Right, off + right.W / 2, 0, right, true);
        }
        else if (kind == 4)
        {
            const double half = r / 2;
            put(s.Number, 0, half, extent(s.Number), true);
            put(s.Bottom, 0, -half, extent(s.Bottom), true);
            put(s.Left, -half, 0, extent(s.Left), true);
            put(s.Right, half, 0, extent(s.Right), true);
        }
        return g;
    }

    // 剖切符号的新国标样式（tianzheng2.dwg 与其他看图软件的显示对照）：
    // 零宽多段线连起各点，终点、以及剖面（类型 1）的起点在圆边截断；两端各画一个内视符号，圆心在端点，半径 = 直径 × 0.5，
    // 三角朝投射方向（内视符号的 Angle = 投射方向 − π/2），编号与比例、大小算法照搬，有索引图号（IndexFlags 含 2 或 4）时
    // 为内视符号类型 1（水平线上编号、下索引图号）。断面（类型 0）的起点不画圆，改画沿投射方向长 7 × 比例、线宽 2 × 比例的粗线。
    // 没有编号时只画剖切线。参数同 BuildInSight
    inline TchGraphics BuildSectionNewStyle(const TchSection& s, double configRatio, int configMode,
                                            const std::function<double(const std::string&)>& textWidth, double styleWidth = 1.0)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double r = s.Diameter * 0.5 * s.Scale;
        auto polar = [](const P& p, double angle, double d) { return P{p.X + std::cos(angle) * d, p.Y + std::sin(angle) * d}; };
        auto angleTo = [](const P& a, const P& b) { return std::atan2(b.Y - a.Y, b.X - a.X); };
        const std::size_t n = s.Points.size();
        TchGraphics::Path line;
        for (std::size_t i = 0; i < n; ++i)
        {
            P p = s.Points[i];
            if (i == 0 && s.Kind != 0) p = polar(p, angleTo(p, s.Points[1]), r);
            else if (i + 1 == n) p = polar(p, angleTo(s.Points[i - 1], p), -r);
            line.Points.push_back({p});
        }
        g.Paths.push_back(std::move(line));
        if (s.Label.empty()) return g;

        TchInSight in;
        in.Scale = s.Scale;
        in.Radius = s.Diameter * 0.5;
        in.TextRatio = s.TextRatio;
        in.TextMode = s.TextMode;
        in.Number = s.Label;
        in.TextStyle = s.TextStyle;
        if ((s.IndexFlags & 6) && !s.Label2.empty()) { in.Type = 1; in.Text2 = s.Label2; }
        auto add = [&](const P& c, double view) {
            in.X = c.X;
            in.Y = c.Y;
            in.Angle = view - std::numbers::pi / 2;
            TchGraphics part = BuildInSight(in, configRatio, configMode, textWidth, styleWidth);
            g.Fills.insert(g.Fills.end(), part.Fills.begin(), part.Fills.end());
            g.Paths.insert(g.Paths.end(), part.Paths.begin(), part.Paths.end());
            g.Circles.insert(g.Circles.end(), part.Circles.begin(), part.Circles.end());
            g.Lines.insert(g.Lines.end(), part.Lines.begin(), part.Lines.end());
            g.Texts.insert(g.Texts.end(), part.Texts.begin(), part.Texts.end());
        };
        const P& first = s.Points.front();
        if (s.Kind != 0) add(first, s.ViewAngle);
        else
        {
            TchGraphics::Path bar;
            bar.Width = 2 * s.Scale;
            bar.Points = {{first}, {polar(first, s.ViewAngle, 7 * s.Scale)}};
            g.Paths.push_back(std::move(bar));
        }
        add(s.Points.back(), s.EndViewAngle);
        return g;
    }
}
