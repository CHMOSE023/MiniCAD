#include "Layer.h"
#include "Serialization/ISerializer.h"
#include "Serialization/SerializeHelpers.hpp"
#include <string>
#include <utility>

namespace MiniCAD
{
    Layer::Layer(LayerID id, std::string name)
        : m_id(id)
        , m_name(std::move(name))
    {
    }

    LayerID Layer::GetID() const { return m_id; }
    const std::string&  Layer::GetName() const { return m_name; }
    const Math::Color4& Layer::GetColor() const { return m_color; }
    bool Layer::IsVisible() const { return m_visible; }
    bool Layer::IsLocked()  const { return m_locked; }
    LineTypeID Layer::GetLineType()   const { return m_lineType; }
    Lineweight Layer::GetLineweight() const { return m_lineweight; }

    void Layer::SetColor  (const Math::Color4& c) { m_color = c; }
    void Layer::SetName   (std::string n) { m_name = std::move(n); }
    void Layer::SetVisible(bool v) { m_visible = v; }
    void Layer::SetLocked (bool l) { m_locked = l; }
    void Layer::SetLineType  (LineTypeID lt) { m_lineType = lt; }
    void Layer::SetLineweight(Lineweight lw) { m_lineweight = lw; }

    void Layer::Serialize(ISerializer& s) const
    {
        const_cast<Layer*>(this)->Deserialize(s);   // 读写合一,写路径不修改自身
    }

    void Layer::Deserialize(ISerializer& s)
    {
        s.Value("id", m_id);
        s.Value("name", m_name);
        Ser::Value(s, "color", m_color);
        s.Value("visible", m_visible);
        s.Value("locked", m_locked);
        s.Value("lineType", m_lineType);

        int32_t lw = static_cast<int32_t>(LineweightToRaw(m_lineweight));
        s.Value("lineweight", lw);
        if (s.IsLoading()) m_lineweight = LineweightFromRaw(static_cast<int16_t>(lw));
    }
}
