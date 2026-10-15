#pragma once
#include "Database/UnknownObjects.h"
#include "Dwg/Read/DwgBitReader.h"
#include <cmath>
#include <string>
#include <vector>

namespace MiniCAD::Tch
{
    // 天正对象公共基类（TCH_Base）私有数据中与显示无关、但决定位流布局的部分。
    // 布局依据已核对的基类私有布局；R2007 起字符串在文字流（R2004 及更早就地嵌在主数据流，见 InlineText）、
    // 句柄在句柄流，主数据流中不占位。只支持基类版本 1～5，更高版本含未核对的段，按不支持处理
    struct TchBase
    {
        int Version = 0;
        int HandleCount = 0;    // 基类在句柄流中占用的句柄数（派生类的句柄排在其后）
        int TextCount = 0;      // 基类在文字流中占用的字符串数（属性包的名字与字符串值）；R2004 及更早已就地读掉，为 0
        double Scale = 1;       // 出图比例（如 100）；标注、轴号的图面尺寸乘以它得到世界尺寸
    };

    namespace Detail
    {
        // R2004 及更早没有独立的文字流：天正对象的字符串（TV）就地写在主数据流中 dwgInFields 读它的位置
        inline bool InlineText(const MiniDWG::DwgBitReader& r) { return r.Version() < MiniDWG::CadVersion::AC1021; }

        // 天正对象的字符串：R2007 起依次取自文字流，更早的版本就地读主数据流。
        // 解码时必须在 dwgInFields 读字符串的位置调用 Next()（R2007 起只要求次序一致）
        struct TextStream
        {
            MiniDWG::DwgBitReader Reader;
            MiniDWG::DwgBitReader* Main = nullptr;
            TextStream(const MiniDWG::RawDwgData& data, MiniDWG::DwgBitReader& main)
                : Reader(data.Text, data.Version, MiniDWG::Codec::CodePage::Gbk), Main(InlineText(main) ? &main : nullptr) {}
            std::string Next() { return Main ? Main->ReadVariableText() : Reader.ReadVariableText(); }
        };

        // 天正对象支持的 DWG 版本：R2004 起（R2004 字符串在主数据流，R2007 起在文字流）
        inline bool SupportedVersion(const MiniDWG::RawDwgData& data)
        {
            return data.Version >= MiniDWG::CadVersion::AC1018 && data.Main.size() >= (data.MainBits + 7) / 8;
        }

        // 属性包：BL 个数；每项为名字（文字流）、BL 类型、按类型读值。类型 100 为嵌套属性包
        inline bool SkipPropertyBag(MiniDWG::DwgBitReader& r, int& texts, int& handles, int depth = 0)
        {
            const bool inlineText = InlineText(r);
            auto text = [&] { if (inlineText) r.ReadVariableText(); else ++texts; };
            if (depth > 8) return false;
            const int count = r.ReadBitLong();
            if (count < 0 || count > 4096) return false;
            for (int i = 0; i < count && !r.Failed(); ++i)
            {
                text();                                                             // 名字
                switch (r.ReadBitLong())
                {
                case 0: r.ReadBit(); break;
                case 1: case 2: case 3: r.ReadByte(); break;
                case 4: case 5: r.ReadBitShort(); break;
                case 6: case 7: r.ReadBitLong(); break;
                case 100: if (!SkipPropertyBag(r, texts, handles, depth + 1)) return false; break;
                case 0x65: text(); break;                                            // 字符串
                case 200: r.ReadBitDouble(); break;
                case 0xc9: case 0xcb: r.ReadRawDouble(); r.ReadRawDouble(); break;                // 二维点为 2RD
                case 0xca: case 0xcc: case 0xcd: r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble(); break;
                case 0xd0: case 0xd1: ++handles; break;                              // 句柄（在句柄流）
                default: return false;
                }
            }
            return !r.Failed();
        }
    }

    inline bool ReadTchBase(MiniDWG::DwgBitReader& r, TchBase& base)
    {
        base = {};
        base.Version = r.ReadByte();
        if (base.Version < 1 || base.Version > 5)
            return false;
        if (base.Version > 1) base.Scale = r.ReadBitDouble();
        if (base.Version > 2) r.ReadBitDouble();
        if (base.Version > 3)
        {
            const int flags = r.ReadByte();
            if (flags & 1) base.HandleCount += r.ReadByte();
            if (flags & 2) base.HandleCount += r.ReadByte();
        }
        if (base.Version > 4 && !Detail::SkipPropertyBag(r, base.TextCount, base.HandleCount))
            return false;
        return !r.Failed();
    }

    struct TchOpening
    {
        // 2 洞口（两侧各一条横穿墙厚的线）；5 带形窗（沿墙画窗线，没有二维图块）；8 组合门窗（由子门窗组成）
        enum class Kind { Door = 0, Window = 1, Hole = 2, Band = 5, Group = 8 };
        struct Segment { double X1 = 0, Y1 = 0, X2 = 0, Y2 = 0, Bulge = 0; };

