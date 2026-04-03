#include "Layer.h"
#include "Serialization/ISerializer.h"

namespace MiniCAD
{
    Layer::Layer(LayerID id, std::string name)
        : m_id(id)
        , m_name(std::move(name))
    {}

    LayerID Layer::GetID() const { return m_id; }
    const std::string& Layer::GetName() const { return m_name; }
    const DirectX::XMFLOAT4& Layer::GetColor() const { return m_color; }
    bool Layer::IsVisible() const { return m_visible; }
    bool Layer::IsLocked() const { return m_locked; }

    void Layer::SetColor(const DirectX::XMFLOAT4& c) { m_color = c; }
    void Layer::SetName(std::string n) { m_name = std::move(n); }
    void Layer::SetVisible(bool v) { m_visible = v; }
    void Layer::SetLocked(bool l) { m_locked = l; }

    void Layer::Serialize(ISerializer& s) const
    {
        s.WriteUInt64(m_id);
        s.WriteString(m_name);

        s.WriteFloat(m_color.x);
        s.WriteFloat(m_color.y);
        s.WriteFloat(m_color.z);
        s.WriteFloat(m_color.w);

        s.WriteBool(m_visible);
        s.WriteBool(m_locked);
    }

    void Layer::Deserialize(ISerializer& s)
    {
        m_id   = s.ReadUInt64();
        m_name = s.ReadString();

        m_color = 
        {
            s.ReadFloat(),
            s.ReadFloat(),
            s.ReadFloat(),
            s.ReadFloat()
        };

        m_visible = s.ReadBool();
        m_locked = s.ReadBool();
    }
}
