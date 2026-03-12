// ============================================================
// MiniCAD — app/Scene/Scene.cpp
// 职责：Scene 实现
// 依赖：app/Scene/Scene.h, core/Entity/LineEntity.h
// 约束：不依赖 render/ ui/
// ============================================================
#include "Scene.h"
#include "core/Entity/LineEntity.h"
#include <math/Box.hpp>

namespace MiniCAD {

void Scene::AddEntity(std::unique_ptr<Object> entity) {
    if (!entity) return;
    ObjectID id = entity->GetID();
    m_entities[id] = std::move(entity);
    MarkDirty();
}

std::unique_ptr<Object> Scene::RemoveEntity(ObjectID id) {
    auto it = m_entities.find(id);
    if (it == m_entities.end()) return nullptr;
    std::unique_ptr<Object> ret = std::move(it->second);
    m_entities.erase(it);
    MarkDirty();
    return ret;
}

Object* Scene::GetEntity(ObjectID id) {
    auto it = m_entities.find(id);
    return it != m_entities.end() ? it->second.get() : nullptr;
}

const Object* Scene::GetEntity(ObjectID id) const {
    auto it = m_entities.find(id);
    return it != m_entities.end() ? it->second.get() : nullptr;
}

bool Scene::Has(ObjectID id) const {
    return m_entities.count(id) > 0;
}

std::vector<Scene::ObjectID> Scene::GetAllIDs() const {
    std::vector<ObjectID> ids;
    ids.reserve(m_entities.size());
    for (auto& kv : m_entities) ids.push_back(kv.first);
    return ids;
}

std::vector<Scene::ObjectID> Scene::QueryByBox(const Box& box) const {
    std::vector<ObjectID> result;
    for (auto& kv : m_entities) {
        // 尝试调用 GetBoundingBox（通过 IsKindOf 检查 LineEntity）
        const Object* obj = kv.second.get();
        if (obj->IsKindOf<LineEntity>()) {
            const LineEntity* le = static_cast<const LineEntity*>(obj);
            if (box.Intersects(le->GetBoundingBox()))
                result.push_back(kv.first);
        }
    }
    return result;
}

} // namespace MiniCAD
