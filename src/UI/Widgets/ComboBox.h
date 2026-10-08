#pragma once
#include "Core/Node.h"
#include <functional>
#include <string>
#include <vector>

namespace MiniGUI
{
    class DrawList;
    class ListView;
    class Popup;

    // 下拉选择框：点击展开列表，单击确认；
    // 收起时 ↑↓ 直接切换选项，Alt+↓ / F4 / 空格 / Enter 展开；展开后 ↑↓ 移动、Enter 确认、Esc 收起
    class ComboBox : public Node
    {
    public:
        explicit ComboBox(std::vector<std::string> items = {}, int selected = -1);
        ~ComboBox() override;

        void SetItems(std::vector<std::string> items);
        const std::vector<std::string>& GetItems() const { return m_items; }

        void SetSelectedIndex(int index);                 // 不触发 OnChanged
        int  GetSelectedIndex() const { return m_selected; }
        std::string GetSelectedText() const;

        void SetPlaceholder(std::string text) { m_placeholder = std::move(text); Invalidate(); }
        const std::string& GetPlaceholder() const { return m_placeholder; }

        // 自定义选项绘制（线型预览、图层颜色等）：收起时的显示区和下拉列表的每一行都用它画。
        // box 为文字区域（已去掉内边距）；inList 表示画在下拉列表里
        using ItemPainter = std::function<void(DrawList& dl, const Rect& box, int index, bool inList)>;
        void SetItemPainter(ItemPainter painter) { m_itemPainter = std::move(painter); Invalidate(); }
        void SetDropDownWidth(float w) { m_dropDownWidth = w; }       // 0 = 与下拉框同宽
        void SetMaxVisibleItems(int n) { m_maxVisible = n; }
        void SetOnChanged(std::function<void(int)> cb) { m_onChanged = std::move(cb); }

        bool IsOpen() const { return m_popup != nullptr; }
        void Open();
        void Close();

    protected:
        Vec2 MeasureContent(Vec2 available) override;
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void OnPointerEvent(PointerEvent& e) override;
        void OnKeyEvent(KeyEvent& e) override;

    private:
        void Choose(int index);   // 用户选择：更新并触发 OnChanged

        std::vector<std::string> m_items;
        int         m_selected   = -1;
        int         m_maxVisible = 10;
        std::string m_placeholder;
        Popup*      m_popup = nullptr;
        ItemPainter m_itemPainter;
        float       m_dropDownWidth = 0.0f;
        std::function<void(int)> m_onChanged;
    };
}
