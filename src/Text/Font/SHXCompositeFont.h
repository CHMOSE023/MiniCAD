#pragma once
#include <string>
#include <memory>
#include <unordered_map>

#include "IFont.h"

namespace MiniCAD
{
    // SHX "主字体 + 大字体" 组合(AutoCAD 标准 STYLE 模式)
    //
    //   ASCII / 西文符号 (cp < 0x80)   → 主字体(如 txt.shx)
    //   非 ASCII         (cp >= 0x80)  → 大字体(如 tssdchn.shx)
    //   primary 拿不到时,自动 fallback 到另一个
    //
    // 任一字体为空指针都允许:相当于只用一个,但通过 Composite 统一对外。
    class SHXCompositeFont : public IFont
    {
    public:
        SHXCompositeFont(const std::string& name, std::shared_ptr<IFont> mainFont, std::shared_ptr<IFont> bigFont, uint64_t fontId);

        Glyph       GetGlyph  (uint32_t codepoint) override;
        double      GetAdvance(uint32_t codepoint) override;
        double      GetHeight () const             override;
        uint64_t    GetFontId () const             override;
        const char* GetName   () const             override;

        virtual bool HasGlyph(uint32_t codepoint) override { return true; };   // 新增

    private:
        // 决定一个 cp 最终该用哪个 IFont 渲染;两边都查不到返回 nullptr
        IFont* PickFontFor(uint32_t cp);

    private:
        std::string m_name;
        uint64_t    m_fontId = 0;

        std::shared_ptr<IFont> m_mainFont;   // 西文(txt.shx 等)
        std::shared_ptr<IFont> m_bigFont;    // 中文(tssdchn.shx 等)

        // 路由缓存:避免每次都把两个字体都试一遍
        std::unordered_map<uint32_t, IFont*> m_routeCache;
    };
}
