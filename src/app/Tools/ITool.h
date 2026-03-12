// ============================================================
// MiniCAD — app/Tools/ITool.h
// 职责：工具接口：OnMouseDown/Move/Up/KeyDown/Cancel/GetName
// 依赖：math/Point.h
// 约束：工具不直接修改 Scene；结束时调用 Editor::ExecuteCommand()
// ============================================================
#pragma once

#include "math/Point.hpp"
#include <string>

namespace MiniCAD {

class ITool {
public:
    virtual ~ITool() = default;
    virtual void OnMouseDown(const Point2& screenPos, int button) = 0;
    virtual void OnMouseMove(const Point2& screenPos) = 0;
    virtual void OnMouseUp(const Point2& screenPos, int button) = 0;
    virtual void OnKeyDown(int keyCode) = 0;
    virtual void OnCancel() = 0;          // Esc 键取消当前操作
    virtual std::string GetName() const = 0;
};

// 键码常量
constexpr int KEY_ESCAPE = 27;
constexpr int KEY_ENTER  = 13;
constexpr int KEY_DELETE = 46;

} // namespace MiniCAD
