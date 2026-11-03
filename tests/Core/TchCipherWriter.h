// 测试用：合成新版天正（基类版本 9～14）与旧版明文TCH对象的三个数据流（TchCipherTests、TchSheetTests 共用）
#pragma once
#include "Import/TchCipher.h"
#include "Dwg/Write/DwgBitWriter.h"
#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TchTest
{ 
    inline std::vector<int> EncryptUnits(std::string t, int key, int key2)
    {
        if (key > 0x10) { t.insert(1, "7"); t.insert(0, "3"); }
        if (key == 0x12 || key == 0x13) t.insert(0, "5");
        if (key >= 0x14) std::reverse(t.begin(), t.end());
        const auto [seed, base] = MiniCAD::Tch::Detail::CipherParams(key, key2);
        const int mask = (seed >> 8) & 0xff;
        std::vector<int> units;
        for (unsigned char c : t)
        {
            const int v = (c ^ mask) & 0xff;
            units.push_back(base + v / 26);
            units.push_back(base + v % 26);
        }
        return units;
    }

    // 2007 起：文字流里的 UTF-16
    inline void WriteEncrypted(MiniDWG::DwgBitWriter& text, const std::string& t, int key, int key2)
    {
        const std::vector<int> units = EncryptUnits(t, key, key2);
        text.WriteBitShort(static_cast<std::int16_t>(units.size()));
        for (int u : units)
            text.WriteRawShort(static_cast<std::int16_t>(u));
    }

    // 2004：主数据流里的代码页文字（代码页外的字符写成 \U+XXXX）
    inline void WriteEncryptedInline(MiniDWG::DwgBitWriter& main, const std::string& t, int key, int key2)
    {
        std::string utf8;
        for (int u : EncryptUnits(t, key, key2))
            MiniDWG::Codec::AppendUtf8(utf8, static_cast<char32_t>(u));
        main.WriteVariableText(utf8);
    }

    struct CipherWriter
    {
        // r2004：DWG 2004 格式，文字与密文都内嵌在主数据流里
        explicit CipherWriter(bool r2004 = false)
            : Main(r2004 ? MiniDWG::CadVersion::AC1018 : MiniDWG::CadVersion::AC1021, MiniDWG::Codec::CodePage::Gbk), Inline(r2004) {}

        MiniDWG::DwgBitWriter Main;
        MiniDWG::DwgBitWriter Text{MiniDWG::CadVersion::AC1021, MiniDWG::Codec::CodePage::Gbk};
        MiniDWG::DwgBitWriter Handles{MiniDWG::CadVersion::AC1021, MiniDWG::Codec::CodePage::Gbk};
        bool Inline = false;
        int Key = 0, Key2 = 0;

        MiniDWG::DwgBitWriter& Strings() { return Inline ? Main : Text; }

        void Number(const std::string& value, void (*plain)(MiniDWG::DwgBitWriter&, const std::string&))
        {
            if (Key < 16) plain(Main, value);
            else if (Inline) WriteEncryptedInline(Main, value, Key, Key2);
            else WriteEncrypted(Text, value, Key, Key2);
        }
        void Double(double v)
        {
            char s[64];
            std::snprintf(s, sizeof s, "%.16f", v);
            Number(s, [](MiniDWG::DwgBitWriter& w, const std::string& t) { w.WriteBitDouble(std::stod(t)); });
        }
        void Byte(int v) { Number(std::to_string(v), [](MiniDWG::DwgBitWriter& w, const std::string& t) { w.WriteByte(static_cast<std::uint8_t>(std::stoi(t))); }); }
        void Short(int v) { Number(std::to_string(v), [](MiniDWG::DwgBitWriter& w, const std::string& t) { w.WriteBitShort(static_cast<std::int16_t>(std::stoi(t))); }); }
        void Long(int v) { Number(std::to_string(v), [](MiniDWG::DwgBitWriter& w, const std::string& t) { w.WriteBitLong(std::stoi(t)); }); }
        void Bool(bool v) { Number(v ? "1" : "0", [](MiniDWG::DwgBitWriter& w, const std::string& t) { w.WriteBit(t != "0"); }); }
        void Point2(double x, double y)
        {
            if (Key < 17) { Main.WriteRawDouble(x); Main.WriteRawDouble(y); }
            else { Main.WriteRawDouble(y); Main.WriteRawDouble(x); }      // 存为 (y, x)
        }
        void Point3(double x, double y, double z)
        {
            if (Key < 17) { Main.WriteBitDouble(x); Main.WriteBitDouble(y); Main.WriteBitDouble(z); }
            else { Main.WriteBitDouble(y); Main.WriteBitDouble(z); Main.WriteBitDouble(x); }   // 存为 (y, z, x)
        }
        void Handle(MiniDWG::Handle h) { Handles.WriteHandle(MiniDWG::DwgRef::HardPointer, h); }

        // 天正基类版本 11：伪 GUID 第 4、16、30 个字符为第二个密钥的三位数字；xdata 为扩展数据表里的整数项（类型 3）
        void Base(int key, int key2, double scale, int guidKey2, std::initializer_list<std::pair<const char*, int>> xdata = {})
        {
            BaseBegin(key, key2, scale, guidKey2, 0, 0);
            MapBegin();
            for (const auto& [name, value] : xdata) MapInt(name, value);
            BaseEnd();
        }

        // 分段写天正基类：BaseBegin（到属性包项数）、FieldText / FieldNumber、MapBegin、Map*、BaseEnd
        void BaseBegin(int key, int key2, double scale, int guidKey2, double angle, int fields)
        {
            Key = key; Key2 = key2;
            Main.WriteByte(11);
            Main.WriteByte(static_cast<std::uint8_t>(key));
            std::string guid = "{AB2C1D2E-5F60-4A7B-8C9D-0E1F2A3B4C5D}";
            guid[4] = static_cast<char>('0' + guidKey2 / 100);
            guid[16] = static_cast<char>('0' + guidKey2 / 10 % 10);
            guid[30] = static_cast<char>('0' + guidKey2 % 10);
            Strings().WriteVariableText(guid);
            Double(scale);
            Double(angle);
            Byte(0);                                            // 标志：没有基类句柄
            Main.WriteBitLong(fields);
        }
        void FieldText(const char* name, const std::string& value) { Strings().WriteVariableText(name); Main.WriteBitLong(0x65); Strings().WriteVariableText(value); }
        void FieldNumber(const char* name, double value) { Strings().WriteVariableText(name); Main.WriteBitLong(200); Main.WriteBitDouble(value); }
        void MapBegin() { Strings().WriteVariableText("TDBXDATAMAP_BLOCK_BEGIN_"); }
        void MapInt(const char* name, int value) { Strings().WriteVariableText(name); Main.WriteBitLong(3); Main.WriteBitLong(value); }
        void MapNumber(const char* name, double value) { Strings().WriteVariableText(name); Main.WriteBitLong(5); Main.WriteBitDouble(value); }
        void MapPoint(const char* name, double x, double y, double z = 0)
        {
            Strings().WriteVariableText(name);
            Main.WriteBitLong(0x4005);
            Main.WriteBitDouble(x); Main.WriteBitDouble(y); Main.WriteBitDouble(z);
        }
        void MapHandle(const char* name, MiniDWG::Handle h) { Strings().WriteVariableText(name); Main.WriteBitLong(0x13); Handle(h); }
        void BaseEnd()
        {
            Strings().WriteVariableText("TDBXDATAMAP_BLOCK_END_");
            Strings().WriteVariableText("");
            Byte(0);
        }

        // 路径基类版本 3：RC 版本、RC 密钥、句柄
        void PathHead(int key, int key2 = 94)
        {
            Base(key, key2, 100, key2);
            Main.WriteByte(3);
            Main.WriteByte(static_cast<std::uint8_t>(key));
            Handle(0);
        }

        // 版本 + 密钥（之后的数值按这个密钥）
        void Layer(int version, int key)
        {
            Main.WriteByte(static_cast<std::uint8_t>(version));
            Main.WriteByte(static_cast<std::uint8_t>(key));
            Key = key;
        }

        void PlainPoly(std::initializer_list<std::pair<double, double>> points, bool closed = true)
        {
            Main.WriteByte(closed ? 1 : 0);
            Main.WriteBitShort(static_cast<std::int16_t>(points.size()));
            for (const auto& [x, y] : points) { Main.WriteRawDouble(x); Main.WriteRawDouble(y); Main.WriteBitDouble(0); }
        }

        // 旧版天正基类版本 5（明文）：比例、数值、标志 0、空属性包
        void PlainBase(double scale)
        {
            Key = 0; Key2 = 0;
            Main.WriteByte(5);
            Main.WriteBitDouble(scale);
            Main.WriteBitDouble(0);
            Main.WriteByte(0);
            Main.WriteBitLong(0);
        }

        std::unique_ptr<MiniDWG::UnknownEntity> Take(const char* dxf)
        {
            auto e = std::make_unique<MiniDWG::UnknownEntity>();
            e->Raw.DxfName = dxf;
            auto& raw = e->Raw.Dwg;
            raw.Version = Inline ? MiniDWG::CadVersion::AC1018 : MiniDWG::CadVersion::AC1021;
            raw.MainBits = Main.PositionInBits(); raw.Main = Main.Take();
            raw.TextBits = Text.PositionInBits(); raw.Text = Text.Take();
            raw.HandleBits = Handles.PositionInBits(); raw.Handles = Handles.Take();
            return e;
        }
    };
}
