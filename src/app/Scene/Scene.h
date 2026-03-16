// ============================================================
// MiniCAD — app/Scene/Scene.h
// 职责：实体容器，按 ObjectID 索引，维护 DirtyFlag
// 依赖：core/Object/Object.h, math/Box.h
// 约束：不依赖 render/ ui/；变更时调用 onDirty 回调
// ============================================================
#pragma once

#include "core/Object/Object.hpp"
#include "math/Box.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>

namespace MiniCAD {

    class Scene {
    public:
        using ObjectID = Object::ObjectID;
        using DirtyCallback = std::function<void()>;

        Scene() = default;

        // ── 实体管理 ─────────────────────────────────────────────
        void    AddEntity(std::unique_ptr<Object> entity);
        // 移除并返回所有权（供 Undo 使用）
        std::unique_ptr<Object> RemoveEntity(ObjectID id);

        Object* GetEntity(ObjectID id);
        const Object* GetEntity(ObjectID id) const;

        bool Has(ObjectID id) const;

        // 返回所有实体 ID
        std::vector<ObjectID> GetAllIDs() const;

        // AABB 框选：返回与 box 相交的实体 ID
        std::vector<ObjectID> QueryByBox(const Box& box) const;

        // ── DirtyFlag ────────────────────────────────────────────
        bool IsDirty()       const { return m_dirty; }
        void MarkDirty() { m_dirty = true; if (m_onDirty) m_onDirty(); }
        void ClearDirty() { m_dirty = false; }

        void SetDirtyCallback(DirtyCallback cb) { m_onDirty = std::move(cb); }

        int EntityCount() const { return (int)m_entities.size(); }

    private:
        std::unordered_map<ObjectID, std::unique_ptr<Object>> m_entities;
        bool         m_dirty = false;
        DirtyCallback m_onDirty;
    };

} // namespace MiniCAD