        MiniDWG::XYZ Position;      // 墙轴线上的门窗中点；Z 为窗台高
        double Width = 0, Height = 0;
        double Thickness = 0;       // 门窗自身的厚度：没有所在墙时窗的二维图块按它缩放（8#楼平面图独立窗的分解结果核对）
        double Angle = 0;           // 弧度，沿所在墙的方向
        Kind   Type = Kind::Door;
        int    Mirror = 0;          // 0 不镜像，1 X，2 X 与 Y，3 Y（转换函数 0x706e70；3 已由天正 2014 分解结果核对）
        MiniDWG::Handle Block2D = MiniDWG::kNullHandle;  // 二维图块（$TCHSYS$DOOR2D / $TCHSYS$WIN2D）
        MiniDWG::Handle Wall = MiniDWG::kNullHandle;     // 所在墙（TCH_WALL 或幕墙 TCH_CURTAIN_WALL，可以为空）
        // 带形窗、组合门窗用：编号文字、出图比例、文字样式、分段（直段带形窗无分段，按 Position / Width / Angle）
        std::string Label;
        double LabelX = 0, LabelY = 0, LabelHeight = 3.5, Scale = 100;
        MiniDWG::Handle Style = MiniDWG::kNullHandle;
        std::vector<Segment> Segments;
        std::vector<TchOpening> Parts;                   // 组合门窗的子门窗（各自按门、窗显示）
        bool   InGroup = false;                          // 组合门窗的子门窗：不画门槛线

        bool MirrorX() const { return Mirror == 1 || Mirror == 2; }
        bool MirrorY() const { return Mirror == 2 || Mirror == 3; }
    };

    namespace Detail
    {
        // TDbOpening 的私有布局（版本 < 6 的明文分支）：天正基类之后的门窗字段，不检查是否读完
        inline bool ReadOpeningFields(MiniDWG::DwgBitReader& r, TextStream& text, TchOpening& o, int& kind, const char*& why)
        {
            const int version = r.ReadByte();
            if (version < 2 || version > 5) { why = "unsupported native opening version"; return false; }
            o.Position = {r.ReadBitDouble(), r.ReadBitDouble(), r.ReadBitDouble()};
            o.Width = r.ReadBitDouble();
            o.Thickness = r.ReadBitDouble();
            o.Height = r.ReadBitDouble();
            o.Angle = r.ReadBitDouble();
            o.Mirror = r.ReadByte();
            kind = r.ReadByte();
            r.ReadBitDouble();
            o.Label = text.Next();                                                              // 编号
            // 句柄流：二维图块、三维图块
            o.LabelX = r.ReadBitDouble(); o.LabelY = r.ReadBitDouble(); r.ReadBitDouble();     // 编号文字位置
            // 句柄流：所在墙
            const int flags = r.ReadBitShort();
            if ((flags & 1) || kind == 10) { r.ReadBitDouble(); r.ReadBitDouble(); }
            if (kind == 4)
            {
                r.ReadByte(); r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
                if (version > 4) r.ReadBitDouble();
            }
            else if (kind == 6 || kind == 7)
            {
                r.ReadBitDouble();
                if (kind == 6) r.ReadBitDouble();
            }
            // 句柄流：5 个（文字样式、图层）
            o.LabelHeight = r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
            const int segments = r.ReadBitShort();
            if (segments < 0 || segments > 1024) { why = "invalid opening segment count"; return false; }
            for (int i = 0; i < segments && !r.Failed(); ++i)
            {
                TchOpening::Segment s;
                s.X1 = r.ReadRawDouble(); s.Y1 = r.ReadRawDouble(); s.X2 = r.ReadRawDouble(); s.Y2 = r.ReadRawDouble();   // 两个二维点（2RD）
                s.Bulge = r.ReadBitDouble();
                o.Segments.push_back(s);
            }
            if (version > 2)
            {
                r.ReadBitLong(); r.ReadBitShort(); r.ReadBit();
                text.Next();
                for (int k = 0; k < 4; ++k) r.ReadBitDouble();
            }
            if (version > 3) r.ReadBitDouble();
            if (r.Failed()) { why = "native opening fields are truncated"; return false; }
            const double values[] = {o.Position.X, o.Position.Y, o.Position.Z, o.Width, o.Height, o.Angle};
            for (double v : values)
                if (!std::isfinite(v) || std::abs(v) > 1e12) { why = "nonfinite opening geometry"; return false; }
            if (o.Width <= 0 || o.Mirror < 0 || o.Mirror > 3) { why = "invalid opening size or mirror mode"; return false; }
            return true;
        }

