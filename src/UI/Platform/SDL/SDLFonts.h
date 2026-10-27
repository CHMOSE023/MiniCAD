#pragma once
#include <memory>
#include <string>

namespace MiniGUI
{
    class Font;
    class TextSystem;

    // 界面字体（Linux 等用 SDL 宿主的平台）：主字体用随程序携带的 NotoSansSC-UI.ttf（与网页版相同，
    // 字形一致、不依赖系统装了什么），系统里找得到的中文字体作为后备，补上子集里没有的字。
    // bundledFont：随程序携带的字体文件的完整路径（UTF-8）
    bool LoadSDLUIFonts(TextSystem& text, const std::string& bundledFont);

    // 系统里第一个能加载的中文字体（Noto Sans CJK、文泉驿等常见的安装位置）；没有时返回 nullptr
    std::shared_ptr<Font> LoadSystemCjkFont();
}
