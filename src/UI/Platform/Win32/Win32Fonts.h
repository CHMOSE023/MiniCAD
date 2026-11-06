#pragma once
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    class Font;
    class TextSystem;

    // 系统字体目录下的文件路径（UTF-8），例如 GetSystemFontPath("msyh.ttc")
    std::string GetSystemFontPath(const char* fileName);

    // 加载 Windows 默认界面字体：微软雅黑 UI（msyh.ttc 第 2 个字体）为主字体，
    // Segoe UI Symbol 作为后备（符号、箭头等）。返回是否成功加载了主字体
    bool LoadSystemUIFonts(TextSystem& text);

    // 界面语言的字体（TextSystem::SetPreferredFonts）：繁体 → 微软正黑体 UI，日文 → Yu Gothic UI / Meiryo UI / MS UI Gothic，
    // 韩文 → Malgun Gothic；简体与其他语言为空
    std::vector<std::shared_ptr<Font>> LoadSystemLanguageFonts(const std::string& language);

    // 同上，但只加载字体对象，不加入 TextSystem（多个 UIContext 共享字体时使用）
    std::shared_ptr<Font> LoadSystemUIFont();
    std::shared_ptr<Font> LoadSystemSymbolFont();
}
