#pragma once
#include <cmath>
#include <limits>
#include <optional>

namespace MiniGUI
{
    // 未指定的尺寸（由内容或父容器决定）
    inline constexpr float kAuto = std::numeric_limits<float>::quiet_NaN();
    inline constexpr float kInfinity = std::numeric_limits<float>::infinity();

    inline bool IsAuto(float v) { return std::isnan(v); }

    // 主轴方向
    enum class FlexDirection
    {
        Row,        // 水平排列
        Column,     // 垂直排列
    };

    // 交叉轴对齐
    enum class Align
    {
        Start,
        Center,
        End,
        Stretch,    // 拉伸到容器交叉轴尺寸（仅当自身交叉轴尺寸为 auto 时生效）
    };

    // 主轴分布
    enum class Justify
    {
        Start,
        Center,
        End,
        SpaceBetween,   // 首尾贴边，中间等距
        SpaceAround,    // 每项两侧等距
        SpaceEvenly,    // 所有间隔（含首尾）相等
    };

    enum class PositionType
    {
        Relative,   // 参与父容器的弹性排列
        Absolute,   // 不参与排列，按 left/top/right/bottom 相对父节点定位（弹层、角标）
    };

    struct Edges
    {
        float left   = 0.0f;
        float top    = 0.0f;
        float right  = 0.0f;
        float bottom = 0.0f;

        static constexpr Edges All(float v)                      { return { v, v, v, v }; }
        static constexpr Edges Symmetric(float h, float v)       { return { h, v, h, v }; }
        static constexpr Edges Make(float l, float t, float r, float b) { return { l, t, r, b }; }

        constexpr float Horizontal() const { return left + right; }
        constexpr float Vertical()   const { return top + bottom; }
    };

    // Flexbox 子集。与 CSS 的主要差异：
    //   - 不换行（相当于 flex-wrap: nowrap），没有 flex-basis（用 width/height 或内容尺寸代替）
    //   - min 尺寸默认 0（CSS 默认是内容尺寸），收缩时可能比内容更小
    //   - 隐藏的节点不参与布局（相当于 display: none）
    struct LayoutStyle
    {
        // ── 作为容器 ────────────────────────────────────────────
        FlexDirection direction  = FlexDirection::Column;
        Justify       justify    = Justify::Start;
        Align         alignItems = Align::Stretch;
        Edges         padding;
        float         gap        = 0.0f;         // 相邻子节点之间的主轴间距

        // ── 作为子项 ────────────────────────────────────────────
        PositionType  position   = PositionType::Relative;
        float         width      = kAuto;
        float         height     = kAuto;
        float         minWidth   = 0.0f;
        float         minHeight  = 0.0f;
        float         maxWidth   = kInfinity;
        float         maxHeight  = kInfinity;
        float         grow       = 0.0f;         // 分配剩余空间的权重
        float         shrink     = 1.0f;         // 空间不足时的收缩权重（按 权重 × 基础尺寸 分摊）
        std::optional<Align> alignSelf;          // 覆盖父容器的 alignItems
        Edges         margin;

        // 绝对定位时相对父节点边缘的距离；同时指定 left 和 right 时宽度由两者决定
        float         left   = kAuto;
        float         top    = kAuto;
        float         right  = kAuto;
        float         bottom = kAuto;
    };
}
