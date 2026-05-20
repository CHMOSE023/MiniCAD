#pragma once 

#include "Entity.hpp"
#include "Core/Math/Point3.hpp" 
#include <string>
#include <memory>

namespace MiniCAD
{ 
    using FontStyleId = uint32_t;

    class MTextEntity : public Entity
    {
    public:
        explicit MTextEntity(ObjectID id)
            : Entity(id)
        {
        }

        void SetText(const std::string& text)
        {
            m_text = text;
        }

        void SetFont(std::shared_ptr<IFont> font)
        {
            m_font = font;
        }

        void SetHeight(double h)
        {
            m_height = h;
        }

        void SetBoxWidth(double w)
        {
            m_boxWidth = w;
        }

        AABB GetBoundingBox() const override
        {
            // 简化版：后续应该由 layout cache 提供
            return AABB();
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<TextEntity>(newId);
            e->SetAttr(GetAttr());

            e->m_text     = m_text;
            e->m_font     = m_font;
            e->m_height   = m_height;
            e->m_boxWidth = m_boxWidth;

            return e;
        }

        DECLARE_RUNTIME_TYPE(MTextEntity, Entity)

    private:
        std::string  m_text;
        FontStyleId  m_styleId; // 字体类型

        Math::Point3 m_position;

        double m_height   = 1.0;
        double m_boxWidth = 0.0;
    };
}