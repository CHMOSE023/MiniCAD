#pragma once
#include "Codec/CodePage.h"
#include "Database/UnknownObjects.h"
#include "Dwg/Read/DwgBitReader.h"
#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MiniCAD::Tch
{ 
    struct TchXDataMap
    {
        struct Value { int Type = 0; double Number = 0; MiniDWG::XYZ Point; std::string Text; int HandleIndex = -1; MiniDWG::Handle Ref = MiniDWG::kNullHandle; };
        std::map<std::string, Value> Items;

        const Value* Find(const std::string& key) const
        {
            const auto it = Items.find(key);
            return it == Items.end() ? nullptr : &it->second;
        }
        std::optional<double> Number(const std::string& key) const
        {
            const auto* v = Find(key);
            return v && (v->Type == 3 || v->Type == 5) ? std::optional<double>(v->Number) : std::nullopt;
        }
        std::optional<MiniDWG::XYZ> Point(const std::string& key) const
        {
            const auto* v = Find(key);
            return v && v->Type >= 0x4005 && v->Type <= 0x4008 ? std::optional<MiniDWG::XYZ>(v->Point) : std::nullopt;
        }
        std::string Text(const std::string& key) const
        {
            const auto* v = Find(key);
            return v && (v->Type == 8 || v->Type == 0x4009) ? v->Text : std::string();
        }
    };
}

namespace MiniCAD::Tch::Detail
{ 
    inline bool IsCipherObject(const MiniDWG::RawDwgData& data)
    {
        return data.Version >= MiniDWG::CadVersion::AC1015 && data.MainBits >= 16 && data.Main.size() >= (data.MainBits + 7) / 8 &&
               data.Main[0] >= 9 && data.Main[0] <= 14;
    }
     
    inline bool Layout2Wcs(int param, std::string_view s, int& out)
    {
        out = 0;
        if (param < 10) return true;
        if (param >= 19) return false;
        if (s.size() > 35)
        {
            auto digit = [&](std::size_t i) { return s[i] >= '0' && s[i] <= '9' ? s[i] - '0' : 0; };
            out = digit(4) * 100 + digit(16) * 10 + digit(30);
        }
        return true;
    }
     
    inline std::pair<int, int> CipherParams(int key, int key2)
    {
        const int k = key2;
        if (key < 0x11) return {0x528, 0x41};
        if (key == 0x12)
        {
            if (k < 5)  return {k * 10 + 0x72, k * 8 + 0x76};
            if (k < 10) return {k * 11 + 0x93, k * 8 + 0xda};
            if (k < 15) return {k * 13 + 0x13a, k * 8 + 0x13e};
            if (k < 20) return {k * 15 + 0x9d, k * 8 + 0x1a2};
            return {k * 18 + 0xd6, k * 3 + 0x206};
        }
        if (key == 0x13)
        {
            if (k < 20) return {k * 10 + 0x68, k * 5 + 0xda};
            if (k < 40) return {k * 11 + 0x89, (k + 0x67) * 6};
            if (k < 55) return {k * 13 + 0x144, k * 7 + 0x76};
            if (k < 90) return {k * 15 + 0x9d, k * 8 + 0xda};
            return {k * 8 + 0xdf, (k + 0x6a) * 3};
        }
        if (key == 0x14)
        {
            if (k < 20) return {k * 10 + 0x72, k * 5 + 0xd0};
            if (k < 40) return {k * 11 + 0x25, k * 6 + 0xda};
            if (k < 55) return {k * 13 + 0xe0, k * 7 + 0x6c};
            if (k < 90) return {k * 15 + 0x101, k * 8 + 0x13e};
            return {k * 8 + 0x7b, k * 3 + 0x76};
        }
        if (key < 0x15) return k >= 0 && k < 100 ? std::pair{k + 0x9d2, 0xdf} : std::pair{0x9d2, 0xc6};
        if (k < 20) return {(k + 11) * 10, k * 5 + 0xcb};
        if (k < 40) return {k * 11 + 0x27, k * 6 + 0xd0};
        if (k < 55) return {k * 13 + 0xcc, k * 7 + 0x76};
        if (k < 90) return {k * 15 + 0xe3, k * 8 + 0x134};
        return {k * 8 + 0x71, k * 3 + 0x80};
    }
      
