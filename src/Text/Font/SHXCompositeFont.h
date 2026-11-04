#pragma once
#include <functional>
#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "IFont.h"

namespace MiniCAD
{
    class SHXFont;

    // SHX 文字缺字时依次找的单线字体（随程序带的 minicad_*.shx，tools/make_shx_cjk.py 生成）。
    // 单线字体不与 TrueType 填充字体混用，这里只放 SHX 字体
    class SHXFallbackChain
    {
    public:
        using Loader = std::function<std::shared_ptr<SHXFont>(size_t index)>;   // 第 index 个候选，没有时返回空
        SHXFallbackChain(size_t count, Loader loader);

        // 找到时复制字形、给出那个字体的字高（%%nnn 形号也找，如钢筋符号）
        bool Lookup(uint32_t codepoint, Glyph& glyph, double& fontHeight);

    private:
        std::mutex                             m_mutex;
        Loader                                 m_loader;
        std::vector<std::shared_ptr<SHXFont>>  m_fonts;
        std::vector<bool>                      m_probed;
        std::unordered_map<uint32_t, SHXFont*> m_route;
    };

    // SHX "主字体 + 大字体" 组合(AutoCAD 标准 STYLE 模式)
    //
    //   ASCII / 西文符号 (cp < 0x80)   → 主字体(如 txt.shx)
    //   非 ASCII         (cp >= 0x80)  → 大字体(如 tssdchn.shx)
    //   primary 拿不到时,自动 fallback 到另一个
    //   两个都没有 → 单线回退字体（SHXFallbackChain）→ 仍没有时画 "?"（同 AutoCAD）
    //
    // 任一字体为空指针都允许:相当于只用一个,但通过 Composite 统一对外。
    class SHXCompositeFont : public IFont
    {
    public:
        // fallback：两个 SHX 字体都没有的字（繁体字配简体大字体、韩文等）到单线回退字体里找
        SHXCompositeFont(const std::string& name, std::shared_ptr<IFont> mainFont, std::shared_ptr<IFont> bigFont, uint64_t fontId,
                         std::shared_ptr<SHXFallbackChain> fallback = nullptr);

        const Glyph& GetGlyph (uint32_t codepoint) override;
        double      GetAdvance(uint32_t codepoint) override;
        double      GetHeight () const             override;
        uint64_t    GetFontId () const             override;
        const char* GetName   () const             override;

        virtual bool HasGlyph(uint32_t codepoint) override { return true; };   // 新增

        // 主字体是否读进来了（大字体缺失可容忍，主字体没有就什么都画不出）。
        // FontEngine 用它判断缓存是否值得保留，见 SHXFont::IsLoaded 的注释。
        bool IsLoaded() const;

    private:
        // 决定一个 cp 最终该用哪个 IFont 渲染;两边都查不到返回 nullptr
        IFont* PickFontFor(uint32_t cp);

        // 把来源字体单元空间归一到参考(西文)字体单元空间的比例因子
        double NormFactor(IFont* f) const;

    private:
        std::string m_name;
        uint64_t    m_fontId = 0;

        std::shared_ptr<IFont> m_mainFont;   // 西文(txt.shx 等)
        std::shared_ptr<IFont> m_bigFont;    // 中文(tssdchn.shx 等)
        std::shared_ptr<SHXFallbackChain> m_fallback;

        // 路由缓存:避免每次都把两个字体都试一遍
        std::unordered_map<uint32_t, IFont*> m_routeCache;
        // 归一化后的字形缓存（GetGlyph 返回其中的引用）
        std::unordered_map<uint32_t, Glyph>  m_glyphCache;
    };
}
