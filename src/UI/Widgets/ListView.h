#pragma once
#include "Widgets/InlineEditor.h"
#include "Widgets/ScrollView.h"
#include "Style/Theme.hpp"
#include "Text/TextSystem.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    struct MenuItem;

    // ── 列表 ─────────────────────────────────────────────────────

    struct ListItem
    {
        std::string text;                           // 第一列 / 单列时的文字
        std::string detail;                         // 单列时右对齐的次要文字（例如快捷键、数量）
        Color32     swatch = Colors::Transparent;   // 单列时行首的小色块（例如图层颜色），透明表示没有
        std::vector<std::string> cells;             // 多列时第 1 列起的文字（第 0 列用 text）
        uint64_t    userData = 0;
    };

    // 表格列
    struct ListColumn
    {
        std::string title;
        float       width    = 120.0f;
        TextAlign   align    = TextAlign::Start;
        bool        editable = false;               // 允许在位编辑这一列
    };

    enum class SelectionMode
    {
        Single,
        Multiple,   // Ctrl 点选、Shift 连选、Ctrl+A 全选
    };

    // 列表视图：固定行高，只绘制可见的行（十万行也不卡）。
    // - 单列：文字 + 可选色块 + 右侧次要文字，或完全自定义绘制（SetItemPainter，例如线型预览）
    // - 多列（SetColumns）：表头可拖动调整列宽、点击标题排序（由调用方排序数据）、单元格可自定义绘制和点击
    // - 键盘：上下、Home/End、PageUp/PageDown（Shift 连选），Enter 激活，F2 在位编辑，
    //   多选时 Ctrl+空格切换、Ctrl+A 全选；直接输入字母按开头查找
    // - 右键：先选中所在行，再弹出 SetRowContextMenu 提供的菜单
    // - 在位编辑：F2、慢速点击已选中的行、或调用 BeginEdit；回调返回 false 可拒绝（例如重名）
    class ListView : public ScrollView
    {
    public:
        using ItemPainter = std::function<void(DrawList& dl, const Rect& row, int index, bool selected)>;
        using CellPainter = std::function<bool(DrawList& dl, const Rect& cell, int row, int column, bool selected)>;

        ListView();
        ~ListView() override;

        void SetItems(std::vector<ListItem> items);
        void SetItems(const std::vector<std::string>& texts);
        const std::vector<ListItem>& GetItems() const { return m_items; }
        ListItem& GetItem(int index) { return m_items[static_cast<size_t>(index)]; }
        int  GetItemCount() const { return static_cast<int>(m_items.size()); }
        void RefreshItem(int index) { (void)index; Invalidate(); }   // 修改 GetItem 返回的数据后调用

        void SetSelectionMode(SelectionMode mode) { m_mode = mode; }
        void SetRowHeight(float h);
        float GetRowHeight() const { return m_rowHeight; }

        // ── 多列 ────────────────────────────────────────────────
        void SetColumns(std::vector<ListColumn> columns);
        const std::vector<ListColumn>& GetColumns() const { return m_columns; }
        std::string GetCellText(int row, int column) const;
        void SetCellText(int row, int column, std::string text);
        void SetSortIndicator(int column, bool ascending) { m_sortColumn = column; m_sortAscending = ascending; Invalidate(); }
        Rect GetCellRect(int row, int column) const;             // 内容坐标

        static constexpr float kHeaderHeight = 28.0f;

        // ── 自定义绘制 ──────────────────────────────────────────
        void SetItemPainter(ItemPainter painter) { m_itemPainter = std::move(painter); Invalidate(); }
        void SetCellPainter(CellPainter painter) { m_cellPainter = std::move(painter); Invalidate(); }

        // ── 选择 ────────────────────────────────────────────────
        int  GetCurrent() const { return m_current; }               // 焦点行（单选时即选中行）
        void SetCurrent(int index, bool select = true);
        bool IsSelected(int index) const;
        std::vector<int> GetSelection() const;
        void SelectAll();
        void ClearSelection();
        void EnsureVisible(int index);

        // ── 在位编辑 ────────────────────────────────────────────
        void SetEditable(bool editable) { m_editable = editable; }  // 单列模式；多列时看 ListColumn::editable
        bool BeginEdit(int row, int column = -1);                   // column = -1：第一个可编辑的列
        bool IsEditing() const { return m_editor.IsActive(); }
        void CancelEdit() { m_editor.Cancel(); }

        // ── 回调 ────────────────────────────────────────────────
        void SetOnSelectionChanged(std::function<void()> cb) { m_onSelectionChanged = std::move(cb); }
        void SetOnItemActivated(std::function<void(int)> cb) { m_onItemActivated = std::move(cb); }
        void SetOnItemClicked(std::function<void(int)> cb)   { m_onItemClicked = std::move(cb); }   // 单击（下拉列表用来直接确认）
        void SetOnCellClicked(std::function<void(int row, int column)> cb) { m_onCellClicked = std::move(cb); }
        void SetOnHeaderClicked(std::function<void(int column)> cb) { m_onHeaderClicked = std::move(cb); }
        void SetOnItemEdited(std::function<bool(int row, int column, const std::string& text)> cb) { m_onItemEdited = std::move(cb); }
        void SetRowContextMenu(std::function<std::vector<MenuItem>(int row)> build);   // row 为 -1 表示空白处

        CursorShape GetCursor(Vec2 local) const override;

    protected:
        void OnKeyEvent(KeyEvent& e) override;
        void OnTextInput(TextInputEvent& e) override;
        void OnFocusChanged(bool focused) override;
        void OnPaintOverlay(DrawList& dl, const Rect& screenRect) override;
        void OnPointerEvent(PointerEvent& e) override;
        bool InterceptsHit(Vec2 local) const override;

    private:
        friend class ListContent;

        void ClickRow(int row, int column, uint8_t modifiers, int clickCount);
        void MoveCurrent(int index, bool extend, bool keepSelection);
        void SelectRange(int from, int to, bool additive);
        void NotifySelection();
        void Activate(int index);
        int  ColumnAt(float contentX) const;
        int  BoundaryAt(float localX) const;                        // 表头里靠近哪条列分隔线
        int  FirstEditableColumn() const;
        void CancelSlowEdit();

        std::vector<ListItem>   m_items;
        std::vector<char>       m_selected;
        std::vector<ListColumn> m_columns;
        SelectionMode           m_mode      = SelectionMode::Single;
        float                   m_rowHeight = Theme::RowH;
        int                     m_current   = -1;
        int                     m_anchor    = -1;
        int                     m_hoverRow  = -1;
        int                     m_contextRow = -2;                  // 右键所在行；-2 表示用键盘打开（取当前行）

        ItemPainter             m_itemPainter;
        CellPainter             m_cellPainter;

        // 表头
        int   m_sortColumn     = -1;
        bool  m_sortAscending  = true;
        int   m_hoverBoundary  = -1;
        int   m_resizeColumn   = -1;
        float m_resizeStartX   = 0.0f;
        float m_resizeStartW   = 0.0f;
        int   m_pressedHeader  = -1;

        // 在位编辑
        bool         m_editable = false;
        InlineEditor m_editor;
        uint32_t     m_slowEditTimer = 0;

        // 按字母查找
        std::string m_typeBuffer;
        uint32_t    m_typeTime = 0;

        std::function<void()>    m_onSelectionChanged;
        std::function<void(int)> m_onItemActivated;
        std::function<void(int)> m_onItemClicked;
        std::function<void(int, int)> m_onCellClicked;
        std::function<void(int)> m_onHeaderClicked;
        std::function<bool(int, int, const std::string&)> m_onItemEdited;
    };

    // ── 树 ───────────────────────────────────────────────────────

    class TreeView;

    // 树节点：由 TreeView 持有，AddChild 返回的指针在节点被移除前一直有效
    class TreeItem
    {
    public:
        TreeItem* AddChild(std::string text);
        void      RemoveChild(TreeItem* child);
        void      ClearChildren();

        const std::string& GetText() const { return m_text; }
        void      SetText(std::string text);
        TreeItem* GetParent() const { return m_parent; }
        const std::vector<std::unique_ptr<TreeItem>>& GetChildren() const { return m_children; }
        bool      HasChildren() const { return !m_children.empty(); }

        bool IsExpanded() const { return m_expanded; }
        void SetExpanded(bool expanded);

        void     SetSwatch(Color32 c) { m_swatch = c; }
        Color32  GetSwatch() const { return m_swatch; }
        void     SetUserData(uint64_t data) { m_userData = data; }
        uint64_t GetUserData() const { return m_userData; }
        void     SetEditable(bool editable) { m_editable = editable; }   // 树开启编辑后，可单独禁止某个节点（例如分类节点）
        bool     IsEditable() const { return m_editable; }

    private:
        friend class TreeView;
        void MarkDirty();

        TreeView*   m_tree   = nullptr;
        TreeItem*   m_parent = nullptr;
        std::string m_text;
        bool        m_expanded = false;
        bool        m_editable = true;
        Color32     m_swatch   = Colors::Transparent;
        uint64_t    m_userData = 0;
        std::vector<std::unique_ptr<TreeItem>> m_children;
    };

    // 树视图：只绘制可见行。
    // 键盘：上下移动；→ 展开或进入第一个子节点；← 折叠或回到父节点；Enter 激活；F2 在位编辑；
    // 鼠标：点击箭头展开/折叠，双击切换展开并激活，右键先选中再弹出菜单，慢速点击已选中节点进入编辑
    class TreeView : public ScrollView
    {
    public:
        TreeView();
        ~TreeView() override;

        TreeItem* GetRootItem() const { return m_root.get(); }      // 不显示的根
        TreeItem* AddItem(TreeItem* parent, std::string text);      // parent 为 nullptr 时加到根下
        void      RemoveItem(TreeItem* item);
        void      Clear();
        void      ExpandAll(bool expanded = true);

        TreeItem* GetSelected() const { return m_selected; }
        void      SetSelected(TreeItem* item);
        void      EnsureVisible(TreeItem* item);

        void SetEditable(bool editable) { m_editable = editable; }
        bool BeginEdit(TreeItem* item);
        bool IsEditing() const { return m_editor.IsActive(); }

        void SetOnSelectionChanged(std::function<void(TreeItem*)> cb) { m_onSelectionChanged = std::move(cb); }
        void SetOnItemActivated(std::function<void(TreeItem*)> cb)    { m_onItemActivated = std::move(cb); }
        void SetOnItemRenamed(std::function<bool(TreeItem*, const std::string&)> cb) { m_onItemRenamed = std::move(cb); }
        void SetItemContextMenu(std::function<std::vector<MenuItem>(TreeItem*)> build);   // nullptr 表示空白处

        int  GetVisibleRowCount();

    protected:
        void OnKeyEvent(KeyEvent& e) override;
        void OnFocusChanged(bool focused) override;

    private:
        friend class TreeItem;
        friend class TreeContent;

        struct Row
        {
            TreeItem* item;
            int       depth;
        };

        void RowsChanged();
        void RebuildRows();
        int  RowOf(const TreeItem* item);
        void ClickRow(int row, float x, int clickCount);
        void Activate(TreeItem* item);
        void CancelSlowEdit();
        Rect TextRect(int row);                                      // 内容坐标

        std::unique_ptr<TreeItem> m_root;
        std::vector<Row>          m_rows;
        bool                      m_rowsDirty = true;
        TreeItem*                 m_selected  = nullptr;
        TreeItem*                 m_contextItem = nullptr;
        bool                      m_contextFromPointer = false;
        int                       m_hoverRow  = -1;
        float                     m_rowHeight = Theme::RowH;
        float                     m_indent    = 18.0f;

        bool         m_editable = false;
        InlineEditor m_editor;
        uint32_t     m_slowEditTimer = 0;

        std::function<void(TreeItem*)> m_onSelectionChanged;
        std::function<void(TreeItem*)> m_onItemActivated;
        std::function<bool(TreeItem*, const std::string&)> m_onItemRenamed;
    };
}
