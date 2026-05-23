#pragma once
#include "Scene/Scene.h"
#include "Editor/Viewport/Viewport.h"
#include "Editor/Snap/SnapEngine.h"
#include "Editor/Constraint/ConstraintEngine.h"
#include "Editor/Picking/Picking.h"
#include "Editor/Tools/ITool.h"
#include "Editor/Grip/GripEditor.h"

namespace MiniCAD
{
    struct InputContext
    {
        const InputEvent& event;
        Scene&            scene;
        Viewport&         viewport;
        SnapEngine&       snap;
        ConstraintEngine& constraint;
        Picking&          picking;

        ITool*            tool = nullptr;
        GripEditor*       grip = nullptr;
    };
}
