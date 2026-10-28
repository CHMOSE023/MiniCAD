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
        // 再到额外目录（系统字体目录）找：只按文件名，带路径的名字上面已经判过
        if (!fontFile.empty() && fontFile.find('/') == std::string::npos && fontFile.find('\\') == std::string::npos)
            for (const std::string& dir : m_extraDirs)
                for (const std::string& name : {fontFile, lower, upper})
                {
                    const std::string path = dir + name;
                    if (std::filesystem::is_regular_file(Utf8ToPath(path), ec))
                        return path;
                }
        if (found) *found = false;
        LOG_WARN("字体 %s 不存在，以 %s 代替", fontFile.c_str(), substitute);
        return ResolvePath(substitute);
    }

    void FontEngine::AddSearchDir(std::string dir)
    {
        if (dir.empty()) return;
        if (dir.back() != '/' && dir.back() != '\\') dir += '/';
        std::lock_guard<std::mutex> lock(m_mutex);
        m_extraDirs.push_back(std::move(dir));
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
        {
            // 缓存里的 SHX 字体如果当时没读进来（字体目录还没就绪、文件被占用等），
            // 不能一直用这份空字体 —— 否则字体文件后来可用了也不会重试，
            // 表现为「第一次打开的图纸线型没有符号，切换图纸后才正常」。
            bool stale = false;
            if (const auto* shx = dynamic_cast<SHXFont*>(it->second.get()))
                stale = !shx->IsLoaded();
            else if (const auto* comp = dynamic_cast<SHXCompositeFont*>(it->second.get()))
                stale = !comp->IsLoaded();
            if (!stale)
                return *(it->second);
            m_cache.erase(it);      // 读失败的缓存作废，下面重新解析
        }

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
