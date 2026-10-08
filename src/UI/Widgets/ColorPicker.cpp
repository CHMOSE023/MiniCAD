#include "Widgets/ColorPicker.h"
#include "Widgets/Label.h"
#include "Widgets/TextBox.h"
#include "Style/Theme.hpp"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace MiniGUI
{
    std::optional<Color32> ParseHexColor(std::string_view text)
    {
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
        while (!text.empty() && text.back() == ' ')  text.remove_suffix(1);
        if (!text.empty() && text.front() == '#')
            text.remove_prefix(1);
        if (text.size() != 6 && text.size() != 3)
            return std::nullopt;

        uint32_t v = 0;
        for (char c : text)
        {
            int d;
            if (c >= '0' && c <= '9')      d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return std::nullopt;
            v = v * 16 + static_cast<uint32_t>(d);
        }
        if (text.size() == 3)   // #RGB → #RRGGBB
            v = ((v >> 8) & 0xF) * 0x110000 + ((v >> 4) & 0xF) * 0x1100 + (v & 0xF) * 0x11;
        return ColorFromHex(v);
    }

    namespace
    {
        constexpr float kCell = 18.0f;
        constexpr float kGap  = 3.0f;
        constexpr int   kCols = 12;

        Color32 FromHSV(float h, float s, float v)
        {
            const float c = v * s;
            const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
            const float m = v - c;
            float r = 0, g = 0, b = 0;
            if (h < 60)       { r = c; g = x; }
            else if (h < 120) { r = x; g = c; }
            else if (h < 180) { g = c; b = x; }
            else if (h < 240) { g = x; b = c; }
            else if (h < 300) { r = x; b = c; }
            else              { r = c; b = x; }
            auto u8 = [](float f) { return static_cast<uint8_t>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
            return RGBA(u8(r + m), u8(g + m), u8(b + m));
        }

        // 调色板的行：第一行 AutoCAD 标准色，中间色相 × 明度，最后一行灰度
        std::vector<std::vector<Color32>> BuildPalette()
        {
            std::vector<std::vector<Color32>> rows;
            rows.push_back({ ColorFromHex(0xFF0000), ColorFromHex(0xFFFF00), ColorFromHex(0x00FF00), ColorFromHex(0x00FFFF),
                             ColorFromHex(0x0000FF), ColorFromHex(0xFF00FF), ColorFromHex(0xFFFFFF), ColorFromHex(0x808080),
                             ColorFromHex(0xC0C0C0) });
            const float sv[5][2] = { { 0.25f, 1.0f }, { 0.5f, 1.0f }, { 0.85f, 0.95f }, { 0.9f, 0.7f }, { 0.9f, 0.45f } };
            for (const auto& p : sv)
            {
                std::vector<Color32> row;
                for (int i = 0; i < kCols; ++i)
                    row.push_back(FromHSV(static_cast<float>(i) * 30.0f, p[0], p[1]));
                rows.push_back(row);
            }
            std::vector<Color32> grays;
            for (int i = 0; i < kCols; ++i)
            {
                const auto g = static_cast<uint8_t>(std::lround(255.0f * static_cast<float>(i) / (kCols - 1)));
                grays.push_back(RGBA(g, g, g));
            }
            rows.push_back(grays);
            return rows;
        }

        std::string ToHex(Color32 c)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
            return buf;
        }
    }

    // 色块网格：悬停高亮，点击选择
    class ColorSwatchGrid : public Node
    {
    public:
        ColorSwatchGrid(Color32 current, std::function<void(Color32)> choose)
            : m_current(current), m_choose(std::move(choose)), m_rows(BuildPalette()) {}

    protected:
        Rect CellRect(size_t row, size_t col) const
        {
            // 第一行和其余行之间多留一点间距
            const float y = static_cast<float>(row) * (kCell + kGap) + (row > 0 ? 6.0f : 0.0f);
            const float x = static_cast<float>(col) * (kCell + kGap);
            return Rect::FromXYWH(x, y, kCell, kCell);
        }

        Vec2 MeasureContent(Vec2) override
        {
            const Rect last = CellRect(m_rows.size() - 1, kCols - 1);
            return last.max;
        }

        bool CellAt(Vec2 p, size_t& row, size_t& col) const
        {
            for (size_t r = 0; r < m_rows.size(); ++r)
                for (size_t c = 0; c < m_rows[r].size(); ++c)
                    if (CellRect(r, c).Contains(p)) { row = r; col = c; return true; }
            return false;
        }

        void OnPaint(DrawList& dl, const Rect& r) override
        {
            for (size_t row = 0; row < m_rows.size(); ++row)
                for (size_t col = 0; col < m_rows[row].size(); ++col)
                {
                    const Rect lr = CellRect(row, col);
                    const Rect cell{ lr.min + r.min, lr.max + r.min };
                    const Color32 color = m_rows[row][col];
                    dl.AddRectFilled(cell, color, 3.0f);
                    const bool hover   = m_hoverRow == static_cast<int>(row) && m_hoverCol == static_cast<int>(col);
                    const bool current = (color & 0x00FFFFFFu) == (m_current & 0x00FFFFFFu);
                    if (hover || current)
                        dl.AddRect(cell.Deflated(-2.0f), hover ? Theme::Outline : Theme::Accent, 4.0f, 1.5f);
                    else
                        dl.AddRect(cell, ColorRef(ThemeColor::Outline, 30), 3.0f, 1.0f);
                }
        }

        void OnPointerEvent(PointerEvent& e) override
        {
            size_t row = 0, col = 0;
            const bool hit = CellAt(e.localPosition, row, col);
            switch (e.type)
            {
            case PointerEventType::Move:
            {
                const int hr = hit ? static_cast<int>(row) : -1, hc = hit ? static_cast<int>(col) : -1;
                if (hr != m_hoverRow || hc != m_hoverCol) { m_hoverRow = hr; m_hoverCol = hc; Invalidate(); }
                break;
            }
            case PointerEventType::Leave:
                m_hoverRow = m_hoverCol = -1;
                Invalidate();
                break;
            case PointerEventType::Down:
                if (hit && e.button == MouseButton::Left)
                {
                    e.handled = true;
                    auto choose = m_choose;     // 回调会关闭弹层（销毁自己推迟到下一帧，复制一份更稳妥）
                    choose(m_rows[row][col]);
                }
                break;
            default:
                break;
            }
        }

    private:
        Color32                     m_current;
        std::function<void(Color32)> m_choose;
        std::vector<std::vector<Color32>> m_rows;
        int m_hoverRow = -1, m_hoverCol = -1;
    };

    // =========================================================
    // ColorButton
    // =========================================================
    ColorButton::ColorButton(Color32 color)
        : m_color(color)
    {
        SetFocusable(true);
    }

    Vec2 ColorButton::MeasureContent(Vec2)
    {
        return { 48.0f, Theme::ControlH };
    }

    void ColorButton::OnPaint(DrawList& dl, const Rect& r)
    {
        const bool focusRing = HasFocus() && GetContext() && GetContext()->IsFocusVisible();
        dl.AddRectFilled(r, IsHovered() ? Theme::ControlHover : Theme::Control, Theme::Rounding);
        dl.AddRect(r, (m_popup || focusRing) ? Theme::Accent : Theme::Border, Theme::Rounding, 1.0f);
        const Rect swatch = r.Deflated(5.0f);
        dl.AddRectFilled(swatch, m_color | 0xFF000000u, 2.0f);
        dl.AddRect(swatch, ColorRef(ThemeColor::Outline, 40), 2.0f, 1.0f);
    }

    void ColorButton::Choose(Color32 color)
    {
        color |= 0xFF000000u;
        if (m_popup)
            m_popup->Close();
        if (color == m_color)
            return;
        m_color = color;
        Invalidate();
        if (m_onChanged)
        {
            auto cb = m_onChanged;
            cb(color);
        }
    }

    void ColorButton::Open()
    {
        UIContext* ctx = GetContext();
        if (!ctx || m_popup)
            return;
        m_popup = ShowColorPalette(*ctx, GetScreenBounds(), m_color, [this](Color32 c) { Choose(c); }, this);
        Popup* raw = m_popup;
        raw->SetOnClosed([this, raw] { if (m_popup == raw) { m_popup = nullptr; Invalidate(); } });
        Invalidate();
    }

    // =========================================================
    // 调色板弹层
    // =========================================================
    Popup* ShowColorPalette(UIContext& ctx, const Rect& anchor, Color32 current, std::function<void(Color32)> onChosen, Node* owner)
    {
        auto popup = std::make_unique<Popup>();
        {
            LayoutStyle s;
            s.padding = Edges::All(10.0f);
            s.gap     = 10.0f;
            popup->SetLayoutStyle(s);
        }
        Popup* raw = popup.get();
        // 选中后先关闭弹层再回调：回调里可能打开新的弹层
        auto choose = [raw, onChosen](Color32 c)
        {
            raw->Close();
            if (onChosen)
                onChosen(c | 0xFF000000u);
        };
        popup->AddChild<ColorSwatchGrid>(current, choose);

        Node* row = popup->AddChild<Node>();
        {
            LayoutStyle s;
            s.direction  = FlexDirection::Row;
            s.alignItems = Align::Center;
            s.gap        = 8.0f;
            row->SetLayoutStyle(s);
        }
        row->AddChild<Label>("十六进制", 12.0f, Theme::TextDim);
        TextBox* hex = row->AddChild<TextBox>(ToHex(current));
        hex->EditLayoutStyle().grow = 1.0f;
        hex->SetOnSubmit([choose, hex](const std::string& text)
        {
            if (auto c = ParseHexColor(text))
                choose(*c);
            else
                hex->SelectAll();
        });

        popup->SetAnchor(anchor, PopupPlacement::Below);
        if (owner)
            popup->SetOwner(owner);
        ctx.OpenPopup(std::move(popup));
        return raw;
    }

    void ColorButton::OnPointerEvent(PointerEvent& e)
    {
        if (e.phase == EventPhase::Capture)
            return;
        if (e.type == PointerEventType::Enter || e.type == PointerEventType::Leave)
            Invalidate();
        if (e.type == PointerEventType::Down && e.button == MouseButton::Left)
        {
            if (m_popup) m_popup->Close(); else Open();
            e.handled = true;
        }
    }

    void ColorButton::OnKeyEvent(KeyEvent& e)
    {
        if (e.phase == EventPhase::Target && e.type == KeyEventType::Down && e.modifiers == 0 &&
            (e.key == Key::Space || e.key == Key::Enter))
        {
            Open();
            e.handled = true;
        }
    }
}
