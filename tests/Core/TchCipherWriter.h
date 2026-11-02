// 测试用：合成新版天正（基类版本 9～14）与旧版明文天正对象的三个数据流（TchCipherTests、TchSheetTests 共用）
#pragma once
#include "Import/TchCipher.h"
#include "Dwg/Write/DwgBitWriter.h"
#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

namespace TchTest
{ 
    inline void WriteEncrypted(MiniDWG::DwgBitWriter& text, std::string t, int key, int key2)
    {
        if (key > 0x10) { t.insert(1, "7"); t.insert(0, "3"); }
        if (key == 0x12 || key == 0x13) t.insert(0, "5");
        if (key >= 0x14) std::reverse(t.begin(), t.end());
        const auto [seed, base] = MiniCAD::Tch::Detail::CipherParams(key, key2);
        const int mask = (seed >> 8) & 0xff;
        text.WriteBitShort(static_cast<std::int16_t>(t.size() * 2));
        for (unsigned char c : t)
        {
            const int v = (c ^ mask) & 0xff;
            text.WriteRawShort(static_cast<std::int16_t>(base + v / 26));
            text.WriteRawShort(static_cast<std::int16_t>(base + v % 26));
        }
    } 

    struct CipherWriter
    {
        MiniDWG::DwgBitWriter Main{MiniDWG::CadVersion::AC1021, MiniDWG::Codec::CodePage::Gbk};
        MiniDWG::DwgBitWriter Text{MiniDWG::CadVersion::AC1021, MiniDWG::Codec::CodePage::Gbk};
        MiniDWG::DwgBitWriter Handles{MiniDWG::CadVersion::AC1021, MiniDWG::Codec::CodePage::Gbk};
        int Key = 0, Key2 = 0;

        void Number(const std::string& value, void (*plain)(MiniDWG::DwgBitWriter&, const std::string&))
        {
            if (Key < 16) plain(Main, value);
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
            Key = key; Key2 = key2;
            Main.WriteByte(11);
            Main.WriteByte(static_cast<std::uint8_t>(key));
            std::string guid = "{AB2C1D2E-5F60-4A7B-8C9D-0E1F2A3B4C5D}";
            guid[4] = static_cast<char>('0' + guidKey2 / 100);
            guid[16] = static_cast<char>('0' + guidKey2 / 10 % 10);
            guid[30] = static_cast<char>('0' + guidKey2 % 10);
            Text.WriteVariableText(guid);
            Double(scale);
            Double(0);
            Byte(0);                                            // 标志：没有基类句柄
            Main.WriteBitLong(0);                               // 属性包
            Text.WriteVariableText("TDBXDATAMAP_BLOCK_BEGIN_");
            for (const auto& [name, value] : xdata)
            {
                Text.WriteVariableText(name);
                Main.WriteBitLong(3);
                Main.WriteBitLong(value);
            }
            Text.WriteVariableText("TDBXDATAMAP_BLOCK_END_");
            Text.WriteVariableText("");
            Byte(0);
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
            raw.Version = MiniDWG::CadVersion::AC1021;
            raw.MainBits = Main.PositionInBits(); raw.Main = Main.Take();
            raw.TextBits = Text.PositionInBits(); raw.Text = Text.Take();
            raw.HandleBits = Handles.PositionInBits(); raw.Handles = Handles.Take();
            return e;
        }
    };
}
