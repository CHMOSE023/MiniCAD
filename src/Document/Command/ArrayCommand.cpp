#include "ArrayCommand.h"
#include "Document/DimAssoc.h"
#include "Scene/Scene.h"
#include "Core/Entity/Entity.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include "Document/Command/EntityTranslate.h"
#include "Document/Command/EntityRotate.h"
#include "Core/Math/Constants.hpp"
#include <cmath>

namespace MiniCAD
{
    // ── ArrayPlacement::Apply ─────────────────────────────────────
    void ArrayPlacement::Apply(Entity& e) const
    {
        if (rotate) RotateEntityInPlace(e, center, angle);
        else        TranslateEntityInPlace(e, delta);
    }

    // ── 源对象组的参考基点(合并包围盒中心) ───────────────────────
    // 用于「环形阵列且不旋转项目」时把整组作为刚体绕中心搬运。
    Math::Point3 ComputeArrayGroupBase(Scene& scene, const std::vector<Object::ObjectID>& ids)
    {
        bool   has = false;
        AABB   box;
        for (auto id : ids)
        {
            auto* obj = scene.GetEntity(id);
            if (!obj || !obj->IsKindOf<Entity>()) continue;
            AABB b = static_cast<Entity*>(obj)->GetBoundingBox();
            if (!has) { box = b; has = true; }
            else      { box.Expand(b); }
        }
        if (!has) return {};
        return { (box.Min.x + box.Max.x) * 0.5,
                 (box.Min.y + box.Max.y) * 0.5,
                 (box.Min.z + box.Max.z) * 0.5 };
    }

    // ── 枚举除源原位以外的全部副本位置 ───────────────────────────
    std::vector<ArrayPlacement> ComputeArrayPlacements(const ArrayParams& p, const Math::Point3& groupBase)
    {
        std::vector<ArrayPlacement> out;
        if (!p.ProducesCopies()) return out;

        if (p.type == ArrayType::Rectangular)
        {
            const double a = p.angleDeg * Math::PI / 180.0;
            const double ca = std::cos(a), sa = std::sin(a);
            out.reserve(static_cast<size_t>(p.rows) * p.cols);
            for (int r = 0; r < p.rows; ++r)
            {
                for (int c = 0; c < p.cols; ++c)
                {
                    if (r == 0 && c == 0) continue;       // 源原位,已存在
                    const double lx = c * p.colSpacing;   // 阵列局部坐标
                    const double ly = r * p.rowSpacing;
                    ArrayPlacement pl;
                    pl.rotate = false;
                    pl.delta  = { lx * ca - ly * sa, lx * sa + ly * ca, 0.0 };
                    out.push_back(pl);
                }
            }
            return out;
        }

        // ── 环形阵列 ──────────────────────────────────────────
        const double fillRad = p.fillAngleDeg * Math::PI / 180.0;
        const bool   full     = std::abs(std::abs(p.fillAngleDeg) - 360.0) < 1e-6;
        const double step     = full ? (fillRad / p.count)
                                     : (p.count > 1 ? fillRad / (p.count - 1) : 0.0);

        out.reserve(static_cast<size_t>(p.count - 1));
        for (int i = 1; i < p.count; ++i)
        {
            const double ang = i * step;
            ArrayPlacement pl;
            if (p.rotateItems)
            {
                pl.rotate = true;
                pl.center = p.center;
                pl.angle  = ang;
            }
            else
            {
                // 整组刚体搬运:组基点绕中心旋转后求平移量
                Math::Point3 moved = RotatePoint(groupBase, p.center, ang);
                pl.rotate = false;
                pl.delta  = { moved.x - groupBase.x, moved.y - groupBase.y, 0.0 };
            }
            out.push_back(pl);
        }
        return out;
    }

    // ── ArrayCommand ──────────────────────────────────────────────
    ArrayCommand::ArrayCommand(std::vector<Object::ObjectID> sourceIds, ArrayParams params)
        : m_sourceIds(std::move(sourceIds))
        , m_params(params)
    {
    }

    bool ArrayCommand::Execute(Scene& scene)
    {
        const Math::Point3 groupBase = ComputeArrayGroupBase(scene, m_sourceIds);
        const auto placements = ComputeArrayPlacements(m_params, groupBase);
        if (placements.empty()) return false;

        const bool redo = m_executed && !m_newIds.empty();
        size_t idx = 0;

        if (!redo)
        {
            m_newIds.clear();
            m_newIds.reserve(placements.size() * m_sourceIds.size());
        }

        // 副本顺序:外层位置 × 内层源对象,Execute / Redo 必须完全一致
        for (const auto& pl : placements)
        {
            std::vector<std::pair<Object::ObjectID, Object::ObjectID>> group;   // 本位置上的副本
            for (auto srcId : m_sourceIds)
            {
                auto* src = scene.GetEntity(srcId);
                if (!src || !src->IsKindOf<Entity>()) continue;

                Object::ObjectID newId = redo ? m_newIds[idx] : scene.NextObjectID();
                auto clone = static_cast<Entity*>(src)->Clone(newId);
                pl.Apply(*clone);
                scene.AddEntity(std::move(clone));

                if (!redo) m_newIds.push_back(newId);
                group.emplace_back(srcId, newId);
                ++idx;
            }
            DimAssoc::RemapCopies(scene, group);   // 同一位置上一起复制的标注改为关联到副本
        }

        m_executed = true;
        return !m_newIds.empty();
    }

    void ArrayCommand::Undo(Scene& scene)
    {
        for (auto id : m_newIds)
            scene.RemoveEntity(id);
    }
}
