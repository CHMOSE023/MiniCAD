#pragma once
#include "Editor/Input/InputEvent.h"
#include "Editor/Picking/SpatialIndex.h"
#include "Viewport/Viewport.h"
#include "Scene/Scene.h"
#include "Core/Object/Object.hpp"
#include "Core/Math/Point2.hpp"
#include <cstdint>
#include <unordered_set>

namespace MiniCAD
{ 
    class Picking
    {
    public:
        using ObjectID = Object::ObjectID;

        Picking() = default;
        void Bind(Scene& scene, Viewport& viewport);

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
        void QueryWorldAABB(const AABB& q, std::vector<ObjectID>& out)
        {
            EnsureIndex();
            m_index.Query(q, out);
        }

        // 实体是否可被拾取:实体本身可见,且所在图层未关闭。锁定图层上的对象可以拾取、选中、捕捉，
        // 但不能修改（见 Scene::IsEntityLocked，各编辑入口自行跳过）。点选 / 窗选共用。
        bool IsPickable(const Object& obj) const;

        // 状态访问
        const std::unordered_set<ObjectID>& GetSelection() const { return m_selection; }
        const std::unordered_set<ObjectID>& GetHovered()   const { return m_hovered; }

        // 选择范围框
        Math::Point2 GetBoxStart()    const;
        Math::Point2 GetBoxEnd()      const;
        bool         IsBoxSelecting() const;

        // Dirty
        void MarkDirty()     { m_dirty = true; }
        bool IsDirty() const { return m_dirty; }
        void ClearDirty()    { m_dirty = false; }

        // 清空选择
        void ClearSelection() { m_selection.clear(); }

        // 整体替换选择集（供命令 / 自测直接指定选择）
        void SetSelection(std::unordered_set<ObjectID> ids) { m_selection = std::move(ids); m_dirty = true; }

        void RestoreLastSelection();

        // ── 悬停（Hover）启用 / 禁用 ─────────────────────────
        // 禁用后不再做悬停命中测试（省去每次鼠标移动的扫描），并清空当前悬停。
        bool IsHoverEnabled() const { return m_hoverEnabled; }
        void SetHoverEnabled(bool enabled);

        // 每帧渲染前调用：在 Scene::ClearDirty 之前按脏实体集增量同步空间索引。
        // 增量更新依赖脏集,必须保证脏集被清掉前已被索引消费(查询路径也会按需同步)。
        void SyncIndex() { EnsureIndex(); }

        // 解绑文档：释放空间索引与选择集（大图纸的索引有几十 MB）
        void Unbind()
        {
            m_scene    = nullptr;
            m_viewport = nullptr;
            m_index    = {};
            m_indexVersion = ~0ull;
            m_candidates   = {};
            m_selection.clear();
            m_lastSelection.clear();
            m_hovered.clear();
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
        // 若场景几何版本变化则重建索引，保证候选集与场景一致。
        void EnsureIndex();
        // 屏幕矩形 [x0,y0]-[x1,y1] 反投影为世界空间查询 AABB（z 取全范围）。
        AABB ScreenRectToWorldAABB(double x0, double y0, double x1, double y1) const;

    private:
        enum class DragState : uint8_t { Idle, Pressing, BoxSelecting }; 

        static constexpr float DRAG_THRESH  = 2.0f; // 像素阈值：用于区分“点击”和“拖拽”      过小 → 容易误触拖拽，过大 → 拖拽响应迟钝
        static constexpr float HOVER_THRESH = 6.0f; // 像素阈值：悬浮检测半径（屏幕空间）     一般略大于 PICK_THRESH，提高可用性（更容易“扫到”）
        static constexpr float PICK_THRESH  = 5.0f; // 像素阈值：点击选中检测半径（屏幕空间） 通常略小于 HOVER_THRESH，避免“看起来没选中却点中了”

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

        bool m_hoverEnabled = true;   // 悬停高亮开关

        // 空间索引及其同步的几何版本（UINT64_MAX = 尚未构建）
        SpatialIndex m_index;
        uint64_t     m_indexVersion = ~0ull;
        std::vector<ObjectID> m_candidates;   // 查询候选复用缓冲，避免每次分配
    };
}