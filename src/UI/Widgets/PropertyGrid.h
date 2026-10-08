#pragma once
#include "Widgets/ScrollView.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    class Label;

    // 可折叠区：标题行（▸/▾ + 标题）+ 内容区。点击标题、或标题获得焦点时按空格 / ← → 切换
    class Expander : public Node
    {
    public:
        explicit Expander(std::string title, bool expanded = true);

        Node* GetContent() const { return m_content; }
        void  SetExpanded(bool expanded);
        bool  IsExpanded() const { return m_expanded; }
        void  SetTitle(std::string title) { m_title = std::move(title); Invalidate(); }
        void  SetOnToggled(std::function<void(bool)> cb) { m_onToggled = std::move(cb); }

        static constexpr float kHeaderHeight = 28.0f;

    protected:
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void OnPointerEvent(PointerEvent& e) override;
        void OnKeyEvent(KeyEvent& e) override;

    private:
        std::string m_title;
        Node*       m_content  = nullptr;
        bool        m_expanded = true;
        bool        m_hover    = false;
        std::function<void(bool)> m_onToggled;
    };

    // 特性面板：按组列出"名称 | 编辑控件"，名称列宽度可拖动分隔线调整，组可折叠。
    // 编辑控件可以是任意节点（TextBox、NumberBox、ComboBox、ColorButton、CheckBox…），
    // 多个实体取值不同时，惯例是清空输入框并把占位文字设为"*多种*"
    class PropertyGrid : public ScrollView
    {
    public:
        PropertyGrid();

        Expander* AddGroup(std::string title, bool expanded = true);
        Node*     AddProperty(Expander* group, std::string name, std::unique_ptr<Node> editor);

        template<typename T, typename... Args>
        T* AddProperty(Expander* group, std::string name, Args&&... args)
        {
            auto e = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = e.get();
            AddProperty(group, std::move(name), std::move(e));
            return raw;
        }

        void  SetNameWidth(float width);
        float GetNameWidth() const { return m_nameWidth; }
        bool  IsBoundaryHot() const { return m_dragging || m_hoverBoundary; }     // 分界线悬停或拖动中（高亮）

        CursorShape GetCursor(Vec2 local) const override;

        static constexpr float kRowHeight  = 28.0f;
        static constexpr float kIndent     = 12.0f;
        static constexpr float kNameGap    = 8.0f;      // 名称列与编辑控件之间的间隔，分界线画在正中

    protected:
        void OnPointerEvent(PointerEvent& e) override;
        bool InterceptsHit(Vec2 local) const override;

    private:
        bool NearBoundary(Vec2 local) const;

        Node*               m_body = nullptr;
        std::vector<Label*> m_names;
        float               m_nameWidth = 110.0f;
        bool                m_dragging  = false;
        bool                m_hoverBoundary = false;
    };
}
