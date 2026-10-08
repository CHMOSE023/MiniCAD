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
        return true;
    }
}
