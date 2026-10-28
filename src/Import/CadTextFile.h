#pragma once
// CAD 软件的文本文件（.pat 等）的编码：经 MiniDWG 的代码页表解码，公开头文件不暴露 MiniDWG 类型。
#include <string>
#include <string_view>

namespace MiniCAD
{
    // 文件内容转 UTF-8：本来就是合法 UTF-8（含纯 ASCII、带 BOM 的）原样返回，
    // 否则按 GBK 解码——中文版 AutoCAD 与天正自带的 .pat 都是 GBK
    std::string CadTextToUtf8(std::string_view bytes);
}
