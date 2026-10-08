#pragma once
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Core/Math/Point3.hpp"
#include <functional>
#include "Core/Log.h"

namespace MiniCAD
{
    class TextTool : public ITool
    {
    public:
        TextTool()
        {
            LOG_DEBUG("[TextTool] 左键选择插入点 | ESC 退出");
        }

        ~TextTool() { LOG_DEBUG("退出文字绘制"); }

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

        std::string GetPrompt() const override
        {
            return "指定文字插入点 [ESC 退出]:";
        }
    };
}
