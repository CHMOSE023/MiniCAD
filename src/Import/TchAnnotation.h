#pragma once
#include "Import/TchText.h"
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace MiniCAD::Tch
{
    namespace Detail
    {
        // 派生类的字符串：先跳过文字流中基类属性包占用的字符串（R2004 已在主数据流就地读掉），之后在读到的位置调用 Next()
        inline TextStream SymbolTexts(const MiniDWG::RawDwgData& data, MiniDWG::DwgBitReader& r, const TchSymbolBase& symbol)
        {
            TextStream stream(data, r);
            for (int i = 0; i < symbol.Base.TextCount; ++i) stream.Next();
            return stream;
        }

        inline bool ReadSymbolHead(const MiniDWG::UnknownEntity& entity, const char* dxf, MiniDWG::DwgBitReader& r, TchSymbolBase& symbol)
        {
            using namespace MiniDWG;
            const auto& data = entity.Raw.Dwg;
            if (entity.Raw.DxfName != dxf || !SupportedVersion(data)) return false;
            return ReadTchSymbolBase(r, symbol);
        }
    }

    // ── TCH_ELEVATION 标高（版本 < 5 为明文分支）──
    struct TchElevation
    {
        double Scale = 100, TextHeight = 3.5;
        int    Flags = 0;               // 位 0：三角与文字在线下；位 1：水平线朝左；位 4：两位小数（否则三位）；位 5：实心三角
        double X = 0, Y = 0;            // 标高点（三角尖）
        double LineLength = 9.66;       // 水平线自三角尖向外的长度（× 比例）
        std::string Value;              // 标高值文字（如 "2.00000"），按精度取整显示
        MiniDWG::Handle Style = MiniDWG::kNullHandle;
    };

    namespace Detail
    {
        // 标高点（二维点）、文字、线长；标志位 2、3 各多一个数值；数值、字节、2 个二维点、字符串、字节
        inline bool DecodeElevationCipher(const MiniDWG::UnknownEntity& entity, TchElevation& elevation, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            TchSymbolBase symbol;
            TchElevation e;
            if (!ReadCipherSymbolBase(f, symbol, e.Style)) return fail("unsupported Tianzheng symbol base layout");
            if (f.Main.ReadByte() != 5) return fail("unsupported native elevation version");
            f.ReadKey();
            e.Scale = symbol.Base.Scale; e.TextHeight = symbol.TextHeight;
            e.Flags = f.Byte();
            const auto p = f.Point2();
            e.X = p.X; e.Y = p.Y;
            e.Value = f.Text();
            e.LineLength = f.Double();
            if (e.Flags & 4) f.Double();
            if (e.Flags & 8) f.Double();
            f.Double(); f.Byte();
            f.Point2(); f.Point2();
            f.Text();
            f.Byte();
            if (!f.Exact()) return fail("native elevation fields do not consume the payload exactly");
            if (!Finite(e.X) || !Finite(e.Y) || !Finite(e.LineLength) || !(e.Scale > 0) || !(e.TextHeight > 0))
                return fail("invalid native elevation settings");
            elevation = std::move(e);
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    // 符号基类、RC 版本、RC 标志、2RD 标高点、文字（文字流）、BD 线长；标志位 2、3 各多一个 BD；
    // 版本 ≥ 2：BD、RC、2RD、2RD、文字；版本 ≥ 4：RC
    inline bool DecodeElevation(const MiniDWG::UnknownEntity& entity, TchElevation& elevation, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchSymbolBase symbol;
        if (entity.Raw.DxfName != "TCH_ELEVATION") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeElevationCipher(entity, elevation, diagnostic);
        if (!Detail::ReadSymbolHead(entity, "TCH_ELEVATION", r, symbol)) return fail("unsupported Tianzheng symbol base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 4) return fail("unsupported native elevation version");
        TchElevation e;
        e.Scale = symbol.Base.Scale; e.TextHeight = symbol.TextHeight;
        auto text = Detail::SymbolTexts(data, r, symbol);
        e.Flags = r.ReadByte();
        e.X = r.ReadRawDouble(); e.Y = r.ReadRawDouble();
        e.Value = text.Next();
        e.LineLength = r.ReadBitDouble();
        if (e.Flags & 4) r.ReadBitDouble();
        if (e.Flags & 8) r.ReadBitDouble();
        if (version > 1)
        {
            r.ReadBitDouble(); r.ReadByte();
            r.ReadRawDouble(); r.ReadRawDouble(); r.ReadRawDouble(); r.ReadRawDouble();
            text.Next();
        }
        if (version > 3) r.ReadByte();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native elevation fields do not consume the payload exactly");
        if (!Detail::Finite(e.X) || !Detail::Finite(e.Y) || !Detail::Finite(e.LineLength) || !(e.Scale > 0) || !(e.TextHeight > 0))
            return fail("invalid native elevation settings");
        if (!Detail::ReadSymbolHandles(entity, symbol, e.Style)) return fail("elevation handle stream is incomplete");
        elevation = std::move(e);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 文字在线上 0.3 × 名义字高（线下时基线在线下 1.3 × 名义字高），朝右时右端距线端 0.3 × 比例，朝左时左端距线端 0.3 × 比例
    inline TchGraphics BuildElevation(const TchElevation& e)
    {
        using P = TchGraphics::Point;
        TchGraphics g;
        const double s = e.Scale, h = 3 * s, nominal = e.TextHeight * s;
        const double up = (e.Flags & 1) ? -1.0 : 1.0, side = (e.Flags & 2) ? -1.0 : 1.0;
        const double y = e.Y + up * h;
        const P tip{e.X, e.Y}, a{e.X - h, y}, b{e.X + h, y};
        if (e.Flags & 32) g.Arrows.push_back({tip, a, b});
        g.Lines.push_back({tip, a});
        g.Lines.push_back({tip, b});
        const double far = e.X + side * e.LineLength * s;
        g.Lines.push_back({{e.X - side * h, y}, {far, y}});
        std::string value = e.Value;
        char* end = nullptr;
        const double v = std::strtod(value.c_str(), &end);
        if (end != value.c_str() && *end == 0) value = Detail::FormatNumber(v, (e.Flags & 16) ? 2 : 3);
        if (e.Flags & 32)
        {
            // 实心三角（总图样式）：文字在水平线外端之外、与标高点同高（1 个变体核对，误差约 0.3 × 比例）
            Detail::AddRuns(g, value, nominal, far + side * 1.35 * s, e.Y - 0.25 * s, 0, side > 0 ? 0 : 2);
            return g;
        }
        const double baseline = up > 0 ? y + 0.3 * nominal : y - 1.3 * nominal;
        Detail::AddRuns(g, value, nominal, far - side * 0.3 * s, baseline, 0, side > 0 ? 2 : 0);
        return g;
    }
    // ── TCH_MULTILEADER 引出标注（版本 < 7 为明文分支）──
    struct TchMultiLeader
    {
        double Scale = 100, TextHeight = 3.5;
        int    Arrow = 1;                   // 0 无、1 圆点、2 实心箭头（其余按无处理）
        double ElbowX = 0, ElbowY = 0;      // 转折点（水平线起点）
        double LineLength = 10;             // 水平线长（× 比例）
        int    Side = 0;                    // 0 水平线朝右，否则朝左
        double ArrowSize = 3;               // 箭头长（× 比例）
        std::vector<TchGraphics::Point> Targets;   // 各引线的被标注点
        std::string Upper, Lower;           // 线上、线下文字
        MiniDWG::Handle Style = MiniDWG::kNullHandle;
        double Gap = -1000;                 // 文字与水平线的间距（× 名义字高）；−1000 为默认
        int    Align = 3;                   // 文字对齐：3 对齐水平线远端，1 对齐转折点一侧，2 居中（未核对）
    };

    namespace Detail
    {
        inline const char* CheckMultiLeader(const TchMultiLeader& m)
        {
            if (!Finite(m.ElbowX) || !Finite(m.ElbowY) || !Finite(m.LineLength) || !Finite(m.ArrowSize) || !(m.Scale > 0) || !(m.TextHeight > 0))
                return "invalid native leader settings";
            for (const auto& p : m.Targets)
                if (!Finite(p.X) || !Finite(p.Y)) return "invalid native leader point";
            return nullptr;
        }

        // 线上 / 线下文字、点数与各点；箭头长、文字间距、文字对齐、数值、字节、二维点、2 个三维点
        inline bool DecodeMultiLeaderCipher(const MiniDWG::UnknownEntity& entity, TchMultiLeader& leader, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            TchSymbolBase symbol;
            TchMultiLeader m;
            if (!ReadCipherSymbolBase(f, symbol, m.Style)) return fail("unsupported Tianzheng symbol base layout");
            if (f.Main.ReadByte() != 7) return fail("unsupported native leader version");
            f.ReadKey();
            m.Scale = symbol.Base.Scale; m.TextHeight = symbol.TextHeight;
            m.Arrow = f.Byte();
            const auto elbow = f.Point2();
            m.ElbowX = elbow.X; m.ElbowY = elbow.Y;
            m.LineLength = f.Double();
            m.Side = f.Byte();
            m.Upper = f.Text(); m.Lower = f.Text();
            for (int i = 0, n = f.Byte(); i < n && f.Ok(); ++i)
            {
                const auto p = f.Point2();
                m.Targets.push_back({p.X, p.Y});
            }
            m.ArrowSize = f.Double();
            m.Gap = f.Double();
            m.Align = f.Byte();
            f.Double();
            f.Byte(); f.Point2();
            f.Point3(); f.Point3();
            if (!f.Exact()) return fail("native leader fields do not consume the payload exactly");
            if (const char* why = CheckMultiLeader(m)) return fail(why);
            leader = std::move(m);
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    // 符号基类、RC 版本、RC 箭头样式、2RD 转折点、BD 线长、RC 方向、线上 / 线下文字（文字流）、RC 点数与各点 2RD；
    // 版本 ≥ 2：BD 箭头长；≥ 3：BD 文字间距、RC 文字对齐、BD；≥ 4：RC、2RD；≥ 5：两个 3BD；= 6：两段文字
    inline bool DecodeMultiLeader(const MiniDWG::UnknownEntity& entity, TchMultiLeader& leader, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_MULTILEADER") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeMultiLeaderCipher(entity, leader, diagnostic);
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchSymbolBase symbol;
        if (!Detail::ReadSymbolHead(entity, "TCH_MULTILEADER", r, symbol)) return fail("unsupported Tianzheng symbol base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 6) return fail("unsupported native leader version");
        TchMultiLeader m;
        m.Scale = symbol.Base.Scale; m.TextHeight = symbol.TextHeight;
        auto text = Detail::SymbolTexts(data, r, symbol);
        m.Arrow = r.ReadByte();
        m.ElbowX = r.ReadRawDouble(); m.ElbowY = r.ReadRawDouble();
        m.LineLength = r.ReadBitDouble();
        m.Side = r.ReadByte();
        m.Upper = text.Next(); m.Lower = text.Next();
        const int count = r.ReadByte();
        for (int i = 0; i < count && !r.Failed(); ++i)
        {
            const double x = r.ReadRawDouble(), y = r.ReadRawDouble();
            m.Targets.push_back({x, y});
        }
        if (version > 1) m.ArrowSize = r.ReadBitDouble();
        if (version > 2) { m.Gap = r.ReadBitDouble(); m.Align = r.ReadByte(); r.ReadBitDouble(); }
        if (version > 3) { r.ReadByte(); r.ReadRawDouble(); r.ReadRawDouble(); }
        if (version > 4) for (int i = 0; i < 6; ++i) r.ReadBitDouble();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native leader fields do not consume the payload exactly");
        if (const char* why = Detail::CheckMultiLeader(m)) return fail(why);
        if (!Detail::ReadSymbolHandles(entity, symbol, m.Style)) return fail("leader handle stream is incomplete");
        leader = std::move(m);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    namespace Detail
    {
        inline void AddLeaderEnd(TchGraphics& g, int kind, TchGraphics::Point tip, TchGraphics::Point toward, double scale, double size)
        {
            if (kind == 1)
            {
                const double r = 0.2 * scale, dx = toward.X - tip.X, dy = toward.Y - tip.Y, d = std::hypot(dx, dy);
                const double ux = d > 0 ? dx / d : 1, uy = d > 0 ? dy / d : 0;
                TchGraphics::Path dot;
                dot.Width = 0.4 * scale;
                dot.Points = {{{tip.X - ux * r, tip.Y - uy * r}, 1.0}, {{tip.X + ux * r, tip.Y + uy * r}, 1.0}, {{tip.X - ux * r, tip.Y - uy * r}}};
                g.Paths.push_back(std::move(dot));
            }
            else if (kind == 2)
            {
                const double dx = toward.X - tip.X, dy = toward.Y - tip.Y, d = std::hypot(dx, dy);
                if (d <= 0) return;
                const double ux = dx / d, uy = dy / d, len = size * scale, w = 0.4 * scale;
                const TchGraphics::Point base{tip.X + ux * len, tip.Y + uy * len};
                g.Arrows.push_back({{base.X - uy * w, base.Y + ux * w}, {base.X + uy * w, base.Y - ux * w}, tip});
            }
        }
    }

    inline TchGraphics BuildMultiLeader(const TchMultiLeader& m, const TchTextStyleRule& rule = {})
    {
        TchGraphics g;
        const double s = m.Scale, nominal = m.TextHeight * s, side = m.Side == 0 ? 1.0 : -1.0;
        const TchGraphics::Point elbow{m.ElbowX, m.ElbowY};
        for (const auto& t : m.Targets)
        {
            g.Lines.push_back({t, elbow});
            Detail::AddLeaderEnd(g, m.Arrow, t, elbow, s, m.ArrowSize);
        }
        const double end = m.ElbowX + side * m.LineLength * s;
        g.Lines.push_back({elbow, {end, m.ElbowY}});
        const double gap = m.Gap >= 0 && m.Gap < 10 ? m.Gap : 0.3;
        const double dir = end >= m.ElbowX ? 1.0 : -1.0;        // 水平线自转折点的走向
        const double inset = 0.3 * nominal;
        double anchor = end - dir * inset;
        int column = dir > 0 ? 2 : 0;                           // 0 左对齐、1 居中、2 右对齐
        if (m.Align == 1) { anchor = m.ElbowX + dir * inset; column = dir > 0 ? 0 : 2; }
        else if (m.Align == 2) { anchor = (m.ElbowX + end) / 2; column = 1; }
        const double upper = m.ElbowY + gap * nominal, lower = m.ElbowY - (1 + gap) * nominal;
        if (rule.TrueType)
        {
            for (const auto& [value, y] : {std::pair{m.Upper, upper}, std::pair{m.Lower, lower}})
            {
                if (value.empty()) continue;
                TchText t;
                t.X = anchor; t.Y = y; t.Height = m.TextHeight; t.Scale = s; t.Attachment = 7 + column; t.Value = value;
                const auto part = BuildText(t, rule);
                g.Texts.insert(g.Texts.end(), part.Texts.begin(), part.Texts.end());
            }
            return g;
        }
        const double width = rule.StyleWidth;
        for (const auto& [value, y] : {std::pair{m.Upper, upper}, std::pair{m.Lower, lower}})
            Detail::AddRuns(g, value, nominal, anchor, y, 0, column, 7, 0, 0, 0.85, 0.6 / 0.85 * width, 0.5714 * width, 0.69 * width, true, rule.WideHeight);
        return g;
    }
    // ── TCH_COMPOSING 做法标注（版本 < 4 为明文分支）──
    struct TchComposing
    {
        double Scale = 100, TextHeight = 3.5;
        int    TextSide = 1;                // 1 文字在线端（其余按文字在线上处理）
        double LineLength = 10;             // 水平线长（× 比例）
        double StartX = 0, StartY = 0;      // 引线起点（被标注处）
        double ElbowX = 0, ElbowY = 0;      // 转折点
        std::string Text;                   // 多行以 \P 或换行分隔
        MiniDWG::Handle Style = MiniDWG::kNullHandle;
    };

    namespace Detail
    {
        inline const char* CheckComposing(const TchComposing& c)
        {
            if (!Finite(c.StartX) || !Finite(c.StartY) || !Finite(c.ElbowX) || !Finite(c.ElbowY) || !Finite(c.LineLength) ||
                !(c.Scale > 0) || !(c.TextHeight > 0))
                return "invalid native composing settings";
            return nullptr;
        }

        inline bool DecodeComposingCipher(const MiniDWG::UnknownEntity& entity, TchComposing& composing, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            TchSymbolBase symbol;
            TchComposing c;
            if (!ReadCipherSymbolBase(f, symbol, c.Style)) return fail("unsupported Tianzheng symbol base layout");
            if (f.Main.ReadByte() != 4) return fail("unsupported native composing version");
            f.ReadKey();
            c.Scale = symbol.Base.Scale; c.TextHeight = symbol.TextHeight;
            c.TextSide = f.Byte();
            c.LineLength = f.Double();
            const auto start = f.Point2(), elbow = f.Point2();
            c.StartX = start.X; c.StartY = start.Y;
            c.ElbowX = elbow.X; c.ElbowY = elbow.Y;
            c.Text = f.Text();
            for (int i = 0; i < 5; ++i) f.Double();
            f.Byte(); f.Byte();
            if (!f.Exact()) return fail("native composing fields do not consume the payload exactly");
            if (const char* why = CheckComposing(c)) return fail(why);
            composing = std::move(c);
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    // 符号基类、RC 版本、RC 文字位置、BD 线长（自定义编码：00 为 RD、01 为 1、10 为 0）、2RD 起点、2RD 转折点、文字；
    // 版本 ≥ 2：5 个 BD（行距、段距等）与 RC；版本 ≥ 3：RC
    inline bool DecodeComposing(const MiniDWG::UnknownEntity& entity, TchComposing& composing, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_COMPOSING") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeComposingCipher(entity, composing, diagnostic);
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchSymbolBase symbol;
        if (!Detail::ReadSymbolHead(entity, "TCH_COMPOSING", r, symbol)) return fail("unsupported Tianzheng symbol base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 3) return fail("unsupported native composing version");
        TchComposing c;
        c.Scale = symbol.Base.Scale; c.TextHeight = symbol.TextHeight;
        auto text = Detail::SymbolTexts(data, r, symbol);
        c.TextSide = r.ReadByte();
        const int code = (r.ReadBit() ? 2 : 0) + (r.ReadBit() ? 1 : 0);
        c.LineLength = code == 0 ? r.ReadRawDouble() : code == 1 ? 1.0 : 0.0;
        c.StartX = r.ReadRawDouble(); c.StartY = r.ReadRawDouble();
        c.ElbowX = r.ReadRawDouble(); c.ElbowY = r.ReadRawDouble();
        c.Text = text.Next();
        if (version > 1) { for (int i = 0; i < 5; ++i) r.ReadBitDouble(); r.ReadByte(); }
        if (version > 2) r.ReadByte();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native composing fields do not consume the payload exactly");
        if (const char* why = Detail::CheckComposing(c)) return fail(why);
        if (!Detail::ReadSymbolHandles(entity, symbol, c.Style)) return fail("composing handle stream is incomplete");
        composing = std::move(c);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    namespace Detail
    {
        inline std::vector<std::string> SplitLines(const std::string& text)
        {
            std::vector<std::string> lines(1);
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] == '\\' && i + 1 < text.size() && (text[i + 1] == 'P' || text[i + 1] == 'p')) { lines.emplace_back(); ++i; }
                else if (text[i] == '\n') lines.emplace_back();
                else if (text[i] != '\r') lines.back() += text[i];
            }
            return lines;
        }
    }

    inline TchGraphics BuildComposing(const TchComposing& c)
    {
        TchGraphics g;
        const double s = c.Scale, nominal = c.TextHeight * s;
        const TchGraphics::Point start{c.StartX, c.StartY}, elbow{c.ElbowX, c.ElbowY};
        if (std::hypot(elbow.X - start.X, elbow.Y - start.Y) > 1e-9) g.Lines.push_back({start, elbow});
        const double side = elbow.X >= start.X ? 1.0 : -1.0;
        const double end = elbow.X + side * c.LineLength * s;
        g.Lines.push_back({elbow, {end, elbow.Y}});
        const auto lines = Detail::SplitLines(c.Text);
        if (c.TextSide == 1)
        {
            for (std::size_t i = 0; i < lines.size(); ++i)
                Detail::AddRuns(g, lines[i], nominal, end + side * 0.57 * nominal, elbow.Y - 0.5 * nominal - 1.4 * nominal * i, 0, side > 0 ? 0 : 2);
            return g;
        }
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            const double y = elbow.Y + 1.4 * nominal * static_cast<double>(lines.size() - 1 - i);
            Detail::AddRuns(g, lines[i], nominal, elbow.X + side * 0.57 * nominal, y + 0.3 * nominal, 0, side > 0 ? 0 : 2);
        }
        return g;
    }
    // ── TCH_DRAWINGINDEX 索引图名（版本 < 3 为明文分支）──
    struct TchDrawingIndex
    {
        double Scale = 100, TextHeight = 3.5;
        double X = 0, Y = 0;                // 圆心
        double Radius = 7;                  // 圆半径（× 比例）
        double LineWidth = 0.5;             // 圆的线宽（× 比例）
        std::string Number, Sheet, ScaleText;   // 索引编号、所在图号（空则不画中线）、比例文字
        bool   ShowScale = false;
        double ScaleHeight = 3.5;           // 比例文字名义字高
        MiniDWG::Handle Style = MiniDWG::kNullHandle, ScaleStyle = MiniDWG::kNullHandle;
    };

    namespace Detail
    {
        inline const char* CheckDrawingIndex(const TchDrawingIndex& d)
        {
            if (!Finite(d.X) || !Finite(d.Y) || !(d.Radius > 0) || !Finite(d.Radius) || !Finite(d.LineWidth) || !(d.Scale > 0) || !(d.ScaleHeight > 0))
                return "invalid native drawing index settings";
            return nullptr;
        }

        inline bool DecodeDrawingIndexCipher(const MiniDWG::UnknownEntity& entity, TchDrawingIndex& index, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            TchSymbolBase symbol;
            TchDrawingIndex d;
            if (!ReadCipherSymbolBase(f, symbol, d.Style)) return fail("unsupported Tianzheng symbol base layout");
            if (f.Main.ReadByte() != 3) return fail("unsupported native drawing index version");
            f.ReadKey();
            d.Scale = symbol.Base.Scale; d.TextHeight = symbol.TextHeight;
            const auto p = f.Point2();
            d.X = p.X; d.Y = p.Y;
            d.Radius = f.Double();
            d.LineWidth = f.Double();
            d.Number = f.Text(); d.Sheet = f.Text(); d.ScaleText = f.Text();
            d.ScaleStyle = f.Handle();
            d.ScaleHeight = f.Double();
            f.Double();
            d.ShowScale = f.Byte() != 0;
            f.Byte();
            f.Double(); f.Double();
            if (!f.Exact()) return fail("native drawing index fields do not consume the payload exactly");
            if (const char* why = CheckDrawingIndex(d)) return fail(why);
            index = std::move(d);
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    // 符号基类、RC 版本、2RD 圆心、BD 半径、BD 线宽、编号与图号（文字流）；版本 ≥ 2：比例文字、句柄（文字图层）、
    // BD 比例字高、BD、RC 显示比例、RC、BD；句柄流：编号文字样式、文字图层、比例文字样式
    inline bool DecodeDrawingIndex(const MiniDWG::UnknownEntity& entity, TchDrawingIndex& index, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_DRAWINGINDEX") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeDrawingIndexCipher(entity, index, diagnostic);
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchSymbolBase symbol;
        if (!Detail::ReadSymbolHead(entity, "TCH_DRAWINGINDEX", r, symbol)) return fail("unsupported Tianzheng symbol base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 2) return fail("unsupported native drawing index version");
        TchDrawingIndex d;
        d.Scale = symbol.Base.Scale; d.TextHeight = symbol.TextHeight;
        auto text = Detail::SymbolTexts(data, r, symbol);
        d.X = r.ReadRawDouble(); d.Y = r.ReadRawDouble();
        d.Radius = r.ReadBitDouble();
        d.LineWidth = r.ReadBitDouble();
        d.Number = text.Next(); d.Sheet = text.Next();
        if (version > 1)
        {
            d.ScaleText = text.Next();
            d.ScaleHeight = r.ReadBitDouble();
            r.ReadBitDouble();
            d.ShowScale = r.ReadByte() != 0;
            r.ReadByte();
            r.ReadBitDouble();
        }
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native drawing index fields do not consume the payload exactly");
        if (const char* why = Detail::CheckDrawingIndex(d)) return fail(why);
        DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
        for (int i = 0; i < symbol.Base.HandleCount; ++i) handles.ReadHandle(entity.ObjectHandle);
        d.Style = handles.ReadHandle(entity.ObjectHandle);
        if (version > 1) { handles.ReadHandle(entity.ObjectHandle); d.ScaleStyle = handles.ReadHandle(entity.ObjectHandle); }
        if (handles.Failed()) return fail("drawing index handle stream is incomplete");
        index = std::move(d);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildDrawingIndex(const TchDrawingIndex& d)
    {
        TchGraphics g;
        const double s = d.Scale, r = d.Radius * s;
        TchGraphics::Path circle;
        circle.Width = d.LineWidth * s;
        circle.Points = {{{d.X + r, d.Y}, 1.0}, {{d.X - r, d.Y}, 1.0}, {{d.X + r, d.Y}}};
        g.Paths.push_back(std::move(circle));
        if (d.Sheet.empty())
        {
            const double h = 0.55 * 2 * r;
            g.Texts.push_back({{d.X, d.Y - h / 2}, h, 0, d.Number, 8, 0, 7});
        }
        else
        {
            g.Lines.push_back({{d.X + r, d.Y}, {d.X - r, d.Y}});
            const double h = 0.26 * 2 * r;
            g.Texts.push_back({{d.X, d.Y + 0.19 * r}, h, 0, d.Number, 8, 0, 7});
            g.Texts.push_back({{d.X, d.Y - 0.71 * r}, h, 0, d.Sheet, 8, 0, 7});
        }
        if (d.ShowScale && !d.ScaleText.empty())
            Detail::AddRuns(g, d.ScaleText, d.ScaleHeight * s, d.X + r + 2 * s, d.Y - r, 0, 0, 7, 0, 1);
        return g;
    }
    // ── TCH_NORTHTHUMB 指北针（版本 < 3 为明文分支）──
    struct TchNorthThumb
    {
        double Scale = 100;
        double X = 0, Y = 0;
        double Radius = 12;                 // 圆半径（× 比例）
        double Rotation = 0;                // 指向（弧度）
        double TextHeight = 5;              // 文字名义字高
        int    Color = 7;
        std::string Text = "北";   // i18n: 数据
        MiniDWG::Handle Style = MiniDWG::kNullHandle;
    };

    namespace Detail
    {
        inline const char* CheckNorthThumb(const TchNorthThumb& n)
        {
            if (!Finite(n.X) || !Finite(n.Y) || !(n.Radius > 0) || !Finite(n.Radius) || !Finite(n.Rotation) || !(n.Scale > 0) || !(n.TextHeight >= 0))
                return "invalid native north thumb settings";
            return nullptr;
        }

        inline bool DecodeNorthThumbCipher(const MiniDWG::UnknownEntity& entity, TchNorthThumb& north, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 3) return fail("unsupported native north thumb version");
            f.ReadKey();
            TchNorthThumb n;
            n.Scale = f.Scale;
            const auto p = f.Point2();
            n.X = p.X; n.Y = p.Y;
            n.Radius = f.Double();
            n.Rotation = f.Double();
            n.Style = f.Handle();
            n.TextHeight = f.Double();
            n.Color = f.Short();
            n.Text = f.Text();
            f.Point2();
            if (!f.Exact()) return fail("native north thumb fields do not consume the payload exactly");
            if (const char* why = CheckNorthThumb(n)) return fail(why);
            north = std::move(n);
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    // TCH基类、RC 版本、2RD 圆心、BD 半径、BD 指向；版本 ≥ 2：句柄（文字样式）、BD 字高（自定义编码）、BS 颜色、文字、2RD
    inline bool DecodeNorthThumb(const MiniDWG::UnknownEntity& entity, TchNorthThumb& north, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_NORTHTHUMB") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeNorthThumbCipher(entity, north, diagnostic);
        if (data.Version < CadVersion::AC1021 || data.Main.size() < (data.MainBits + 7) / 8) return fail("unsupported DWG version for north thumb");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        const int version = r.ReadByte();
        if (version < 1 || version > 2) return fail("unsupported native north thumb version");
        TchNorthThumb n;
        n.Scale = base.Scale;
        n.X = r.ReadRawDouble(); n.Y = r.ReadRawDouble();
        n.Radius = r.ReadBitDouble();
        n.Rotation = r.ReadBitDouble();
        if (version > 1)
        {
            const int code = (r.ReadBit() ? 2 : 0) + (r.ReadBit() ? 1 : 0);
            n.TextHeight = code == 0 ? r.ReadRawDouble() : code == 1 ? 1.0 : 0.0;
            n.Color = r.ReadBitShort();
            r.ReadRawDouble(); r.ReadRawDouble();
        }
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native north thumb fields do not consume the payload exactly");
        if (const char* why = Detail::CheckNorthThumb(n)) return fail(why);
        if (version > 1)
        {
            Detail::TextStream stream(data, r);
            for (int i = 0; i < base.TextCount; ++i) stream.Next();
            n.Text = stream.Next();
            DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
            for (int i = 0; i < base.HandleCount; ++i) handles.ReadHandle(entity.ObjectHandle);
            n.Style = handles.ReadHandle(entity.ObjectHandle);
        }
        north = std::move(n);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildNorthThumb(const TchNorthThumb& n)
    {
        TchGraphics g;
        const double s = n.Scale, r = n.Radius * s, ux = std::cos(n.Rotation), uy = std::sin(n.Rotation), w = 1.5 * s;
        g.Circles.push_back({{n.X, n.Y}, r});
        const TchGraphics::Point tail{n.X - ux * r, n.Y - uy * r}, tip{n.X + ux * r, n.Y + uy * r};
        g.Arrows.push_back({{tail.X - uy * w, tail.Y + ux * w}, {tail.X + uy * w, tail.Y - ux * w}, tip});
        if (!n.Text.empty())
        {
            const double h = n.TextHeight * s * (Detail::HasWideChar(n.Text) ? 0.96454 : 1.0);
            g.Texts.push_back({{n.X + ux * (r + h), n.Y + uy * (r + h)}, h, 0, n.Text, 5, 0, n.Color});
        }
        return g;
    }

    // ── TCH_CUT 折断线（按样例与天正 2014 分解结果确定）──
    struct TchCut
    {
        double Scale = 100;
        struct Vertex { double X = 0, Y = 0; int Breaks = 0; };   // Breaks：到下一点这段的折断数
        std::vector<Vertex> Vertices;
    };

    namespace Detail
    {
        // 新版 TCH_CUT 版本 2
        inline bool DecodeCutCipher(const MiniDWG::UnknownEntity& entity, TchCut& cut, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 2) return fail("unsupported native cut line version");
            f.ReadKey();
            f.Main.ReadByte();
            const int count = f.Main.ReadBitShort();
            if (count < 2 || count > 10000) return fail("invalid cut line vertex count");
            TchCut c;
            c.Scale = f.Scale;
            for (int i = 0; i < count && f.Ok(); ++i)
            {
                TchCut::Vertex v;
                v.X = f.Main.ReadRawDouble(); v.Y = f.Main.ReadRawDouble();
                const double breaks = f.Main.ReadBitDouble();
                if (!Finite(v.X) || !Finite(v.Y) || !(breaks >= 0 && breaks <= 100)) return fail("invalid cut line vertex");
                v.Breaks = static_cast<int>(std::lround(breaks));
                c.Vertices.push_back(v);
            }
            f.Byte();
            for (int i = 0; i < count; ++i) f.Bool();
            if (!f.Exact()) return fail("native cut line fields do not consume the payload exactly");
            if (!(c.Scale > 0)) return fail("invalid native cut line scale");
            cut = std::move(c);
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    inline bool DecodeCut(const MiniDWG::UnknownEntity& entity, TchCut& cut, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_CUT") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeCutCipher(entity, cut, diagnostic);
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for cut line");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        if (r.ReadByte() != 1) return fail("unsupported native cut line version");
        r.ReadByte();
        const int count = r.ReadBitShort();
        if (count < 2 || count > 10000) return fail("invalid cut line vertex count");
        TchCut c;
        c.Scale = base.Scale;
        for (int i = 0; i < count && !r.Failed(); ++i)
        {
            TchCut::Vertex v;
            v.X = r.ReadRawDouble(); v.Y = r.ReadRawDouble();
            const double breaks = r.ReadBitDouble();
            if (!Detail::Finite(v.X) || !Detail::Finite(v.Y) || !(breaks >= 0 && breaks <= 100)) return fail("invalid cut line vertex");
            v.Breaks = static_cast<int>(std::lround(breaks));
            c.Vertices.push_back(v);
        }
        r.ReadByte();
        for (int i = 0; i < count; ++i) r.ReadBit();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native cut line fields do not consume the payload exactly");
        if (!(c.Scale > 0)) return fail("invalid native cut line scale");
        cut = std::move(c);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildCut(const TchCut& c)
    {
        TchGraphics g;
        const double s = c.Scale;
        const std::size_t n = c.Vertices.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto& a = c.Vertices[i];
            if (a.Breaks <= 0) continue;
            const auto& b = c.Vertices[(i + 1) % n];
            const double L = std::hypot(b.X - a.X, b.Y - a.Y);
            if (L < 1e-9) continue;
            const double ux = (b.X - a.X) / L, uy = (b.Y - a.Y) / L;
            TchGraphics::Path path;
            auto at = [&](double t, double off) { return TchGraphics::PathPoint{{a.X + ux * t - uy * off, a.Y + uy * t + ux * off}}; };
            path.Points.push_back(at(0, 0));
            for (int k = 0; k < a.Breaks; ++k)
            {
                const double m = L * (k + 0.5) / a.Breaks;
                path.Points.push_back(at(m - 1.125 * s, 0));
                path.Points.push_back(at(m - 0.36066 * s, 2.1 * s));
                path.Points.push_back(at(m + 0.37991 * s, -2.1 * s));
                path.Points.push_back(at(m + 1.125 * s, 0));
            }
            path.Points.push_back(at(L, 0));
            g.Paths.push_back(std::move(path));
        }
        return g;
    }

    // ── TCH_SYMMETRY 对称轴（版本 < 2 为明文分支）──
    struct TchSymmetry
    {
        double Scale = 100;
        double X1 = 0, Y1 = 0, X2 = 0, Y2 = 0;
        double TickLength = 8, Gap = 2, Spacing = 2.5;     // 对称符号的横线长、第一条横线距端点、两横线间距（× 比例）
    };

    namespace Detail
    {
        inline bool DecodeSymmetryCipher(const MiniDWG::UnknownEntity& entity, TchSymmetry& symmetry, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 2) return fail("unsupported native symmetry version");
            f.ReadKey();
            TchSymmetry m;
            m.Scale = f.Scale;
            const auto a = f.Point2(), b = f.Point2();
            m.X1 = a.X; m.Y1 = a.Y; m.X2 = b.X; m.Y2 = b.Y;
            m.TickLength = f.Double(); m.Gap = f.Double(); m.Spacing = f.Double();
            if (!f.Exact()) return fail("native symmetry fields do not consume the payload exactly");
            for (double v : {m.X1, m.Y1, m.X2, m.Y2, m.TickLength, m.Gap, m.Spacing})
                if (!Finite(v)) return fail("invalid native symmetry settings");
            if (!(m.Scale > 0)) return fail("invalid native symmetry scale");
            symmetry = m;
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    inline bool DecodeSymmetry(const MiniDWG::UnknownEntity& entity, TchSymmetry& symmetry, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_SYMMETRY") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeSymmetryCipher(entity, symmetry, diagnostic);
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for symmetry");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        if (r.ReadByte() != 1) return fail("unsupported native symmetry version");
        TchSymmetry m;
        m.Scale = base.Scale;
        m.X1 = r.ReadRawDouble(); m.Y1 = r.ReadRawDouble();
        m.X2 = r.ReadRawDouble(); m.Y2 = r.ReadRawDouble();
        m.TickLength = r.ReadBitDouble(); m.Gap = r.ReadBitDouble(); m.Spacing = r.ReadBitDouble();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native symmetry fields do not consume the payload exactly");
        for (double v : {m.X1, m.Y1, m.X2, m.Y2, m.TickLength, m.Gap, m.Spacing})
            if (!Detail::Finite(v)) return fail("invalid native symmetry settings");
        if (!(m.Scale > 0)) return fail("invalid native symmetry scale");
        symmetry = m;
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildSymmetry(const TchSymmetry& m)
    {
        TchGraphics g;
        const double s = m.Scale, L = std::hypot(m.X2 - m.X1, m.Y2 - m.Y1);
        g.Lines.push_back({{m.X1, m.Y1}, {m.X2, m.Y2}});
        if (L < 1e-9) return g;
        const double ux = (m.X2 - m.X1) / L, uy = (m.Y2 - m.Y1) / L, half = m.TickLength * s / 2;
        for (double t : {m.Gap * s, (m.Gap + m.Spacing) * s, L - m.Gap * s, L - (m.Gap + m.Spacing) * s})
        {
            const double px = m.X1 + ux * t, py = m.Y1 + uy * t;
            g.Lines.push_back({{px - uy * half, py + ux * half}, {px + uy * half, py - ux * half}});
        }
        return g;
    }
    // ── TCH_MODI 修订云线（版本 1 的明文分支）──
    struct TchModi
    {
        std::vector<TchGraphics::Point> Points;     // 云线各弧的端点
        bool   Closed = true;
        double Bulge = 0.5;                         // 每段圆弧的凸度
    };

    namespace Detail
    {
        // 新版 TCH_MODI 版本 2-5；版本 ≥ 3 的附加多段线未画
        inline bool DecodeModiCipher(const MiniDWG::UnknownEntity& entity, TchModi& modi, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            struct V { double X = 0, Y = 0, Bulge = 0; };
            CipherReader f(entity);
            TchSymbolBase symbol;
            MiniDWG::Handle style = MiniDWG::kNullHandle;
            if (!ReadCipherSymbolBase(f, symbol, style)) return fail("unsupported Tianzheng symbol base layout");
            const int version = f.Main.ReadByte();
            if (version < 2 || version > 5) return fail("unsupported native revision cloud version");
            f.ReadKey();
            TchModi m;
            std::vector<V> vertices;
            if (!ReadPoly2D(f.Main, m.Closed, &vertices) || vertices.size() < 2) return fail("invalid revision cloud vertex count");
            for (const auto& v : vertices)
            {
                if (!Finite(v.X) || !Finite(v.Y)) return fail("invalid revision cloud vertex");
                m.Points.push_back({v.X, v.Y});
            }
            f.Double();
            m.Bulge = f.Double();
            f.Byte();
            f.Text();
            f.Point3();
            int kind = 0;
            if (version >= 3)
            {
                kind = f.Byte();
                if (kind == 1)
                {
                    f.Short(); f.Byte(); f.Double(); f.Double();
                    for (int i = 0; i < 8; ++i) f.Text();
                }
                const int extra = f.Short();
                for (int i = 0; i < extra && f.Ok(); ++i)
                {
                    bool closed = false;
                    if (!ReadPoly2D<V>(f.Main, closed, nullptr)) return fail("invalid revision cloud polyline");
                }
            }
            if (version >= 4)
            {
                for (int i = 0; i < 5; ++i) f.Text();
                f.Byte();
            }
            if (version >= 5)
            {
                f.Byte();
                if (kind == 1) f.Text();
            }
            if (!f.Exact()) return fail("native revision cloud fields do not consume the payload exactly");
            if (!Finite(m.Bulge) || std::abs(m.Bulge) > 10) return fail("invalid revision cloud bulge");
            modi = std::move(m);
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }

    // 符号基类、RC 版本（1）、RC 闭合、BS 点数与各点 2RD + BD、BD 弧长系数、BD 凸度、RC、编号（文字）、3BD 编号位置
    inline bool DecodeModi(const MiniDWG::UnknownEntity& entity, TchModi& modi, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_MODI") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeModiCipher(entity, modi, diagnostic);
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchSymbolBase symbol;
        if (!Detail::ReadSymbolHead(entity, "TCH_MODI", r, symbol)) return fail("unsupported Tianzheng symbol base layout");
        if (r.ReadByte() != 1) return fail("unsupported native revision cloud version");
        TchModi m;
        auto text = Detail::SymbolTexts(data, r, symbol);
        m.Closed = r.ReadByte() != 0;
        const int count = r.ReadBitShort();
        if (count < 2 || count > 100000) return fail("invalid revision cloud vertex count");
        for (int i = 0; i < count && !r.Failed(); ++i)
        {
            const double x = r.ReadRawDouble(), y = r.ReadRawDouble();
            r.ReadBitDouble();
            if (!Detail::Finite(x) || !Detail::Finite(y)) return fail("invalid revision cloud vertex");
            m.Points.push_back({x, y});
        }
        r.ReadBitDouble();
        m.Bulge = r.ReadBitDouble();
        r.ReadByte();
        text.Next();                                    // 编号
        r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
        if (r.Failed() || r.PositionInBits() != data.MainBits) return fail("native revision cloud fields do not consume the payload exactly");
        if (!Detail::Finite(m.Bulge) || std::abs(m.Bulge) > 10) return fail("invalid revision cloud bulge");
        modi = std::move(m);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildModi(const TchModi& m)
    {
        TchGraphics g;
        TchGraphics::Path p;
        for (const auto& pt : m.Points) p.Points.push_back({pt, m.Bulge});
        if (m.Closed && !m.Points.empty()) p.Points.push_back({m.Points.front()});
        if (!p.Points.empty()) p.Points.back().Bulge = 0;
        g.Paths.push_back(std::move(p));
        return g;
    }
}
