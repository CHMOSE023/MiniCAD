#include "Platform/Web/WebFonts.h"
#include "Text/Font.h"
#include "Text/TextSystem.h"

namespace MiniGUI
{
    std::shared_ptr<Font> LoadWebUIFont(const std::string& path)
    {
        return Font::LoadShared(path, 0);
    }

    void AddWebLazyFonts(TextSystem& text, const std::string& dir)
    {
        for (const char* name : kWebLazyFontNames)
            if (auto f = Font::LoadShared(dir + name, 0))
                text.AddFallbackFont(std::move(f));
    }

    std::vector<std::shared_ptr<Font>> LoadWebLanguageFonts(const std::string& language)
    {
        const std::string name = BundledUiFontForLanguage(language);
        if (name.empty())
            return {};
        if (auto f = Font::LoadShared(std::string("/fonts/") + name, 0))
            return { f };
        return {};
    }

    bool LoadWebUIFonts(TextSystem& text, const std::string& path)
    {
        auto main = LoadWebUIFont(path);
        if (!main)
            return false;
        text.AddFont(std::move(main));
        const size_t slash = path.find_last_of('/');
        const std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
        if (auto hangul = Font::LoadShared(dir + kWebUIHangulFontName, 0))
            text.AddFallbackFont(std::move(hangul));
        AddWebLazyFonts(text, dir);
        return true;
    }
}
