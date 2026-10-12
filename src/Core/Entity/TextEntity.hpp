#pragma once
#include "Entity.hpp"
#include "Core/Math/Point3.hpp"
#include <algorithm>
#include <string>
#include <cstdint>
#include <cmath>

namespace MiniCAD
{
    class TextEntity : public Entity
    {
    public:
        TextEntity(ObjectID id, const Math::Point3& pos,
                   const std::string& utf8Text,
                   float height   = 2.5f,
                   float rotation = 0.f,
                   uint32_t styleId = 0)
            : Entity(id)
            , m_position(pos)
            , m_text(utf8Text)
            , m_height(height)
            , m_rotation(rotation)
            , m_styleId(styleId)
        {}

        const std::string&   GetText()     const { return m_text; }
        const Math::Point3&  GetPosition() const { return m_position; }
        float                GetHeight()   const { return m_height; }
        float                GetRotation() const { return m_rotation; }
        uint32_t             GetStyleId()  const { return m_styleId; }      // 文字样式表 ID（Scene::GetTextStyleTable）
        // 对齐方式（DXF 组码 72 / 73 的子集）：水平 0 左 / 1 中 / 2 右，竖直 0 基线 / 1 底 / 2 中 / 3 上；
        // 非左对齐基线时 position 为对齐点
        int                  GetHAlign()   const { return m_hAlign; }
        int                  GetVAlign()   const { return m_vAlign; }
        // 宽度因子（DXF 组码 41）：> 0 时代替样式的宽度因子，0 = 随样式（MiniCAD 新建的文字）
        double               GetWidthFactor() const { return m_widthFactor; }

        void SetText    (const std::string&  t) { m_text     = t; }
        void SetPosition(const Math::Point3& p) { m_position = p; }
        void SetHeight  (float h)               { m_height   = h; }
        void SetRotation(float r)               { m_rotation = r; }
        void SetStyleId (uint32_t id)           { m_styleId  = id; }
        void SetAlignment(int h, int v)         { m_hAlign = std::clamp(h, 0, 2); m_vAlign = std::clamp(v, 0, 3); }
        void SetWidthFactor(double w)           { m_widthFactor = w > 0.0 ? w : 0.0; }

        AABB GetBoundingBox() const override
        {
            // 近似宽度：字高 × 0.6 × 字符数（UTF-8 按字节估算字符数不准，用字节数 / 1.5）
            float approxChars = static_cast<float>(m_text.size()) / 1.5f;
            float approxW     = m_height * 0.6f * approxChars;
            if (m_widthFactor > 0.0) approxW *= static_cast<float>(m_widthFactor);
            if (approxW <= 0.f) approxW = m_height;

            // 文字矩形(局部,未旋转):x∈[0,w],y∈[-0.2h,h](含下伸部余量);
            // 绕插入点旋转四角后取轴对齐包围盒,任意角度均正确。
            const double c = std::cos(static_cast<double>(m_rotation));
            const double s = std::sin(static_cast<double>(m_rotation));
            // 对齐点在文字矩形中的位置（与 EmitAlignedText 一致）
            const double dx = m_hAlign == 1 ? -0.5 * approxW : m_hAlign == 2 ? -static_cast<double>(approxW) : 0.0;
            const double dy = m_vAlign == 1 ? 0.2 * m_height : m_vAlign == 2 ? -0.5 * m_height : m_vAlign == 3 ? -static_cast<double>(m_height) : 0.0;
            const double xs[2] = { dx, dx + static_cast<double>(approxW) };
            const double ys[2] = { dy - 0.2 * m_height, dy + static_cast<double>(m_height) };

            double minX =  1e300, minY =  1e300;
            double maxX = -1e300, maxY = -1e300;
            for (double x : xs)
                for (double y : ys)
                {
                    const double rx = x * c - y * s;
                    const double ry = x * s + y * c;
                    minX = std::min(minX, rx); maxX = std::max(maxX, rx);
                    minY = std::min(minY, ry); maxY = std::max(maxY, ry);
                }

            return AABB(
                { m_position.x + minX, m_position.y + minY, m_position.z },
                { m_position.x + maxX, m_position.y + maxY, m_position.z }
            );
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const auto& attr = GetAttr();
            const Math::Color4& col = isSelected ? IDrawSink::kSelectionColor
                                    : isHovered  ? IDrawSink::kHoverColor
                                                 : ResolveDrawColor(sink);
            if (m_hAlign == 0 && m_vAlign == 0 && m_widthFactor <= 0.0)
                sink.EmitMText(m_position, m_text, m_styleId, m_height, m_rotation, 0.0, col);
            else
                sink.EmitAlignedText(m_position, m_text, m_styleId, m_height, m_rotation, m_hAlign, m_vAlign, m_widthFactor, col);
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<TextEntity>(newId, m_position, m_text, m_height, m_rotation, m_styleId);
            e->SetAttr(GetAttr());
            e->SetAlignment(m_hAlign, m_vAlign);
            e->SetWidthFactor(m_widthFactor);
            return e;
        }

        DECLARE_RUNTIME_TYPE(TextEntity, Entity)

    private:
        Math::Point3 m_position;
        std::string  m_text;
        float        m_height;
        float        m_rotation;
        uint32_t     m_styleId = 0;     // 文字样式表 ID
        int          m_hAlign  = 0;
        int          m_vAlign  = 0;
        double       m_widthFactor = 0.0;   // 0 = 随样式
    };
}
