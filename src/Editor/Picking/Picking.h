#pragma once
#include "Editor/Input/InputEvent.h"
#include "Editor/Picking/SpatialIndex.h"
#include "Viewport/Viewport.h"
#include "Scene/Scene.h"
#include "Core/Object/Object.hpp"
#include "Core/Entity/EntityAttr.hpp"
#include "Core/Math/Point2.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace MiniCAD
{ 
    class Picking
    {
    public:
        using ObjectID = Object::ObjectID;

        Picking() = default;
        void Bind(Scene& scene, Viewport& viewport);

        // ── 布局过滤（布局页专用）────────────────────────────────────
        //
        // 渲染侧由 Editor::UpdateSceneVertices 的 isShown() 调
        // Document::IsVisibleInActiveLayout 过滤：布局页只画**本布局**的图纸空间实体。
        // 拾取侧若不过滤同一条件，就会「渲染看不见、拾取仍命中」——
        // 用户报的「布局页视口外还能选中隐藏的图元」正是这个：
        // 图纸空间里属于**别的**布局的实体（LayoutKey 不同），画不出来但点得中。
        //
        // 传空函数 = 不过滤（模型页，或未绑文档的场景）。
        using LayoutFilter = std::function<bool(const EntityAttr&)>;
        void SetLayoutFilter(LayoutFilter filter) { m_layoutFilter = std::move(filter); }

        // 输入入口
        bool OnInput(const InputEvent& e);

        // 查询接口
        ObjectID HitTest(const Math::Point2& pt, double thresh);
        std::unordered_set<ObjectID> BoxSelect(const Math::Point2& a, const Math::Point2& b);

        // 捕捉候选收集：以屏幕点为中心、radiusPx 为半径的查询窗口，经空间索引
        // 返回包围盒相交的实体 ID（含 overflow 中的 XLine/Ray 等无限实体）。
        // 供 SnapEngine 把各捕捉模式的全场景遍历缩减为光标附近的候选集。
        void CollectSnapCandidates(const Math::Point2& pt, double radiusPx, std::vector<ObjectID>& out);

        // 世界空间 AABB 候选查询：返回包围盒与 q 相交的实体 ID（含 overflow
        // 无界实体）。供 Trim/Extend 等工具把"与全场景求交"缩减为目标附近的候选集。
        // limit 见 SpatialIndex::Query
        void QueryWorldAABB(const AABB& q, std::vector<ObjectID>& out, size_t limit = SIZE_MAX)
        {
            EnsureIndex();
            m_index.Query(q, out, limit);
        }
        // 索引里记的包围盒（不重新计算），见 SpatialIndex::GetBox
        bool GetIndexedBox(ObjectID id, AABB& out)
        {
            EnsureIndex();
            return m_index.GetBox(id, out);
        }

        // 实体是否可被拾取:实体本身可见,且所在图层未关闭、未锁定。锁定图层上的对象不能点选、框选、悬停高亮，
        // 也不能被工具点取，但仍可捕捉（SnapEngine 不经这里）。点选 / 窗选共用。
        bool IsPickable(const Object& obj) const;

        // 状态访问
        const std::unordered_set<ObjectID>& GetSelection() const { return m_selection; }
        const std::unordered_set<ObjectID>& GetHovered()   const { return m_hovered; }

        // 选择范围框
        Math::Point2 GetBoxStart()    const;
        Math::Point2 GetBoxEnd()      const;
        bool         IsBoxSelecting() const;

        // Dirty
        void MarkDirty()     { m_dirty = true; ++m_selectionVersion; }
        bool IsDirty() const { return m_dirty; }
        void ClearDirty()    { m_dirty = false; }
        // 选择集每次变化加一：界面按它缓存选择集的统计（公共图层、颜色、特性……），不必每次刷新都扫一遍
        uint64_t SelectionVersion() const { return m_selectionVersion; }

        // 清空选择
        void ClearSelection() { m_selection.clear(); ++m_selectionVersion; }

        // 整体替换选择集（供命令 / 自测直接指定选择）
        void SetSelection(std::unordered_set<ObjectID> ids) { m_selection = std::move(ids); m_dirty = true; ++m_selectionVersion; }

        void RestoreLastSelection();

        // ── 悬停（Hover）启用 / 禁用 ─────────────────────────
        // 禁用后不再做悬停命中测试（省去每次鼠标移动的扫描），并清空当前悬停。
        bool IsHoverEnabled() const { return m_hoverEnabled; }
        void SetHoverEnabled(bool enabled);
        // 换视口（文档拖出成独立窗口 / 拖回）：空间索引与选择集都在世界坐标里，不用重建
        void SetViewport(Viewport& viewport) { m_viewport = &viewport; }

        // 每帧渲染前调用：在 Scene::ClearDirty 之前按脏实体集增量同步空间索引。
        // 增量更新依赖脏集,必须保证脏集被清掉前已被索引消费(查询路径也会按需同步)。
        void SyncIndex() { EnsureIndex(); }

        // 解绑文档：释放空间索引与选择集（大图纸的索引有几十 MB）
        void Unbind()
        {
            m_scene    = nullptr;
            m_viewport = nullptr;
            m_layoutFilter = nullptr;   // 回调捕获的是 Editor，解绑后不得再调用
            m_index    = {};
            m_indexVersion = ~0ull;
            m_displayVersion = ~0ull;
            decltype(m_candidates){}.swap(m_candidates);
            decltype(m_pickGeometry){}.swap(m_pickGeometry);
            decltype(m_selection){}.swap(m_selection);
            decltype(m_lastSelection){}.swap(m_lastSelection);
            decltype(m_hovered){}.swap(m_hovered);
            m_drag = DragState::Idle;
        }

    private:
        // 输入分发
        void OnMouseDown  (const InputEvent& e);
        void OnMouseMove  (const InputEvent& e);
        void OnMouseUp    (const InputEvent& e);
        void OnKeyDown    (const InputEvent& e); 

        // 核心逻辑
        void UpdateHovered(const InputEvent& e);
        void DoPointPick  (const InputEvent& e);
        void DoBoxPick    (const InputEvent& e); 

        template<typename T>
        static bool SetEquals(const std::unordered_set<T>& a, const std::unordered_set<T>& b);

        // ── 空间索引 ─────────────────────────────────────────
        // 若场景几何版本变化则重建索引，保证候选集与场景一致；显示版本变化（图层关闭 / 锁定）时另去掉选不了的对象。
        void EnsureIndex();
        void DropUnpickable();
        // 屏幕矩形 [x0,y0]-[x1,y1] 反投影为世界空间查询 AABB（z 取全范围）。
        AABB ScreenRectToWorldAABB(double x0, double y0, double x1, double y1) const;

    public:
        // 像素阈值：悬停 / 点选的命中半径（屏幕空间）。测试与诊断要用，故公开。
        // 悬停略大于点选，更容易"扫到"；点选略小，避免"看起来没选中却点中了"。
        static constexpr float HOVER_THRESH = 6.0f;
        static constexpr float PICK_THRESH  = 5.0f;

    private:
        enum class DragState : uint8_t { Idle, Pressing, BoxSelecting };

        static constexpr float DRAG_THRESH  = 2.0f; // 像素阈值：用于区分“点击”和“拖拽”      过小 → 容易误触拖拽，过大 → 拖拽响应迟钝

        Scene*    m_scene    = nullptr;
        Viewport* m_viewport = nullptr;

        std::unordered_set<ObjectID> m_lastSelection;  // 上次选择集快照 

        std::unordered_set<ObjectID> m_selection;
        std::unordered_set<ObjectID> m_hovered;

        DragState m_drag = DragState::Idle;

        int m_pressX = 0;
        int m_pressY = 0;
        int m_currX  = 0;
        int m_currY  = 0;
        bool m_dirty = false;

        bool m_hoverEnabled = false;   // 悬停高亮开关

        // 布局过滤（布局页）：非空时 IsPickable 先用它排除不属于当前布局的图纸空间实体，
        // 与渲染侧的 Document::IsVisibleInActiveLayout 保持同一口径
        LayoutFilter m_layoutFilter;

        // 空间索引及其同步的几何版本（UINT64_MAX = 尚未构建）
        SpatialIndex m_index;
        uint64_t     m_indexVersion = ~0ull;
        uint64_t     m_displayVersion = ~0ull;  // 拾取几何缓存对应的场景显示版本
        std::vector<ObjectID> m_candidates;   // 查询候选复用缓冲，避免每次分配

        // 通用拾取回退（块参照、天正对象、标注等）的世界空间几何缓存：按实体 ID 保存其 Draw() 输出的线段与三角形，
        // 实体变化时随空间索引同步失效。大块参照每次重新绘制要十几毫秒，不缓存时鼠标移动（悬停拾取）会卡
        struct PickGeometry
        {
            std::vector<std::pair<Math::Point3, Math::Point3>> Segments;
            std::vector<std::array<Math::Point3, 3>>           Triangles;
            // 点图元（.pat 的 dash = 0，如 DOTS 点阵）。点走 IDrawSink::DrawPoint，
            // 不产线段/三角形，若不单独收集就**完全拾取不到**（表现为能显示但不能选中）。
            // 点的屏幕尺寸是 1 像素，命中判定按世界距离 + 阈值即可。
            std::vector<Math::Point3>                          Points;
        };
        const PickGeometry& GetPickGeometry(const Object& obj);
        std::unordered_map<ObjectID, PickGeometry> m_pickGeometry;
        uint64_t m_selectionVersion = 0;
    };
}
