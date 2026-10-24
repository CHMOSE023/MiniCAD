#pragma once
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"

namespace MiniCAD
{
    // 实时平移（PAN）：按住左键拖动平移视图，右键 / ESC / 回车结束。
    // 与中键拖动一样只改相机，不入撤销栈
    class PanTool : public ITool
    {
    public:
        bool OnInput(const EditorContext& ctx) override
        {
            const auto& e = ctx.event;

            if (e.Type == InputEventType::MouseMove && e.IsMouseButtonDown(MouseButton::Left))
            {
                ctx.viewport.Pan(static_cast<float>(e.MouseX - e.LastMouseX), static_cast<float>(e.MouseY - e.LastMouseY));
                return true;
            }

            if (e.IsLeftClick())
                return true;        // 按下只是开始拖动，不拾取、不框选

            if (e.IsRightClick() || e.IsCancel() || e.IsKeyPressed(KeyCode::Enter))
            {
                if (OnFinished) OnFinished();
                return true;
            }

            return false;
        }

        std::string GetPrompt() const override
        {
            return "按住左键拖动平移视图 [右键/ESC/回车 退出]:";
        }

        void Cancel() override {}
        void OnSceneChanged() override {}
        bool HidesCrosshair() const override { return true; }     // 宿主显示手形光标
    };
}
