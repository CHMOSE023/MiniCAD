#pragma once
#include "Codec/CodePage.h"
#include "Database/CadVersion.h"
#include "Database/Color.hpp"
#include "Database/Handle.hpp"
#include "Database/Transparency.hpp"
#include "Database/Types.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace MiniDWG
{
    // DWG 位流读取（对应 ACadSharp 的 DwgStreamReaderBase 及各版本子类）。
    // 位置以位计；数据在内存中，同一缓冲可以有多个独立位置的读取器（对象数据、字符串流、句柄流）。
    //
    // 不抛异常（库要能在不开异常的 Emscripten 中编译）：读到末尾之外返回 0 并置错误标志，
    // 之后的读取都返回 0；调用方在对象读完后检查 Failed()。
    class DwgBitReader
    {
    public:
        DwgBitReader() = default;
        DwgBitReader(std::span<const std::uint8_t> data, CadVersion version, Codec::CodePage codePage)
            : m_data(data), m_version(version), m_codePage(codePage)
        {
        }

        CadVersion Version() const { return m_version; }
        Codec::CodePage CodePage() const { return m_codePage; }

        // ── 位置 ──
        std::uint64_t PositionInBits() const { return m_bit; }
        void SetPositionInBits(std::uint64_t bit) { m_bit = bit; }
        std::uint64_t Position() const { return m_bit >> 3; }          // 字节位置（向下取整）
        void SetPosition(std::uint64_t byte) { m_bit = byte << 3; }
        std::uint64_t SizeInBits() const { return std::uint64_t(m_data.size()) << 3; }
        std::uint64_t RemainingBits() const { return m_bit < SizeInBits() ? SizeInBits() - m_bit : 0; }
        void Advance(std::uint64_t bytes) { m_bit += bytes << 3; }
        // 跳到下一个字节边界
        void AlignToByte() { m_bit = (m_bit + 7) & ~std::uint64_t(7); }

        bool Failed() const { return m_failed; }
        void Fail() { m_failed = true; }

        // 计数合理性检查：count 个元素、每个至少 minBits 位，超出剩余数据时置错误并返回 false
        bool CheckCount(std::int64_t count, std::uint64_t minBits = 1);

        // ── 基本类型（DWG 规范的缩写）──
        bool ReadBit();                                  // B
        std::uint8_t Read2Bits();                        // BB
        std::uint8_t Read3Bits();                        // 3B
        std::uint8_t ReadByte();                         // RC
        std::vector<std::uint8_t> ReadBytes(std::size_t count);
        std::int16_t ReadRawShort();                     // RS（小端）
        std::uint16_t ReadRawShortBigEndian();
        std::int32_t ReadRawLong();                      // RL
        std::uint64_t ReadRawLongLong();
        double ReadRawDouble();                          // RD
        XY Read2RawDouble();                             // 2RD
        XYZ Read3RawDouble();                            // 3RD

        std::int16_t ReadBitShort();                     // BS
        std::int32_t ReadBitLong();                      // BL
        std::int64_t ReadBitLongLong();                  // BLL
        double ReadBitDouble();                          // BD
        XY Read2BitDouble();                             // 2BD
        XYZ Read3BitDouble();                            // 3BD
        double ReadBitDoubleWithDefault(double def);     // DD
        XY Read2BitDoubleWithDefault(const XY& def);     // 2DD
        XYZ Read3BitDoubleWithDefault(const XYZ& def);   // 3DD

        std::uint64_t ReadModularChar();                 // MC（无符号）
        std::int64_t ReadSignedModularChar();            // MC（有符号）
        std::uint32_t ReadModularShort();                // MS

        // H：句柄引用。代码 2～5 为绝对句柄，6/8/A/C 相对 reference
        Handle ReadHandle(Handle reference = kNullHandle);

        // BE：R2000 起为一位（1 = 0,0,1），否则 3BD
        XYZ ReadBitExtrusion();
        // BT：R2000 起为一位（1 = 0），否则 BD
        double ReadBitThickness();

        // OT：R2010 起为位对 + 1/2 字节，之前为 BS
        std::int16_t ReadObjectType();

        // TV：R2007 起 BS 字符数 + UTF-16LE，之前 BS 字节数 + 代码页文本。返回 UTF-8
        std::string ReadVariableText();
        // TU（扩展数据中的字符串）：R2007 起 RS 字符数 + UTF-16LE，之前 RS 长度 + RC 代码页 + 文本
        std::string ReadTextUnicode();

        // CMC：R2004 起 BS 索引 + BL RGB + RC 标志 + 可选名称（名称从 text 读取）
        Color ReadCmColor(DwgBitReader& text);
        // ENC：实体颜色（R2004 起带标志、真彩色与透明度）；bookColor 表示后面有颜色簿句柄
        Color ReadEnColor(Transparency& transparency, bool& bookColor);

        // 日期：BL 儒略日 + BL 毫秒，合成为儒略日的小数
        double ReadJulianDate();
        // 时间长度：BL 天 + BL 毫秒，合成为天
        double ReadTimeSpanDays();

        // 字符串流：位置 endBit 处有标志位，为 1 时字符串数据位于 endBit 之前（R2007+）；
        // 返回字符串流的起始位置，没有字符串流时读取器标记为空
        void SetPositionByFlag(std::uint64_t endBit);
        bool IsEmpty() const { return m_empty; }
        // 字符串流的结束位置（大小字段之前）；没有字符串流时为 0
        std::uint64_t StreamEndInBits() const { return m_streamEnd; }

        // 从 startBit 起复制 bits 位（按 DWG 位序左对齐到字节）
        std::vector<std::uint8_t> CopyBits(std::uint64_t startBit, std::uint64_t bits) const;

        // 16 字节哨兵
        bool CheckSentinel(const std::uint8_t (&expected)[16]);

    private:
        std::uint8_t ReadBitsRaw(int count);
        std::string FromUtf16(const std::vector<std::uint8_t>& bytes) const;

        std::span<const std::uint8_t> m_data;
        std::uint64_t                 m_bit = 0;
        CadVersion                    m_version = CadVersion::AC1015;
        Codec::CodePage               m_codePage = Codec::CodePage::Windows1252;
        bool                          m_failed = false;
        bool                          m_empty = false;
        std::uint64_t                 m_streamEnd = 0;
    };

    // 三路读取（对应 ACadSharp DwgMergedReader）：数据从 Main 读，字符串从 Text 读，句柄从 Handles 读。
    // R2007 起字符串与句柄各在对象数据末尾的独立区域；之前三者是同一个读取器
    struct DwgStreams
    {
        DwgBitReader* Main = nullptr;
        DwgBitReader* Text = nullptr;
        DwgBitReader* Handles = nullptr;

        std::string ReadVariableText() { return Text->IsEmpty() ? std::string() : Text->ReadVariableText(); }
        Handle ReadHandle(Handle reference = kNullHandle) { return Handles->ReadHandle(reference); }
        Color ReadCmColor() { return Main->ReadCmColor(*Text); }
        bool Failed() const { return Main->Failed() || Text->Failed() || Handles->Failed(); }
    };
}
