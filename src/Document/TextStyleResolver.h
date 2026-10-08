#pragma once
#include "Document/DrawContext.hpp"
#include "Scene/TextStyleTable.h"
#include "Text/FontSystem.h"
#include "Core/Math/Constants.hpp"

namespace MiniCAD
{
    // 文字样式解析回调：styleId 查文字样式表（找不到按 Standard），字体经 FontEngine 按字体组合缓存。
    // table 须比回调活得久（通常是文档场景里的表）。
    inline FontResolver MakeTextStyleResolver(FontSystem* fontSystem, const TextStyleTable* table)
    {
        if (!fontSystem || !fontSystem->IsReady() || !table)
            return nullptr;

        return [fontSystem, table](uint32_t styleId) -> ResolvedTextStyle
        {
            const TextStyleRecord& rec = table->Resolve(styleId);
            FontStyle fs;
            fs.name        = rec.FontFile + "+" + rec.BigFontFile;     // 缓存键：同一字体组合共用
            fs.fontFile    = rec.FontFile;
            fs.isShx       = rec.IsShx();
            fs.bigFontFile = fs.isShx ? rec.BigFontFile : std::string();
            ResolvedTextStyle rs;
            rs.Font        = &fontSystem->ResolveFont(fs);
            rs.WidthFactor = rec.WidthFactor > 0.0 ? rec.WidthFactor : 1.0;
            rs.Oblique     = rec.ObliqueDeg * Math::PI / 180.0;
            return rs;
        };
    }
}
