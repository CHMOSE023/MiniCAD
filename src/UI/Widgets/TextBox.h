#pragma once
#include "Core/Node.h"
#include "Style/Theme.hpp"
#include "Text/TextSystem.h"
#include "Widgets/ListView.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    class AutoComplete;

    struct TextBoxStyle
    {
        ColorRef background        = Theme::Background;
        ColorRef border            = Theme::Border;
        ColorRef borderHover       = Theme::BorderHover;
        ColorRef borderFocus       = Theme::Accent;
        ColorRef text              = Theme::Text;
        ColorRef placeholder       = Theme::Placeholder;
        ColorRef selection         = ColorRef(ThemeColor::Accent, 110);
        ColorRef selectionInactive = ColorRef(ThemeColor::TextDim, 70);
        ColorRef caret             = Theme::Text;
        float   fontSize          = 14.0f;
        float   rounding          = 4.0f;
        Edges   padding           = Edges::Symmetric(8.0f, 5.0f);
    };

    // 文本输入框：单行 / 多行（自动换行），UTF-8。
    // - 光标、选区、拖拽选择、双击选词、三击选行
    // - 键盘：方向键（Ctrl 按词）、Home/End、Backspace/Delete（Ctrl 按词）、Ctrl+A/C/X/V/Z/Y
    // - 撤销/重做：连续输入合并为一步
    // - 输入法：组合串内嵌显示在光标处并加下划线，候选窗由平台层按 GetTextCaretRect 定位
    // - 单行时 Enter 触发 OnSubmit；Tab 和 Esc 不处理（留给焦点切换和上层）
    class TextBox : public Node
    {
    public:
        explicit TextBox(std::string text = {});
        ~TextBox() override;

        // 开启输入建议（命令行补全）：provider 按当前文字返回候选项
        AutoComplete* EnableAutoComplete(std::function<std::vector<ListItem>(const std::string&)> provider);
        AutoComplete* GetAutoComplete() const { return m_autoComplete.get(); }

        void SetText(std::string text);                     // 清空撤销历史，光标移到末尾
        const std::string& GetText() const { return m_text; }

        void SetPlaceholder(std::string placeholder);
        const std::string& GetPlaceholder() const { return m_placeholder; }
        void SetMultiline(bool multiline);
        bool IsMultiline() const { return m_multiline; }
        void SetRows(int rows);                             // 多行时的默认可见行数
        void SetReadOnly(bool readOnly);
        void SetStyle(const TextBoxStyle& style);

        void SetOnChanged(std::function<void(const std::string&)> cb) { m_onChanged = std::move(cb); }
        void SetOnSubmit (std::function<void(const std::string&)> cb) { m_onSubmit  = std::move(cb); }
        void SetOnFocusChanged(std::function<void(bool)> cb)          { m_onFocusChanged = std::move(cb); }

        // 按键预处理：在输入框自己处理之前调用，返回 true 表示已处理（输入建议列表用它接管上下键和回车）
        void SetKeyPreview(std::function<bool(KeyEvent&)> cb) { m_keyPreview = std::move(cb); }

        // 替换全部文字并可撤销（与 SetText 不同，SetText 会清空撤销历史）
        void ReplaceAll(std::string_view text);

        // ── 选区（字节偏移，落在字符边界上）─────────────────────
        size_t GetCaret()  const { return m_caret; }
        size_t GetAnchor() const { return m_anchor; }
        size_t SelectionStart() const { return std::min(m_caret, m_anchor); }
        size_t SelectionEnd()   const { return std::max(m_caret, m_anchor); }
        bool   HasSelection()   const { return m_caret != m_anchor; }
        std::string GetSelectedText() const;
        void   SetSelection(size_t anchor, size_t caret);
        void   SelectAll();

        bool CanUndo() const { return !m_undo.empty(); }
        bool CanRedo() const { return !m_redo.empty(); }
        void Undo();
        void Redo();

        // 编辑命令（右键菜单和快捷键共用）
        void Cut();
        void Copy();
        void Paste();
        void DeleteSelection();

        const std::string& GetCompositionText() const { return m_composition; }

        bool AcceptsTextInput() const override { return !m_readOnly; }
        bool GetTextCaretRect(Rect& out) const override;
        CursorShape GetCursor(Vec2) const override { return CursorShape::IBeam; }

    protected:
        Vec2 MeasureContent(Vec2 available) override;
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void OnPointerEvent(PointerEvent& e) override;
        void OnKeyEvent(KeyEvent& e) override;
        void OnTextInput(TextInputEvent& e) override;
        void OnComposition(const CompositionEvent& e) override;
        void OnFocusChanged(bool focused) override;

    private:
        enum class EditKind { None, Typing, Deleting, Other };

        struct UndoState
        {
            std::string text;
            size_t      caret;
            size_t      anchor;
        };

        // ── 编辑 ────────────────────────────────────────────────
        void   ReplaceSelection(std::string_view text, EditKind kind);
        void   DeleteRange(size_t begin, size_t end, EditKind kind);
        void   PushUndo(EditKind kind);
        void   MoveCaret(size_t pos, bool extend);
        void   Changed();
        void   ShowEditMenu(Vec2 windowPos);
        void   RestartBlink();                               // 编辑或移动光标后光标保持可见，重新计时
        void   StopBlink();

        // ── 字符与词边界 ────────────────────────────────────────
        size_t PrevChar(size_t pos) const;
        size_t NextChar(size_t pos) const;
        size_t PrevWord(size_t pos) const;
        size_t NextWord(size_t pos) const;
        void   WordAt(size_t pos, size_t& begin, size_t& end) const;

        // ── 排版与几何（局部坐标）───────────────────────────────
        std::string        DisplayText() const;            // 含组合串
        size_t             DisplayCaret() const;
        const TextLayout&  EnsureLayout() const;
        TextParams         Params() const;
        Rect               ContentRect() const;
        Vec2               TextOrigin() const;              // 文字左上角 = 内容区左上角 - 滚动
        Rect               CaretRectLocal() const;
        void               ScrollToCaret() const;
        float              MaxScrollY() const;
        size_t             HitTest(Vec2 local) const;       // 局部坐标 → 原文偏移
        size_t             LineStart(size_t pos) const;
        size_t             LineEnd(size_t pos) const;
        size_t             VerticalMove(size_t pos, int lines) const;

        void DrawRanges(DrawList& dl, Vec2 origin, size_t begin, size_t end, ColorRef color, bool underline) const;

    private:
        std::string  m_text;
        std::string  m_placeholder;
        TextBoxStyle m_style;
        bool         m_multiline = false;
        bool         m_readOnly  = false;
        int          m_rows      = 4;

        size_t m_caret  = 0;
        size_t m_anchor = 0;
        float  m_preferredX = -1.0f;            // 上下移动时保持的横坐标

        std::string m_composition;
        int         m_compositionCaret = 0;     // 组合串内光标（字符数）
        bool        m_composing = false;

        std::vector<UndoState> m_undo;
        std::vector<UndoState> m_redo;
        EditKind               m_lastEdit = EditKind::None;

        bool   m_dragging = false;

        // 光标闪烁（只在持有焦点时运行定时器）
        uint32_t m_blinkTimer   = 0;
        bool     m_caretVisible = true;

        std::function<void(const std::string&)> m_onChanged;
        std::function<void(const std::string&)> m_onSubmit;
        std::function<void(bool)>               m_onFocusChanged;
        std::function<bool(KeyEvent&)>          m_keyPreview;
        std::unique_ptr<AutoComplete>           m_autoComplete;

        // 排版缓存与滚动（在绘制、光标查询时按需更新，所以是 mutable）
        mutable TextLayout  m_layout;
        mutable std::string m_layoutText;
        mutable float       m_layoutWidth = -1.0f;
        mutable float       m_layoutScale = 0.0f;
        mutable bool        m_layoutValid = false;
        mutable Vec2        m_scroll;
        mutable bool        m_followCaret = true;   // 下次绘制时让滚动跟随光标
    };
}
