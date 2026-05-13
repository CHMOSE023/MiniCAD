#pragma once
#include <string>
#include <cstdint> 
#include "Core/Math/Color4.hpp"
namespace MiniCAD
{ 
    class ISerializer;
    using LayerID = uint32_t;
    class Layer
    {
    public:
        Layer() = default;
        Layer(LayerID id, std::string name);

        static constexpr LayerID DefaultLayerID = 0;

        LayerID                   GetID()      const;
        const std::string&  GetName()    const;
        const Math::Color4& GetColor()   const;
        bool                      IsVisible()  const;
        bool                      IsLocked()   const;

        void SetColor(const Math::Color4& c);
        void SetName(std::string n);
        void SetVisible(bool v);
        void SetLocked(bool l);

        void Serialize(ISerializer& s) const;
        void Deserialize(ISerializer& s);

    private:
        LayerID            m_id = 0;
        std::string        m_name;
        Math::Color4       m_color{ 1,1,1,1 };
        bool               m_visible = true;
        bool               m_locked = false;
    };
}