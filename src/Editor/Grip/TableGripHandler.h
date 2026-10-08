#pragma once
#include "IEntityGripHandler.h"
#include "GripType.h"
#include "Core/Entity/TableEntity.hpp"
#include "Core/Math/Constants.hpp"
#include "Document/Command/DragEntitiesCommand.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace MiniCAD
{
    struct TableDragState : public IGripDragState
    {
        Math::Point3        BasePos;
        std::vector<double> BaseCols;
        std::vector<double> BaseRows;
    };

    // ─────────────────────────────────────────────
    // TableGripHandler
    //
    //   Start (SubIndex=0)  — 左上角，拖动整体移动
    //   Mid   (SubIndex=j)  — 第 j 列右边界（顶边上），拖动改该列宽度
    //   End   (SubIndex=i)  — 第 i 行下边界（左边上），拖动改该行高度
    // 改列宽 / 行高只影响该列 / 行，表格总尺寸随之变化。
    // ─────────────────────────────────────────────
    class TableGripHandler : public IEntityGripHandler
    {
    public:
        void BuildGrips(Entity* entity, std::vector<Grip>& out) override
        {
            auto* t = static_cast<TableEntity*>(entity);
            const auto id = entity->GetID();
            out.push_back({ id, Grip::Type::Start, t->GetPosition(), 0 });
            for (size_t j = 0; j < t->ColCount(); ++j)
                out.push_back({ id, Grip::Type::Mid, t->ToWorld(t->ColEdge(j + 1), 0.0), static_cast<int>(j) });
            for (size_t i = 0; i < t->RowCount(); ++i)
                out.push_back({ id, Grip::Type::End, t->ToWorld(0.0, -t->RowEdge(i + 1)), static_cast<int>(i) });
        }

        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip&) override
        {
            auto* t = static_cast<TableEntity*>(entity);
            auto st = std::make_unique<TableDragState>();
            st->BasePos  = t->GetPosition();
            st->BaseCols = t->ColWidths();
            st->BaseRows = t->RowHeights();
            return st;
        }

        void UpdateDrag(Entity* entity, IGripDragState* dragState, const Grip& active,
                        const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* t  = static_cast<TableEntity*>(entity);
            auto* st = static_cast<TableDragState*>(dragState);

            // 始终从快照出发
            t->SetPosition(st->BasePos);
            t->SetColWidths(st->BaseCols);
            t->SetRowHeights(st->BaseRows);

            if (active.GripType == Grip::Type::Start)
            {
                t->SetPosition(worldPos);
            }
            else
            {
                double lx, ly;
                t->ToLocal(worldPos, lx, ly);       // 以快照位置为准（位置此刻已还原）
                if (active.GripType == Grip::Type::Mid)
                {
                    const size_t j = static_cast<size_t>(active.SubIndex);
                    if (j < st->BaseCols.size())
                    {
                        auto cols = st->BaseCols;
                        double left = 0; for (size_t k = 0; k < j; ++k) left += cols[k];
                        cols[j] = lx - left;
                        t->SetColWidths(std::move(cols));
                    }
                }
                else if (active.GripType == Grip::Type::End)
                {
                    const size_t i = static_cast<size_t>(active.SubIndex);
                    if (i < st->BaseRows.size())
                    {
                        auto rows = st->BaseRows;
                        double top = 0; for (size_t k = 0; k < i; ++k) top += rows[k];
                        rows[i] = -ly - top;
                        t->SetRowHeights(std::move(rows));
                    }
                }
            }

            // 夹点跟手
            for (auto& g : grips)
            {
                if (g.OwnerID != entity->GetID()) continue;
                const size_t k = static_cast<size_t>(std::max(g.SubIndex, 0));
                if (g.GripType == Grip::Type::Start)      g.WorldPos = t->GetPosition();
                else if (g.GripType == Grip::Type::Mid)   g.WorldPos = t->ToWorld(t->ColEdge(k + 1), 0.0);
                else if (g.GripType == Grip::Type::End)   g.WorldPos = t->ToWorld(0.0, -t->RowEdge(k + 1));
            }
        }

        bool EndDrag(Entity* entity, IGripDragState* dragState, DragEntityEntry& out) override
        {
            auto* t  = static_cast<TableEntity*>(entity);
            auto* st = static_cast<TableDragState*>(dragState);

            auto same = [](const std::vector<double>& a, const std::vector<double>& b)
            {
                if (a.size() != b.size()) return false;
                for (size_t i = 0; i < a.size(); ++i)
                    if (std::abs(a[i] - b[i]) > Math::LengthEPS) return false;
                return true;
            };
            const auto& p = t->GetPosition();
            const bool moved = std::abs(p.x - st->BasePos.x) > Math::LengthEPS || std::abs(p.y - st->BasePos.y) > Math::LengthEPS;
            if (!moved && same(t->ColWidths(), st->BaseCols) && same(t->RowHeights(), st->BaseRows))
                return false;

            out.Id          = entity->GetID();
            out.Kind        = DragEntityEntry::Kind::Table;
            out.BeforeTable = { st->BasePos, st->BaseCols, st->BaseRows };
            out.AfterTable  = { p, t->ColWidths(), t->RowHeights() };
            return true;
        }

        void CancelDrag(Entity* entity, IGripDragState* dragState) override
        {
            auto* t  = static_cast<TableEntity*>(entity);
            auto* st = static_cast<TableDragState*>(dragState);
            t->SetPosition(st->BasePos);
            t->SetColWidths(st->BaseCols);
            t->SetRowHeights(st->BaseRows);
        }
    };
}
