#include "Platform/Web/WebFonts.h"
#include "Text/Font.h"
#include "Text/TextSystem.h"

namespace MiniGUI
{
    std::shared_ptr<Font> LoadWebUIFont(const std::string& path)
    {
        return Font::LoadFromFile(path, 0);
    }

    bool LoadWebUIFonts(TextSystem& text, const std::string& path)
    {
        auto main = LoadWebUIFont(path);
        if (!main)
            return false;
        text.AddFont(std::move(main));
        const size_t slash = path.find_last_of('/');
        const std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
        if (auto hangul = Font::LoadFromFile(dir + kWebUIHangulFontName, 0))
            text.AddFallbackFont(std::move(hangul));
        return true;
    }
}
