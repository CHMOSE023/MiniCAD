#pragma once
#include "Core/Object/Object.hpp"
#include "Core/GeomKernel/Point.hpp"
#include "Core/GeomKernel/Line.hpp"
#include "Core/GeomKernel/Circle.hpp"
#include "Core/GeomKernel/Rectangle.hpp"
#include "Core/GeomKernel/Arc.hpp"
#include "Core/GeomKernel/Ellipse.hpp"
#include "Core/GeomKernel/Polyline.hpp"
#include "Core/GeomKernel/Spline.hpp"

namespace MiniCAD
{
    struct MoveEntityEntry
    {
        Object::ObjectID Id = Object::InvalidID;

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

        // 仅使用与 Kind 对应的那对字段,其余字段未定义但不读取
        Point     BeforePoint, AfterPoint;
        Line      BeforeLine, AfterLine;
        Circle    BeforeCircle, AfterCircle;
        Rectangle BeforeRect, AfterRect;
        Arc       BeforeArc, AfterArc;
        Ellipse   BeforeEllipse, AfterEllipse;
        Polyline  BeforePolyline, AfterPolyline;
        Spline    BeforeSpline, AfterSpline;
    };
}
