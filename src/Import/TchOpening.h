#pragma once
#include "Database/UnknownObjects.h"
#include "Dwg/Read/DwgBitReader.h"
#include "Import/TchCipher.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace MiniCAD::Tch
{ 
    struct TchBase
    {
        int Version = 0;
        int HandleCount = 0;    // 基类在句柄流中占用的句柄数（派生类的句柄排在其后）
        int TextCount = 0;      // 基类在文字流中占用的字符串数（属性包的名字与字符串值）；R2004 及更早已就地读掉，为 0
        double Scale = 1;       // 出图比例（如 100）；标注、轴号的图面尺寸乘以它得到世界尺寸
    };

    namespace Detail
    {
        inline bool InlineText(const MiniDWG::DwgBitReader& r) { return r.Version() < MiniDWG::CadVersion::AC1021; }

        struct TextStream
        {
            MiniDWG::DwgBitReader Reader;
            MiniDWG::DwgBitReader* Main = nullptr;
            TextStream(const MiniDWG::RawDwgData& data, MiniDWG::DwgBitReader& main)
                : Reader(data.Text, data.Version, MiniDWG::Codec::CodePage::Gbk), Main(InlineText(main) ? &main : nullptr) {}
            std::string Next() { return Main ? Main->ReadVariableText() : Reader.ReadVariableText(); }
        };

        inline bool SupportedVersion(const MiniDWG::RawDwgData& data)
        {
            return data.Version >= MiniDWG::CadVersion::AC1015 && data.Main.size() >= (data.MainBits + 7) / 8;
        }

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

    namespace Detail
    {
        // 版本 7 的属性表：键在文字流，值在主数据流；data 用来从头读文字流（只适用于对象自身的基类）
        inline bool SkipXDataMap(MiniDWG::DwgBitReader& r, const MiniDWG::RawDwgData& data, TchBase& base)
        {
            TextStream text(data, r);
            const bool inlineText = text.Main != nullptr;
            for (int i = 0; i < base.TextCount; ++i) text.Next();
            int texts = 0, guard = 0;
            auto next = [&] { ++texts; return text.Next(); };
            for (std::string s = next(); s != "TDBXDATAMAP_BLOCK_BEGIN_"; s = next())
                if (++guard > 64 || r.Failed() || text.Reader.Failed()) return false;
            for (std::string key = next(); key != "TDBXDATAMAP_BLOCK_END_"; key = next())
            {
                if (++guard > 4096 || r.Failed() || text.Reader.Failed()) return false;
                switch (r.ReadBitLong())
                {
                case 3: r.ReadBitLong(); break;
                case 5: r.ReadBitDouble(); break;
                case 8: case 0x4009: next(); break;
                case 0x13: case 0x15: ++base.HandleCount; break;
                case 0x4005: case 0x4006: case 0x4007: case 0x4008: r.Read3BitDouble(); break;
                default: return false;
                }
            }
            if (!inlineText) base.TextCount += texts;
            return !r.Failed() && !text.Reader.Failed();
        }
    }

    // 版本 1～5 旧版明文；6～8（T20）在版本 5 后加属性表，要传 data
    inline bool ReadTchBase(MiniDWG::DwgBitReader& r, TchBase& base, const MiniDWG::RawDwgData* data = nullptr)
    {
        base = {};
        base.Version = r.ReadByte();
        const bool t20 = base.Version >= 6 && base.Version <= 8;
        if (base.Version < 1 || base.Version > 8 || (t20 && !data))
            return false;
        if (base.Version > 1) base.Scale = r.ReadBitDouble();
        if (base.Version > 2) r.ReadBitDouble();
        if (base.Version > 3)
        {
            const int flags = r.ReadByte();
            const int bias = base.Version >= 7 ? 5 : 0;
            if (flags & 1) base.HandleCount += r.ReadByte() - bias;
            if (flags & 2) base.HandleCount += r.ReadByte() - (bias ? 3 : 0);
            if (base.HandleCount < 0) return false;
        }
        if (base.Version > 4 && !Detail::SkipPropertyBag(r, base.TextCount, base.HandleCount))
            return false;
        if (t20 && !Detail::SkipXDataMap(r, *data, base))
            return false;
        if (base.Version == 8)
            r.ReadVariableText();
        return !r.Failed();
    }

    struct TchOpening
    {
        enum class Kind { Door = 0, Window = 1, Hole = 2, Band = 5, Group = 8 };
        struct Segment { double X1 = 0, Y1 = 0, X2 = 0, Y2 = 0, Bulge = 0; };

        MiniDWG::XYZ Position;      // 墙轴线上的门窗中点；Z 为窗台高
        double Width = 0, Height = 0;
        double Thickness = 0;       // 门窗自身的厚度：没有所在墙时窗的二维图块按它缩放（建筑平面样图独立窗的分解结果核对）
        double Angle = 0;           // 弧度，沿所在墙的方向
        Kind   Type = Kind::Door;
        int    Mirror = 0;          
        MiniDWG::Handle Block2D = MiniDWG::kNullHandle;  // 二维图块（$TCHSYS$DOOR2D / $TCHSYS$WIN2D）
        MiniDWG::Handle Wall = MiniDWG::kNullHandle;     // 所在墙（TCH_WALL 或幕墙 TCH_CURTAIN_WALL，可以为空）
        std::string Label;
        double LabelX = 0, LabelY = 0, LabelHeight = 3.5, Scale = 100;
        MiniDWG::Handle Style = MiniDWG::kNullHandle;
        MiniDWG::Handle LabelLayer = MiniDWG::kNullHandle;
        std::vector<Segment> Segments;
        std::vector<TchOpening> Parts;                   // 组合门窗的子门窗（各自按门、窗显示）
        bool   InGroup = false;                          // 组合门窗的子门窗：不画门槛线
        int    Flags = 0;
        std::int32_t DisplayFlags = 0;

        bool MirrorX() const { return Mirror == 1 || Mirror == 2; }
        bool MirrorY() const { return Mirror == 2 || Mirror == 3; }
    };

    namespace Detail
    {
        inline bool CheckOpening(const TchOpening& o, const char*& why)
        {
            const double values[] = {o.Position.X, o.Position.Y, o.Position.Z, o.Width, o.Height, o.Angle};
            for (double v : values)
                if (!std::isfinite(v) || std::abs(v) > 1e12) { why = "nonfinite opening geometry"; return false; }
            if (o.Width <= 0 || o.Mirror < 0 || o.Mirror > 3) { why = "invalid opening size or mirror mode"; return false; }
            return true;
        }

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
            const int flags = o.Flags = r.ReadBitShort();
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
                o.DisplayFlags = r.ReadBitLong(); r.ReadBitShort(); r.ReadBit();
                text.Next();
                for (int k = 0; k < 4; ++k) r.ReadBitDouble();
            }
            if (version > 3) r.ReadBitDouble();
            if (r.Failed()) { why = "native opening fields are truncated"; return false; }
            return CheckOpening(o, why);
        }

        inline void ReadOpeningHandles(MiniDWG::DwgBitReader& handles, MiniDWG::Handle owner, TchOpening& o)
        {
            o.Block2D = handles.ReadHandle(owner);
            handles.ReadHandle(owner);
            o.Wall = handles.ReadHandle(owner);
            o.Style = handles.ReadHandle(owner);
            o.LabelLayer = handles.ReadHandle(owner);
            for (int i = 0; i < 3; ++i) handles.ReadHandle(owner);
        }
         
        inline bool DecodeOpeningCipher(const MiniDWG::UnknownEntity& entity, TchOpening& opening, std::string* diagnostic)
        {
            auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
            CipherReader f(entity);
            if (!f.ReadBase()) return fail("unsupported Tianzheng base layout");
            if (f.Main.ReadByte() != 6) return fail("unsupported native opening version");
            f.ReadKey();
            TchOpening o;
            o.Position = f.Point3();
            o.Width = f.Double(); o.Thickness = f.Double(); o.Height = f.Double(); o.Angle = f.Double();
            o.Mirror = f.Byte();
            const int kind = f.Byte();
            f.Double();
            o.Label = f.Text();
            o.Block2D = f.Handle();
            f.Handle();
            const auto label = f.Point3();
            o.LabelX = label.X; o.LabelY = label.Y;
            o.Wall = f.Handle();
            o.Flags = f.Short();
            if ((o.Flags & 1) || kind == 10) { f.Double(); f.Double(); }
            if (kind == 4 || kind == 8) return fail("bay and group openings are not supported");
            if (kind == 6 || kind == 7 || kind == 10) { f.Double(); if (kind != 7) f.Double(); }
            o.Style = f.Handle();
            o.LabelLayer = f.Handle();
            for (int i = 0; i < 3; ++i) f.Handle();
            o.LabelHeight = f.Double(); f.Double(); f.Double(); f.Double();
            const int segments = f.Short();
            if (segments > 1024) return fail("invalid opening segment count");
            for (int i = 0; i < segments && f.Ok(); ++i)
            {
                const auto a = f.Point2(), b = f.Point2();
                o.Segments.push_back({a.X, a.Y, b.X, b.Y, f.Double()});
            }
            o.DisplayFlags = f.Long(); f.Short(); f.Bool(); f.Text();
            for (int i = 0; i < 5; ++i) f.Double();
            if (!f.Exact()) return fail("native opening fields do not consume the payload exactly");
            const char* why = "";
            if (!CheckOpening(o, why)) return fail(why);
            if (kind != 0 && kind != 1 && kind != 2 && kind != 5) return fail("only doors, windows, holes and band openings are supported");
            o.Type = static_cast<TchOpening::Kind>(kind);
            if (o.Block2D == MiniDWG::kNullHandle && o.Type != TchOpening::Kind::Band && o.Type != TchOpening::Kind::Hole)
                return fail("opening handle stream is incomplete");
            o.Scale = f.Scale;
            opening = std::move(o);
            if (diagnostic) diagnostic->clear();
            return true;
        }
    }
     
    inline bool DecodeOpening(const MiniDWG::UnknownEntity& entity, TchOpening& opening, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_OPENING") return false;
        if (Detail::IsCipherObject(data)) return Detail::DecodeOpeningCipher(entity, opening, diagnostic);
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (!Detail::SupportedVersion(data)) return fail("unsupported DWG version for native opening");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base, &data)) return fail("unsupported Tianzheng base layout");
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
                if (!ReadTchBase(r, partBase, &data)) return fail("unsupported group opening part base layout");
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
        o.Scale = base.Scale;
        if (text.Reader.Failed()) o.Label.clear();
        opening = o;
        if (diagnostic) diagnostic->clear();
        return true;
    }
}
