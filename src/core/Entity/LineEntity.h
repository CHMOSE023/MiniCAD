// ============================================================
// MiniCAD — core/Entity/LineEntity.h
// 职责：线段实体，持有 Line + EntityAttr
// 依赖：core/Object/Object.h, core/GeomKernel/Line.h, core/Entity/EntityAttr.h
// 约束：不依赖 render / ui
// ============================================================
#pragma once
#include "core/Object/Object.hpp"
#include "core/Object/RuntimeType.hpp"
#include "core/GeomKernel/Line.hpp"
#include "core/Entity/EntityAttr.h"
#include "math/Box.hpp"

namespace MiniCAD { 
    class LineEntity : public Object {
    public:
        LineEntity(ObjectID id, const Point3& start, const Point3& end);

        const Line& GetLine()   const { return m_line; }
        void        SetLine(const Line& line) { m_line = line; }
        EntityAttr& GetAttr() { return m_attr; }
        const EntityAttr& GetAttr()   const { return m_attr; }

        Box GetBoundingBox() const;

        // Object interface
        std::vector<uint8_t> Snapshot() const override;
        void RestoreSnapshot(const std::vector<uint8_t>& data) override;
        void Serialize(ISerializer& s) const override;
        void Deserialize(ISerializer& s) override;
          
    private:
        Line       m_line;
        EntityAttr m_attr;
    };

} // namespace MiniCAD
