#pragma once
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    class Font;
    class TextSystem;

    // 界面字体（Linux）：主字体用随程序携带的 NotoSansSC-UI.ttf（与网页版相同，
    // 字形一致、不依赖系统装了什么），系统里找得到的中文字体作为后备，补上子集里没有的字；
    // 同目录的 NotoSansKR-UI.otf 再作韩文后备。
    // bundledFont：随程序携带的字体文件的完整路径（UTF-8）
    bool LoadLinuxUIFonts(TextSystem& text, const std::string& bundledFont);

    // 界面语言的字体（TextSystem::SetPreferredFonts）：系统 Noto Sans CJK 里该语言的字体，没有时用 fontDir 下随程序带的子集字体；
    // 简体与其他语言为空。fontDir 以 / 结尾
    std::vector<std::shared_ptr<Font>> LoadLinuxLanguageFonts(const std::string& fontDir, const std::string& language);

    // 系统里第一个能加载的中文字体（Noto Sans CJK、文泉驿等常见的安装位置）；没有时返回 nullptr
    std::shared_ptr<Font> LoadSystemCjkFont();
}