    class CipherReader
    {
    public:
        explicit CipherReader(const MiniDWG::UnknownEntity& entity)
            : Main(entity.Raw.Dwg.Main, entity.Raw.Dwg.Version, MiniDWG::Codec::CodePage::Gbk),
              m_data(entity.Raw.Dwg), m_owner(entity.ObjectHandle),
              m_text(entity.Raw.Dwg.Text, entity.Raw.Dwg.Version, MiniDWG::Codec::CodePage::Gbk),
              m_handles(entity.Raw.Dwg.Handles, entity.Raw.Dwg.Version, MiniDWG::Codec::CodePage::Gbk),
              m_inline(entity.Raw.Dwg.Version < MiniDWG::CadVersion::AC1021) {}

        MiniDWG::DwgBitReader Main;      
        int    Key   = 0;                 
        int    Key2  = 0;                 
        double Scale = 1;               
        MiniDWG::Handle LastBaseHandle = MiniDWG::kNullHandle;    
        double Angle = 0;
        TchXDataMap XData;
        std::map<std::string, std::string> FieldStrings;
        std::map<std::string, double> FieldNumbers;

        void ReadKey() { Key = Main.ReadByte(); }
        double Double() { return Key < 16 ? Main.ReadBitDouble() : ParseDouble(Decrypt()); }
        int Byte() { return Key < 16 ? Main.ReadByte() : static_cast<std::uint8_t>(ParseInt(Decrypt())); }
        int Short() { return Key < 16 ? static_cast<std::uint16_t>(Main.ReadBitShort()) : static_cast<std::uint16_t>(ParseInt(Decrypt())); }
        std::int32_t Long() { return Key < 16 ? Main.ReadBitLong() : static_cast<std::int32_t>(ParseInt(Decrypt())); }
        bool Bool() { return Key < 16 ? Main.ReadBit() != 0 : ParseInt(Decrypt()) != 0; }
        MiniDWG::XYZ Point3()
        {
            const auto p = Main.Read3BitDouble();
            return Key < 17 ? p : MiniDWG::XYZ{p.Z, p.X, p.Y};
        }
        MiniDWG::XY Point2()
        {
            const auto p = Main.Read2RawDouble();
            return Key < 17 ? p : MiniDWG::XY{p.Y, p.X};
        }
        std::string Text() { return m_inline ? Main.ReadVariableText() : m_text.ReadVariableText(); }
        MiniDWG::Handle Handle() { return m_handles.ReadHandle(m_owner); } 

        void SkipBase(int texts, int handles)
        {
            for (int i = 0; i < texts && Ok(); ++i) Text();
            for (int i = 0; i < handles && Ok(); ++i) LastBaseHandle = Handle();
        }
         
        bool HasMoreValues() const
        {
            return Key < 16 || m_inline ? Main.PositionInBits() < m_data.MainBits : m_text.PositionInBits() + 2 <= m_data.TextBits;
        }
        bool Ok() const { return !m_bad && !Main.Failed() && !m_text.Failed() && !m_handles.Failed(); } 

        bool Exact() const
        {
            return Ok() && Main.PositionInBits() == m_data.MainBits && m_text.PositionInBits() <= m_data.TextBits &&
                   m_handles.PositionInBits() <= m_data.HandleBits;
        }
 
        bool ReadBase()
        {
            const int version = Main.ReadByte();
            if (version < 9 || version > 14) return false;
            ReadKey();
            if (version >= 10)
            {
                const std::string guid = Text();
                int key2 = 0;
                if (version < 14)
                {
                    if (!Layout2Wcs(version, guid, key2)) return false;
                    if (version > 12) key2 -= 2;
                }
                else if (!Layout2Wcs(Key, guid, key2)) return false;
                Key2 = static_cast<signed char>(key2 & 0xff);     // 按有符号字节保存
            }
            if (version > 12) Short();
            Scale = Double();
            Angle = Double();
            const int flags = Byte();
            if (flags & 1) for (int i = 0, n = Byte() - 5; i < n && Ok(); ++i) LastBaseHandle = Handle();
            if (flags & 2) for (int i = 0, n = Byte() - 3; i < n && Ok(); ++i) LastBaseHandle = Handle();
            if (!ReadFields(0) || !ReadXDataMap()) return false;
            Text();
            if (version >= 10) Byte();
            return Ok();
        }

