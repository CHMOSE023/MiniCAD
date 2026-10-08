#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace MiniDWG::Codec
{
    // 文本编码，取值为 Windows 代码页号
    enum class CodePage : std::uint16_t
    {
        Unknown     = 0,
        Ascii       = 20127,
        Utf8        = 65001,
        Windows1252 = 1252,
        Gbk         = 936,
        ShiftJis    = 932,
        Korean      = 949,
        Big5        = 950,
    };

    // $DWGCODEPAGE 的名称（ANSI_936、DOS850、BIG5 ...，不区分大小写）→ 代码页；不认识的返回 Unknown
    CodePage CodePageFromName(std::string_view name);

    // 代码页 → $DWGCODEPAGE 名称（写文件用）；没有对应名称时返回 ANSI_1252
    std::string_view CodePageName(CodePage codePage);

    // DWG 文件头与扩展数据中的代码页编号（0～44，与 ACadSharp CadUtils._pageCodes 一致）→ 代码页；
    // 超出范围返回 Unknown
    CodePage CodePageFromDwgIndex(int index);

    // 代码页 → DWG 代码页编号（CodePageFromDwgIndex 的反查，取第一个匹配的编号）；没有对应编号时返回 30（ANSI_1252）
    int DwgIndexFromCodePage(CodePage codePage);

    // 是否有该代码页的转换表（UTF-8、ASCII 总是支持）
    bool IsSupported(CodePage codePage);

    // 按代码页解码为 UTF-8。不支持的代码页按 Windows-1252 处理；无法解码的字节替换为 U+FFFD
    std::string ToUtf8(std::string_view bytes, CodePage codePage);

    // UTF-8 编码为代码页；代码页中没有的字符写成 \U+XXXX（与 AutoCAD 写 DXF 的做法一致）
    std::string FromUtf8(std::string_view utf8, CodePage codePage);

    // 把字符串中的 \U+XXXX 转义（AutoCAD 对代码页外字符的写法）还原为 UTF-8
    std::string DecodeUnicodeEscapes(std::string_view utf8);

    // 追加一个 Unicode 码点的 UTF-8 编码
    void AppendUtf8(std::string& out, char32_t codePoint);
}
