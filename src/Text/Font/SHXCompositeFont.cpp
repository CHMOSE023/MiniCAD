#include "SHXCompositeFont.h"
#include "pch.h"
#include <cstdio>

namespace MiniCAD
{
    SHXCompositeFont::SHXCompositeFont(const std::string& name, std::shared_ptr<IFont> mainFont, std::shared_ptr<IFont> bigFont, uint64_t fontId)
        : m_name(name)
        , m_fontId(fontId)
        , m_mainFont(std::move(mainFont))
        , m_bigFont(std::move(bigFont))
    {
        printf("[Composite] '%s'  main=%s  big=%s\n", m_name.c_str(), m_mainFont ? m_mainFont->GetName() : "(none)", m_bigFont ? m_bigFont->GetName() : "(none)");
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
        printf("[Composite] U+%04X → %s\n", cp, picked ? picked->GetName() : "(none)");
        return picked;
    }

    //--------------------------------------------------------------
    // IFont 接口实现
    //--------------------------------------------------------------
    Glyph SHXCompositeFont::GetGlyph(uint32_t cp)
    {
        IFont* f = PickFontFor(cp);
        if (!f) return Glyph{};

        Glyph g = f->GetGlyph(cp);

        // 把来源字体的 SHX 单元空间归一到参考(西文)字体单元空间,
        // 否则中英文混排时各自原生 fontHeight 不同会导致字高不一致
        double k = NormFactor(f);
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
        return g;
    }

    double SHXCompositeFont::GetAdvance(uint32_t cp)
    {
        IFont* f = PickFontFor(cp);
        if (f) return f->GetAdvance(cp) * NormFactor(f);

        // 两边都 MISS:给一个合理的默认宽度,避免文本布局崩
        double h = GetHeight();
        return h * 0.6;
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
}
