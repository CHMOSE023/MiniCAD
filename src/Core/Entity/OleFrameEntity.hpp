#pragma once
#include "RectangleEntity.hpp"
#include "Core/Ole/OleObject.h"
#include <memory>
#include <vector>

namespace MiniCAD
{
    // OLE 对象（OLE2FRAME）：图纸里嵌入的 Word / Excel 文档等，画成一张贴在矩形上的"快照"（见 Core/Ole/OleObject.h）。
    //
    // 几何与 ImageEntity 一样用 RectangleEntity 的四个顶点（左下 P1、右下 P2、右上 P3、左上 P4），移动 / 捕捉 / 拾取沿用矩形。
    // 数据是 OLE2FRAME 的全部二进制（AutoCAD 头 + 复合文档），原样保留：另存为原格式原版本时写回原对象，
    // 也随 .mcad / JSON 保存。光栅化在 Editor 的图像回调里按需进行（图元文件要平台回放，网页版没有时只画边框）
    class OleFrameEntity : public RectangleEntity
    {
    public:
        OleFrameEntity(ObjectID id, const Math::Point3& p1, const Math::Point3& p2, const Math::Point3& p3, const Math::Point3& p4)
            : RectangleEntity(id, p1, p2, p3, p4)
        {
        }

        void SetData(std::shared_ptr<const std::vector<uint8_t>> data)
        {
            m_data = std::move(data);
            m_key  = m_data && !m_data->empty() ? Ole::HashData(*m_data) : 0;
            if (m_key != 0)
                Ole::DataStore::Instance().Put(m_key, m_data);
        }
        const std::shared_ptr<const std::vector<uint8_t>>& GetData() const { return m_data; }
        uint64_t GetDataKey() const { return m_key; }

        // OLE 对象类型（DXF 组码 71）：1 链接、2 嵌入、3 静态
        void SetOleType(int type) { m_type = type; }
        int  GetOleType() const   { return m_type; }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            const auto& r = GetRectangle();
            auto e = std::make_unique<OleFrameEntity>(newId, r.P1, r.P2, r.P3, r.P4);
            e->SetAttr(GetAttr());
            e->m_data = m_data;
            e->m_key  = m_key;
            e->m_type = m_type;
            return e;
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const auto& r = GetRectangle();
            const bool highlight = isSelected || isHovered;
            // 选中 / 悬停：图像已画在场景流里，只叠加高亮边框
            const bool shown = highlight || (m_key != 0 && sink.EmitImage(Ole::ImagePath(m_key), { r.P1, r.P2, r.P3, r.P4 }));

            // 边框（AutoCAD 的 OLEFRAME 默认显示、不打印）
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor
                                      : isHovered  ? IDrawSink::kHoverColor
                                                   : ResolveDrawColor(sink);
            sink.DrawLine(r.P1, r.P2, color, false);
            sink.DrawLine(r.P2, r.P3, color, false);
            sink.DrawLine(r.P3, r.P4, color, false);
            sink.DrawLine(r.P4, r.P1, color, false);
            if (!shown)         // 画不出内容（没有展示数据 / 平台不能光栅化）：对角线作占位
            {
                sink.DrawLine(r.P1, r.P3, color, false);
                sink.DrawLine(r.P2, r.P4, color, false);
            }
        }

        DECLARE_RUNTIME_TYPE(OleFrameEntity, RectangleEntity)

    private:
        std::shared_ptr<const std::vector<uint8_t>> m_data;
        uint64_t m_key  = 0;
        int      m_type = 2;
    };
}
