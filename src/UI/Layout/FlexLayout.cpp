#include "Layout/FlexLayout.h"
#include "Core/Node.h"
#include <algorithm>
#include <cmath>

namespace MiniGUI
{
    namespace
    {
        float MainOf (Vec2 v, bool row) { return row ? v.x : v.y; }
        float CrossOf(Vec2 v, bool row) { return row ? v.y : v.x; }
        Vec2  MakeVec(float main, float cross, bool row) { return row ? Vec2{ main, cross } : Vec2{ cross, main }; }

        // 先限制最大值再限制最小值：两者冲突时最小值优先（与 CSS 一致）
        float Clamp(float v, float mn, float mx) { return std::max(mn, std::min(v, mx)); }

        float Snap(float v, float scale) { return std::round(v * scale) / scale; }

        Rect SnapRect(const Rect& r, float scale)
        {
            if (scale <= 0.0f)
                return r;
            return { Snap(r.min.x, scale), Snap(r.min.y, scale), Snap(r.max.x, scale), Snap(r.max.y, scale) };
        }

        bool ParticipatesInFlow(const Node& n)
        {
            return n.IsVisible() && n.GetLayoutStyle().position == PositionType::Relative;
        }

        // 可用空间减去外边距；无穷大保持无穷大
        float Shrunk(float available, float amount)
        {
            return std::isinf(available) ? available : std::max(available - amount, 0.0f);
        }
    }

    FlexLayout& FlexLayout::Default()
    {
        static FlexLayout instance;
        return instance;
    }

    // =========================================================
    // Measure：容器内容尺寸 = 子项主轴尺寸之和 + 间距，交叉轴取最大值，再加内边距
    // =========================================================
    Vec2 FlexLayout::MeasureChildren(Node& container, Vec2 available)
    {
        const LayoutStyle& cs  = container.GetLayoutStyle();
        const bool         row = cs.direction == FlexDirection::Row;

        const Vec2 inner{ Shrunk(available.x, cs.padding.Horizontal()), Shrunk(available.y, cs.padding.Vertical()) };

        float main  = 0.0f;
        float cross = 0.0f;
        int   count = 0;

        for (const auto& child : container.GetChildren())
        {
            if (!ParticipatesInFlow(*child))
                continue;

            const Edges& m = child->GetLayoutStyle().margin;
            const Vec2 childAvail{ Shrunk(inner.x, m.Horizontal()), Shrunk(inner.y, m.Vertical()) };
            const Vec2 size = child->Measure(childAvail);

            main  += MainOf(size, row) + (row ? m.Horizontal() : m.Vertical());
            cross  = std::max(cross, CrossOf(size, row) + (row ? m.Vertical() : m.Horizontal()));
            ++count;
        }
        if (count > 1)
            main += cs.gap * static_cast<float>(count - 1);

        const Vec2 content = MakeVec(main, cross, row);
        return { content.x + cs.padding.Horizontal(), content.y + cs.padding.Vertical() };
    }

