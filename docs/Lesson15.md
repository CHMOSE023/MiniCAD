# Lesson15：综合实战：扩展一个 MiniCAD 功能

## 学习目标

本课把前 11 课串起来，完成一个小型扩展。学习者应能按照 MiniCAD 的现有架构添加命令、工具或 UI 信息，而不是绕过命令栈或直接修改渲染层。

## 相关源码

- `src/Editor/Editor.cpp`
- `src/Editor/Tools/ITool.h`
- `src/Editor/Tools/RectangleTool.h`
- `src/Core/Entity/RectangleEntity.hpp`
- `src/Document/Command/AddEntityCommand.h`
- `src/Document/CommandStack/CommandStack.cpp`
- `src/UI/UIManager.cpp`

## 原理

MiniCAD 的扩展路径应遵守已有链路：

```text
输入事件 -> Editor -> Tool -> Command -> Scene -> Entity::Draw -> Viewport -> Renderer
```

新功能要放在正确层级。命令别名属于 `Editor`；绘图交互属于 `Tool`；图形数据属于 `Core/Entity`；场景变更必须通过 `CommandStack`；UI 只负责展示和触发，不应直接改写场景对象。

这样做的好处是，新功能能自然获得撤销/重做、选择、预览、跨平台渲染和 dirty 刷新能力。

## MiniCAD 中的应用

### 实战一：添加命令别名

命令别名由 `Editor::RegisterAlias()` 管理，最终映射到工具 ID。用户输入字母后，`Editor::HandleGlobal()` 把字符加入 `m_cmdBuffer`，按 Enter 或 Space 后调用 `ActivateToolByAlias()`。

在 `Editor::RegisterBuiltinTools()` 中添加：

```cpp
RegisterAlias("R", "Rectangle");
```

然后运行程序，输入 `R` 并回车，验证是否启动矩形工具。

### 实战二：添加一个简单绘图工具

一个新工具通常需要：

1. 继承 `ITool`。
2. 在 `OnInput()` 中维护点击状态。
3. 用 `Overlay` 显示临时预览。
4. 用实体和命令提交最终结果。
5. 在 `Editor::RegisterBuiltinTools()` 中注册。

可以实现一个简化版“两点矩形工具”。项目中已有 `RectangleTool.h`，初学者可以先阅读它，再尝试做一个变体：第一次左键记录角点，鼠标移动显示预览矩形，第二次左键创建 `RectangleEntity`。

核心路线：

```text
TwoPointRectangleTool
-> OnInput()
-> overlay.AddLine() 预览四条边
-> RectangleEntity
-> AddEntityCommand
-> CommandStack::Execute()
```

### 实战三：显示选中对象信息

选择集由 `Editor::GetSelection()` 提供，主选中对象可以通过 `Editor::GetPrimarySelectedObject()` 获取。UI 层可以读取这些信息并显示，不应直接修改场景。

在 `UIManager::DrawStatusBar()` 中增加选中对象数量：

```cpp
auto count = dm.GetEditor().GetSelection().size();
ImGui::Text("Selected: %zu", count);
```

如果需要显示对象类型，可以对 `GetPrimarySelectedObject()` 返回值使用运行时类型系统判断。

## 课堂演示

推荐课堂上完成实战一，因为它改动小、反馈直接：

1. 打开 `src/Editor/Editor.cpp`。
2. 找到 `Editor::RegisterBuiltinTools()`。
3. 添加 `RegisterAlias("R", "Rectangle");`。
4. 构建并运行。
5. 在命令输入中按 `R`，再按 Enter。
6. 验证矩形工具是否启动。

如果时间充足，再演示在状态栏显示选择数量，帮助学习者理解 UI 读取 `Editor` 状态的方式。

## 拓展练习

1. 为常用命令建立一组更接近 AutoCAD 习惯的别名，并同步检查工具栏 tooltip 是否一致。
2. 给两点矩形工具加入 Shift 键约束，让矩形变成正方形。
3. 增加一个属性面板，显示对象 ID、图层 ID、实体类型和包围盒。
4. 为属性修改设计命令对象，确保修改颜色或图层时支持撤销/重做。

## 综合检查清单

- 是否遵守现有模块边界？
- 是否通过 `CommandStack` 修改场景？
- 是否调用 `Scene::MarkDirty()` 或通过命令触发 dirty？
- 是否支持撤销/重做？
- 是否兼容 Windows 和 Web 的共享核心？
- 是否避免直接修改 `3rd/` 中的第三方代码？
