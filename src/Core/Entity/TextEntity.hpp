#pragma once
#include "Entity.hpp"
#include "Core/Math/Point3.hpp"
#include <string>
#include <cmath>

namespace MiniCAD
{
    class TextEntity : public Entity
    {
    public:
        TextEntity(ObjectID id, const Math::Point3& pos,
                   const std::string& utf8Text,
                   float height   = 2.5f,
                   float rotation = 0.f)
            : Entity(id)
            , m_position(pos)
            , m_text(utf8Text)
            , m_height(height)
            , m_rotation(rotation)
        {}

        const std::string&   GetText()     const { return m_text; }
        const Math::Point3&  GetPosition() const { return m_position; }
        float                GetHeight()   const { return m_height; }
        float                GetRotation() const { return m_rotation; }

        void SetText    (const std::string&  t) { m_text     = t; }
        void SetPosition(const Math::Point3& p) { m_position = p; }
        void SetHeight  (float h)               { m_height   = h; }
        void SetRotation(float r)               { m_rotation = r; }

        AABB GetBoundingBox() const override
        {
            // 近似宽度：字高 × 0.6 × 字符数（UTF-8 按字节估算字符数不准，用字节数 / 1.5）
            float approxChars = static_cast<float>(m_text.size()) / 1.5f;
            float approxW     = m_height * 0.6f * approxChars;

            // 不考虑旋转，给一个保守的轴对齐包围盒
            float cosR = std::fabs(std::cosf(m_rotation));
            float sinR = std::fabs(std::sinf(m_rotation));
            float bw   = approxW * cosR + m_height * sinR;
            float bh   = approxW * sinR + m_height * cosR;

            return AABB(
                { m_position.x - bw * 0.05, m_position.y - m_height * 0.2, m_position.z },
                { m_position.x + bw,         m_position.y + bh,              m_position.z }
            );
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const auto& attr = GetAttr();
            const Math::Color4& col = isSelected ? IDrawSink::kSelectionColor
                                    : isHovered  ? IDrawSink::kHoverColor
                                                 : attr.Color;
            sink.EmitText(m_position, m_text, m_height, m_rotation, col);
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<TextEntity>(newId, m_position, m_text, m_height, m_rotation);
            e->SetAttr(GetAttr());
            return e;
        }

        DECLARE_RUNTIME_TYPE(TextEntity, Entity)

    private:
        Math::Point3 m_position;
        std::string  m_text;
        float        m_height;
        float        m_rotation;
    };
}
