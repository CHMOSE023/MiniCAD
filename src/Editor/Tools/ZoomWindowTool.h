#pragma once
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/ZoomViewCommand.h"
#include "Core/Math/Point3.hpp"
#include <algorithm>
#include <cmath>

namespace MiniCAD
{
    // 窗口缩放（ZOOM E）：在屏幕上点两个角点，把这个矩形区域放大到充满视口。可撤销 / 重做。
    class ZoomWindowTool : public ITool
    {
    public:
        bool OnInput(const EditorContext& ctx) override
        {
            m_overlay = &ctx.overlay;      // 事件之外只用它：ctx 是栈对象，事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                const Math::Point3 pt = GetPoint(ctx, e);
                if (!m_hasStart)
                {
                    m_firstCorner = pt;
                    m_hasStart    = true;
                    return true;
                }

                Commit(ctx, m_firstCorner, pt);
                Finish();
                return true;
            }

            if (e.Type == InputEventType::MouseMove && m_hasStart)
            {
                ctx.overlay.Clear();
                ctx.overlay.AddRect(m_firstCorner, GetPoint(ctx, e), { 1.0, 1.0, 1.0, 1.0 });
                return false;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                Finish();
                return true;
            }

            return false;
        }

        std::string GetPrompt() const override
        {
            return m_hasStart ? "指定窗口的另一个角点 [右键/ESC 取消]:" : "指定窗口的第一个角点 [右键/ESC 取消]:";
        }

        void Cancel() override { Reset(); }
        void OnSceneChanged() override {}

    private:
        static Math::Point3 GetPoint(const EditorContext& ctx, const InputEvent& e)
        {
            if (e.HasSnap) return e.SnapWorld;
            return ctx.viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        static void Commit(const EditorContext& ctx, const Math::Point3& a, const Math::Point3& b)
        {
            const double w = std::abs(a.x - b.x), h = std::abs(a.y - b.y);
            if (w < 1e-9 && h < 1e-9)
                return;   // 两个角点重合：没有窗口

            Camera& cam = ctx.viewport.GetCamera();
            const CameraState before = cam.GetState();
            cam.ZoomToBounds(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y), 1.0);
            const CameraState after = cam.GetState();
            cam.SetState(before);   // 由命令来应用，保证撤销时回到 before
            ctx.cmdStack.Execute(std::make_unique<ZoomViewCommand>(cam, before, after), ctx.scene);
        }

        void Reset()
        {
            m_hasStart = false;
            if (m_overlay) m_overlay->Clear();
        }

        void Finish()
        {
            Reset();
            if (OnFinished) OnFinished();
        }

        Overlay*     m_overlay = nullptr;
        bool         m_hasStart = false;
        Math::Point3 m_firstCorner{};
    };
}
