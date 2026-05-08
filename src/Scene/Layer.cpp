#include "Layer.h" 

namespace MiniCAD
{
    Layer::Layer(LayerID id, std::string name)
        : m_id(id)
        , m_name(std::move(name))
    {
    }

    LayerID Layer::GetID() const { return m_id; }
    const std::string& Layer::GetName() const { return m_name; }
    const DirectX::XMFLOAT4& Layer::GetColor() const { return m_color; }
    bool Layer::IsVisible() const { return m_visible; }
    bool Layer::IsLocked() const { return m_locked; }

    void Layer::SetColor(const DirectX::XMFLOAT4& c) { m_color = c; }
    void Layer::SetName(std::string n) { m_name = std::move(n); }
    void Layer::SetVisible(bool v) { m_visible = v; }
    void Layer::SetLocked(bool l) { m_locked = l; }

}
