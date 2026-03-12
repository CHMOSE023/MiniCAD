// ============================================================
// MiniCAD — doc/UndoRedo/ICommand.h
// 职责：命令接口：Execute / Undo / Redo / GetDescription
// 依赖：无
// 约束：不依赖 core/ render/ ui/；不感知具体命令类型
// ============================================================
#pragma once

#include <string>

namespace MiniCAD {

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void Execute() = 0;
    virtual void Undo() = 0;
    virtual void Redo() { Execute(); }  // 默认 Redo = Execute，子类可覆盖
    virtual std::string GetDescription() const = 0;
};

} // namespace MiniCAD