    private:

        bool ReadFields(int depth)
        {
            if (depth > 8) return false;
            const int count = Main.ReadBitLong();
            if (count < 0 || count > 4096) return false;
            for (int i = 0; i < count && Ok(); ++i)
            {
                const std::string name = Text();
                switch (Main.ReadBitLong())
                {
                case 0: Main.ReadBit(); break;
                case 1: case 2: case 3: Main.ReadByte(); break;
                case 4: case 5: Main.ReadBitShort(); break;
                case 6: case 7: Main.ReadBitLong(); break;
                case 100: if (!ReadFields(depth + 1)) return false; break;
                case 0x65: FieldStrings[name] = Text(); break;
                case 200: FieldNumbers[name] = Main.ReadBitDouble(); break;
                case 0xc9: case 0xcb: Main.Read2RawDouble(); break;
                case 0xca: case 0xcc: case 0xcd: Main.Read3BitDouble(); break;
                case 0xd0: case 0xd1: LastBaseHandle = Handle(); break;
                default: return false;
                }
            }
            return Ok();
        }
         
        bool ReadXDataMap()
        {
            int guard = 0;
            for (std::string s = Text(); s != "TDBXDATAMAP_BLOCK_BEGIN_"; s = Text())
                if (++guard > 64 || !Ok()) return false;
            for (std::string key = Text(); key != "TDBXDATAMAP_BLOCK_END_"; key = Text())
            {
                if (++guard > 4096 || !Ok()) return false;
                TchXDataMap::Value v;
                v.Type = Main.ReadBitLong();
                switch (v.Type)
                {
                case 3: v.Number = Main.ReadBitLong(); break;
                case 5: v.Number = Main.ReadBitDouble(); break;
                case 8: case 0x4009: v.Text = Text(); break;
                case 0x13: case 0x15: v.Ref = Handle(); break;
                case 0x4005: case 0x4006: case 0x4007: case 0x4008: v.Point = Main.Read3BitDouble(); break;
                default: return false;
                }
                if (const auto at = key.find("$vector3d"); at != std::string::npos) key.erase(at);
                XData.Items[key] = std::move(v);
            }
            return Ok();
        }
         
        std::string Decrypt()
        {
            std::string s;
            std::vector<int> units;    
            if (m_inline)
            {
                // 2004 及更早：代码页外的字符写成 \U+XXXX
                const std::string utf8 = MiniDWG::Codec::DecodeUnicodeEscapes(Main.ReadVariableText());
                for (std::size_t i = 0; i < utf8.size();)
                    units.push_back(static_cast<int>(NextCodePoint(utf8, i) & 0xff));
            }
            else
            {
                const int length = m_text.ReadBitShort();
                if (length < 0 || length > 4096) { m_bad = true; return s; }
                for (int i = 0; i < length; ++i)
                    units.push_back(static_cast<std::uint16_t>(m_text.ReadRawShort()) & 0xff);
            }
            if (units.size() > 4096) { m_bad = true; return s; }
            const auto [seed, base] = CipherParams(Key, Key2);
            const int mask = (seed >> 8) & 0xff;
            for (std::size_t i = 0; i + 1 < units.size(); i += 2)
                s += static_cast<char>((((units[i] - base) * 26 + (units[i + 1] - base)) & 0xff) ^ mask);
            auto erase = [&](std::size_t at) { if (at < s.size()) s.erase(at, 1); };
            if (Key >= 0x14) s.assign(s.rbegin(), s.rend());
            if (Key == 0x12 || Key == 0x13) erase(0);
            if (Key > 0x10) { erase(0); erase(1); }
            return s;
        }
         
        static char32_t NextCodePoint(const std::string& s, std::size_t& i)
        {
            const auto c = static_cast<unsigned char>(s[i++]);
            const int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
            char32_t cp = extra ? c & (0x3f >> extra) : c;
            for (int k = 0; k < extra && i < s.size(); ++k)
                cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3f);
            return cp;
        }

