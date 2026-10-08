#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Editor/Grip/GripEditor.h"
#include "Core/Object/Object.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/TableEntity.hpp"
#include "Core/Entity/XLineEntity.hpp"
#include "Core/Entity/RayEntity.hpp"
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/LeaderEntity.hpp"
#include "Core/Entity/MLeaderEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Math/Point3.hpp"
#include "Editor/Grip/GripType.h"
#include <memory>

namespace MiniCAD
{ 
    class DragEntitiesCommand : public ICommand
    {
    public:
        explicit DragEntitiesCommand(std::vector<DragEntityEntry> entries)
            : m_entries(std::move(entries)) {}

        bool Execute(Scene& scene) override
        {
            if (m_entries.empty())
                return false;

            Apply(scene, /*useAfter=*/true);
            return true;
        }

        void Undo(Scene& scene) override
        {
            Apply(scene, /*useAfter=*/false);
        }

        std::string GetName() const override { return "拖动对象"; }

    private:
        std::vector<DragEntityEntry> m_entries;

        void Apply(Scene& scene, bool useAfter)
        {
            for (auto& e : m_entries)
            {
                auto obj = scene.GetEntity(e.Id);
                if (!obj) continue;
                scene.MarkEntityDirty(e.Id);   // 原位改几何:标脏,撤销 / 重做后该实体重新细分

                if (e.Kind == DragEntityEntry::Kind::Line)
                {
                    auto* line = static_cast<LineEntity*>(obj);
                    const auto& seg = useAfter ? e.AfterLine : e.BeforeLine;
                    line->SetLine({ seg.Start, seg.End });
                }
                
                if (e.Kind == DragEntityEntry::Kind::Point)
                {
                    auto* pt = static_cast<PointEntity*>(obj);
                    const auto& p = useAfter ? e.AfterPoint : e.BeforePoint;
                    pt->SetPoint({ p });
                }

                if (e.Kind == DragEntityEntry::Kind::Circle)
                {
                    auto* circle = static_cast<CircleEntity*>(obj);
                    const auto& snap = useAfter ? e.AfterCircle : e.BeforeCircle;
                    circle->SetCircle({ snap.Center,snap.Radius });
                }

                if (e.Kind == DragEntityEntry::Kind::Rectangle)
                {
                    auto* rect = static_cast<RectangleEntity*>(obj);
                    const auto& snap = useAfter ? e.AfterRect : e.BeforeRect;
                    rect->SetRectangle(snap);
                }

                if (e.Kind == DragEntityEntry::Kind::Text)
                {
                    auto* text = static_cast<TextEntity*>(obj);
                    const auto& p = useAfter ? e.AfterPoint : e.BeforePoint;
                    text->SetPosition(p);
                }

                if (e.Kind == DragEntityEntry::Kind::MText)
                {
                    auto* mtext = static_cast<MTextEntity*>(obj);
                    const auto& snap = useAfter ? e.AfterMText : e.BeforeMText;
                    mtext->SetPosition(snap.Position);
                    mtext->SetBoxWidth(snap.BoxWidth);
                }

                if (e.Kind == DragEntityEntry::Kind::Table)
                {
                    auto* table = static_cast<TableEntity*>(obj);
                    const auto& snap = useAfter ? e.AfterTable : e.BeforeTable;
                    table->SetPosition(snap.Position);
                    table->SetColWidths(snap.ColWidths);
                    table->SetRowHeights(snap.RowHeights);
                }

                if (e.Kind == DragEntityEntry::Kind::XLine)
                {
                    auto* xline = static_cast<XLineEntity*>(obj);
                    xline->SetXLine(useAfter ? e.AfterXLine : e.BeforeXLine);
                }

                if (e.Kind == DragEntityEntry::Kind::Ray)
                {
                    auto* ray = static_cast<RayEntity*>(obj);
                    ray->SetRay(useAfter ? e.AfterXLine : e.BeforeXLine);
                }

                if (e.Kind == DragEntityEntry::Kind::Dimension)
                {
                    auto* dim   = static_cast<DimensionEntity*>(obj);
                    const auto& s = useAfter ? e.AfterDim : e.BeforeDim;
                    dim->SetType(s.Type);
                    dim->SetP1(s.P1);
                    dim->SetP2(s.P2);
                    dim->SetDimLinePoint(s.DimLinePoint);
                    dim->SetCenterPoint(s.CenterPoint);
                    dim->SetLinearAngle(s.LinearAngle);
                    dim->SetOrdinateAxis(s.Axis);
                    if (s.UseDefaultTextPos)
                        dim->ResetTextPosition();
                    else
                        dim->SetTextPosition(s.TextPos);
                }

                if (e.Kind == DragEntityEntry::Kind::Leader)
                {
                    auto* leader = static_cast<LeaderEntity*>(obj);
                    leader->SetVertices(useAfter ? e.AfterLeader : e.BeforeLeader);
                }

                if (e.Kind == DragEntityEntry::Kind::MLeader)
                {
                    auto* ml = static_cast<MLeaderEntity*>(obj);
                    const auto& s = useAfter ? e.AfterMLeader : e.BeforeMLeader;
                    ml->GetLeaderLines() = s.Lines;
                    ml->SetLanding(s.Landing);
                    ml->SetDoglegDir(s.DoglegDir);
                }

                if (e.Kind == DragEntityEntry::Kind::Arc)
                    static_cast<ArcEntity*>(obj)->SetArc(useAfter ? e.AfterArc : e.BeforeArc);

                if (e.Kind == DragEntityEntry::Kind::Ellipse)
                    static_cast<EllipseEntity*>(obj)->SetEllipse(useAfter ? e.AfterEllipse : e.BeforeEllipse);

                if (e.Kind == DragEntityEntry::Kind::Polyline)
                    static_cast<PolylineEntity*>(obj)->SetPolyline(useAfter ? e.AfterPolyline : e.BeforePolyline);

                if (e.Kind == DragEntityEntry::Kind::Spline)
                    static_cast<SplineEntity*>(obj)->GetSpline() = useAfter ? e.AfterSpline : e.BeforeSpline;

                if (e.Kind == DragEntityEntry::Kind::Hatch)
                {
                    auto* hatch = static_cast<HatchEntity*>(obj);
                    hatch->GetLoops() = useAfter ? e.AfterHatch : e.BeforeHatch;
                }

                if (e.Kind == DragEntityEntry::Kind::Insert)
                {
                    auto* ins = static_cast<InsertEntity*>(obj);
                    const auto& p = useAfter ? e.AfterPoint : e.BeforePoint;
                    ins->SetPosition(p);
                }
            }
        }
    };
}
