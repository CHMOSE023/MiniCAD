#pragma once
#include "Editor/Tools/ITool.h"
#include "Editor/Viewport/Viewport.h"
#include "Editor/Overlay/Overlay.h"
#include "Core/Math/Point3.hpp"
#include <functional>
#include <cstdio>

namespace MiniCAD
{
    // 文字绘制工具
    // 工作流：左键点击选择插入点 → 触发 OnInsertPointPicked → OnFinished
    // 实际文字输入由 UIManager 弹出 ImGui 输入框完成
    class TextTool : public ITool
    {
    public:
        TextTool(Viewport& viewport, Overlay& overlay)
            : m_viewport(viewport)
            , m_overlay(overlay)
        {
            printf("[TextTool] 左键选择插入点 | ESC 退出\n");
        }

        ~TextTool() { printf("退出文字绘制\n"); }

        // 插入点确定后由此回调通知外部（EditorContext 设置）
        std::function<void(Math::Point3)> OnInsertPointPicked;

        bool OnInput(const InputEvent& e) override
        {
            if (e.IsLeftClick())
            {
                Math::Point3 pt = e.HasSnap
                    ? e.SnapWorld
                    : m_viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);

                if (OnInsertPointPicked)
                    OnInsertPointPicked(pt);

                if (OnFinished) OnFinished();
                return true;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                m_overlay.Clear();
                if (OnFinished) OnFinished();
                return true;
            }

            return false;
        }

    private:
        Viewport& m_viewport;
        Overlay&  m_overlay;
    };
}
