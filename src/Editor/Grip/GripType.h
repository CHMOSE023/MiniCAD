#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "IEntityGripHandler.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Object/Object.hpp"
#include "Core/GeomKernel/Circle.hpp"
#include "Core/GeomKernel/Line.hpp"
#include "Core/GeomKernel/Rectangle.hpp"
#include "Core/GeomKernel/Arc.hpp"
#include "Core/GeomKernel/Ellipse.hpp"
#include "Core/GeomKernel/Polyline.hpp"
#include "Core/GeomKernel/Spline.hpp"
#include "Core/GeomKernel/XLine.hpp"
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/MLeaderEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Viewport/Viewport.h"

namespace MiniCAD
{ 
    struct Grip
    {
        enum class Type : uint8_t
        {
            Start,
            End,
            Mid,
            Corner,     // 矩形角点
            Center,     // 圆心
            Radius,
            Quadrant,   // 圆象限点
            Tangent     // 曲线控制点
        };

        Object::ObjectID OwnerID = Object::InvalidID;
        Type             GripType = Type::Start;
        Math::Point3     WorldPos = {};

        // 子索引：
        //   Corner : P0/P1/P2/P3
        //   Mid    : Edge0/Edge1...
        int              SubIndex = -1;
    }; 
  
    // ─────────────────────────────────────────────
    // GripDragEntry
    //
    // 每个参与拖拽的 Entity 一个条目
    // ─────────────────────────────────────────────
    struct GripDragEntry
    {
        Object::ObjectID                Id;
        Grip                            ActiveGrip;         // 值拷贝，不持有指针
        IEntityGripHandler*             Handler    = nullptr;
        std::unique_ptr<IGripDragState> DragState;
    };

    // MText 拖拽快照（位置 + 宽度）
    struct MTextSnapshot
    {
        Math::Point3 Position;
        double       BoxWidth = 0.0;
    };

    // Table 拖拽快照：插入点 + 列宽 + 行高
    struct TableSnapshot
    {
        Math::Point3        Position;
        std::vector<double> ColWidths;
        std::vector<double> RowHeights;
    };

    // Dimension 拖拽快照：覆盖各标注子类型用到的全部定义点与参数。
    struct DimSnapshot
    {
        DimType      Type              = DimType::Aligned;
        Math::Point3 P1;
        Math::Point3 P2;
        Math::Point3 DimLinePoint;
        Math::Point3 CenterPoint;
        double       LinearAngle       = 0.0;
        OrdinateAxis Axis              = OrdinateAxis::X;
        Math::Point3 TextPos;
        bool         UseDefaultTextPos = true;
    };

    // MLeader 拖拽快照：全部引线顶点 + 基线端点 + dogleg 方向。
    struct MLeaderSnapshot
    {
        std::vector<MLeaderEntity::LeaderLine> Lines;
        Math::Point3                           Landing;
        Math::Vec3                             DoglegDir{ 1, 0, 0 };
    };

    struct DragEntityEntry
    {
        Object::ObjectID Id;

        enum class Kind
        {
            Point,
            Line,
            Circle,
            Rectangle,
            Arc,
            Ellipse,
            Polyline,
            Spline,
            Text,
            MText,
            XLine,
            Ray,
            Dimension,
            Leader,
            MLeader,
            Hatch,
            Insert,
            Table,
        } Kind;


        Math::Point3   BeforePoint;
        Math::Point3   AfterPoint;

        Line     BeforeLine;
        Line     AfterLine;

        Arc      BeforeArc;
        Arc      AfterArc;

        Ellipse  BeforeEllipse;
        Ellipse  AfterEllipse;

        Polyline BeforePolyline;
        Polyline AfterPolyline;

        Spline   BeforeSpline;
        Spline   AfterSpline;

        Circle   BeforeCircle;
        Circle   AfterCircle;

        Rectangle      BeforeRect;
        Rectangle      AfterRect;

        MTextSnapshot  BeforeMText;
        MTextSnapshot  AfterMText;

        // XLine / Ray 共用 XLine 几何(基点 + 方向)
        XLine    BeforeXLine;
        XLine    AfterXLine;

        DimSnapshot    BeforeDim;
        DimSnapshot    AfterDim;

        // Leader 路径顶点快照
        std::vector<Math::Point3> BeforeLeader;
        std::vector<Math::Point3> AfterLeader;

        // MLeader 快照
        MLeaderSnapshot BeforeMLeader;
        MLeaderSnapshot AfterMLeader;

        // Table 快照
        TableSnapshot BeforeTable;
        TableSnapshot AfterTable;

        // Hatch 边界环快照
        std::vector<HatchLoop> BeforeHatch;
        std::vector<HatchLoop> AfterHatch;
    };

    // ─────────────────────────────────────────────
    // GripColors — 夹点渲染配色
    // ─────────────────────────────────────────────
    struct GripColors
    {
        Math::Color4 Normal  = { 0.0, 0.75, 0.75, 1.0 };  // 青色
        Math::Color4 Hovered = { 1.0, 1.0,  0.0,  1.0 };  // 黄色
        Math::Color4 Active  = { 1.0, 0.35, 0.0,  1.0 };  // 橙红
    };
}