    // =========================================================
    // 解析弹性长度（CSS Flexbox §9.7 的简化版）：
    // 按 grow 或 shrink 分配剩余空间，违反 min/max 的项冻结后重新分配，直到没有违反
    // =========================================================
    void FlexLayout::ResolveFlexibleLengths(std::vector<Item>& items, float innerMain, float gap)
    {
        const size_t n = items.size();
        if (n == 0)
            return;

        float fixedSpace = gap * static_cast<float>(n - 1);
        float hypothetical = 0.0f;
        for (const Item& it : items)
        {
            fixedSpace   += it.marginMain;
            hypothetical += Clamp(it.basis, it.minMain, it.maxMain);
        }
        const bool growing = innerMain - fixedSpace - hypothetical > 0.0f;

        for (Item& it : items)
        {
            const LayoutStyle& s = it.node->GetLayoutStyle();
            it.frozen = growing ? (s.grow <= 0.0f) : (s.shrink <= 0.0f);
            it.target = it.frozen ? Clamp(it.basis, it.minMain, it.maxMain) : it.basis;
        }

        std::vector<float> violation(n);
        for (size_t iter = 0; iter <= n; ++iter)
        {
            float used          = 0.0f;
            float sumGrow       = 0.0f;
            float sumScaled     = 0.0f;
            bool  anyUnfrozen   = false;
            for (const Item& it : items)
            {
                const LayoutStyle& s = it.node->GetLayoutStyle();
                if (it.frozen)
                {
                    used += it.target;
                    continue;
                }
                anyUnfrozen = true;
                used      += it.basis;
                sumGrow   += s.grow;
                sumScaled += s.shrink * it.basis;
            }
            if (!anyUnfrozen)
                break;

            float freeSpace = innerMain - fixedSpace - used;
            if (growing && sumGrow < 1.0f)
                freeSpace *= sumGrow;   // grow 总和小于 1 时只分配对应比例的剩余空间

            float totalViolation = 0.0f;
            for (size_t i = 0; i < n; ++i)
            {
                Item& it = items[i];
                violation[i] = 0.0f;
                if (it.frozen)
                    continue;

                const LayoutStyle& s = it.node->GetLayoutStyle();
                float target = it.basis;
                if (growing && sumGrow > 0.0f)
                    target += freeSpace * s.grow / sumGrow;
                else if (!growing && sumScaled > 0.0f)
                    target += freeSpace * (s.shrink * it.basis) / sumScaled;

                const float clamped = Clamp(target, it.minMain, it.maxMain);
                violation[i]    = clamped - target;
                totalViolation += violation[i];
                it.target       = clamped;
            }

            if (std::abs(totalViolation) < 1e-4f)
                break;

            // 总违反量为正：冻结被最小值撑大的项；为负：冻结被最大值压小的项
            for (size_t i = 0; i < n; ++i)
            {
                if (items[i].frozen)
                    continue;
                if ((totalViolation > 0.0f && violation[i] > 0.0f) ||
                    (totalViolation < 0.0f && violation[i] < 0.0f))
                    items[i].frozen = true;
            }
        }
    }

