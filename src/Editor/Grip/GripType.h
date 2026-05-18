#pragma once
#include <cstdint>
#include <memory>
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
#include "Editor/Viewport/Viewport.h"

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
