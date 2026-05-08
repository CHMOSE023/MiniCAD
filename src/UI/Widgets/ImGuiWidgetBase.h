#pragma once
#include <string> 
namespace MiniCAD
{
    class Document;
    class ImGuiWidgetBase
    {
    public:
        virtual ~ImGuiWidgetBase() = default;

        virtual void        OnRender(Document& document) = 0;
        virtual const char* GetName() const = 0;

        const char* GetID() const { return m_id; } 

        bool IsVisible() const   { return m_visible; }
        void SetVisible(bool v)  { m_visible = v; }
        void Toggle()            { m_visible = !m_visible; }

    protected: 
        const char* m_id      = "widget_base";
        bool        m_visible = true; // 默认显示
    };
}