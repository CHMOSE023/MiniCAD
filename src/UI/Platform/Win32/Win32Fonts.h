#pragma once
#include <memory>
#include <string>

namespace MiniGUI
{
    class Font;
    class TextSystem;

    // 系统字体目录下的文件路径（UTF-8），例如 GetSystemFontPath("msyh.ttc")
    std::string GetSystemFontPath(const char* fileName);

    // 加载 Windows 默认界面字体：微软雅黑 UI（msyh.ttc 第 2 个字体）为主字体，
    // Segoe UI Symbol 作为后备（符号、箭头等）。返回是否成功加载了主字体
    bool LoadSystemUIFonts(TextSystem& text);

    // 同上，但只加载字体对象，不加入 TextSystem（多个 UIContext 共享字体时使用）
    std::shared_ptr<Font> LoadSystemUIFont();
    std::shared_ptr<Font> LoadSystemSymbolFont();
}
