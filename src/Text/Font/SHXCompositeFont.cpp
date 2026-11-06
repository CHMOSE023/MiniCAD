#include "SHXCompositeFont.h"
#include "SHXFont.h"        // IsLoaded() 要对内层 SHXFont 做 dynamic_cast，需要完整类型
#include "Core/Log.h"

namespace MiniCAD
{
    SHXFallbackChain::SHXFallbackChain(size_t count, Loader loader)
        : m_loader(std::move(loader))
        , m_fonts(count)
        , m_probed(count, false)
    {}

    bool SHXFallbackChain::Lookup(uint32_t codepoint, Glyph& glyph, double& fontHeight)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        SHXFont* found = nullptr;
        if (auto it = m_route.find(codepoint); it != m_route.end())
            found = it->second;
        else
        {
            for (size_t i = 0; i < m_fonts.size() && !found; ++i)
            {
                if (!m_probed[i])
                {
                    m_probed[i] = true;
                    m_fonts[i]  = m_loader(i);
                    if (m_fonts[i] && !m_fonts[i]->IsLoaded())
                        m_fonts[i].reset();
                }
                if (m_fonts[i] && m_fonts[i]->HasGlyph(codepoint))
                    found = m_fonts[i].get();
            }
            m_route.emplace(codepoint, found);
        }
        if (!found)
            return false;
        glyph      = found->GetGlyph(codepoint);
        fontHeight = found->GetHeight();
        return true;
    }

    namespace
    {
        // 缺字画 "?" 的范围：非 ASCII 的可见字符；%%nnn 形号、控制字符、零宽字符、变体选择符不画
        bool ShowsMissingMark(uint32_t cp)
        {
            if (cp < 0xA0 || (cp & kRawShapeFlag))
                return false;
            return !((cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2028 && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x206F)
                     || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF);
        }
    }

    SHXCompositeFont::SHXCompositeFont(const std::string& name, std::shared_ptr<IFont> mainFont, std::shared_ptr<IFont> bigFont, uint64_t fontId,
                                       std::shared_ptr<SHXFallbackChain> fallback)
        : m_name(name)
        , m_fontId(fontId)
        , m_mainFont(std::move(mainFont))
        , m_bigFont(std::move(bigFont))
        , m_fallback(std::move(fallback))
    {
        LOG_DEBUG("[Composite] '%s'  main=%s  big=%s", m_name.c_str(),
            m_mainFont ? m_mainFont->GetName() : "(none)",
            m_bigFont  ? m_bigFont->GetName()  : "(none)");
    }

    //--------------------------------------------------------------
    // 路由:先 primary 再 fallback,以 Glyph.Lines 是否为空判定 HIT
    //--------------------------------------------------------------
    IFont* SHXCompositeFont::PickFontFor(uint32_t cp)
    {
        auto it = m_routeCache.find(cp);
        if (it != m_routeCache.end()) return it->second;

        IFont* primary = (cp < 0x80) ? m_mainFont.get() : m_bigFont.get();
        IFont* fallback = (cp < 0x80) ? m_bigFont.get() : m_mainFont.get();

        IFont* picked = nullptr;
        if (primary && primary->HasGlyph(cp))  picked = primary;
        else if (fallback && fallback->HasGlyph(cp)) picked = fallback;

        m_routeCache[cp] = picked;
        LOG_TRACE("[Composite] U+%04X -> %s", cp, picked ? picked->GetName() : "(none)");
        return picked;
    }

    //--------------------------------------------------------------
    // IFont 接口实现
    //--------------------------------------------------------------
    const Glyph& SHXCompositeFont::GetGlyph(uint32_t cp)
    {
        if (auto it = m_glyphCache.find(cp); it != m_glyphCache.end())
            return it->second;

        Glyph g;
        double k = 1.0;
        if (IFont* f = PickFontFor(cp))
        {
            g = f->GetGlyph(cp);
            // 把来源字体的 SHX 单元空间归一到参考(西文)字体单元空间,
            // 否则中英文混排时各自原生 fontHeight 不同会导致字高不一致
            k = NormFactor(f);
        }
        else if (double h = 0.0; m_fallback && m_fallback->Lookup(cp, g, h))
            k = (h > 0.0) ? GetHeight() / h : 1.0;
        else if (ShowsMissingMark(cp))
        {
            Glyph mark = GetGlyph('?');
            return m_glyphCache.emplace(cp, std::move(mark)).first->second;
        }
        else
            return m_glyphCache[cp];

        if (k != 1.0)
        {
            for (auto& line : g.Lines)
            {
                line.Start.x *= k; line.Start.y *= k;
                line.End.x   *= k; line.End.y   *= k;
            }
            g.Advance *= k;
            ComputeBounds(g);
        }
        return m_glyphCache.emplace(cp, std::move(g)).first->second;
    }

    double SHXCompositeFont::GetAdvance(uint32_t cp)
    {
        IFont* f = PickFontFor(cp);
        if (f) return f->GetAdvance(cp) * NormFactor(f);

        // 回退字体里的字、画成 "?" 的缺字按字形的步进；都没有时给一个合理的默认宽度，避免文本布局崩
        const Glyph& g = GetGlyph(cp);
        if (g.Advance > 0.0 || !g.Lines.empty())
            return g.Advance;
        return GetHeight() * 0.6;
    }

    double SHXCompositeFont::NormFactor(IFont* f) const
    {
        double ref = GetHeight();          // 参考(西文)字体单元高度
        double src = f ? f->GetHeight() : ref;
        return (src > 0.0) ? ref / src : 1.0;
    }

    double SHXCompositeFont::GetHeight() const
    {
        // 以主字体的 height 作为基准(西文 baseline);
        // 大字体的字形通常需要按比例缩放才能跟主字体协调
        if (m_mainFont) return m_mainFont->GetHeight();
        if (m_bigFont)  return m_bigFont->GetHeight();
        return 1.0;
    }

    uint64_t SHXCompositeFont::GetFontId()  const { return m_fontId; }
    const char* SHXCompositeFont::GetName() const { return m_name.c_str(); }

    bool SHXCompositeFont::IsLoaded() const
    {
        // 大字体缺失可以容忍（AutoCAD 也会退化成单字体），主字体读不到就没救了。
        // 两个都没有时也算没加载成功，让 FontEngine 有机会重试。
        auto Loaded = [](const std::shared_ptr<IFont>& f) -> bool
        {
            if (!f) return false;
            if (const auto* shx = dynamic_cast<const SHXFont*>(f.get()))
                return shx->IsLoaded();
            return true;    // TTF 等其它字体不参与这个判断
        };
        if (m_mainFont) return Loaded(m_mainFont);
        return Loaded(m_bigFont);
    }
}
