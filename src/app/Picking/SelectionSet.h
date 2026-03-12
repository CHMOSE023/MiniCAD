// ============================================================
// MiniCAD — app/Picking/SelectionSet.h
// 职责：当前选中集合，维护选中 EntityID 列表
// 依赖：core/Object/Object.h
// 约束：不依赖 render/ ui/
// ============================================================
#pragma once

#include "core/Object/Object.hpp"
#include <vector>
#include <functional>
#include <algorithm>

namespace MiniCAD {

class SelectionSet {
public:
    using ObjectID = Object::ObjectID;
    using ChangedCallback = std::function<void()>;

    void Select(ObjectID id);
    void Deselect(ObjectID id);
    void ToggleSelect(ObjectID id);
    void Clear();

    bool IsSelected(ObjectID id) const;

    const std::vector<ObjectID>& GetSelected() const { return m_selected; }
    int Count() const { return (int)m_selected.size(); }

    void SetChangedCallback(ChangedCallback cb) { m_onChanged = std::move(cb); }

private:
    std::vector<ObjectID> m_selected;
    ChangedCallback        m_onChanged;

    void Notify() { if (m_onChanged) m_onChanged(); }
};

} // namespace MiniCAD
