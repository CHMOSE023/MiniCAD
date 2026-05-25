#pragma once
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Core/Math/Point3.hpp"
#include <functional>
#include <cstdio>

namespace MiniCAD
{
    class TextTool : public ITool
    {
    public:
        TextTool()
        {
            printf("[TextTool] 左键选择插入点 | ESC 退出\n");
        }

        ~TextTool() { printf("退出文字绘制\n"); }

        std::function<void(Math::Point3)> OnInsertPointPicked;

        bool OnInput(const EditorContext& ctx) override
        {
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                Math::Point3 pt = e.HasSnap
                    ? e.SnapWorld
                    : ctx.viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);

                if (OnInsertPointPicked)
                    OnInsertPointPicked(pt);

                if (OnFinished) OnFinished();
                return true;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                ctx.overlay.Clear();
                if (OnFinished) OnFinished();
                return true;
            }

            return false;
        }
    };
}