        static std::string_view Trim(std::string_view s)
        {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
            if (!s.empty() && s.front() == '+') s.remove_prefix(1);
            return s;
        }
        static double ParseDouble(std::string_view s)
        {
            s = Trim(s);
            double v = 0;
            std::from_chars(s.data(), s.data() + s.size(), v);
            return v;
        }
        static long long ParseInt(std::string_view s)
        {
            s = Trim(s);
            long long v = 0;
            std::from_chars(s.data(), s.data() + s.size(), v);
            return v;
        }

        const MiniDWG::RawDwgData& m_data;
        MiniDWG::Handle m_owner;
        MiniDWG::DwgBitReader m_text, m_handles;
        bool m_inline = false;          // R2004：字符串就地在主数据流
        bool m_bad    = false;
    };

    // 自带 RC 版本与 RC 密钥、第二个密钥为 0 的子层（表格各子对象、屋顶基类）；读完恢复外层的两个密钥
    class CipherLayer
    {
    public:
        explicit CipherLayer(CipherReader& f) : m_f(f), m_key(f.Key), m_key2(f.Key2) { f.Key2 = 0; }
        ~CipherLayer() { m_f.Key = m_key; m_f.Key2 = m_key2; }
        CipherLayer(const CipherLayer&) = delete;
        CipherLayer& operator=(const CipherLayer&) = delete;
        // RC 版本（大于 maxVersion 为不支持，返回 −1）；版本 ≥ keyFrom 时 RC 密钥，否则按明文
        int Version(int maxVersion, int keyFrom)
        {
            const int version = m_f.Main.ReadByte();
            if (version > maxVersion) return -1;
            m_f.Key = version >= keyFrom ? m_f.Main.ReadByte() : 0;
            return version;
        }

    private:
        CipherReader& m_f;
        int m_key, m_key2;
    };

    // 新旧版字段布局相同的类共用一份字段读取：PlainFields 的句柄在句柄流里，H() 返回空句柄
    struct PlainFields
    {
        MiniDWG::DwgBitReader& R;
        double D() { return R.ReadBitDouble(); }
        int RC() { return R.ReadByte(); }
        int BS() { return R.ReadBitShort(); }
        std::int32_t BL() { return R.ReadBitLong(); }
        bool B() { return R.ReadBit() != 0; }
        MiniDWG::XYZ P3() { return R.Read3BitDouble(); }
        MiniDWG::XY P2() { return R.Read2RawDouble(); }
        MiniDWG::Handle H() { return MiniDWG::kNullHandle; }
        std::string T() { return {}; }
        MiniDWG::DwgBitReader& Main() { return R; }
        bool Ok() const { return !R.Failed(); }
    };

    struct CipherFields
    {
        CipherReader& F;
        double D() { return F.Double(); }
        int RC() { return F.Byte(); }
        int BS() { return static_cast<std::int16_t>(F.Short()); }
        std::int32_t BL() { return F.Long(); }
        bool B() { return F.Bool(); }
        MiniDWG::XYZ P3() { return F.Point3(); }
        MiniDWG::XY P2() { return F.Point2(); }
        MiniDWG::Handle H() { return F.Handle(); }
        std::string T() { return F.Text(); }
        MiniDWG::DwgBitReader& Main() { return F.Main; }
        bool Ok() const { return F.Ok(); }
    };

    // 新版路径基类 版本 3
    inline bool ReadCipherPathHead(CipherReader& f)
    {
        if (!f.ReadBase() || f.Main.ReadByte() != 3) return false;
        f.ReadKey();
        f.Handle();
        return f.Ok();
    }

    struct CipherPathVertex { MiniDWG::XYZ Point; double Value = 0; };

    // 新版天正曲线 版本 1
    inline bool ReadCipherPolyCurve(CipherReader& f, std::vector<CipherPathVertex>& vertices, bool& closed, MiniDWG::XYZ& normal)
    {
        if (f.Main.ReadByte() != 1) return false;
        f.ReadKey();
        f.Handle();
        const int count = f.Short();
        if (count < 0 || count > 100000) return false;
        vertices.clear();
        for (int i = 0; i < count && f.Ok(); ++i)
        {
            CipherPathVertex v;
            v.Point = f.Point3();
            v.Value = f.Double();
            vertices.push_back(v);
        }
        closed = f.Bool();
        normal = f.Point3();
        return f.Ok();
    }
}
