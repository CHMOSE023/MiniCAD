#include "Widgets/TextBox.h"
#include "Widgets/AutoComplete.h"
#include "Widgets/Menu.h"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include "Text/Utf8.hpp"
#include <cmath>
#include <limits>

namespace MiniGUI
{
    namespace
    {
        constexpr size_t kMaxUndo = 100;

        // 字符类别：用于按词移动、双击选词。连续的同类字符构成一个"词"
        enum class CharClass { Space, Newline, Word, CJK, Punct };

        CharClass Classify(uint32_t cp)
        {
            if (cp == '\n')                                      return CharClass::Newline;
            if (cp == ' ' || cp == '\t' || cp == 0x3000)         return CharClass::Space;
            if (IsCJK(cp) && !(cp >= 0x3000 && cp <= 0x303F) && !(cp >= 0xFF00 && cp <= 0xFF0F))
                return CharClass::CJK;
            if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || cp == '_' || cp >= 0x80)
                return CharClass::Word;
            return CharClass::Punct;
        }

        bool IsPrintableKey(Key k)
        {
            return (k >= Key::A && k <= Key::Z) || (k >= Key::Num0 && k <= Key::Num9) || k == Key::Space;
        }
    }

    TextBox::TextBox(std::string text)
        : m_text(std::move(text))
    {
        SetFocusable(true);
        m_caret = m_anchor = m_text.size();
        // 默认的编辑右键菜单；调用方可以用 SetContextMenuHandler 替换
        SetContextMenuHandler([this](Vec2 pos) { ShowEditMenu(pos); return true; });
    }

    void TextBox::ShowEditMenu(Vec2 windowPos)
    {
        UIContext* ctx = GetContext();
        if (!ctx)
            return;
        const bool sel      = HasSelection();
        const bool editable = !m_readOnly;
        const bool canPaste = editable && !ctx->GetClipboard().GetText().empty();

        ShowContextMenu(*ctx, {
            MenuItem("撤销(&U)", [this] { Undo(); }, "Ctrl+Z").Enabled(editable && CanUndo()),
            MenuItem("重做(&R)", [this] { Redo(); }, "Ctrl+Y").Enabled(editable && CanRedo()),
            MenuItem::Separator(),
            MenuItem("剪切(&T)", [this] { Cut(); },   "Ctrl+X").Enabled(editable && sel),
            MenuItem("复制(&C)", [this] { Copy(); },  "Ctrl+C").Enabled(sel),
            MenuItem("粘贴(&P)", [this] { Paste(); }, "Ctrl+V").Enabled(canPaste),
            MenuItem("删除(&D)", [this] { DeleteSelection(); }, "Del").Enabled(editable && sel),
            MenuItem::Separator(),
            MenuItem("全选(&A)", [this] { SelectAll(); }, "Ctrl+A").Enabled(!m_text.empty()),
        }, windowPos);
    }

    void TextBox::Cut()
    {
        Copy();
        DeleteSelection();
    }

    void TextBox::DeleteSelection()
    {
        if (HasSelection())
            DeleteRange(SelectionStart(), SelectionEnd(), EditKind::Other);
    }

    TextBox::~TextBox() = default;

    AutoComplete* TextBox::EnableAutoComplete(std::function<std::vector<ListItem>(const std::string&)> provider)
    {
        m_autoComplete = std::make_unique<AutoComplete>(this, std::move(provider));
        return m_autoComplete.get();
    }

    // =========================================================
    // 属性
    // =========================================================
    void TextBox::SetText(std::string text)
    {
        m_text = std::move(text);
        m_caret = m_anchor = m_text.size();
        m_undo.clear();
        m_redo.clear();
        m_lastEdit    = EditKind::None;
        m_scroll      = {};
        m_followCaret = !m_multiline;     // 多行文字从头显示；单行显示光标所在的末尾
        m_layoutValid = false;
        Invalidate();
    }

    void TextBox::SetPlaceholder(std::string placeholder)
    {
        m_placeholder = std::move(placeholder);
        Invalidate();
    }

    void TextBox::SetMultiline(bool multiline)
    {
        m_multiline   = multiline;
        m_layoutValid = false;
        if (multiline && !HasFocus())
        {
            m_scroll      = {};
            m_followCaret = false;
        }
        InvalidateLayout();
    }

    void TextBox::SetRows(int rows)
    {
        m_rows = std::max(rows, 1);
        InvalidateLayout();
    }

    void TextBox::SetReadOnly(bool readOnly)
    {
        m_readOnly = readOnly;
        Invalidate();
    }

    void TextBox::SetStyle(const TextBoxStyle& style)
    {
        m_style       = style;
        m_layoutValid = false;
        InvalidateLayout();
    }

    // =========================================================
    // 选区
    // =========================================================
    std::string TextBox::GetSelectedText() const
    {
        return m_text.substr(SelectionStart(), SelectionEnd() - SelectionStart());
    }

    void TextBox::SetSelection(size_t anchor, size_t caret)
    {
        // 外部传入的偏移可能落在多字节字符中间：向前对齐到字符边界
        auto snap = [this](size_t pos)
        {
            pos = std::min(pos, m_text.size());
            while (pos > 0 && pos < m_text.size() && (static_cast<uint8_t>(m_text[pos]) & 0xC0) == 0x80)
                --pos;
            return pos;
        };
        m_anchor     = snap(anchor);
        m_caret      = snap(caret);
        m_lastEdit   = EditKind::None;
        m_preferredX = -1.0f;
        Invalidate();
    }

    void TextBox::SelectAll()
    {
        SetSelection(0, m_text.size());
    }

    void TextBox::MoveCaret(size_t pos, bool extend)
    {
        m_caret = std::min(pos, m_text.size());
        if (!extend)
            m_anchor = m_caret;
        m_lastEdit   = EditKind::None;   // 光标移动打断连续输入的撤销合并
        m_preferredX = -1.0f;
        m_followCaret = true;
        RestartBlink();
        Invalidate();
    }

    void TextBox::ReplaceAll(std::string_view text)
    {
        if (m_readOnly || text == m_text)
            return;
        PushUndo(EditKind::Other);
        m_text.assign(text);
        m_caret = m_anchor = m_text.size();
        m_lastEdit = EditKind::None;
        Changed();
    }

    void TextBox::RestartBlink()
    {
        m_caretVisible = true;
        if (!HasFocus())
            return;
        UIContext* ctx = GetContext();
        if (!ctx)
            return;
        if (m_blinkTimer)
            ctx->StopTimer(m_blinkTimer);
        m_blinkTimer = ctx->StartTimer(530, [this]
        {
            m_caretVisible = !m_caretVisible;
            Invalidate();
        }, true);
    }

    void TextBox::StopBlink()
    {
        if (m_blinkTimer)
        {
            if (UIContext* ctx = GetContext())
                ctx->StopTimer(m_blinkTimer);
            m_blinkTimer = 0;
        }
        m_caretVisible = true;
    }

    // =========================================================
    // 编辑
    // =========================================================
    void TextBox::PushUndo(EditKind kind)
    {
        // 连续输入 / 连续删除合并为一步撤销
        const bool merge = kind == m_lastEdit && (kind == EditKind::Typing || kind == EditKind::Deleting) && !m_undo.empty();
        if (!merge)
        {
            m_undo.push_back(UndoState{ m_text, m_caret, m_anchor });
            if (m_undo.size() > kMaxUndo)
                m_undo.erase(m_undo.begin());
        }
        m_redo.clear();
        m_lastEdit = kind;
    }

    void TextBox::ReplaceSelection(std::string_view text, EditKind kind)
    {
        if (m_readOnly)
            return;
        PushUndo(kind);
        const size_t s = SelectionStart();
        m_text.replace(s, SelectionEnd() - s, text);
        m_caret = m_anchor = s + text.size();
        m_preferredX = -1.0f;
        Changed();
    }

    void TextBox::DeleteRange(size_t begin, size_t end, EditKind kind)
    {
        if (m_readOnly || begin >= end)
            return;
        PushUndo(kind);
        m_text.erase(begin, end - begin);
        m_caret = m_anchor = begin;
        m_preferredX = -1.0f;
        Changed();
    }

    void TextBox::Changed()
    {
        m_layoutValid = false;
        m_followCaret = true;
        RestartBlink();
        Invalidate();
        if (m_autoComplete)
            m_autoComplete->OnTextChanged();
        if (m_onChanged)
            m_onChanged(m_text);
    }

    void TextBox::Undo()
    {
        if (m_composing || m_undo.empty())
            return;
        m_redo.push_back(UndoState{ m_text, m_caret, m_anchor });
        const UndoState s = std::move(m_undo.back());
        m_undo.pop_back();
        m_text = s.text; m_caret = s.caret; m_anchor = s.anchor;
        m_lastEdit = EditKind::None;
        Changed();
    }

    void TextBox::Redo()
    {
        if (m_composing || m_redo.empty())
            return;
        m_undo.push_back(UndoState{ m_text, m_caret, m_anchor });
        const UndoState s = std::move(m_redo.back());
        m_redo.pop_back();
        m_text = s.text; m_caret = s.caret; m_anchor = s.anchor;
        m_lastEdit = EditKind::None;
        Changed();
    }

    void TextBox::Copy()
    {
        UIContext* ctx = GetContext();
        if (ctx && HasSelection())
            ctx->GetClipboard().SetText(GetSelectedText());
    }

    void TextBox::Paste()
    {
        UIContext* ctx = GetContext();
        if (!ctx || m_readOnly)
            return;

        std::string text;
        for (char c : ctx->GetClipboard().GetText())
        {
            if (c == '\r')
                continue;
            if (c == '\n' && !m_multiline)
                c = ' ';    // 单行输入框：换行变空格
            text.push_back(c);
        }
        if (!text.empty())
            ReplaceSelection(text, EditKind::Other);
    }

    // =========================================================
    // 字符与词边界
    // =========================================================
    size_t TextBox::PrevChar(size_t pos) const
    {
        if (pos == 0)
            return 0;
        --pos;
        while (pos > 0 && (static_cast<uint8_t>(m_text[pos]) & 0xC0) == 0x80)
            --pos;
        return pos;
    }

    size_t TextBox::NextChar(size_t pos) const
    {
        if (pos >= m_text.size())
            return m_text.size();
        DecodeUtf8(m_text, pos);
        return pos;
    }

    size_t TextBox::PrevWord(size_t pos) const
    {
        auto classBefore = [&](size_t p) { size_t q = PrevChar(p); return Classify(DecodeUtf8(m_text, q)); };

        while (pos > 0 && classBefore(pos) == CharClass::Space)
            pos = PrevChar(pos);
        if (pos == 0)
            return 0;
        const CharClass c = classBefore(pos);
        while (pos > 0 && classBefore(pos) == c)
            pos = PrevChar(pos);
        return pos;
    }

    size_t TextBox::NextWord(size_t pos) const
    {
        auto classAt = [&](size_t p) { return Classify(DecodeUtf8(m_text, p)); };

        if (pos >= m_text.size())
            return m_text.size();
        const CharClass c = classAt(pos);
        if (c != CharClass::Space)
        {
            while (pos < m_text.size() && classAt(pos) == c)
                pos = NextChar(pos);
        }
        while (pos < m_text.size() && classAt(pos) == CharClass::Space)
            pos = NextChar(pos);
        return pos;
    }

    void TextBox::WordAt(size_t pos, size_t& begin, size_t& end) const
    {
        begin = end = pos;
        if (m_text.empty())
            return;
        if (pos >= m_text.size())
            pos = PrevChar(m_text.size());

        auto classAt = [&](size_t p) { return Classify(DecodeUtf8(m_text, p)); };
        const CharClass c = classAt(pos);

        begin = pos;
        while (begin > 0 && classAt(PrevChar(begin)) == c)
            begin = PrevChar(begin);
        end = NextChar(pos);
        while (end < m_text.size() && classAt(end) == c)
            end = NextChar(end);
    }

    // =========================================================
    // 排版与几何
    // =========================================================
    TextParams TextBox::Params() const
    {
        TextParams p;
        p.size  = m_style.fontSize;
        p.color = m_style.text;
        p.wrap  = m_multiline;
        return p;
    }

    std::string TextBox::DisplayText() const
    {
        if (!m_composing || m_composition.empty())
            return m_text;
        std::string s = m_text.substr(0, m_caret);
        s += m_composition;
        s += m_text.substr(m_caret);
        return s;
    }

    size_t TextBox::DisplayCaret() const
    {
        if (!m_composing || m_composition.empty())
            return m_caret;

        // 组合串内的光标按字符计，换算成字节偏移
        size_t pos = 0;
        for (int i = 0; i < m_compositionCaret && pos < m_composition.size(); ++i)
            DecodeUtf8(m_composition, pos);
        return m_caret + pos;
    }

    Rect TextBox::ContentRect() const
    {
        const Vec2   size = GetSize();
        const Edges& pad  = m_style.padding;
        return Rect{ pad.left, pad.top, std::max(size.x - pad.right, pad.left), std::max(size.y - pad.bottom, pad.top) };
    }

    Vec2 TextBox::TextOrigin() const
    {
        return ContentRect().min - m_scroll;
    }

    const TextLayout& TextBox::EnsureLayout() const
    {
        UIContext* ctx = GetContext();
        if (!ctx)
            return m_layout;

        const std::string display = DisplayText();
        const float width = m_multiline ? ContentRect().Width() : std::numeric_limits<float>::infinity();
        const float scale = ctx->GetPixelScale();
        if (m_layoutValid && width == m_layoutWidth && scale == m_layoutScale && display == m_layoutText)
            return m_layout;

        ctx->GetTextSystem().BuildLayout(display, Params(), width, m_layout);
        m_layoutText  = display;
        m_layoutWidth = width;
        m_layoutScale = scale;
        m_layoutValid = true;
        return m_layout;
    }

    Rect TextBox::CaretRectLocal() const
    {
        const TextLayout& layout = EnsureLayout();
        const Vec2   origin = TextOrigin();
        if (layout.lines.empty())
            return Rect{ origin, origin + Vec2{ 1.0f, m_style.fontSize } };

        const size_t dc   = DisplayCaret();
        const size_t line = layout.LineOf(dc);
        const float  x    = origin.x + layout.CaretX(line, dc);
        const float  y    = origin.y + layout.lines[line].y;
        return Rect{ x, y, x + 1.0f, y + layout.lineHeight };
    }

    void TextBox::ScrollToCaret() const
    {
        // 光标移动或编辑之后才跟随光标滚动；其余时候只把滚动量限制在有效范围内（保留滚轮滚动的位置）
        const TextLayout& layout = EnsureLayout();
        if (layout.lines.empty())
            return;

        const bool   follow  = m_followCaret;
        m_followCaret = false;
        const Rect   content = ContentRect();
        const size_t dc      = DisplayCaret();
        const size_t line    = layout.LineOf(dc);

        if (!m_multiline)
        {
            const float x = layout.CaretX(line, dc);
            if (follow && x - m_scroll.x > content.Width() - 1.0f) m_scroll.x = x - content.Width() + 1.0f;
            if (follow && x - m_scroll.x < 0.0f)                   m_scroll.x = x;
            const float maxScroll = std::max(0.0f, layout.lines[0].width - content.Width() + 1.0f);
            m_scroll.x = std::clamp(m_scroll.x, 0.0f, maxScroll);
            m_scroll.y = 0.0f;
        }
        else
        {
            const float y  = layout.lines[line].y;
            const float lh = layout.lineHeight;
            if (follow && y + lh - m_scroll.y > content.Height()) m_scroll.y = y + lh - content.Height();
            if (follow && y - m_scroll.y < 0.0f)                  m_scroll.y = y;
            m_scroll.y = std::clamp(m_scroll.y, 0.0f, MaxScrollY());
            m_scroll.x = 0.0f;
        }
    }

    float TextBox::MaxScrollY() const
    {
        const TextLayout& layout = EnsureLayout();
        return std::max(0.0f, layout.lineHeight * static_cast<float>(layout.lines.size()) - ContentRect().Height());
    }

    size_t TextBox::HitTest(Vec2 local) const
    {
        const TextLayout& layout = EnsureLayout();
        return std::min(layout.HitTest(local - TextOrigin()), m_text.size());
    }

    size_t TextBox::LineStart(size_t pos) const
    {
        if (!m_multiline)
            return 0;
        const TextLayout& layout = EnsureLayout();
        return layout.lines.empty() ? 0 : layout.lines[layout.LineOf(pos)].byteBegin;
    }

    size_t TextBox::LineEnd(size_t pos) const
    {
        if (!m_multiline)
            return m_text.size();
        const TextLayout& layout = EnsureLayout();
        return layout.lines.empty() ? m_text.size() : layout.lines[layout.LineOf(pos)].byteEnd;
    }

    size_t TextBox::VerticalMove(size_t pos, int lines) const
    {
        const TextLayout& layout = EnsureLayout();
        if (layout.lines.empty())
            return pos;

        const long line   = static_cast<long>(layout.LineOf(pos));
        const long target = line + lines;
        if (target < 0)
            return 0;
        if (target >= static_cast<long>(layout.lines.size()))
            return m_text.size();

        const float x = m_preferredX >= 0.0f ? m_preferredX : layout.CaretX(static_cast<size_t>(line), pos);
        return layout.LineHitTest(static_cast<size_t>(target), x);
    }

    bool TextBox::GetTextCaretRect(Rect& out) const
    {
        if (!GetContext())
            return false;
        const Rect local  = CaretRectLocal();
        const Vec2 origin = GetScreenBounds().min;
        out = Rect{ local.min + origin, local.max + origin };
        return true;
    }

    Vec2 TextBox::MeasureContent(Vec2 available)
    {
        (void)available;
        UIContext*  ctx = GetContext();
        const float lh  = ctx ? ctx->GetTextSystem().GetLineHeight(m_style.fontSize) : m_style.fontSize * 1.35f;
        const int   rows = m_multiline ? m_rows : 1;
        return { 160.0f + m_style.padding.Horizontal(), lh * static_cast<float>(rows) + m_style.padding.Vertical() };
    }

    // =========================================================
    // 绘制
    // =========================================================
    void TextBox::DrawRanges(DrawList& dl, Vec2 origin, size_t begin, size_t end, ColorRef color, bool underline) const
    {
        const TextLayout& layout = m_layout;
        for (size_t li = 0; li < layout.lines.size(); ++li)
        {
            const TextLayout::Line& line = layout.lines[li];
            const size_t lb = std::max(begin, line.byteBegin);
            const size_t le = std::min(end, line.byteEnd);
            // 选区跨过换行符时，在行尾多画一小段，表示换行符也被选中
            const bool pastEnd = end > line.byteEnd && li + 1 < layout.lines.size() && begin <= line.byteEnd;
            if (lb > le || (lb == le && !pastEnd))
                continue;

            const float x0 = layout.CaretX(li, lb);
            float       x1 = le >= line.byteEnd ? line.width : layout.CaretX(li, le);
            if (pastEnd && !underline)
                x1 += 4.0f;

            if (underline)
                dl.AddRectFilled(Rect{ origin.x + x0, origin.y + line.y + layout.lineHeight - 2.0f,
                                       origin.x + x1, origin.y + line.y + layout.lineHeight - 1.0f }, color);
            else
                dl.AddRectFilled(Rect{ origin.x + x0, origin.y + line.y,
                                       origin.x + x1, origin.y + line.y + layout.lineHeight }, color);
        }
    }

    void TextBox::OnPaint(DrawList& dl, const Rect& screenRect)
    {
        const bool focused = HasFocus();
        const ColorRef border = focused ? m_style.borderFocus : (IsHovered() ? m_style.borderHover : m_style.border);
        dl.AddRectFilled(screenRect, m_style.background, m_style.rounding);
        dl.AddRect(screenRect, border, m_style.rounding, 1.0f);

        UIContext* ctx = GetContext();
        if (!ctx)
            return;
        TextSystem& text = ctx->GetTextSystem();

        ScrollToCaret();
        EnsureLayout();     // 确保 m_layoutText / m_layout 对应当前文字

        const Rect content = ContentRect();
        const Rect contentScreen{ content.min + screenRect.min, content.max + screenRect.min };
        const Vec2 origin = screenRect.min + TextOrigin();

        // 光标可能正好在内容区右边缘，裁剪区向右多留 1 像素
        dl.PushClipRect(Rect{ contentScreen.min, contentScreen.max + Vec2{ 1.0f, 0.0f } });

        TextParams params = Params();
        if (!IsEnabled())
            params.color = ColorScaleAlpha(params.color, 0.45f);

        // 文字框：单行时不限宽度（由横向滚动处理），多行时宽度等于内容区（与排版时的换行宽度一致）
        const float boxWidth = m_multiline ? content.Width() : 1.0e6f;
        const Rect  box{ origin, origin + Vec2{ boxWidth, 1.0e6f } };

        if (m_layoutText.empty())
        {
            if (!m_placeholder.empty())
            {
                TextParams ph = params;
                ph.color = m_style.placeholder;
                text.Draw(dl, box, m_placeholder, ph);
            }
        }
        else
        {
            if (HasSelection() && !m_composing)
                DrawRanges(dl, origin, SelectionStart(), SelectionEnd(), focused ? m_style.selection : m_style.selectionInactive, false);

            text.Draw(dl, box, m_layoutText, params);

            if (m_composing && !m_composition.empty())
                DrawRanges(dl, origin, m_caret, m_caret + m_composition.size(), params.color, true);
        }

        if (focused && (m_caretVisible || m_composing))
        {
            const Rect caret = CaretRectLocal();
            // 光标横坐标对齐到物理像素，任何缩放下都是清晰的一条竖线
            const float scale = ctx->GetPixelScale();
            const float x     = std::round((screenRect.min.x + caret.min.x) * scale) / scale;
            const float w     = std::max(1.0f, std::round(scale)) / scale;
            dl.AddRectFilled(Rect{ x, screenRect.min.y + caret.min.y, x + w, screenRect.min.y + caret.max.y }, m_style.caret);
        }

        dl.PopClipRect();

        // 多行内容超出时，在右侧画一条细的位置指示（只指示，不可拖动）
        const float maxScroll = m_multiline ? MaxScrollY() : 0.0f;
        if (maxScroll > 0.0f)
        {
            const float trackH = contentScreen.Height();
            const float total  = trackH + maxScroll;
            const float thumbH = std::max(16.0f, trackH * trackH / total);
            const float y      = contentScreen.min.y + (trackH - thumbH) * (m_scroll.y / maxScroll);
            const float x      = screenRect.max.x - 5.0f;
            dl.AddRectFilled(Rect{ x, y, x + 3.0f, y + thumbH }, Theme::ScrollThumb, 1.5f);
        }
    }

    // =========================================================
    // 输入
    // =========================================================
    void TextBox::OnPointerEvent(PointerEvent& e)
    {
        if (e.phase == EventPhase::Capture)
            return;

        switch (e.type)
        {
        case PointerEventType::Enter:
        case PointerEventType::Leave:
            Invalidate();
            break;

        case PointerEventType::Down:
            // 右键：点在选区外时先把光标移到点击处（点在选区内保留选区，方便右键复制）
            if (e.button == MouseButton::Right && !m_composing)
            {
                const size_t pos = HitTest(e.localPosition);
                if (pos < SelectionStart() || pos > SelectionEnd())
                    MoveCaret(pos, false);
                break;
            }
            if (e.button != MouseButton::Left)
                break;
            e.handled = true;
            if (m_composing)
                break;      // 组合中不移动光标，避免组合串跑到别处
            {
                const size_t pos = HitTest(e.localPosition);
                if (e.clickCount >= 3)
                {
                    SetSelection(LineStart(pos), LineEnd(pos));
                }
                else if (e.clickCount == 2)
                {
                    size_t b, en;
                    WordAt(pos, b, en);
                    SetSelection(b, en);
                }
                else
                {
                    MoveCaret(pos, e.HasModifier(ModifierKey::Shift));
                }
                m_dragging = true;
                GetContext()->SetCapture(this);
            }
            break;

        case PointerEventType::Move:
            if (m_dragging && HasCapture())
            {
                MoveCaret(HitTest(e.localPosition), true);
                e.handled = true;
            }
            break;

        case PointerEventType::Up:
            if (e.button == MouseButton::Left && m_dragging)
            {
                m_dragging = false;
                GetContext()->ReleaseCapture();
                e.handled = true;
            }
            break;

        case PointerEventType::Cancel:
            m_dragging = false;
            break;

        case PointerEventType::Wheel:
            // 多行输入框：滚轮滚动内容（滚到头时不处理，交给外层滚动视图）
            if (m_multiline && e.wheelDelta.y != 0.0f)
            {
                const float before = m_scroll.y;
                m_scroll.y = std::clamp(m_scroll.y - e.wheelDelta.y * EnsureLayout().lineHeight * 3.0f, 0.0f, MaxScrollY());
                if (m_scroll.y != before)
                {
                    e.handled = true;
                    Invalidate();
                }
            }
            break;

        default:
            break;
        }
    }

    void TextBox::OnKeyEvent(KeyEvent& e)
    {
        if (e.phase != EventPhase::Target || e.type != KeyEventType::Down || e.Alt())
            return;

        if ((m_autoComplete && m_autoComplete->OnKey(e)) || (m_keyPreview && m_keyPreview(e)))
        {
            e.handled = true;
            return;
        }

        const bool ctrl  = e.Ctrl();
        const bool shift = e.Shift();
        bool handled = true;

        switch (e.key)
        {
        case Key::Left:
            if (HasSelection() && !shift && !ctrl) MoveCaret(SelectionStart(), false);
            else                                  MoveCaret(ctrl ? PrevWord(m_caret) : PrevChar(m_caret), shift);
            break;
        case Key::Right:
            if (HasSelection() && !shift && !ctrl) MoveCaret(SelectionEnd(), false);
            else                                  MoveCaret(ctrl ? NextWord(m_caret) : NextChar(m_caret), shift);
            break;

        case Key::Up:
        case Key::Down:
            if (!m_multiline)
            {
                handled = false;     // 单行时留给上层（例如列表、下拉框）
                break;
            }
            {
                const TextLayout& layout = EnsureLayout();
                const float x = m_preferredX >= 0.0f ? m_preferredX
                                                     : layout.CaretX(layout.LineOf(m_caret), m_caret);
                MoveCaret(VerticalMove(m_caret, e.key == Key::Up ? -1 : 1), shift);
                m_preferredX = x;    // 连续上下移动时保持同一横坐标
            }
            break;

        case Key::Home: MoveCaret(ctrl ? 0 : LineStart(m_caret), shift);              break;
        case Key::End:  MoveCaret(ctrl ? m_text.size() : LineEnd(m_caret), shift);    break;

        case Key::Backspace:
            if (HasSelection())   DeleteRange(SelectionStart(), SelectionEnd(), EditKind::Deleting);
            else if (m_caret > 0) DeleteRange(ctrl ? PrevWord(m_caret) : PrevChar(m_caret), m_caret, EditKind::Deleting);
            break;
        case Key::Delete:
            if (HasSelection())               DeleteRange(SelectionStart(), SelectionEnd(), EditKind::Deleting);
            else if (m_caret < m_text.size()) DeleteRange(m_caret, ctrl ? NextWord(m_caret) : NextChar(m_caret), EditKind::Deleting);
            break;

        case Key::Enter:
            if (m_multiline)
            {
                m_lastEdit = EditKind::None;
                ReplaceSelection("\n", EditKind::Other);
            }
            else if (m_onSubmit)
            {
                auto cb = m_onSubmit;   // 回调里可能修改或销毁输入框
                cb(m_text);
            }
            else
            {
                handled = false;        // 没有提交回调：交给上层（例如对话框的默认按钮）
            }
            break;

        case Key::A: if (ctrl) SelectAll(); else handled = true; break;
        case Key::C: if (ctrl) Copy(); break;
        case Key::X: if (ctrl) { if (m_readOnly) Copy(); else Cut(); } break;
        case Key::V: if (ctrl) Paste(); break;
        case Key::Z: if (ctrl) { if (shift) Redo(); else Undo(); } break;
        case Key::Y: if (ctrl) Redo(); break;

        default:
            // 可打印字符的按下由随后的字符输入处理；这里标记已处理，避免冒泡到上层被当成命令键
            handled = !ctrl && IsPrintableKey(e.key);
            break;
        }

        e.handled = handled;
    }

    void TextBox::OnTextInput(TextInputEvent& e)
    {
        if (m_readOnly || e.phase != EventPhase::Target)
            return;

        std::string text;
        for (char c : e.text)
        {
            const auto b = static_cast<uint8_t>(c);
            if (b < 0x20 || b == 0x7F)
            {
                if (c == '\n' && m_multiline)
                    text.push_back(c);
                continue;
            }
            text.push_back(c);
        }

        // 输入法确认的文字到达：它替换掉组合串
        m_composition.clear();
        m_compositionCaret = 0;
        e.handled = true;
        if (text.empty())
            return;

        // 空格开始新的撤销分组：撤销按词回退，而不是一次撤掉整段
        if (text == " ")
            m_lastEdit = EditKind::None;
        ReplaceSelection(text, EditKind::Typing);
    }

    void TextBox::OnComposition(const CompositionEvent& e)
    {
        if (m_readOnly)
            return;

        switch (e.type)
        {
        case CompositionEvent::Type::Start:
        case CompositionEvent::Type::Update:
            if (!m_composing)
            {
                // 开始组合：先删掉选中的文字，组合串插在光标处
                if (HasSelection())
                    DeleteRange(SelectionStart(), SelectionEnd(), EditKind::Other);
                m_composing = true;
                m_lastEdit  = EditKind::None;
            }
            m_composition      = e.text;
            m_compositionCaret = e.caret;
            m_followCaret      = true;
            break;

        case CompositionEvent::Type::End:
            m_composing = false;
            m_composition.clear();
            m_compositionCaret = 0;
            break;
        }
        m_layoutValid = false;
        Invalidate();
    }

    void TextBox::OnFocusChanged(bool focused)
    {
        if (!focused)
        {
            m_composing = false;
            m_composition.clear();
            m_dragging = false;
            m_layoutValid = false;
            StopBlink();
            if (m_autoComplete)
                m_autoComplete->OnFocusLost();
        }
        else
        {
            // 用 Tab 进入单行输入框时全选（与系统输入框一致）
            UIContext* ctx = GetContext();
            if (ctx && ctx->IsFocusVisible() && !m_multiline)
                SelectAll();
            RestartBlink();
        }
        Invalidate();
        if (m_onFocusChanged)
            m_onFocusChanged(focused);
    }
}
