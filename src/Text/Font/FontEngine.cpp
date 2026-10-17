#include "FontEngine.h"
#include "SHXCompositeFont.h"
#include "Core/Log.h"
#include "Core/PathUtils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

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

    // 字体文件不在字体目录中时（图纸引用了 gbenor、complex、gbcbig 等未随程序提供的字体），
    // 依次试原名、全小写、全大写（网页版的虚拟文件系统区分大小写），都没有就换成程序自带的替代字体
    // （与 AutoCAD 的 FONTALT 类似）：SHX 主字体 → simplex.shx，SHX 大字体 → TSSDCHN.SHX，TTF → GB2312.ttf
    std::string FontEngine::FindPath(const std::string& fontFile, const char* substitute, bool* found) const
    {
        if (found) *found = true;
        std::string lower = fontFile, upper = fontFile;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        std::error_code ec;
        for (const std::string& name : {fontFile, lower, upper})
        {
            const std::string path = ResolvePath(name);
            if (!name.empty() && std::filesystem::is_regular_file(Utf8ToPath(path), ec))
                return path;
        }
        if (found) *found = false;
        LOG_WARN("字体 %s 不存在，以 %s 代替", fontFile.c_str(), substitute);
        return ResolvePath(substitute);
    }

    std::string FontEngine::BuildKey(const FontStyle& style) const
    {
        return style.name + "|" + style.fontFile + "|" + style.bigFontFile + "|" + (style.isShx ? "shx" : "ttf");
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

        bool found = true;
        const std::string path = FindPath(style.fontFile, style.isShx ? "simplex.shx" : "GB2312.ttf", &found);

        std::shared_ptr<IFont> font;
        // 缺失的 SHX 主字体可能本身含汉字（如 hztxt.shx），替代时同时配上中文大字体
        if (style.isShx && (!style.bigFontFile.empty() || !found))
        {
            const std::string big = style.bigFontFile.empty() ? ResolvePath("TSSDCHN.SHX") : FindPath(style.bigFontFile, "TSSDCHN.SHX");
            auto mainFont = std::make_shared<SHXFont>(style.fontFile, path, m_nextRuntimeFontId++);
            auto bigFont  = std::make_shared<SHXFont>(style.bigFontFile.empty() ? std::string("TSSDCHN.SHX") : style.bigFontFile, big, m_nextRuntimeFontId++);
            font = std::make_shared<SHXCompositeFont>(style.name, mainFont, bigFont, m_nextRuntimeFontId++);
        }
        else if (style.isShx)
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
