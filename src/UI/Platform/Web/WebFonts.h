#pragma once
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    class Font;
    class TextSystem;

    // 浏览器里读不到系统字体：界面字体随程序打包进 Emscripten 虚拟文件系统。
    // 默认路径对应 assets/fonts/NotoSansSC-UI.ttf（tools/make_web_font.py 生成，SIL OFL 1.1）
    constexpr const char* kWebUIFontPath = "/fonts/NotoSansSC-UI.ttf";
    // 韩文后备字体（Noto Sans SC 没有韩文），放在主字体旁边，没有时不加载
    constexpr const char* kWebUIHangulFontName = "NotoSansKR-UI.otf";

    // 繁体、日文字体放在网页旁边的 fonts/ 下，启动后才下载（不进 .data，首屏不等它们）；下载完写进虚拟文件系统的 /fonts/
    constexpr const char* kWebLazyFontNames[] = { "NotoSansTC-UI.otf", "NotoSansJP-UI.otf" };

    std::shared_ptr<Font> LoadWebUIFont(const std::string& path = kWebUIFontPath);

    // 界面语言的字体（TextSystem::SetPreferredFonts）：/fonts/ 下该语言的子集字体，还没下载完或简体等语言时为空
    std::vector<std::shared_ptr<Font>> LoadWebLanguageFonts(const std::string& language);

    // 已经下载好的繁体、日文字体加为后备字体（加过的不重复加）；dir 以 / 结尾
    void AddWebLazyFonts(TextSystem& text, const std::string& dir = "/fonts/");

    // 加载界面字体并加入 TextSystem；返回是否成功
    bool LoadWebUIFonts(TextSystem& text, const std::string& path = kWebUIFontPath);
}