        // 句柄流中一个门窗的 8 个句柄：二维图块、三维图块、所在墙、文字样式、4 个图层
        inline void ReadOpeningHandles(MiniDWG::DwgBitReader& handles, MiniDWG::Handle owner, TchOpening& o)
        {
            o.Block2D = handles.ReadHandle(owner);
            handles.ReadHandle(owner);
            o.Wall = handles.ReadHandle(owner);
            o.Style = handles.ReadHandle(owner);
            for (int i = 0; i < 4; ++i) handles.ReadHandle(owner);
        }
    }

    // TCH_OPENING 门窗：按 TDbOpening 的私有布局版本 < 6 的明文分支解析，必须恰好读完主数据流。
    // 接受门（类型 0）、窗（类型 1）、洞口（类型 2）、带形窗（类型 5）与组合门窗（类型 8，类 TDbGroupOpening）。门窗的显示方式
    // （插入二维图块，X 缩放为宽度，Y 缩放门为宽度、窗为墙厚，旋转为 Angle）与天正 2014 分解出的块参照一致，见 docs/Tch-compatibility.md。
    // 组合门窗（天正内核 TDbGroupOpening::dwgInFields）：自身的门窗字段之后为 RC 版本（0）、BL 子门窗数，
    // 每个子门窗是嵌入对象：AcDbEntity 通用字段（样例为固定的 37 位，句柄流中 4 个句柄）后接完整的 TDbOpening 数据
    inline bool DecodeOpening(const MiniDWG::UnknownEntity& entity, TchOpening& opening, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_OPENING") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for native opening");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        Detail::TextStream text(data, r);
        for (int i = 0; i < base.TextCount; ++i) text.Next();
        TchOpening o;
        int kind = 0;
        const char* why = "";
        if (!Detail::ReadOpeningFields(r, text, o, kind, why)) return fail(why);
        std::vector<TchBase> partBases;
        if (kind == 8)
        {
            if (r.ReadByte() != 0) return fail("unsupported native group opening version");
            const int count = r.ReadBitLong();
            if (count < 1 || count > 64) return fail("invalid group opening part count");
            for (int i = 0; i < count && !r.Failed(); ++i)
            {
                // 嵌入对象的 AcDbEntity 通用字段：样例为固定的 37 位
                static constexpr const char* kPrefix = "0010101101111100000000000001000011101";
                for (const char* c = kPrefix; *c; ++c)
                    if (r.ReadBit() != (*c == '1')) return fail("unsupported group opening part header");
                TchBase partBase;
                if (!ReadTchBase(r, partBase)) return fail("unsupported group opening part base layout");
                for (int k = 0; k < partBase.TextCount; ++k) text.Next();
                TchOpening part;
                int partKind = 0;
                if (!Detail::ReadOpeningFields(r, text, part, partKind, why)) return fail(why);
                if (partKind != 0 && partKind != 1) return fail("group opening parts must be doors or windows");
                part.Type = static_cast<TchOpening::Kind>(partKind);
                part.Scale = partBase.Scale;
                part.InGroup = true;
                part.Label.clear();
                o.Parts.push_back(std::move(part));
                partBases.push_back(partBase);
            }
        }
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native opening fields do not consume the payload exactly");
        if (kind != 0 && kind != 1 && kind != 2 && kind != 5 && kind != 8)
            return fail("only doors, windows, holes, band and group openings are supported");
        o.Type = static_cast<TchOpening::Kind>(kind);
        // 句柄流按读取顺序：基类句柄、二维图块、三维图块、所在墙……（空句柄也占位，所以不能用 References 的下标）
        DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
        for (int i = 0; i < base.HandleCount; ++i) handles.ReadHandle(entity.ObjectHandle);
        Detail::ReadOpeningHandles(handles, entity.ObjectHandle, o);
        for (std::size_t i = 0; i < o.Parts.size(); ++i)
        {
            for (int k = 0; k < 4 + partBases[i].HandleCount; ++k) handles.ReadHandle(entity.ObjectHandle);
            Detail::ReadOpeningHandles(handles, entity.ObjectHandle, o.Parts[i]);
            if (o.Parts[i].Block2D == kNullHandle) return fail("group opening part 2D block is missing");
        }
        const bool labelled = o.Type == TchOpening::Kind::Band || o.Type == TchOpening::Kind::Group;
        if (handles.Failed() || handles.PositionInBits() > data.HandleBits ||
            (o.Block2D == kNullHandle && !labelled && o.Type != TchOpening::Kind::Hole))
            return fail("opening handle stream is incomplete");
        // 编号只用于带形窗、组合门窗（普通门窗的编号由图块显示）
        if (labelled) o.Scale = base.Scale;
        if (!labelled || text.Reader.Failed()) o.Label.clear();
        opening = o;
        if (diagnostic) diagnostic->clear();
        return true;
    }
}
