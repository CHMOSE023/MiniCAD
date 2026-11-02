#pragma once
// CAD 软件的文本文件（.pat 等）的编码：经 MiniDWG 的代码页表解码，公开头文件不暴露 MiniDWG 类型。
#include <string>
#include <string_view>

namespace MiniCAD
{
    std::string CadTextToUtf8(std::string_view bytes);
}
