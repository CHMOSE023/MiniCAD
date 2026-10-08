#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Entity/TableEntity.hpp"
#include "Core/Log.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace MiniCAD
{
    // 表格工具（TABLE）：点第 1 点定左上角，再点对角点定范围，行列数按默认单元格尺寸取整
    // （行高 ≈ 3.2 倍字高、列宽 ≈ 12 倍字高），实际行高 / 列宽均分整个范围。
    // 第 1 点后按回车 = 4 列 × 3 行的默认尺寸。建好之后单击已选中的表格即可编辑单元格，
    // 用夹点拖动行高 / 列宽。
    class TableTool : public ITool
    {
    public:
        explicit TableTool(double textHeight)
            : m_th(textHeight > 0.0 ? textHeight : 2.5)
        {
            LOG_DEBUG("[TableTool] 左键指定左上角 | 再指定对角点（回车 = 默认 4×3）| ESC 退出");
        }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                const auto pt = GetPoint(e);
                if (!m_hasFirst)
                {
                    m_first    = pt;
                    m_hasFirst = true;
                    Preview(pt);
                }
                else if (std::abs(pt.x - m_first.x) > 1e-9 && std::abs(pt.y - m_first.y) > 1e-9)
                {
                    Commit(pt);
                }
                return true;
            }

            if (e.Type == InputEventType::MouseMove)
            {
                Preview(GetPoint(e));
                return false;
            }

            if (m_hasFirst && e.IsKeyPressed(KeyCode::Enter))
            {
                Commit({ m_first.x + 4.0 * ColW(), m_first.y - 3.0 * RowH(), m_first.z });
                return true;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                m_ctx->overlay.Clear();
                if (OnFinished) OnFinished();
                return true;
            }

            return false;
        }

        bool HasAnchor() const override { return m_hasFirst; }
        Math::Point3 GetAnchor() const override { return m_first; }

        std::string GetPrompt() const override
        {
            return m_hasFirst ? "指定对角点 [回车=默认 4×3]:" : "指定表格左上角:";
        }

    private:
        double ColW() const { return 12.0 * m_th; }
        double RowH() const { return 3.2 * m_th; }

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        struct Layout
        {
            Math::Point3 topLeft;
            size_t       rows = 1, cols = 1;
            double       width = 0, height = 0;
        };

        // 范围 → 左上角与行列数（对角点可在任意象限）
        Layout Compute(const Math::Point3& other) const
        {
            Layout l;
            const double x0 = std::min(m_first.x, other.x), x1 = std::max(m_first.x, other.x);
            const double y0 = std::min(m_first.y, other.y), y1 = std::max(m_first.y, other.y);
            l.topLeft = { x0, y1, m_first.z };
            l.width   = x1 - x0;
            l.height  = y1 - y0;
            l.cols    = static_cast<size_t>(std::max(1.0, std::round(l.width / ColW())));
            l.rows    = static_cast<size_t>(std::max(1.0, std::round(l.height / RowH())));
            return l;
        }

        void Preview(const Math::Point3& cursor)
        {
            m_ctx->overlay.Clear();
            const Math::Color4 c{ 1.0, 1.0, 1.0, 1.0 };
            if (!m_hasFirst)
            {
                // 未点第 1 点：显示默认大小的表格轮廓跟着光标
                const Math::Point3 br{ cursor.x + 4.0 * ColW(), cursor.y - 3.0 * RowH(), cursor.z };
                m_ctx->overlay.AddRect(cursor, br, c);
                return;
            }
            const Layout l = Compute(cursor);
            if (l.width < 1e-9 || l.height < 1e-9) return;
            const double bx = l.topLeft.x, by = l.topLeft.y;
            m_ctx->overlay.AddRect({ bx, by - l.height, l.topLeft.z }, { bx + l.width, by, l.topLeft.z }, c);
            for (size_t i = 1; i < l.rows; ++i)
            {
                const double y = by - l.height * i / l.rows;
                m_ctx->overlay.AddLine({ bx, y, l.topLeft.z }, { bx + l.width, y, l.topLeft.z }, c);
            }
            for (size_t j = 1; j < l.cols; ++j)
            {
                const double x = bx + l.width * j / l.cols;
                m_ctx->overlay.AddLine({ x, by, l.topLeft.z }, { x, by - l.height, l.topLeft.z }, c);
            }
        }

        void Commit(const Math::Point3& other)
        {
            const Layout l = Compute(other);
            std::vector<double> cols(l.cols, l.width / static_cast<double>(l.cols));
            std::vector<double> rows(l.rows, l.height / static_cast<double>(l.rows));

            auto id = m_ctx->scene.NextObjectID();
            auto t  = std::make_unique<TableEntity>(id, l.topLeft, std::move(cols), std::move(rows), m_th,
                                                    m_ctx->scene.GetCurrentTextStyle());
            m_ctx->ApplyCurrentAttr(*t);
            m_ctx->cmdStack.Execute(std::make_unique<AddEntityCommand>(std::move(t)), m_ctx->scene);
            LOG_DEBUG("[TableTool] 表格 Id=%d %zu 行 × %zu 列", static_cast<int>(id), l.rows, l.cols);

            m_ctx->overlay.Clear();
            if (OnFinished) OnFinished();
        }

    private:
        const EditorContext* m_ctx = nullptr;
        double               m_th;
        bool                 m_hasFirst = false;
        Math::Point3         m_first{};
    };
}
