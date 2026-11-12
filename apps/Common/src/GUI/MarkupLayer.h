#pragma once
#include "Core/Types/Rect.hpp"
#include "Core/Types/Color.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace MiniGUI
{
    class DrawList;
    class TextSystem;
    struct ViewportPointerEvent;
}

namespace MiniCAD
{
    class Camera;

    // 批注图形的类型（与嵌入网页的约定一致：pin / rect / cloud / arrow / text / ink）
    enum class MarkupType : uint8_t { Pin, Rect, Cloud, Arrow, Text, Ink };

    // 一个批注图形：世界坐标（x, y）。pin 1 点；rect / cloud 两角点；arrow 起点、终点；text 1 点 + 文字；ink 折线
    struct Markup
    {
        std::string         id;         // 宿主的标识（意见编号），点击时回传
        MarkupType          type = MarkupType::Pin;
        std::vector<double> points;     // x0, y0, x1, y1, ...
        std::string         text;       // text 的文字
        std::string         label;      // 图上显示的编号
        MiniGUI::Color32    color = MiniGUI::RGBA(220, 38, 38);
    };

    // 批注层：嵌入它的网页（协作批注）交来的批注图形，叠在 CAD 视口上显示，以及画新批注的交互。
    //   - 不进场景、不进撤销栈、不随图纸保存；按世界坐标存，每帧换算到屏幕，随缩放平移
    //   - 图钉、文字、编号、箭头头部按屏幕像素大小绘制
    //   - 画批注时吃掉左键（图钉 / 文字点一下，矩形 / 云线 / 箭头按住拖动，手绘按住画），中键、滚轮照常平移缩放
    //   - 不画批注时点在批注上回调 onClick，其余事件交给视口
    class MarkupLayer
    {
    public:
        static bool        ParseType(std::string_view name, MarkupType& out);
        static const char* TypeName(MarkupType type);

        std::function<void()>                             onChanged;      // 需要重绘
        std::function<void(const std::optional<Markup>&)> onPlaced;       // 画完一笔（世界坐标）；取消为空
        std::function<void(const std::string& id)>        onClick;        // 点了图上的批注

        void SetMarkups(std::vector<Markup> markups);
        void Highlight(const std::string& id, double nowSeconds);         // 空串取消高亮；高亮时闪烁一会儿
        const std::string& GetHighlight() const { return m_highlight; }

        void Start(MarkupType type);    // 进入画批注状态（正在画的先取消）
        void Cancel();                  // 取消正在画的批注，回调 onPlaced(空)
        bool IsDrawing() const { return m_drawing; }

        // 视口里的指针事件（e.pixel 为视口内物理像素，与相机一致）；返回 true 表示批注层处理了，不再交给视口
        bool OnPointer(const MiniGUI::ViewportPointerEvent& e, const Camera& camera);

        // 叠在视口画面上画批注。screen 为视口在窗口里的矩形（逻辑像素），pixelScale = 物理像素 / 逻辑像素
        void Paint(MiniGUI::DrawList& dl, MiniGUI::TextSystem& text, const MiniGUI::Rect& screen,
                   const Camera& camera, float pixelScale, double nowSeconds);
        bool NeedsAnimation(double nowSeconds) const;   // 高亮闪烁中：需要继续重绘

        bool Empty() const { return m_markups.empty() && !m_drawing; }

    private:
        void Finish(Markup m);
        int  HitTest(MiniGUI::Vec2 local) const;        // 上次绘制时记下的范围（视口内逻辑像素）

        std::vector<Markup>        m_markups;
        std::vector<MiniGUI::Rect> m_hitRects;          // 与 m_markups 一一对应，视口内逻辑像素
        std::string                m_highlight;
        double                     m_highlightAt = -1.0;

        bool                       m_drawing  = false;
        MarkupType                 m_tool     = MarkupType::Pin;
        bool                       m_pressed  = false;  // 画批注时左键按下
        std::vector<double>        m_points;            // 正在画的点（世界坐标）
        MiniGUI::Vec2              m_pressPx;           // 按下位置（物理像素）
        MiniGUI::Vec2              m_lastPx;
        float                      m_pixelScale = 1.0f;

        int                        m_clickIndex = -1;   // 不画批注时按在了哪个批注上
    };
}
