#pragma once
#include "Scene/Scene.h"
#include "Viewport/Viewport.h"
#include "Editor/Snap/SnapEngine.h"
#include "Editor/Constraint/ConstraintEngine.h"
#include "Editor/Picking/Picking.h"
#include "Document/CommandStack/CommandStack.h"
#include "Editor/Overlay/Overlay.h"
#include "Editor/Tools/ITool.h"
#include "Editor/Grip/GripEditor.h"
#include "Editor/Resolver/InputResult.h"
namespace MiniCAD
{ 

    struct EditorContext
    {
        InputEvent&       event;   // 非 const：Resolve 解析后就地回填捕获点
        Scene&            scene;
        Viewport&         viewport;
        SnapEngine&       snap;
        ConstraintEngine& constraint;
        Picking&          picking;
        CommandStack&     cmdStack;
        Overlay&          overlay;

        InputResult       resolved ; //

        ITool*            tool = nullptr;
        GripEditor*       grip = nullptr;

        // 新建实体的统一属性入口:落在当前图层,线型/线宽取当前设置(默认 ByLayer),
        // 颜色保持默认 ByLayer —— 显示时随图层解析。各绘图工具提交实体前调用。
        void ApplyCurrentAttr(Entity& e) const
        {
            auto& attr      = e.GetAttr();
            attr.LayerId    = scene.GetLayerManager().GetActiveLayerID();
            attr.LineType   = scene.GetCurrentLineType();
            attr.Lineweight = scene.GetCurrentLineweight();
        }
    };
}
