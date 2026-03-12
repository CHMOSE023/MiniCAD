// ============================================================
// MiniCAD — app/Scene/Layer.h
// 职责：图层：名称 / 颜色 / 可见性 / 锁定状态
// 依赖：core/Entity/EntityAttr.h
// 约束：不依赖 render/ ui/
// ============================================================
#pragma once
#include "core/Entity/EntityAttr.h"
#include <string>
#include <cstdint>
#include <utility>

namespace MiniCAD {

class Layer {
public:
    using LayerID = uint64_t;

    Layer(LayerID id, std::string name)
        : m_id(id), m_name(std::move(name)) {}

    LayerID            GetID()      const { return m_id; }
    const std::string& GetName()    const { return m_name; }
    const Color4&       GetColor()   const { return m_color; }
    bool               IsVisible()  const { return m_visible; }
    bool               IsLocked()   const { return m_locked; }

    void SetName(std::string n)      { m_name    = std::move(n); }
    void SetColor(const Color4& c)   { m_color   = c;            }
    void SetVisible(bool v)          { m_visible = v;            }
    void SetLocked(bool l)           { m_locked  = l;            }

private:
    LayerID     m_id;
    std::string m_name;
    Color4       m_color   = Color4::White();
    bool        m_visible = true;
    bool        m_locked  = false;
};

} // namespace MiniCAD
