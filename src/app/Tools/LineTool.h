// ============================================================
// MiniCAD — app/Tools/LineTool.h
// 职责：两点画线工具：等待第一点→等待第二点→提交 AddEntityCommand
// 依赖：app/Tools/ITool.h, app/Editor.h
// 约束：工具不直接修改 Scene；结束时调用 Editor::PushCommand()
// ============================================================
#pragma once

#include "ITool.h"
#include "math/Point.hpp"
#include <string>

namespace MiniCAD {

    class LineTool : public ITool {
    public:
        LineTool() = default;

        void OnMouseDown(const Point2& screenPos, int button) override;
        void OnMouseMove(const Point2& screenPos) override;
        void OnMouseUp(const Point2& screenPos, int button) override;
        void OnKeyDown(int keyCode) override;
        void OnCancel() override;
        std::string GetName() const override { return "LineTool"; }

        // ── 预览线查询（供 SceneRenderer 在每帧读取）────────────────
        // 仅在 WaitingSecond 阶段返回 true，此时 outStart/outEnd 有效
        bool GetPreviewLine(Point2& outStart, Point2& outEnd) const;

    private:
        enum class State { WaitingFirst, WaitingSecond };
        State  m_state = State::WaitingFirst;
        Point2 m_firstPt;
        Point2 m_currentPt;

        void CommitLine(const Point2& second);
        void Reset();
    };

} // namespace MiniCAD