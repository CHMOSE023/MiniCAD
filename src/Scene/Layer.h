#pragma once
#include <string>
#include <cstdint>
#include "Core/Math/Color4.hpp"
#include "Core/Entity/Lineweight.hpp"
namespace MiniCAD
{
    class ISerializer;
    using LayerID    = uint32_t;
    using LineTypeID = uint32_t;   // 索引进 LineTypeTable(图层线型不允许 ByLayer/ByBlock)
    class Layer
    {
    public:
        Layer() = default;
        Layer(LayerID id, std::string name);

        static constexpr LayerID DefaultLayerID = 0;

        LayerID             GetID()      const;
        const std::string&  GetName()    const;
        const Math::Color4& GetColor()   const;
        bool                IsVisible()  const;
        bool                IsLocked()   const;
        LineTypeID          GetLineType()   const;
        Lineweight          GetLineweight() const;

        void SetColor(const Math::Color4& c);
        void SetName(std::string n);
        void SetVisible(bool v);
        void SetLocked(bool l);
        void SetLineType(LineTypeID lt);
        void SetLineweight(Lineweight lw);

        void Serialize(ISerializer& s) const;
        void Deserialize(ISerializer& s);

    private:
        LayerID            m_id = 0;
        std::string        m_name;
        Math::Color4       m_color{ 1,1,1,1 };
        bool               m_visible = true;
        bool               m_locked = false;
        LineTypeID         m_lineType   = 2;                   // LineTypeTable::ContinuousID
        Lineweight         m_lineweight = Lineweight::Default; // 默认线宽(细线显示)
    };
}