    // =========================================================
    // Arrange
    // =========================================================
    void FlexLayout::ArrangeChildren(Node& container, float pixelScale)
    {
        const LayoutStyle& cs   = container.GetLayoutStyle();
        const bool         row  = cs.direction == FlexDirection::Row;
        const Vec2         size = container.GetSize();

        const float innerX = cs.padding.left;
        const float innerY = cs.padding.top;
        const float innerW = std::max(size.x - cs.padding.Horizontal(), 0.0f);
        const float innerH = std::max(size.y - cs.padding.Vertical(), 0.0f);

        const float innerMain       = row ? innerW : innerH;
        const float innerCross      = row ? innerH : innerW;
        const float innerMainStart  = row ? innerX : innerY;
        const float innerCrossStart = row ? innerY : innerX;

        // ── 1. 收集参与排列的子项，计算主轴基础尺寸 ────────────
        std::vector<Item> items;
        for (const auto& child : container.GetChildren())
        {
            if (!child->IsVisible())
                continue;
            if (child->GetLayoutStyle().position == PositionType::Absolute)
            {
                ArrangeAbsolute(*child, size, pixelScale);
                continue;
            }

            const LayoutStyle& s = child->GetLayoutStyle();
            const float marginMain  = row ? s.margin.Horizontal() : s.margin.Vertical();
            const float marginCross = row ? s.margin.Vertical()   : s.margin.Horizontal();

            Item it;
            it.node       = child.get();
            it.marginMain = marginMain;
            it.minMain    = row ? s.minWidth : s.minHeight;
            it.maxMain    = row ? s.maxWidth : s.maxHeight;
            it.basis      = MainOf(child->Measure(MakeVec(Shrunk(innerMain, marginMain), Shrunk(innerCross, marginCross), row)), row);
            items.push_back(it);
        }

        // ── 2. 分配剩余空间 ─────────────────────────────────────
        ResolveFlexibleLengths(items, innerMain, cs.gap);

        // ── 3. 主轴分布 ─────────────────────────────────────────
        float used = 0.0f;
        for (const Item& it : items)
            used += it.target + it.marginMain;
        if (items.size() > 1)
            used += cs.gap * static_cast<float>(items.size() - 1);

        const float remaining = innerMain - used;
        const float count     = static_cast<float>(items.size());
        float lead    = 0.0f;
        float between = cs.gap;
        switch (cs.justify)
        {
        case Justify::Start:
            break;
        case Justify::End:
            lead = remaining;
            break;
        case Justify::Center:
            lead = remaining * 0.5f;
            break;
        case Justify::SpaceBetween:
            if (remaining > 0.0f && items.size() > 1)
                between += remaining / (count - 1.0f);
            break;
        case Justify::SpaceAround:
            if (remaining > 0.0f && !items.empty())
            {
                lead     = remaining / count * 0.5f;
                between += remaining / count;
            }
            break;
        case Justify::SpaceEvenly:
            if (remaining > 0.0f && !items.empty())
            {
                lead     = remaining / (count + 1.0f);
                between += remaining / (count + 1.0f);
            }
            break;
        }

        // ── 4. 交叉轴尺寸与对齐，写回 bounds ────────────────────
        float cursor = innerMainStart + lead;
        for (const Item& it : items)
        {
            Node&              child = *it.node;
            const LayoutStyle& s     = child.GetLayoutStyle();
            const Align        align = s.alignSelf.value_or(cs.alignItems);

            const float marginMainStart  = row ? s.margin.left : s.margin.top;
            const float marginMainEnd    = row ? s.margin.right : s.margin.bottom;
            const float marginCrossStart = row ? s.margin.top : s.margin.left;
            const float marginCrossEnd   = row ? s.margin.bottom : s.margin.right;
            const float minCross         = row ? s.minHeight : s.minWidth;
            const float maxCross         = row ? s.maxHeight : s.maxWidth;
            const float fixedCross       = row ? s.height : s.width;
            const float crossSpace       = std::max(innerCross - marginCrossStart - marginCrossEnd, 0.0f);

            float cross;
            if (!IsAuto(fixedCross))
                cross = fixedCross;
            else if (align == Align::Stretch)
                cross = crossSpace;
            else  // 主轴尺寸已确定，按它重新测量交叉轴（为以后的自动换行文字预留）
                cross = CrossOf(child.Measure(MakeVec(it.target, crossSpace, row)), row);
            cross = Clamp(cross, minCross, maxCross);

            float crossPos = innerCrossStart + marginCrossStart;
            switch (align)
            {
            case Align::Center: crossPos += (crossSpace - cross) * 0.5f; break;
            case Align::End:    crossPos += crossSpace - cross;          break;
            default:                                                     break;
            }

            const float mainPos = cursor + marginMainStart;
            cursor = mainPos + it.target + marginMainEnd + between;

            const Vec2 pos  = MakeVec(mainPos, crossPos, row);
            const Vec2 dims = MakeVec(it.target, cross, row);
            child.SetBounds(SnapRect(Rect{ pos, pos + dims }, pixelScale));
        }
    }

    void FlexLayout::ArrangeAbsolute(Node& child, Vec2 containerSize, float pixelScale)
    {
        const LayoutStyle& s = child.GetLayoutStyle();

        // 只有在尺寸需要由内容决定时才测量
        Vec2 measured;
        const bool needMeasure = (IsAuto(s.width)  && (IsAuto(s.left) || IsAuto(s.right))) ||
                                 (IsAuto(s.height) && (IsAuto(s.top)  || IsAuto(s.bottom)));
        if (needMeasure)
            measured = child.Measure(containerSize);

        float w = !IsAuto(s.width)                        ? s.width
                : (!IsAuto(s.left) && !IsAuto(s.right))   ? containerSize.x - s.left - s.right
                :                                           measured.x;
        float h = !IsAuto(s.height)                       ? s.height
                : (!IsAuto(s.top) && !IsAuto(s.bottom))   ? containerSize.y - s.top - s.bottom
                :                                           measured.y;
        w = Clamp(w, s.minWidth, s.maxWidth);
        h = Clamp(h, s.minHeight, s.maxHeight);

        const float x = !IsAuto(s.left) ? s.left : (!IsAuto(s.right)  ? containerSize.x - s.right  - w : 0.0f);
        const float y = !IsAuto(s.top)  ? s.top  : (!IsAuto(s.bottom) ? containerSize.y - s.bottom - h : 0.0f);

        child.SetBounds(SnapRect(Rect::FromXYWH(x, y, w, h), pixelScale));
    }
}
