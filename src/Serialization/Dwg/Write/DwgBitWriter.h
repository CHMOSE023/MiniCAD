#pragma once
#include "Codec/CodePage.h"
#include "Database/CadVersion.h"
#include "Database/Color.hpp"
#include "Database/Handle.hpp"
#include "Database/Transparency.hpp"
#include "Database/Types.hpp"
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace MiniDWG
{
    // DWG 句柄引用的类型码（句柄字节前的高 4 位）
    enum class DwgRef : std::uint8_t
    {
        Undefined     = 0,
        SoftOwnership = 2,
        HardOwnership = 3,
        SoftPointer   = 4,
        HardPointer   = 5,
    };

    // DWG 位流写入（对应 ACadSharp 的 DwgStreamWriterBase 及各版本子类，与 DwgBitReader 一一对应）。
    // 只向末尾追加；PatchRawLong 用于回填先占位的大小字段。
    class DwgBitWriter
    {
    public:
        DwgBitWriter() = default;
        DwgBitWriter(CadVersion version, Codec::CodePage codePage) : m_version(version), m_codePage(codePage) {}

        CadVersion Version() const { return m_version; }
        Codec::CodePage CodePage() const { return m_codePage; }

        std::uint64_t PositionInBits() const { return m_bits; }
        bool Empty() const { return m_bits == 0; }
        void Clear()
        {
            m_data.clear();
            m_bits = 0;
        }

        // 写到字节边界（补 0 位）后的数据
        const std::vector<std::uint8_t>& Data() const { return m_data; }
        std::vector<std::uint8_t> Take()
        {
            m_bits = 0;
            return std::move(m_data);
        }

        // ── 基本类型（DWG 规范的缩写）──
        void WriteBit(bool value);                          // B
        void Write2Bits(std::uint8_t value);                // BB
        void Write3Bits(std::uint8_t value);                // 3B
        void WriteByte(std::uint8_t value);                 // RC
        void WriteBytes(std::span<const std::uint8_t> bytes);
        void WriteRawShort(std::int16_t value);             // RS（小端）
        void WriteRawShortBigEndian(std::uint16_t value);
        void WriteRawLong(std::int32_t value);              // RL
        void WriteRawLongLong(std::uint64_t value);
        void WriteRawDouble(double value);                  // RD
        void Write2RawDouble(const XY& value);              // 2RD
        void Write3RawDouble(const XYZ& value);             // 3RD

        void WriteBitShort(std::int16_t value);             // BS
        void WriteBitLong(std::int32_t value);              // BL
        void WriteBitLongLong(std::int64_t value);          // BLL
        void WriteBitDouble(double value);                  // BD
        void Write2BitDouble(const XY& value);              // 2BD
        void Write3BitDouble(const XYZ& value);             // 3BD
        void WriteBitDoubleWithDefault(double def, double value);           // DD
        void Write2BitDoubleWithDefault(const XY& def, const XY& value);    // 2DD
        void Write3BitDoubleWithDefault(const XYZ& def, const XYZ& value);  // 3DD

        void WriteModularChar(std::uint64_t value);         // MC（无符号）
        void WriteSignedModularChar(std::int64_t value);    // MC（有符号）
        void WriteModularShort(std::uint32_t value);        // MS

        // H：绝对句柄引用（代码 + 字节数 + 大端句柄字节）
        void WriteHandle(DwgRef code, Handle handle);

        // BE：R2000 起为一位（1 = 0,0,1），否则 3BD
        void WriteBitExtrusion(const XYZ& normal);
        // BT：R2000 起为一位（1 = 0），否则 BD
        void WriteBitThickness(double thickness);

        // OT：R2010 起为位对 + 1/2 字节，之前为 BS
        void WriteObjectType(std::int16_t type);

        // TV：R2007 起 BS 字符数 + UTF-16LE，之前 BS 字节数 + 代码页文本。text 为 UTF-8
        void WriteVariableText(std::string_view text);
        // TU（扩展数据、XRECORD 中的字符串）：R2007 起 RS 字符数 + UTF-16LE，之前 RS 长度 + RC 代码页 + 文本
        void WriteTextUnicode(std::string_view text);

        // CMC：R2004 起 BS 0 + BL RGB（最高字节为类型）+ RC 0；之前为 BS 索引（真彩色取近似索引）
        void WriteCmColor(const Color& color);
        // ENC：实体颜色。R2004 起带标志、真彩色与透明度
        // bookColor：颜色在句柄流引用的 DBCOLOR 中（R2004 起，标志 0x4000，不写 RGB）
        void WriteEnColor(const Color& color, const Transparency& transparency, bool bookColor = false);

        // 日期：BL 儒略日 + BL 毫秒
        void WriteJulianDate(double julian);
        // 时间长度：BL 天 + BL 毫秒
        void WriteTimeSpanDays(double days);

        // 补 0 位到字节边界
        void AlignToByte();

        // 在 bitPos 处覆盖写入 RL（小端 32 位，按位写，可以不在字节边界）
        void PatchRawLong(std::uint64_t bitPos, std::uint32_t value);

        // 追加另一个流的前 bits 位
        void AppendBits(const DwgBitWriter& other, std::uint64_t bits);
        // 追加一段位数据的前 bits 位（按 DWG 的位序：每字节从最高位开始）
        void AppendRawBits(std::span<const std::uint8_t> data, std::uint64_t bits);

    private:
        void WriteBitsRaw(std::uint32_t value, int count);
        void WriteUtf16(std::string_view utf8, bool countPrefixBitShort, bool terminator);

        std::vector<std::uint8_t> m_data;
        std::uint64_t             m_bits = 0;
        CadVersion                m_version = CadVersion::AC1015;
        Codec::CodePage           m_codePage = Codec::CodePage::Windows1252;
    };

    // 按 UTF-8 解码为 UTF-16 码元（非法字节按 U+FFFD）
    std::vector<char16_t> Utf8ToUtf16(std::string_view utf8);
}
