#include "Platform/SDL/SDLFonts.h"
#include "Text/Font.h"
#include "Text/TextSystem.h"

namespace MiniGUI
{
    std::shared_ptr<Font> LoadSystemCjkFont()
    {
        // Debian / Ubuntu / Fedora / Arch 的常见位置；Noto Sans CJK 的 .ttc 里第 3 个字形（索引 2）是简体中文
        struct Candidate { const char* path; int face; };
        static const Candidate kCandidates[] =
        {
            { "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",       2 },
            { "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",            2 },
            { "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",     2 },
            { "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",               0 },
            { "/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc",     0 },
            { "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",    0 },
        };
        for (const Candidate& c : kCandidates)
            if (auto f = Font::LoadFromFile(c.path, c.face))
                return f;
        return nullptr;
    }

    bool LoadSDLUIFonts(TextSystem& text, const std::string& bundledFont)
    {
        auto main     = Font::LoadFromFile(bundledFont, 0);
        auto fallback = LoadSystemCjkFont();
        if (!main)
            main = std::move(fallback);     // 没带字体文件时退回系统字体
        if (!main)
            return false;
        text.AddFont(std::move(main));
        if (fallback)
            text.AddFallbackFont(std::move(fallback));
        return true;
    }
}
