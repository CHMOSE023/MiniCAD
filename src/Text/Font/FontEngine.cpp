#include "FontEngine.h"
#include <algorithm>

namespace MiniCAD
{
    // ──────────────────────────────────────────────────────────────────────
    // 路径解析：若 fontFile 不含路径分隔符，则拼接 m_fontDir
    // ──────────────────────────────────────────────────────────────────────

    std::string FontEngine::ResolvePath(const std::string& fontFile) const
    {
        bool hasSlash = fontFile.find('/') != std::string::npos
                     || fontFile.find('\\') != std::string::npos;
        if (hasSlash) return fontFile; // 已是绝对/相对完整路径

        return m_fontDir + fontFile;
    }

    std::string FontEngine::BuildKey(const FontStyle& style) const
    {
        return style.name + "|" + style.fontFile + "|" + (style.isShx ? "shx" : "ttf");
    }

    // ──────────────────────────────────────────────────────────────────────
    // 解析字体（缓存命中直接返回）
    // ──────────────────────────────────────────────────────────────────────

    IFont& FontEngine::Resolve(const FontStyle& style)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        const std::string key = BuildKey(style);
        auto it = m_cache.find(key);
        if (it != m_cache.end())
            return *(it->second);

        const std::string path = ResolvePath(style.fontFile);

        std::shared_ptr<IFont> font;
        if (style.isShx)
            font = std::make_shared<SHXFont>(style.name, path, m_nextRuntimeFontId++);
        else
            font = std::make_shared<TTFFont>(style.name, path, m_nextRuntimeFontId++);

        auto [iter, _] = m_cache.emplace(key, std::move(font));
        return *(iter->second);
    }

    void FontEngine::Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache.clear();
    }
}
