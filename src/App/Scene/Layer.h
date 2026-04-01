#pragma once
#include <string>
#include <cstdint>
#include <utility>
#include <DirectXMath.h>
#include "Core/Object/Object.hpp"
namespace MiniCAD
{ 
    class Layer {
    public:
        using LayerID = Object::LayerID;

        Layer(LayerID id, std::string name)
            : m_id(id)
            , m_name(std::move(name))
            , m_color(1.f, 1.f, 1.f, 1.f) 
            , m_visible(true)
            , m_locked(false)
        {
        }

        LayerID                   GetID()      const { return m_id; }
        const std::string&        GetName()    const { return m_name; }
        const DirectX::XMFLOAT4&  GetColor()   const { return m_color; }
        bool                      IsVisible()  const { return m_visible; }
        bool                      IsLocked()   const { return m_locked; }

        void SetColor(const DirectX::XMFLOAT4& c) { m_color = c; }
        void SetName(std::string n) { m_name = std::move(n); }
        void SetVisible(bool v) { m_visible = v; }
        void SetLocked(bool l) { m_locked = l; }

    private:
        LayerID            m_id;
        std::string        m_name;
        DirectX::XMFLOAT4  m_color;
        bool               m_visible = true;
        bool               m_locked = false;
    };

}  
