#pragma once
#include "Core/Object/Object.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Vec3.hpp"
#include "Core/GeomKernel/Point.hpp"
#include "Core/GeomKernel/Line.hpp"
#include "Core/GeomKernel/Circle.hpp"
#include "Core/GeomKernel/Rectangle.hpp"
#include "Core/GeomKernel/Arc.hpp"
#include "Core/GeomKernel/Ellipse.hpp"
#include "Core/GeomKernel/Polyline.hpp"
#include "Core/GeomKernel/Spline.hpp"
#include "Core/Entity/HatchEntity.hpp"   // HatchLoop / HatchEdge
#include <vector>

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
            Text,
            MText,
            Insert,      // InsertEntity    — 位置 + 旋转角
            Dimension,   // DimensionEntity — 四个定义点 + linearAngle
            Hatch,       // HatchEntity     — 完整边界环结构
            Leader,      // LeaderEntity    — 顶点列表
            MLeader,     // MLeaderEntity   — landing + 各引线顶点
            Ray,         // RayEntity       — 起点 + 方向向量
            XLine,       // XLineEntity     — 基点 + 方向向量
            Tolerance,   // ToleranceEntity — 插入点 + 方向向量
        } Kind;

        // 仅使用与 Kind 对应的那对字段，其余字段未定义但不读取
        Point     BeforePoint,    AfterPoint;
        Line      BeforeLine,     AfterLine;
        Circle    BeforeCircle,   AfterCircle;
        Rectangle BeforeRect,     AfterRect;
        Arc       BeforeArc,      AfterArc;
        Ellipse   BeforeEllipse,  AfterEllipse;
        Polyline  BeforePolyline, AfterPolyline;
        Spline    BeforeSpline,   AfterSpline;

        // Text / MText：位置 + 旋转角
        struct TextSnap { Math::Point3 pos = {}; double rotation = 0.0; };
        TextSnap BeforeText,  AfterText;
        TextSnap BeforeMText, AfterMText;

        // Insert：插入点 + 旋转角 + 缩放（镜像通过翻转某一缩放轴表示反射）
        struct InsertSnap { Math::Point3 pos = {}; double rotation = 0.0; Math::Vec3 scale{ 1, 1, 1 }; };
        InsertSnap BeforeInsert, AfterInsert;

        // Dimension：四个定义点 + 线性角度（Linear 类型旋转/镜像时需变）
        struct DimSnap { Math::Point3 p1, p2, dimPt, center; double linearAngle = 0.0; };
        DimSnap BeforeDim, AfterDim;

        // Leader：顶点列表
        std::vector<Math::Point3> BeforeLeaderVerts, AfterLeaderVerts;

        // MLeader：landing 点 + 各引线顶点列表
        struct MLeaderSnap
        {
            Math::Point3                           landing;
            std::vector<std::vector<Math::Point3>> linePoints;
        };
        MLeaderSnap BeforeMLeader, AfterMLeader;

        // Hatch：完整边界环结构
        std::vector<HatchLoop> BeforeHatch, AfterHatch;

        // Ray / XLine / Tolerance：原点/插入点 + 方向向量（旋转/镜像时方向也变）
        struct PosVecSnap { Math::Point3 pos = {}; Math::Vec3 dir = {}; };
        PosVecSnap BeforePosVec, AfterPosVec;
    };
}
