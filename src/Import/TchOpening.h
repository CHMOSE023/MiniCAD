#pragma once
#include "Database/UnknownObjects.h"
#include "Dwg/Read/DwgBitReader.h"
#include <cmath>
#include <string>

namespace MiniCAD::Tch
{
    // 天正对象公共基类（TCH_Base）私有数据中与显示无关、但决定位流布局的部分。
    // 布局依据已核对的基类私有布局；字符串在文字流、
    // 句柄在句柄流，主数据流中不占位。只支持基类版本 1～5，更高版本含未核对的段，按不支持处理
    struct TchBase
    {
        int Version = 0;
        int HandleCount = 0;    // 基类在句柄流中占用的句柄数（派生类的句柄排在其后）
        int TextCount = 0;      // 基类在文字流中占用的字符串数（属性包的名字与字符串值）
        double Scale = 1;       // 出图比例（如 100）；标注、轴号的图面尺寸乘以它得到世界尺寸
    };

    namespace Detail
    {
        // 属性包：BL 个数；每项为名字（文字流）、BL 类型、按类型读值。类型 100 为嵌套属性包
        inline bool SkipPropertyBag(MiniDWG::DwgBitReader& r, int& texts, int& handles, int depth = 0)
        {
            if (depth > 8) return false;
            const int count = r.ReadBitLong();
            if (count < 0 || count > 4096) return false;
            for (int i = 0; i < count && !r.Failed(); ++i)
            {
                ++texts;                                                            // 名字
                switch (r.ReadBitLong())
                {
                case 0: r.ReadBit(); break;
                case 1: case 2: case 3: r.ReadByte(); break;
                case 4: case 5: r.ReadBitShort(); break;
                case 6: case 7: r.ReadBitLong(); break;
                case 100: if (!SkipPropertyBag(r, texts, handles, depth + 1)) return false; break;
                case 0x65: ++texts; break;                                           // 字符串
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
        enum class Kind { Door = 0, Window = 1 };

        MiniDWG::XYZ Position;      // 墙轴线上的门窗中点；Z 为窗台高
        double Width = 0, Height = 0;
        double Angle = 0;           // 弧度，沿所在墙的方向
        Kind   Type = Kind::Door;
        int    Mirror = 0;          // 0 不镜像，1 X，2 X 与 Y，3 Y（转换函数 0x706e70；3 已由天正 2014 分解结果核对）
        MiniDWG::Handle Block2D = MiniDWG::kNullHandle;  // 二维图块（$TCHSYS$DOOR2D / $TCHSYS$WIN2D）
        MiniDWG::Handle Wall = MiniDWG::kNullHandle;     // 所在墙（TCH_WALL）

        bool MirrorX() const { return Mirror == 1 || Mirror == 2; }
        bool MirrorY() const { return Mirror == 2 || Mirror == 3; }
    };

    // TCH_OPENING 门窗：按 TDbOpening 的私有布局版本 < 6 的明文分支解析，
    // 必须恰好读完主数据流。只接受门（类型 0）与窗（类型 1）。显示方式（插入二维图块，X 缩放为宽度，
    // Y 缩放门为宽度、窗为墙厚，旋转为 Angle）与天正 2014 分解出的块参照一致，见 docs/Tch-compatibility.md
    inline bool DecodeOpening(const MiniDWG::UnknownEntity& entity, TchOpening& opening, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        if (entity.Raw.DxfName != "TCH_OPENING") return false;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (data.Version < CadVersion::AC1021 || data.Main.size() < (data.MainBits + 7) / 8)
            return fail("unsupported DWG version for native opening");
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        const int version = r.ReadByte();
        if (version < 2 || version > 5) return fail("unsupported native opening version");

        TchOpening o;
        o.Position = {r.ReadBitDouble(), r.ReadBitDouble(), r.ReadBitDouble()};
        o.Width = r.ReadBitDouble();
        r.ReadBitDouble();
        o.Height = r.ReadBitDouble();
        o.Angle = r.ReadBitDouble();
        o.Mirror = r.ReadByte();
        const int kind = r.ReadByte();
        r.ReadBitDouble();
        // 文字流：编号；句柄流：二维图块、三维图块
        r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();     // 编号文字位置
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
        r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
        const int segments = r.ReadBitShort();
        if (segments < 0 || segments > 1024) return fail("invalid opening segment count");
        for (int i = 0; i < segments && !r.Failed(); ++i)
        {
            r.ReadRawDouble(); r.ReadRawDouble(); r.ReadRawDouble(); r.ReadRawDouble();   // 两个二维点（2RD）
            r.ReadBitDouble();
        }
        if (version > 2)
        {
            r.ReadBitLong(); r.ReadBitShort(); r.ReadBit();
            for (int k = 0; k < 4; ++k) r.ReadBitDouble();      // 字符串在文字流
        }
        if (version > 3) r.ReadBitDouble();
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native opening fields do not consume the payload exactly");

        if (kind != 0 && kind != 1) return fail("only doors and windows are supported");
        o.Type = static_cast<TchOpening::Kind>(kind);
        const double values[] = {o.Position.X, o.Position.Y, o.Position.Z, o.Width, o.Height, o.Angle};
        for (double v : values)
            if (!std::isfinite(v) || std::abs(v) > 1e12) return fail("nonfinite opening geometry");
        if (o.Width <= 0 || o.Mirror < 0 || o.Mirror > 3) return fail("invalid opening size or mirror mode");
        // 句柄流按读取顺序：基类句柄、二维图块、三维图块、所在墙（空句柄也占位，所以不能用 References 的下标）
        DwgBitReader handles(data.Handles, data.Version, Codec::CodePage::Gbk);
        for (int i = 0; i < base.HandleCount; ++i) handles.ReadHandle(entity.ObjectHandle);
        o.Block2D = handles.ReadHandle(entity.ObjectHandle);
        handles.ReadHandle(entity.ObjectHandle);
        o.Wall = handles.ReadHandle(entity.ObjectHandle);
        if (handles.Failed() || handles.PositionInBits() > data.HandleBits || o.Block2D == kNullHandle)
            return fail("opening handle stream is incomplete");
        opening = o;
        if (diagnostic) diagnostic->clear();
        return true;
    }
}
