#pragma once
#include "Import/TchDimension.h"
#include <array>
#include <functional>
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
        int    Mode = 1;            // 1：第一个文字放在尾端外侧；2：两个文字在首段中点上下
        std::vector<Vertex> Vertices;
        double ArrowLength = 3;     // 图面单位
        std::string Text1, Text2;
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle;
    };

    // TCH_SYMB_SECTION 剖切符号：剖切线各转折点处的粗短线、两端的投射方向线与编号
    struct TchSection
    {
        double Scale = 1, TextHeight = 5;
        int    Kind = 1;            // 1：剖面（两端画投射方向线）；0：断面（不画）
        double ViewAngle = 0;       // 投射方向（弧度）
        std::vector<TchGraphics::Point> Points;
        std::string Label;          // 两端都用这个编号
        MiniDWG::Handle TextStyle = MiniDWG::kNullHandle;
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
        int    LineMode = 1;                // 1、2：单下划线（0 为双线，未核对）
        int    Color = 7;                   // 文字颜色（ACI）
        MiniDWG::Handle NameStyle = MiniDWG::kNullHandle, ScaleStyle = MiniDWG::kNullHandle;
    };

    namespace Detail
    {
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
    }

    // 按 TDbSymbCoord 的私有布局版本 < 7 的明文分支解析，
    // 必须恰好读完主数据流。只接受版本 4～5（之后的版本多一段文字与点，未核对）
    inline bool DecodeCoord(const MiniDWG::UnknownEntity& entity, TchCoord& coord, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_COORD") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (data.Version < CadVersion::AC1021 || data.Main.size() < (data.MainBits + 7) / 8)
            return fail("unsupported DWG version for native coordinate");
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
        const double values[] = {c.Scale, c.TextHeight, c.PointX, c.PointY, c.ValueX, c.ValueY, c.LeaderX, c.LeaderY, c.TextGap, c.LineLength};
        for (double v : values)
            if (!Detail::Finite(v)) return fail("nonfinite coordinate geometry");
        if (c.Scale <= 0 || c.TextHeight <= 0 || c.Side > 1) return fail("invalid coordinate settings");
        if (!Detail::ReadSymbolHandles(entity, symbol, c.TextStyle)) return fail("coordinate handle stream is incomplete");
        coord = c;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 按 TDbSymbArrow 的私有布局版本 < 6 的明文分支解析，
    // 多段线布局（RC 闭合、BS 顶点数、每顶点 2RD + BD 凸度）。只接受版本 4～5、模式 1 与 2
    inline bool DecodeArrow(const MiniDWG::UnknownEntity& entity, TchArrow& arrow, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_ARROW") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (data.Version < CadVersion::AC1021 || data.Main.size() < (data.MainBits + 7) / 8)
            return fail("unsupported DWG version for native arrow");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchSymbolBase symbol;
        if (!ReadTchSymbolBase(r, symbol)) return fail("unsupported Tianzheng symbol base layout");
        const int version = r.ReadByte();
        if (version < 4 || version > 5) return fail("unsupported native arrow version");
        TchArrow a;
        a.Scale = symbol.Base.Scale;
        a.TextHeight = symbol.TextHeight;
        a.Mode = r.ReadByte();
        // 文字流：第一个文字
        r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
        r.ReadByte();                                           // 闭合标志
        const int count = r.ReadBitShort();
        if (count < 2 || count > 4096) return fail("invalid arrow vertex count");
        a.Vertices.resize(static_cast<std::size_t>(count));
        for (auto& v : a.Vertices) { v.X = r.ReadRawDouble(); v.Y = r.ReadRawDouble(); v.Bulge = r.ReadBitDouble(); }
        a.ArrowLength = r.ReadBitDouble();
        r.ReadByte();
        r.ReadBitDouble(); r.ReadBit();
        // 文字流：第二个文字
        r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native arrow fields do not consume the payload exactly");
        if (a.Mode != 1 && a.Mode != 2) return fail("unsupported arrow text mode");
        const double values[] = {a.Scale, a.TextHeight, a.ArrowLength};
        for (double v : values)
            if (!Detail::Finite(v)) return fail("nonfinite arrow settings");
        for (const auto& v : a.Vertices)
            if (!Detail::Finite(v.X) || !Detail::Finite(v.Y) || !Detail::Finite(v.Bulge) || std::abs(v.Bulge) > 1e6)
                return fail("nonfinite arrow vertex");
        if (a.Scale <= 0 || a.TextHeight <= 0 || a.ArrowLength < 0) return fail("invalid arrow settings");
        Detail::TextStream text(data);
        for (int i = 0; i < symbol.Base.TextCount; ++i) text.Next();
        a.Text1 = text.Next();
        a.Text2 = text.Next();
        if (text.Reader.Failed()) return fail("arrow text stream is incomplete");
        if (!Detail::ReadSymbolHandles(entity, symbol, a.TextStyle)) return fail("arrow handle stream is incomplete");
        arrow = std::move(a);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 按 CADReader 的 TDbSymbSection 读取函数（静态 VA 0x6f2fa0，虚表槽 36）版本 < 3 的明文分支解析，必须恰好读完主数据流
    inline bool DecodeSection(const MiniDWG::UnknownEntity& entity, TchSection& section, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_SYMB_SECTION") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (data.Version < CadVersion::AC1021 || data.Main.size() < (data.MainBits + 7) / 8)
            return fail("unsupported DWG version for native section symbol");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchSymbolBase symbol;
        if (!ReadTchSymbolBase(r, symbol)) return fail("unsupported Tianzheng symbol base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 2) return fail("unsupported native section symbol version");
        TchSection s;
        s.Scale = symbol.Base.Scale;
        s.TextHeight = symbol.TextHeight;
        s.Kind = r.ReadByte();
        s.ViewAngle = r.ReadBitDouble();
        const int count = r.ReadByte();
        if (count < 2) return fail("section symbol needs at least two points");
        for (int i = 0; i < count && !r.Failed(); ++i) s.Points.push_back({r.ReadRawDouble(), r.ReadRawDouble()});
        // 文字流：编号
        for (int i = 0; i < 4; ++i) r.ReadRawDouble();
        if (version > 1)
        {
            // 文字流：第二个字符串（不用于显示）
            for (int i = 0; i < 4; ++i) r.ReadRawDouble();
            r.ReadBitDouble(); r.ReadBitShort();
            // 句柄流：一个句柄
            r.ReadBitDouble(); r.ReadBitDouble();
        }
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native section symbol fields do not consume the payload exactly");
        if (s.Kind != 0 && s.Kind != 1) return fail("unsupported section symbol kind");
        if (!Detail::Finite(s.Scale) || s.Scale <= 0 || !Detail::Finite(s.TextHeight) || !Detail::Finite(s.ViewAngle))
            return fail("invalid section symbol settings");
        for (const auto& p : s.Points)
            if (!Detail::Finite(p.X) || !Detail::Finite(p.Y)) return fail("nonfinite section symbol point");
        Detail::TextStream text(data);
        for (int i = 0; i < symbol.Base.TextCount; ++i) text.Next();
        s.Label = text.Next();
        if (text.Reader.Failed()) return fail("section symbol text stream is incomplete");
        if (!Detail::ReadSymbolHandles(entity, symbol, s.TextStyle)) return fail("section symbol handle stream is incomplete");
        section = std::move(s);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 按 CADReader 的 TDbSymbIndexPointer 读取函数（静态 VA 0x6f07a0，虚表槽 36）版本 < 5 的明文分支解析，必须恰好读完主数据流。
    // 只接受版本 3～4（版本 2 及以前缺少后段，未核对）
    inline bool DecodeIndexPointer(const MiniDWG::UnknownEntity& entity, TchIndexPointer& pointer, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_INDEXPOINTER") return false;
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
        if (p.Kind != 0 && p.Kind != 1) return fail("unsupported index symbol kind");
        const double values[] = {p.Scale, p.TextHeight, p.PointX, p.PointY, p.CornerX, p.CornerY, p.Radius, p.Run, p.Mark};
        for (double v : values)
            if (!Detail::Finite(v)) return fail("nonfinite index symbol geometry");
        if (p.Scale <= 0 || p.Radius <= 0) return fail("invalid index symbol settings");
        Detail::TextStream text(data);
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

    // 按 CADReader 的 TDbDrawingName 读取函数（静态 VA 0x6ce8c0，虚表槽 36）版本 < 4 的明文分支解析，必须恰好读完主数据流。
    // 天正基类后：BS 版本、3BD 插入点（版本 ≥ 2 时以末尾的 3BD 为准）、图名与比例（文字流）、BD、BD 图名字高、BD 比例字高、BD 下划线宽、
    // BD 间距系数、两个文字样式句柄、B 显示比例、BS 下划线方式、BS 文字颜色、版本 ≥ 2 的 3BD 插入点
    inline bool DecodeDrawingName(const MiniDWG::UnknownEntity& entity, TchDrawingName& name, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_DRAWINGNAME") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (data.Version < CadVersion::AC1021 || data.Main.size() < (data.MainBits + 7) / 8)
            return fail("unsupported DWG version for native drawing name");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        const int version = r.ReadBitShort();
        if (version < 1 || version > 3) return fail("unsupported native drawing name version");
        TchDrawingName d;
        d.Scale = base.Scale;
        d.X = r.ReadBitDouble(); d.Y = r.ReadBitDouble(); r.ReadBitDouble();
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
        if (d.LineMode != 1 && d.LineMode != 2) return fail("unsupported drawing name underline mode");
        const double values[] = {d.Scale, d.X, d.Y, d.NameHeight, d.ScaleHeight, d.LineWidth, d.Gap};
        for (double v : values)
            if (!Detail::Finite(v)) return fail("nonfinite drawing name settings");
        if (d.Scale <= 0 || d.NameHeight <= 0 || d.Color < 0 || d.Color > 256) return fail("invalid drawing name settings");
        Detail::TextStream text(data);
        for (int i = 0; i < base.TextCount; ++i) text.Next();
        d.Name = text.Next();
        d.ScaleText = text.Next();
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
    // 含汉字的文字天正把字高乘以 0.96454（大字体 GBCBIG 下实测）；宽度与最低点取 textbox，间隔与下划线外伸仍按名义字高。
    // extent(文字, 实际字高, 是否比例文字) 返回 textbox 宽度与最低点（世界单位）
    inline TchGraphics BuildDrawingName(const TchDrawingName& d,
                                        const std::function<Detail::TextExtent(const std::string&, double, bool)>& extent)
    {
        TchGraphics g;
        const double s = d.Scale, h1 = d.NameHeight * s, h2 = d.ScaleHeight * s, line = d.LineWidth * s;
        const double a1 = Detail::HasWideChar(d.Name) ? 0.96454 * h1 : h1;
        const double a2 = Detail::HasWideChar(d.ScaleText) ? 0.96454 * h2 : h2;
        const bool scale = d.ShowScale && !d.ScaleText.empty();
        const Detail::TextExtent e1 = extent(d.Name, a1, false);
        const double w2 = scale ? extent(d.ScaleText, a2, true).Width : 0.0;
        const double total = scale ? e1.Width + d.Gap * h1 + w2 : e1.Width + 0.2 * h1;
        const double left = d.X - total / 2;
        if (!d.Name.empty()) g.Texts.push_back({{left, d.Y + a1 / 2}, a1, 0, d.Name, 4, 0, d.Color});
        if (scale) g.Texts.push_back({{left + e1.Width + d.Gap * h1, d.Y + a2 / 2}, a2, 0, d.ScaleText, 4, 1, d.Color});
        TchGraphics::Path underline;
        underline.Width = line;
        const double y = d.Y + e1.Bottom - 2 * line;
        underline.Points = {{{left, y}}, {{left + e1.Width + 0.2 * h1, y}}};
        g.Paths.push_back(std::move(underline));
        return g;
    }

    // 剖切符号（与天正 2014 对 6 个变体的分解结果核对）：每个点画线宽 0.5 × 比例的粗线，沿相邻段各 10 × 比例；
    // 剖面（类型 1）两端再沿投射方向画 6 × 比例；编号中心在端点沿投射方向（6 × 比例 + 名义字高）处，字高 0.85 × 名义字高
    inline TchGraphics BuildSection(const TchSection& s)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double stroke = 10 * s.Scale, leg = s.Kind == 1 ? 6 * s.Scale : 0.0, width = 0.5 * s.Scale;
        const double nominal = s.TextHeight * s.Scale;
        const P view{std::cos(s.ViewAngle), std::sin(s.ViewAngle)};
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
                if (leg > 0) path.Points.push_back({{p.X + view.X * leg, p.Y + view.Y * leg}});
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
            for (const P& p : {s.Points.front(), s.Points.back()})
                g.Texts.push_back({{p.X + view.X * (leg + nominal), p.Y + view.Y * (leg + nominal)}, 0.85 * nominal, 0, s.Label, 5});
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

    // 箭头（与天正 2014 对 8 个变体的分解结果核对）：最后一段末端为长 ArrowLength × 比例、底宽为其 1/3.75 的实心箭头；
    // 文字高 0.9645 × 名义字高。模式 1：第一个文字在尾端外侧 0.3 × 名义字高处、垂直居中；模式 2：两个文字在首段中点上下，
    // 距线 0.3 × 名义字高
    inline TchGraphics BuildArrow(const TchArrow& a)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const auto& v = a.Vertices;
        const std::size_t n = v.size();
        const P tip{v[n - 1].X, v[n - 1].Y}, before{v[n - 2].X, v[n - 2].Y};
        const double last = std::hypot(tip.X - before.X, tip.Y - before.Y);
        const double head = std::min(a.ArrowLength * a.Scale, last);
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
            const P mid{(v[0].X + v[1].X) / 2, (v[0].Y + v[1].Y) / 2};
            if (!a.Text1.empty()) g.Texts.push_back({{mid.X, mid.Y + gap + height / 2}, height, 0, a.Text1, 5});
            if (!a.Text2.empty()) g.Texts.push_back({{mid.X, mid.Y - gap - height / 2}, height, 0, a.Text2, 5});
        }
        return g;
    }
}
