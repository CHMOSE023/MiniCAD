#pragma once
#include "Codec/CodePage.h"
#include "Database/DxfValue.h"
#include "Database/Handle.hpp"
#include "Database/Types.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace MiniDWG
{
    // DXF 组码流写入（对应 ACadSharp 的 DxfAsciiWriter / DxfBinaryWriter）。
    // 值按组码的类型（GroupCodeTypeOf）输出；字符串由 UTF-8 编码为目标代码页，并转义 ^、换行等字符。
    class DxfStreamWriter
    {
    public:
        // binary：二进制 DXF（R13 及以后的格式，组码 2 字节）；codePage：字符串编码（R2007 起为 UTF-8）
        DxfStreamWriter(bool binary, Codec::CodePage codePage);

        bool IsBinary() const { return m_binary; }

        void Write(int code, std::string_view value);
        void Write(int code, const char* value) { Write(code, std::string_view(value)); }
        void Write(int code, const std::string& value) { Write(code, std::string_view(value)); }
        void Write(int code, double value);
        void Write(int code, bool value);

        // 整数：按组码类型写 16/32/64 位（字节组码 280～289 按 16 位）
        void WriteInt(int code, std::int64_t value);

        void WriteHandle(int code, Handle value);
        void WriteBytes(int code, std::span<const std::uint8_t> value);

        // 点：code、code + 10、code + 20
        void Write(int code, const XYZ& value);
        void Write(int code, const XY& value);

        // 任意值（扩展数据、XRecord 条目）：按组码类型取值
        void WriteValue(int code, const DxfValue& value);

        // 取出已写的数据
        std::vector<std::uint8_t> Take() { return std::move(m_out); }

    private:
        void WriteCode(int code);
        void WriteLine(std::string_view text);
        std::string Encode(std::string_view utf8) const;

        template <class T>
        void WriteLE(T value);

        bool                      m_binary = false;
        Codec::CodePage           m_codePage = Codec::CodePage::Utf8;
        std::vector<std::uint8_t> m_out;
    };

    // 浮点数的 DXF 文本：最短的可往返十进制表示，总带小数点（1 → "1.0"，1e20 → "1.0E+20"）
    std::string FormatDxfDouble(double value);
}
