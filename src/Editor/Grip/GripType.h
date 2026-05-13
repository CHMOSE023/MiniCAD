#pragma once
#include <cstdint>
#include <memory>
#include "IEntityGripHandler.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Object/Object.hpp"
#include "Editor/Viewport/Viewport.h"

namespace MiniCAD
{
    // 线段快照
    struct LineSegment
    {
        Math::Point3 Start;
        Math::Point3 End;
    };

    // 圆快照
    struct CircleSnapshot
    {
        Math::Point3 Center;
        double       Radius = 0.0;
    };

    struct Grip
    {
        enum class Type : uint8_t
        {
            Start,
            End,
            Mid,

            Corner, // 例如：矩形的四个角点
            Center,

            Radius,
            Quadrant,

            Tangent
        };

        Object::ObjectID OwnerID  = Object::InvalidID;
        Type             GripType = Type::Start;
        Math::Point3     WorldPos = {};

        // 子索引：
        // Corner : P1/P2/P3...
        // Mid    : Edge0/Edge1...
        int              SubIndex = -1;
    };

    // ─────────────────────────────────────────────
    // 拖拽条目
    // 每个 Entity 一个独立 DragState
    // ─────────────────────────────────────────────
    struct GripDragEntry
    {
        Object::ObjectID    Id;
        Grip                ActiveGrip;
        IEntityGripHandler* Handler = nullptr;

        std::unique_ptr<IGripDragState> DragState;
    };
}
