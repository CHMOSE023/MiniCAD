#include "Platform/Linux/LinuxFonts.h"
#include "Text/Font.h"
#include "Text/TextSystem.h"

namespace MiniGUI
{
    namespace
    {
        // Debian / Ubuntu / Fedora / Arch 的常见位置
        const char* const kNotoCjk[] =
        {
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        };

        std::shared_ptr<Font> LoadNotoCjk(int face)
        {
            for (const char* path : kNotoCjk)
                if (auto f = Font::LoadShared(path, face))
                    return f;
            return nullptr;
        }
    }

    std::shared_ptr<Font> LoadSystemCjkFont()
    {
        // Noto Sans CJK 的 .ttc 里第 3 个字形（索引 2）是简体中文
        if (auto f = LoadNotoCjk(2))
            return f;
        for (const char* path : { "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", "/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc",
                                  "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf" })
            if (auto f = Font::LoadShared(path, 0))
                return f;
        return nullptr;
    }

    std::vector<std::shared_ptr<Font>> LoadLinuxLanguageFonts(const std::string& fontDir, const std::string& language)
    {
        const std::string bundled = BundledUiFontForLanguage(language);
        if (bundled.empty())
            return {};
        if (auto f = LoadNotoCjk(NotoCjkFaceForLanguage(language)))
            return { f };
        if (auto f = Font::LoadShared(fontDir + bundled, 0))
            return { f };
        return {};
    }

    bool LoadLinuxUIFonts(TextSystem& text, const std::string& bundledFont)
    {
        auto main     = Font::LoadShared(bundledFont, 0);
        auto fallback = LoadSystemCjkFont();
        if (!main)
            main = std::move(fallback);     // 没带字体文件时退回系统字体
        if (!main)
            return false;
        text.AddFont(std::move(main));
        if (fallback)
            text.AddFallbackFont(std::move(fallback));
        // 系统没有中日韩字体时，韩文、繁体、日文用随程序携带的子集字体（与主字体同目录）
        const size_t slash = bundledFont.find_last_of('/');
        const std::string dir = slash == std::string::npos ? std::string() : bundledFont.substr(0, slash + 1);
        for (const char* name : { "NotoSansKR-UI.otf", "NotoSansTC-UI.otf", "NotoSansJP-UI.otf" })
            if (auto f = Font::LoadShared(dir + name, 0))
                text.AddFallbackFont(std::move(f));
        return true;
    }
}
