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

    // 形字体解析回调：复杂线型形段（组码 75）按形文件路径取字体。
    //
    // 为什么不能复用 MakeTextStyleResolver：形段的形号（130/131/134…）必须由
    // SHX **形文件**提供（TRACKS/ZIGZAG/BATTING 全部指向 ltypeshp.shx），
    // 而这个形文件通常没有对应文字样式表项（AutoCAD 的线型 STYLE 常是无名项），
    // 按 styleId 解析只会落到 Standard（simplex.shx），里面没有这些形。
    //
    // 字体名即缓存键：同一形文件只加载一份。FontEngine 内部按字体组合缓存，
    // 所以 bigFontFile 留空（形文件都是单字节的 shapes 文件，不是大字体）。
    inline ShapeFontResolver MakeShapeFontResolver(FontSystem* fontSystem)
    {
        if (!fontSystem || !fontSystem->IsReady())
            return nullptr;

        return [fontSystem](const std::string& fontFile) -> IFont*
        {
            if (fontFile.empty())
                return nullptr;
            FontStyle fs;
            fs.name     = fontFile;      // 缓存键：同一形文件共用一份
            fs.fontFile = fontFile;
            fs.isShx    = true;
            return &fontSystem->ResolveFont(fs);
        };
    }
}
