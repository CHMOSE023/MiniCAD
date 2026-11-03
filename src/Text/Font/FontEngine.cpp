#include "FontEngine.h"
#include "SHXCompositeFont.h"
#include "Core/Log.h"
#include "Core/PathUtils.h"
#include "Utf8Iterator.h"
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

    namespace
    {
        struct FallbackFont { const char* file; int face; };

        // 各文字缺字时依次找的字体：Windows 自带、Linux / 安卓的 Noto Sans CJK（.ttc 里 0 日 1 韩 2 简 3 繁）、随程序带的界面字体
        const std::vector<FallbackFont>& ScriptFonts(CjkScript script)
        {
            static const std::vector<FallbackFont> kFonts[4] = {
                { { "GB2312.ttf", 0 }, { "msyh.ttc", 0 }, { "simsun.ttc", 0 }, { "NotoSansCJK-Regular.ttc", 2 },
                  { "NotoSansCJKsc-Regular.otf", 0 }, { "wqy-microhei.ttc", 0 }, { "NotoSansSC-UI.ttf", 0 } },
                { { "msjh.ttc", 0 }, { "mingliu.ttc", 0 }, { "NotoSansCJK-Regular.ttc", 3 },
                  { "NotoSansCJKtc-Regular.otf", 0 }, { "NotoSansTC-UI.otf", 0 } },
                { { "msgothic.ttc", 0 }, { "YuGothM.ttc", 0 }, { "meiryo.ttc", 0 }, { "NotoSansCJK-Regular.ttc", 0 },
                  { "NotoSansCJKjp-Regular.otf", 0 }, { "NotoSansJP-UI.otf", 0 } },
                { { "malgun.ttf", 0 }, { "gulim.ttc", 0 }, { "NotoSansCJK-Regular.ttc", 1 },
                  { "NotoSansCJKkr-Regular.otf", 0 }, { "NotoSansKR-UI.otf", 0 } },
            };
            return kFonts[static_cast<int>(script)];
        }

        CjkScript ScriptOfCodePage(uint16_t codePage)
        {
            switch (codePage)
            {
            case 950: return CjkScript::Traditional;
            case 932: return CjkScript::Japanese;
            case 949: return CjkScript::Korean;
            default:  return CjkScript::Simplified;
            }
        }

        std::string LowerAscii(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }
    }

    CjkScript FontEngine::ScriptOfFont(const std::string& fontFile)
    {
        std::string name = fontFile;
        if (const size_t slash = name.find_last_of("/\\"); slash != std::string::npos)
            name = name.substr(slash + 1);
        name = LowerAscii(name);
        auto has = [&name](std::initializer_list<const char*> parts)
        {
            return std::any_of(parts.begin(), parts.end(), [&name](const char* p) { return name.find(p) != std::string::npos; });
        };
        Utf8Iterator it(name);
        while (it.HasNext())
        {
            const uint32_t cp = it.Next();
            if ((cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F) || (cp >= 0xAC00 && cp <= 0xD7A3))
                return CjkScript::Korean;
            if ((cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0xFF66 && cp <= 0xFF9F))
                return CjkScript::Japanese;
        }
        // i18n: 数据（字体名）
        if (has({ "mingliu", "kaiu", "dfkai", "msjh", "jhenghei", "細明", "標楷", "正黑", "華康", "cjktc", "sanstc", "hant" }))
            return CjkScript::Traditional;
        if (has({ "msgothic", "msmincho", "ms gothic", "ms mincho", "ms pgothic", "ms pmincho", "ms ui gothic", "meiryo",
                  "yugoth", "yumin", "yu gothic", "yu mincho", "cjkjp", "sansjp" }) || name.rfind("hg", 0) == 0)
            return CjkScript::Japanese;
        if (has({ "malgun", "gulim", "batang", "dotum", "gungsuh", "cjkkr", "sanskr" }))
            return CjkScript::Korean;
        return CjkScript::Simplified;
    }

    // 依次试原名、全小写、全大写（网页版的虚拟文件系统区分大小写），先在字体目录、再到额外目录（系统字体目录）找；没有返回空串
    std::string FontEngine::FindExisting(const std::string& fontFile) const
    {
        if (fontFile.empty())
            return {};
        std::string lower = LowerAscii(fontFile), upper = fontFile;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        std::error_code ec;
        for (const std::string& name : {fontFile, lower, upper})
        {
            const std::string path = ResolvePath(name);
            if (std::filesystem::is_regular_file(Utf8ToPath(path), ec))
                return path;
        }
        // 额外目录只按文件名找，带路径的名字上面已经判过
        if (fontFile.find('/') == std::string::npos && fontFile.find('\\') == std::string::npos)
            for (const std::string& dir : m_extraDirs)
                for (const std::string& name : {fontFile, lower, upper})
                {
                    const std::string path = dir + name;
                    if (std::filesystem::is_regular_file(Utf8ToPath(path), ec))
                        return path;
                }
        return {};
    }

    // 字体文件不在字体目录中时（图纸引用了 gbenor、complex、gbcbig 等未随程序提供的字体），
    // 换成程序自带的替代字体（与 AutoCAD 的 FONTALT 类似）：SHX 主字体 → simplex.shx，SHX 大字体 → TSSDCHN.SHX
    std::string FontEngine::FindPath(const std::string& fontFile, const char* substitute, bool* found) const
    {
        if (found) *found = true;
        if (std::string path = FindExisting(fontFile); !path.empty())
            return path;
        if (found) *found = false;
        LOG_WARN_TR("字体 {0} 不存在，以 {1} 代替", fontFile.c_str(), substitute);
        return ResolvePath(substitute);
    }

    // 缺失的 TrueType 字体换成同一种文字的字体（新細明體 → 微软正黑体等），都没有时用自带的 GB2312.ttf
    std::string FontEngine::SubstituteTrueType(CjkScript script, int& face) const
    {
        for (const FallbackFont& f : ScriptFonts(script))
            if (std::string path = FindExisting(f.file); !path.empty())
            {
                face = f.face;
                return path;
            }
        face = 0;
        return ResolvePath("GB2312.ttf");
    }

    std::shared_ptr<TTFFallbackChain> FontEngine::FallbackChain(CjkScript script)
    {
        auto& chain = m_chains[static_cast<int>(script)];
        if (chain)
            return chain;
        // 本文字的字体在前，其余按简、繁、日、韩，最后是 Droid Sans Fallback
        std::vector<FallbackFont> fonts = ScriptFonts(script);
        for (CjkScript other : { CjkScript::Simplified, CjkScript::Traditional, CjkScript::Japanese, CjkScript::Korean })
            if (other != script)
                for (const FallbackFont& f : ScriptFonts(other))
                    fonts.push_back(f);
        fonts.push_back({ "DroidSansFallbackFull.ttf", 0 });
        chain = std::make_shared<TTFFallbackChain>(fonts.size(), [this, fonts](size_t i) -> std::shared_ptr<TTFFont>
        {
            std::string path;
            uint64_t    id = 0;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                path = FindExisting(fonts[i].file);
                if (path.empty())
                    return nullptr;
                id = m_nextRuntimeFontId++;
            }
            return std::make_shared<TTFFont>(fonts[i].file, path, id, fonts[i].face);
        });
        return chain;
    }

    // SHX 缺字只找单线字体：本文字的在前，再按繁、简、日、韩（汉字的繁体字形与日文旧字形接近）
    std::shared_ptr<SHXFallbackChain> FontEngine::ShxFallbackChain(CjkScript script)
    {
        auto& chain = m_shxChains[static_cast<int>(script)];
        if (chain)
            return chain;
        static const char* const kOrder[4][4] = {
            { "minicad_sc.shx", "minicad_tc.shx", "minicad_jp.shx", "minicad_kr.shx" },
            { "minicad_tc.shx", "minicad_sc.shx", "minicad_jp.shx", "minicad_kr.shx" },
            { "minicad_jp.shx", "minicad_tc.shx", "minicad_sc.shx", "minicad_kr.shx" },
            { "minicad_kr.shx", "minicad_tc.shx", "minicad_sc.shx", "minicad_jp.shx" },
        };
        const char* const* files = kOrder[static_cast<int>(script)];
        chain = std::make_shared<SHXFallbackChain>(4, [this, files](size_t i) -> std::shared_ptr<SHXFont>
        {
            std::string path;
            uint64_t    id = 0;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                path = FindExisting(files[i]);
                if (path.empty())
                    return nullptr;
                id = m_nextRuntimeFontId++;
            }
            return std::make_shared<SHXFont>(files[i], path, id);
        });
        return chain;
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
        return style.name + "|" + style.fontFile + "|" + style.bigFontFile + "|" + (style.isShx ? (style.shapesOnly ? "shp" : "shx") : "ttf");
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
        int face = 0;
        std::string path;
        if (style.isShx)
            path = FindPath(style.fontFile, "simplex.shx", &found);
        else if (path = FindExisting(style.fontFile); path.empty())
        {
            found = false;
            path  = SubstituteTrueType(ScriptOfFont(style.fontFile.empty() ? style.name : style.fontFile), face);
            LOG_WARN_TR("字体 {0} 不存在，以 {1} 代替", style.fontFile.c_str(), path);
        }

        std::shared_ptr<IFont> font;
        // 缺失的 SHX 主字体可能本身含汉字（如 hztxt.shx），替代时同时配上中文大字体
        if (style.isShx && (!style.bigFontFile.empty() || !found))
        {
            const std::string big = style.bigFontFile.empty() ? ResolvePath("TSSDCHN.SHX") : FindPath(style.bigFontFile, "TSSDCHN.SHX");
            auto mainFont = std::make_shared<SHXFont>(style.fontFile, path, m_nextRuntimeFontId++);
            auto bigFont  = std::make_shared<SHXFont>(style.bigFontFile.empty() ? std::string("TSSDCHN.SHX") : style.bigFontFile, big, m_nextRuntimeFontId++);
            // 大字体缺的字（简体大字体里的繁体字等）按原大字体的文字找单线字体
            uint16_t codePage = SHXFont::DetectBigFontCodePage(style.bigFontFile.empty() ? style.fontFile : style.bigFontFile, {});
            if (codePage == 936)
                codePage = bigFont->GetCodePage();
            font = std::make_shared<SHXCompositeFont>(style.name, mainFont, bigFont, m_nextRuntimeFontId++, ShxFallbackChain(ScriptOfCodePage(codePage)));
        }
        else if (style.isShx)
        {
            auto mainFont = std::make_shared<SHXFont>(style.name, path, m_nextRuntimeFontId++);
            font = std::make_shared<SHXCompositeFont>(style.name, mainFont, nullptr, m_nextRuntimeFontId++,
                                                      style.shapesOnly ? nullptr : ShxFallbackChain(ScriptOfFont(style.fontFile)));
        }
        else
        {
            auto ttf = std::make_shared<TTFFont>(style.name, path, m_nextRuntimeFontId++, face);
            // 缺字（Arial 等西文字体里的汉字、宋体里的韩文）按字体的文字找回退字体，不画成方块
            ttf->SetFallback(FallbackChain(ScriptOfFont(style.fontFile.empty() ? style.name : style.fontFile)));
            font = std::move(ttf);
        }

        auto [iter, _] = m_cache.emplace(key, std::move(font));
        return *(iter->second);
    }

    void FontEngine::Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache.clear();
        for (auto& chain : m_chains)
            chain.reset();
        for (auto& chain : m_shxChains)
            chain.reset();
    }

    void FontEngine::RescanFallbacks()
    {
        std::vector<std::shared_ptr<TTFFallbackChain>> chains;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            chains.assign(std::begin(m_chains), std::end(m_chains));
        }
        for (auto& chain : chains)
            if (chain)
                chain->Rescan();
    }
}
