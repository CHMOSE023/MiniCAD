// ============================================================
// MiniCAD — app/Tools/LineTool.cpp
// ============================================================
#include "app/Tools/LineTool.h"
#include "app/Editor.h"
#include "app/Command/AddEntityCommand.h"
#include "core/Entity/LineEntity.h"
#include "core/Object/IDAllocator.hpp"
#include "math/Point.hpp"
#include "app/Tools/ITool.h"

namespace MiniCAD {

    void LineTool::OnMouseDown(const Point2& screenPos, int button) {

        if (button != 0) return;  // 仅响应左键
        if (m_state == State::WaitingFirst) {
            m_firstPt = screenPos;
            m_state = State::WaitingSecond;
        }
        else {
            CommitLine(screenPos);
            Reset();
        }
    }

    void LineTool::OnMouseMove(const Point2& screenPos) {
        m_currentPt = screenPos;
        if (m_state == State::WaitingSecond)
            Editor::Instance().RequestRedraw();
    }

    void LineTool::OnMouseUp(const Point2&, int) {
        // 鼠标抬起不提交（需要两次点击）
    }

    void LineTool::OnKeyDown(int keyCode) {
        if (keyCode == KEY_ESCAPE) OnCancel();
    }

    void LineTool::OnCancel() { Reset(); }

    void LineTool::CommitLine(const Point2& second) {
        // 将屏幕坐标直接转为 XY 平面世界坐标（简化，z=0）
        Point3 start(m_firstPt.x, m_firstPt.y, 0.0f);
        Point3 end(second.x, second.y, 0.0f);

        auto id = IDAllocator::Instance().Allocate();
        auto entity = std::make_unique<LineEntity>(id, start, end);

        Editor::Instance().PushCommand(
            std::make_unique<AddEntityCommand>(Editor::Instance().GetScene(), std::move(entity))
        );
    }

    void LineTool::Reset() {
        m_state = State::WaitingFirst;
    }

    bool LineTool::GetPreviewLine(Point2& outStart, Point2& outEnd) const {
        if (m_state != State::WaitingSecond) return false;
        outStart = m_firstPt;
        outEnd = m_currentPt;
        return true;
    }

} // namespace MiniCAD