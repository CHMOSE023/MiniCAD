#pragma once
// ── 线型预览：把 MiniCAD 的 LineTypeRecord 变成图层面板 / 对话框能画的一行示意 ──
//
// 图层面板、线型下拉、线型比例对话框都要「把线型画一条出来」。三处各写一套
// dash 画法必然对不上，所以 dash 本体统一交给 MiniGUI::DrawLinetypeSample
// （它只吃像素序列，不依赖 MiniCAD 类型），这里只负责把 Scene 侧的
// LineTypeRecord 换算过去。
//
// 复杂线型（AutoCAD complex linetype，如 GAS_LINE / TRACKS / ZIGZAG）带文字段
// 或形段。视口里画的是真实字形（文字走 TextLayoutEngine，形段走 SHX 形文件），
// 但界面层拿不到形文件，所以预览里用占位标记示意：文字段画小方框、形段画
// 小菱形，位置与视口里符号的位置一致 —— 用户至少能看出「这条线带符号」，
// 而不是和一条普通虚线混在一起。
#include "Scene/LineTypeTable.h"
#include "Widgets/CadPreview.h"
#include <vector>

namespace MiniCAD
{
    // 线型的 dash 序列（绘图单位）→ 预览用的逻辑像素。
    // 倍率 6 是把 0.5 / 0.2 这类图纸单位放大到 3 / 1.2 像素，刚好看得见；
    // 图层面板只有一行高度，再大就会挤成一团。
    inline std::vector<float> LinetypePreviewPattern(const LineTypeRecord& rec,
                                                     double pixelsPerUnit = 6.0)
    {
        std::vector<float> p;
        p.reserve(rec.Pattern.size());
        for (double v : rec.Pattern)
            p.push_back(static_cast<float>(v * pixelsPerUnit));
        return p;
    }

    // 复杂线型的符号段 → 预览占位标记。没有符号时返回空向量
    // （调用方应改用不带 symbols 的重载，行为与旧的纯 dash 预览完全一致）。
    inline std::vector<MiniGUI::LinetypeSampleSymbol> LinetypePreviewSymbols(const LineTypeRecord& rec)
    {
        std::vector<MiniGUI::LinetypeSampleSymbol> s;
        for (const auto& sy : rec.Symbols)
        {
            // 符号的下标必须落在 Pattern 里，否则该符号在视口里也不会被铺出来
            if (sy.PatternIndex >= rec.Pattern.size())
                continue;
            MiniGUI::LinetypeSampleSymbol one;
            one.patternIndex = sy.PatternIndex;
            one.isShape      = sy.Shape != 0;   // 组码 75 非 0 = 形段
            s.push_back(one);
        }
        return s;
    }
}
