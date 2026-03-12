// ============================================================
// MiniCAD — app/Picking/SelectionSet.cpp
// ============================================================
#include "SelectionSet.h"

namespace MiniCAD {

void SelectionSet::Select(ObjectID id) {
    if (!IsSelected(id)) { m_selected.push_back(id); Notify(); }
}

void SelectionSet::Deselect(ObjectID id) {
    auto it = std::find(m_selected.begin(), m_selected.end(), id);
    if (it != m_selected.end()) { m_selected.erase(it); Notify(); }
}

void SelectionSet::ToggleSelect(ObjectID id) {
    if (IsSelected(id)) Deselect(id);
    else                Select(id);
}

void SelectionSet::Clear() {
    if (!m_selected.empty()) { m_selected.clear(); Notify(); }
}

bool SelectionSet::IsSelected(ObjectID id) const {
    return std::find(m_selected.begin(), m_selected.end(), id) != m_selected.end();
}

} // namespace MiniCAD
