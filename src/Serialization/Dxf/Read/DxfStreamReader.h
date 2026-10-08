#pragma once
#include "Codec/CodePage.h"
#include "Database/DxfValue.h"
#include <cstdint>
#include <span>
#include <string>

namespace MiniDWG
{
    // 一个组码及其值。字符串已转换为 UTF-8（并还原 \U+XXXX 与 ^J 等转义）
    struct DxfGroup
    {
        int      Code = -1;
        DxfValue Value;
    };

    // DXF 组码流读取（对应 ACadSharp 的 DxfTextReader / DxfBinaryReader / DxfBinaryReaderAC1009）
    class DxfStreamReader
    {
    public:
        enum class Format
        {
            Ascii,
            Binary,
            BinaryAC1009,   // R12 二进制 DXF：组码为 1 字节（255 后接 2 字节组码）
        };

        explicit DxfStreamReader(std::span<const std::uint8_t> data);

        Format GetFormat() const { return m_format; }

        // R2007 及以后为 UTF-8；之前按 $DWGCODEPAGE
        void SetCodePage(Codec::CodePage codePage) { m_codePage = codePage; }

        // 回到文件开头
        void Rewind();

        // 读下一个组码；到达文件末尾或数据损坏时返回 false
        bool ReadNext(DxfGroup& group);

        // 当前位置：文本格式为行号，二进制格式为字节偏移
        std::size_t Position() const;

    private:
        bool ReadTextLine(std::string_view& line);
        bool ReadAscii(DxfGroup& group);
        bool ReadBinary(DxfGroup& group);
        std::string DecodeString(std::string_view raw) const;

        std::span<const std::uint8_t> m_data;
        Format                        m_format = Format::Ascii;
        Codec::CodePage               m_codePage = Codec::CodePage::Windows1252;
        std::size_t                   m_pos = 0;
        std::size_t                   m_line = 0;
        std::size_t                   m_start = 0;
    };
}
