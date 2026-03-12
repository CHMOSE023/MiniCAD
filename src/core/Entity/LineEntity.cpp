// ============================================================
// MiniCAD — core/Entity/LineEntity.cpp
// 职责：LineEntity 实现
// 依赖：core/Entity/LineEntity.h, doc/Archive/Serializer.h
// ============================================================
#include "core/Entity/LineEntity.h"
#include "doc/Archive/Serializer.h"
#include "math/MathDefs.hpp"
#include <cstring>
#include <core/Object/Object.hpp>
#include <math/Box.hpp>
#include <math/Point.hpp>
#include <vector>
#include <cstdint>
#include <math/Vector.hpp>

namespace MiniCAD { 

    LineEntity::LineEntity(ObjectID id, const Point3& start, const Point3& end)
        : Object(id), m_line(start, end), m_attr{}
    {
    }

    Box LineEntity::GetBoundingBox() const {
        Box b;
        b.Expand(m_line.start);
        b.Expand(m_line.end);
        return b;
    }

    std::vector<uint8_t> LineEntity::Snapshot() const {
        // Simple raw byte snapshot: 6 floats (start xyz, end xyz)
        std::vector<uint8_t> data(6 * sizeof(float));
        Real vals[6] = {
            m_line.start.x, m_line.start.y, m_line.start.z,
            m_line.end.x,   m_line.end.y,   m_line.end.z
        };
        std::memcpy(data.data(), vals, data.size());
        return data;
    }

    void LineEntity::RestoreSnapshot(const std::vector<uint8_t>& data) {
        if (data.size() < 6 * sizeof(float)) return;
        float vals[6];
        std::memcpy(vals, data.data(), 6 * sizeof(float));
        m_line.start = { vals[0], vals[1], vals[2] };
        m_line.end = { vals[3], vals[4], vals[5] };
    }

    void LineEntity::Serialize(ISerializer& s) const {
        s.Write("type", "LineEntity");
        s.Write("id", static_cast<int>(m_id));
        s.Write("start", m_line.start.ToVec3());
        s.Write("end", m_line.end.ToVec3());
    }

    void LineEntity::Deserialize(ISerializer& s) {
        Vec3 sv = s.ReadVec3("start");
        Vec3 ev = s.ReadVec3("end");
        m_line.start = { sv.x, sv.y, sv.z };
        m_line.end = { ev.x, ev.y, ev.z };
    }

} // namespace MiniCAD
