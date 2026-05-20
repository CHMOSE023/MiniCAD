#include "FontEngine.h"

namespace MiniCAD
{
    std::string FontEngine::BuildKey(const FontStyle& style) const
    {
        return style.name + "|" + style.fontFile + "|" + (style.isShx ? "shx" : "ttf");
    }

    IFont& FontEngine::Resolve(const FontStyle& style)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        const std::string key = BuildKey(style);

        auto it = m_cache.find(key);
        if (it != m_cache.end())
            return *(it->second);

        std::shared_ptr<IFont> font;

        if (style.isShx)
        {
            font = std::make_shared<SHXFont>(style.name, style.fontFile, m_nextRuntimeFontId++);
        }
        else
        {
            font = std::make_shared<TTFFont>(style.name, style.fontFile, m_nextRuntimeFontId++);
        }

        auto [iter, inserted] = m_cache.emplace(key, std::move(font));

        return *(iter->second);
    }

    void FontEngine::Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache.clear();
    }
}