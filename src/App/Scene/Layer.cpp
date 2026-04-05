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

    void Layer::SetColor(const DirectX::XMFLOAT4& c)
    {
        if (m_color.x == c.x && m_color.y == c.y && m_color.z == c.z && m_color.w == c.w)
            return;

        m_color = c;
        NotifyChanged();
    }

    void Layer::SetName(std::string n)
    {
        if (m_name == n)
            return;

        m_name = std::move(n);
        NotifyChanged();
    }

    void Layer::SetVisible(bool v)
    {
        if (m_visible == v)
            return;

        m_visible = v;
        NotifyChanged();
    }

    void Layer::SetLocked(bool l)
    {
        if (m_locked == l)
            return;

        m_locked = l;
        NotifyChanged();
    }

    void Layer::SetChangeCallback(ChangeCallback cb)
    {
        m_onChange = std::move(cb);
    }

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

    void Layer::NotifyChanged()
    {
        if (m_onChange)
            m_onChange();
    }
}
