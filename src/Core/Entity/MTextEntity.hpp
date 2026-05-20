#pragma once

#include "Entity.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include <string>
#include <cmath>

namespace MiniCAD
{
    using FontStyleId = uint32_t;

    // 多行矢量文字实体（纯数据层）。
    // 字形解析与排版在 Document 层的 DrawContext::EmitMText() 中完成。
    class MTextEntity : public Entity
    {
    public:
        explicit MTextEntity(ObjectID id) : Entity(id) {}

        MTextEntity(ObjectID objectId, FontStyleId styleId, std::string text, Math::Point3 postion,double h = 1,double r = 0,double w=1)
            : Entity(objectId)
            , m_styleId(styleId)
            , m_text(text)
            , m_position(postion)
            , m_height(h)
            , m_rotation(r)
            , m_boxWidth(w)
        {       
        }
        // --- 赋值 ---
        void SetText(const std::string& text)      { m_text = text; }
        void SetStyleId(FontStyleId id)            { m_styleId = id; }
        void SetPosition(const Math::Point3& pos)  { m_position = pos; }
        void SetHeight(double h)                   { m_height = h; }
        void SetRotation(double r)                 { m_rotation = r; }
        void SetBoxWidth(double w)                 { m_boxWidth = w; }

        // --- 读取 ---
        const std::string&  GetText()     const { return m_text; }
        FontStyleId         GetStyleId()  const { return m_styleId; }
        const Math::Point3& GetPosition() const { return m_position; }
        double              GetHeight()   const { return m_height; }
        double              GetRotation() const { return m_rotation; }
        double              GetBoxWidth() const { return m_boxWidth; }

        // --- Entity 接口 ---

        // 近似包围盒（精确版由 Document 层在字体加载后计算）
        AABB GetBoundingBox() const override
        {
            double w = m_text.empty() ? m_height : m_text.size() * m_height * 0.6;
            return AABB(
                { m_position.x,     m_position.y,            m_position.z },
                { m_position.x + w, m_position.y + m_height, m_position.z }
            );
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            if (m_text.empty()) return;

            const auto& color = isSelected ? IDrawSink::kSelectionColor
                              : isHovered  ? IDrawSink::kHoverColor
                              : GetAttr().Color;

            sink.EmitMText(m_position, m_text, m_styleId, m_height, m_rotation, m_boxWidth, color);
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<MTextEntity>(newId);
            e->SetAttr(GetAttr());
            e->m_text     = m_text;
            e->m_styleId  = m_styleId;
            e->m_position = m_position;
            e->m_height   = m_height;
            e->m_rotation = m_rotation;
            e->m_boxWidth = m_boxWidth;
            return e;
        }

        DECLARE_RUNTIME_TYPE(MTextEntity, Entity)

    private:
        std::string  m_text;
        FontStyleId  m_styleId  = 0;       // 指向 DocumentManager 中注册的字体样式
        Math::Point3 m_position = {};
        double       m_height   = 1.0;
        double       m_rotation = 0.0;
        double       m_boxWidth = 0.0;    // 0 = 不限宽
    };
}
