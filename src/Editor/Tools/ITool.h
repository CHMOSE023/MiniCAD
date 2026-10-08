#pragma once
#include "Editor/Input/InputEvent.h"
#include "Core/Math/Point3.hpp"
#include <functional>
#include <string>

namespace MiniCAD
{
    struct EditorContext;

    class ITool
    {
    public:
        virtual ~ITool() = default;

        virtual bool     OnInput(const EditorContext& ctx) = 0;
        virtual void     Cancel()          {}
        virtual void     OnSceneChanged()  {}                 // Undo / Redo / Delete 后
        virtual void     OnFocusLost()     {}                 // 中键平移开始
        virtual void     OnFocusRestored() {}                 // 中键平移结束
        virtual bool     HasAnchor() const { return false; }  // 是否有"锚点"

        virtual Math::Point3 GetAnchor() const { return {}; }     // 获取锚点（仅在 HasAnchor() == true 时有效）

        virtual std::string  GetPrompt() const { return {}; }     // 命令行提示（随工具状态变化）

        // 命令行 / 动态输入键入了一个纯数字：工具需要数值参数（圆角半径等）时处理并返回 true，
        // 否则返回 false，由编辑器按「直接距离输入」处理
        virtual bool         OnNumberInput(double /*value*/) { return false; }
        // 键入了 "a,b" 两个数（不带 @）：工具需要两个数值参数（倒角距离等）时处理并返回 true，
        // 否则返回 false，由编辑器按绝对坐标处理
        virtual bool         OnNumberPairInput(double /*a*/, double /*b*/) { return false; }

        std::function<void()> OnFinished;
    };
}
