#include "CopyCommand.h"
#include "Scene/Scene.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Math/Vec3.hpp"
#include "EntityTranslate.h"
namespace MiniCAD
{
    // ── 整体平移已克隆出来的 Entity ─────────────────────────────
    // 简化做法:不区分类型,直接用 dynamic_cast 链 + 各自 Set*
    // 但既然 Entity 已经有 Clone,我们更通用的做法是给每个 Entity 加 Translate,
    // 或者在 Clone 后用 type-switch.这里为了不再扩接口,在 Clone 之前先翻译"源几何 + delta",
    // 再用克隆参数构造新对象 —— 与其按类型再翻一次,不如复用 MoveCommand 那套 Translate 工具函数.
    //
    // 简单起见,这里走 type-switch 平移已克隆对象.
    // 如果你愿意,可以把这段抽到独立的 EntityTranslator.cpp 让 Move/Copy 共用.

    // 复用 MoveCommand.cpp 里已经写好的 Translate 重载族:
    // 把它们从 static 改成放在某个公共 helper 头(EntityTranslate.hpp)更优雅;
    // 这里假设我们已经有这样一个工具函数:
    //
    //   void TranslateEntityInPlace(Entity& e, const Math::Vec3& d);
    //
    // 见下文"三、抽出公共平移工具"

    //extern void TranslateEntityInPlace(Entity& e, const Math::Vec3& d);

    CopyCommand::CopyCommand(std::vector<Object::ObjectID> sourceIds, Math::Vec3 delta)
        : m_sourceIds(std::move(sourceIds))
        , m_delta(delta)
    {
    }

    bool CopyCommand::Execute(Scene& scene)
    {
        // ── Redo 路径:已有新 ID,沿用同一 ID 重新克隆并加入 ───────────
        if (m_executed && !m_newIds.empty())
        {
            for (size_t i = 0; i < m_sourceIds.size(); ++i)
            {
                auto* src = scene.GetEntity(m_sourceIds[i]);
                if (!src || !src->IsKindOf<Entity>()) continue;

                auto clone = static_cast<Entity*>(src)->Clone(m_newIds[i]);
                TranslateEntityInPlace(*clone, m_delta);
                scene.AddEntity(std::move(clone));    
            }
            scene.MarkDirty();
            return true;
        }

        // ── 首次执行:分配新 ID 并入场 ─────────────────────────────
        m_newIds.clear();
        m_newIds.reserve(m_sourceIds.size());

        for (auto srcId : m_sourceIds)
        {
            auto* src = scene.GetEntity(srcId);
            if (!src || !src->IsKindOf<Entity>()) continue;

            Object::ObjectID newId = scene.NextObjectID();
            auto clone = static_cast<Entity*>(src)->Clone(newId);
            TranslateEntityInPlace(*clone, m_delta);
            scene.AddEntity(std::move(clone));                  

            m_newIds.push_back(newId);
        }

        m_executed = true;
        scene.MarkDirty();
        return !m_newIds.empty();
    }

    void CopyCommand::Undo(Scene& scene)
    {
        for (auto id : m_newIds)
            scene.RemoveEntity(id);                             
        scene.MarkDirty();
    }
}
