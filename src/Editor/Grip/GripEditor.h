#pragma once
#include "Scene/Scene.h"
#include "Editor/Input/InputEvent.h"
#include "Editor/Picking/Picking.h"
#include "Editor/Viewport/Viewport.h"
#include "Editor/Overlay/Overlay.h"
#include "Document/CommandStack/CommandStack.h"
#include "Core/Object/Object.hpp"
#include "Core/Entity/Entity.hpp"
#include "Core/Math/Point2.hpp"
#include "Core/Math/Point3.hpp"
#include "IEntityGripHandler.h"
#include "GripType.h"
#include <vector>
#include <memory>
#include <unordered_map>
#include <cstdint>

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // GripEditor
    //
    // CAD 风格夹点编辑流程：
    //   1. MouseDown  命中夹点 → 激活（m_activated = true）
    //   2. MouseUp             → 进入跟随模式（m_following = true）
    //   3. MouseMove           → 所有激活夹点实时跟随鼠标
    //   4. MouseDown  左键     → 确认，提交 Command，回到空闲
    //      MouseDown  右键     → 取消，还原，回到空闲
    // ─────────────────────────────────────────────
    class GripEditor
    {
    public:
        GripEditor(Viewport& viewport, Scene& scene, CommandStack& cmdStack, Picking& picking, Overlay& overlay);

    public:
        bool OnInput      (const InputEvent& e);
        bool IsFollowing  () const { return m_following;  }
        bool IsDragging   () const { return m_following;  } 
        bool IsActivated  () const { return m_activated;  }
        bool IsGripsEmpty () const { return m_grips.empty(); } 
        void MarkDirty    ()       { m_dirty = true; }
        void RebuildGrips ();
        void CancelDrag   ();
    public:
        const std::vector<Grip>& GetGrips    () const { return m_grips; }
        const std::vector<int>&  HoveredGrips() const { return m_hoveredIdxs; }

        // 仅在 IsActivated() 为 true 时有效
        const Grip* GetActiveGrip() const
        {
            if (m_activeGripIdx < 0 || m_activeGripIdx >= (int)m_grips.size())
                return nullptr;
            return &m_grips[m_activeGripIdx];
        }

    public:
        // 注册 Handler（ctor 中调用）
        template<typename T>
        void RegisterHandler(std::unique_ptr<IEntityGripHandler> handler)
        {
            static_assert(std::is_base_of_v<Entity, T>, "T must derive from Entity");
            m_handlers[&T::TypeInfo] = std::move(handler);
        }

        // 允许外部替换配色
        GripColors& GetColors() { return m_colors; }

    private:
        bool OnMouseDown(const InputEvent& e);
        bool OnMouseMove(const InputEvent& e);
        bool OnMouseUp  (const InputEvent& e);

    private:
        // 第一次 MouseDown：激活夹点，收集 drag entries
        bool DoActivate(const InputEvent& e);

        // 第二次 MouseDown（跟随中）左键：确认提交
        bool DoConfirm (const InputEvent& e);

    private:
        IEntityGripHandler* FindHandler(Entity* entity);

    private:
        int              HitTest   (const Math::Point2& screenPt, float thresh = 8.f) const;
        std::vector<int> HitTestAll(const Math::Point2& screenPt, float thresh = 8.f) const;

    private:
        bool Rebuild();

    private:
        Scene&        m_scene;
        Viewport&     m_viewport;
        CommandStack& m_cmdStack;
        Picking&      m_picking;
        Overlay&      m_overlay;

    private:
        // RuntimeTypeInfo* → Handler
        std::unordered_map<const RuntimeTypeInfo*, std::unique_ptr<IEntityGripHandler>> m_handlers;

    private:
        std::vector<Grip> m_grips;
        std::vector<int>  m_hoveredIdxs;

    private:
        std::vector<GripDragEntry> m_dragEntries;

    private:
        GripColors m_colors;

        // 用索引而非指针，避免 vector 重分配后悬空
        int  m_activeGripIdx = -1;

        // 已命中夹点并完成 BeginDrag（MouseDown 后置位）
        bool m_activated = false;

        // MouseUp 后进入跟随模式，MouseMove 实时更新几何
        bool m_following = false;

        bool m_dirty     = true;
    };
}
