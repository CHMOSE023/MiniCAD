# Lesson13：命令行系统

## 学习目标

本课讲清楚 MiniCAD 命令行的完整工作流：用户如何通过键盘输入命令名、工具如何向命令行写提示、UI 如何读取并显示。完成后学习者应能看懂 `CommandLine`、`Editor::RunCommand()`、工具 `GetPrompt()` 三者的协作关系，并能新增命令别名或修改工具提示。

## 相关源码

- `src/Editor/CommandLine/CommandLine.h`
- `src/Editor/Editor.h`（`RunCommand`、`GetCommandNames`、`m_cmdBuffer`、`m_lastCommand`）
- `src/Editor/Editor.cpp`（`HandleGlobal`、`ActivateToolByAlias`）
- `src/Editor/Tools/ITool.h`（`GetPrompt()`）
- `src/UI/UIManager.cpp`（命令行面板渲染）

## 原理

MiniCAD 命令行由三部分组成：

**输入缓冲（cmdBuffer）**：`Editor::HandleGlobal()` 把 A-Z 按键追加到 `m_cmdBuffer`。按 `Enter` 或 `Space` 时调用 `RunCommand(m_cmdBuffer)` 并清空缓冲。按 `Escape` 取消工具并清空缓冲；按 `Enter` 且缓冲为空时重复上一条命令（`m_lastCommand`）。

**命令解析（RunCommand）**：`Editor::RunCommand()` 是唯一入口，负责将文本转换为操作。若文本匹配工具别名则调用 `ActivateToolByAlias()`；若工具正在等待输入（如移动距离）则将文本转交工具处理；否则回显"未知命令"。

**CommandLine 缓冲**：`CommandLine` 对象持有提示文本（`SetPrompt`）和回显历史（`Echo`，上限 500 行）。工具在 `OnInput()` 中每帧调用 `SetPrompt()` 更新当前操作提示。`Editor` 在激活工具、命令完成、错误时调用 `Echo()` 写入历史。

**UI 读取**：`UIManager` 每帧读取 `editor.GetCmdLine().Prompt()` 和 `Lines()`，用 ImGui 的滚动文本框展示，`ConsumeScrollToBottom()` 控制滚动。

## MiniCAD 中的应用

典型流程（用户按 `L + Enter`）：

```text
KeyDown 'L'   → m_cmdBuffer = "L"
KeyDown Enter → RunCommand("L")
              → ActivateToolByAlias("L")
              → ActivateToolById("Line")
              → cmdLine.Echo("Line")
              → LineTool::GetPrompt() = "指定第一个点:"
              → cmdLine.SetPrompt(...)
```

重复上一命令（`Enter` 键且缓冲为空）：

```text
KeyDown Enter → RunCommand("")
              → RunCommand(m_lastCommand)
```

## 课堂演示

1. 运行程序，按 `L + Enter`，观察命令行出现 "指定第一个点:"。
2. 点击第一个点后，提示变为 "指定下一个点:"。
3. 按 `ESC`，命令取消，命令行提示清空。
4. 直接按 `Enter`，重复上次直线命令。
5. 修改 `LineTool::GetPrompt()` 中的提示文字，重新编译后验证变化。

## 拓展练习

1. 在 `Editor::RegisterBuiltinTools()` 中新增别名 `"TR" → "Trim"`，验证命令可用。
2. 让 `Editor::GetCommandNames()` 的结果在命令行中支持 Tab 补全（思考：UI 侧如何接收候选列表）。
3. 观察 `m_lastCommand` 的赋值时机，思考：如果工具激活失败，`m_lastCommand` 应该更新吗？
