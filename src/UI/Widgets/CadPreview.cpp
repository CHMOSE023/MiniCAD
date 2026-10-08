#include "Widgets/CadPreview.h"
#include "Style/Theme.hpp"
#include "Paint/DrawList.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace MiniGUI
{
    const std::vector<LinetypeDef>& StandardLinetypes()
    {
        static const std::vector<LinetypeDef> table =
        {
            { "Continuous", "实线",               {} },
            { "Dashed",     "虚线 __ __ __",       { 12.0f, -6.0f } },
            { "Hidden",     "隐藏线 _ _ _",        { 6.0f, -4.0f } },
            { "Center",     "中心线 ____ _ ____",  { 20.0f, -4.0f, 4.0f, -4.0f } },
            { "Phantom",    "双点划线 ___ _ _ ___", { 20.0f, -4.0f, 4.0f, -4.0f, 4.0f, -4.0f } },
            { "Dot",        "点线 . . . .",        { 0.0f, -4.0f } },
            { "DashDot",    "点划线 __ . __ .",    { 12.0f, -4.0f, 0.0f, -4.0f } },
            { "Border",     "边界线 __ __ . __",   { 12.0f, -4.0f, 12.0f, -4.0f, 0.0f, -4.0f } },
            { "Divide",     "分隔线 __ . . __",    { 12.0f, -4.0f, 0.0f, -4.0f, 0.0f, -4.0f } },
        };
        return table;
    }

    const std::vector<float>& StandardLineweights()
    {
        static const std::vector<float> table =
        {
            0.00f, 0.05f, 0.09f, 0.13f, 0.15f, 0.18f, 0.20f, 0.25f, 0.30f, 0.35f, 0.40f, 0.50f,
            0.53f, 0.60f, 0.70f, 0.80f, 0.90f, 1.00f, 1.06f, 1.20f, 1.40f, 1.58f, 2.00f, 2.11f,
        };
        return table;
    }

    void DrawLinetypeSample(DrawList& dl, const Rect& r, const std::vector<float>& pattern, ColorRef color, float thickness)
    {
        const float y = std::floor(r.Center().y) + 0.5f;   // 落在像素中心，1 像素线清晰
        if (pattern.empty())
        {
            dl.AddLine({ r.min.x, y }, { r.max.x, y }, color, thickness);
            return;
        }

        float x = r.min.x;
        size_t i = 0;
        while (x < r.max.x)
        {
            const float seg = pattern[i % pattern.size()];
            if (seg > 0.0f)
            {
                dl.AddLine({ x, y }, { std::min(x + seg, r.max.x), y }, color, thickness);
                x += seg;
            }
            else if (seg == 0.0f)
            {
                dl.AddCircleFilled({ x + 0.75f, y }, std::max(thickness, 1.2f) * 0.6f, color, 6);
                x += 1.5f;
            }
            else
            {
                x -= seg;
            }
            ++i;
        }
    }

    void DrawColorSwatch(DrawList& dl, const Rect& r, Color32 color, float rounding)
    {
        dl.AddRectFilled(r, color, rounding);
        dl.AddRect(r, ColorRef(ThemeColor::Outline, 40), rounding, 1.0f);
    }

    void DrawLineweightSample(DrawList& dl, const Rect& r, float millimeters, ColorRef color)
    {
        const float px = std::max(1.0f, std::round(millimeters * 96.0f / 25.4f));
        const float y  = std::round(r.Center().y - px * 0.5f);
        dl.AddRectFilled(Rect{ r.min.x, y, r.max.x, y + px }, color);
    }

    void DrawLayerOnIcon(DrawList& dl, const Rect& r, bool on)
    {
        // 灯泡：圆 + 底座
        const Vec2 c{ r.Center().x, r.Center().y - 1.5f };
        const ColorRef bulb = on ? ColorRef(ColorFromHex(0xF5C542)) : Theme::TextDisabled;
        dl.AddCircleFilled(c, 4.5f, bulb);
        dl.AddRectFilled(Rect{ c.x - 2.0f, c.y + 4.0f, c.x + 2.0f, c.y + 6.5f }, on ? ColorFromHex(0xB0B3B8) : ColorFromHex(0x4A4D52), 1.0f);
    }

    void DrawLayerFreezeIcon(DrawList& dl, const Rect& r, bool frozen)
    {
        const Vec2 c = r.Center();
        if (frozen)
        {
            // 雪花：三条交叉线
            const Color32 col = ColorFromHex(0x6CB6FF);
            for (int i = 0; i < 3; ++i)
            {
                const float a = static_cast<float>(i) * std::numbers::pi_v<float> / 3.0f;
                const Vec2  d{ std::cos(a) * 5.5f, std::sin(a) * 5.5f };
                dl.AddLine(c - d, c + d, col, 1.3f);
            }
        }
        else
        {
            // 太阳：实心圆 + 光芒
            const Color32 col = ColorFromHex(0xF5C542);
            dl.AddCircleFilled(c, 3.0f, col);
            for (int i = 0; i < 8; ++i)
            {
                const float a = static_cast<float>(i) * std::numbers::pi_v<float> / 4.0f;
                const Vec2  d{ std::cos(a), std::sin(a) };
                dl.AddLine(c + d * 4.5f, c + d * 6.0f, col, 1.0f);
            }
        }
    }

    void DrawLayerLockIcon(DrawList& dl, const Rect& r, bool locked)
    {
        const Vec2 c = r.Center();
        const ColorRef col = locked ? ColorRef(ColorFromHex(0xE0A050)) : Theme::TextDim;
        dl.AddRectFilled(Rect{ c.x - 4.5f, c.y - 1.0f, c.x + 4.5f, c.y + 5.5f }, col, 1.5f);
        // 锁梁：锁定时闭合，解锁时右侧抬起
        dl.PathArcTo({ c.x, c.y - 1.5f }, 3.0f, std::numbers::pi_v<float>, std::numbers::pi_v<float> * 2.0f);
        dl.PathStroke(col, false, 1.4f);
        if (locked)
            dl.AddLine({ c.x + 3.0f, c.y - 1.5f }, { c.x + 3.0f, c.y - 1.0f }, col, 1.4f);
        else
            dl.AddLine({ c.x + 3.0f, c.y - 1.5f }, { c.x + 3.0f, c.y - 4.0f }, col, 1.4f);
    }
}
