#pragma once

#include "Entity.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include "Core/Object/Object.hpp"
#include <string>
#include <cstdint>
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

        // 近似包围盒（锚点为左上角；精确版由 Document 层在字体加载后计算）
        // 宽度估算：字节数 × 0.6 × height（中文 3 字节/字会高估，ASCII 准确）
        // 行数估算：对每个 \n 分段，若段宽超过 BoxWidth 则按 ceil 折行
        AABB GetBoundingBox() const override
        {
            if (m_text.empty())
            {
                return AABB(
                    { m_position.x,            m_position.y - m_height, m_position.z },
                    { m_position.x + m_height, m_position.y,            m_position.z }
                );
            }

            const double avgCharW = m_height * 0.6;
            int    totalLines = 0;
            double maxW       = 0.0;

            // 逐 \n 分段统计行数与最大宽度
            const char* p    = m_text.c_str();
            const char* pEnd = p + m_text.size();
            const char* seg  = p;

            auto processSeg = [&](const char* s, const char* e)
            {
                double segW = static_cast<double>(e - s) * avgCharW;

                if (m_boxWidth > 0.0 && segW > m_boxWidth)
                {
                    // 自动折行：按字节估算折行数（保守上界）
                    totalLines += static_cast<int>(std::ceil(segW / m_boxWidth));
                }
                else
                {
                    totalLines += 1;
                    if (segW > maxW) maxW = segW;
                }
            };

            for (const char* c = p; c <= pEnd; ++c)
            {
                if (c == pEnd || *c == '\n')
                {
                    processSeg(seg, c);
                    seg = c + 1;
                }
            }

            double w      = (m_boxWidth > 0.0) ? m_boxWidth : maxW;
            double totalH = totalLines * m_height;

            return AABB(
                { m_position.x,     m_position.y - totalH, m_position.z },
                { m_position.x + w, m_position.y,          m_position.z }
            );
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            if (m_text.empty()) return;

            const auto& color = isSelected ? IDrawSink::kSelectionColor
                              : isHovered  ? IDrawSink::kHoverColor
                              : GetAttr().Color;

            // 锚点为左上角：EmitMText 原点在首行底部，故下移一个字高使顶部对齐 m_position
            Math::Point3 drawPos{ m_position.x, m_position.y - m_height, m_position.z };
            sink.EmitMText(drawPos, m_text, m_styleId, m_height, m_rotation, m_boxWidth, color);
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
