#pragma once
#include <memory>
#include <string>

namespace MiniGUI
{
    class Font;
    class TextSystem;

    // 浏览器里读不到系统字体：界面字体随程序打包进 Emscripten 虚拟文件系统。
    // 默认路径对应 assets/fonts/NotoSansSC-UI.ttf（tools/make_web_font.py 生成，SIL OFL 1.1）
    constexpr const char* kWebUIFontPath = "/fonts/NotoSansSC-UI.ttf";

    std::shared_ptr<Font> LoadWebUIFont(const std::string& path = kWebUIFontPath);

    // 加载界面字体并加入 TextSystem；返回是否成功
    bool LoadWebUIFonts(TextSystem& text, const std::string& path = kWebUIFontPath);
}
