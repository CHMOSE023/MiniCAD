# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目简介

MiniCAD 是一个轻量级跨平台二维 CAD 编辑器框架（C++20），目标平台为 Windows 桌面端（D3D11）和 WebAssembly（WebGL 2.0）。代码采用严格的分层架构，每一层都有明确的访问规则。

## 构建命令

### 桌面端（Windows）

```bash
# 配置（Visual Studio 2022）
cmake -S . -B out/desktop -G "Visual Studio 17 2022"

# 编译 Debug
cmake --build out/desktop --config Debug

# 编译 Release  
cmake --build out/desktop --config Release

# 运行
out/desktop/MiniCAD/MiniCAD.exe
```

### WebAssembly

需要 Emscripten SDK 和 Ninja。脚本 `build_web.bat` 自动管理环境配置。

```bat
build_web.bat              # 增量构建
build_web.bat configure    # 重新生成 CMake 工程（会清空 out/web）
build_web.bat clean        # 清理后全量重建
build_web.bat serve        # 构建并在 http://localhost:8080 启动服务器
```

输出：`out/web/MiniCADWeb/index.html`。重新构建后需强制刷新浏览器（`Ctrl+F5`）清除缓存。

**无测试框架**：项目暂无单元测试。验证靠编译 + 手动运行。

## 核心架构（八层）

| 层 | 职责 |
|---|---|
| **Core** | 纯计算库，无 I/O 无副作用：Math（向量、矩阵）、Geom（几何基元）、Entity（实体定义） |
| **Scene** | 只读运行时快照，持有 EntityDatabase、LayerManager |
| **Document** | 数据库内核，唯一的 Scene 写入口，持有 CommandStack |
| **Editor** | 交互层：工具系统、输入处理、吸附、拾取、Grip 编辑、约束、视口 |
| **Render** | 渲染层：D3D11（Windows）和 WebGL（WASM）后端 |
| **Text** | 文本与字体系统：TTF/SHX 字体、字形缓存、虚拟机执行、排版引擎 |
| **UI** | ImGui 界面层：菜单栏、工具栏、文档选项卡、状态栏 |
| **App** | 启动层，仅做组装，无业务逻辑 |

### 层间访问规则（严格执行）

| 层 | 可访问 | 不可访问 |
|---|---|---|
| App | 所有层（仅做组装） | 不含业务逻辑 |
| UI | DocumentManager（读）、Editor（读选择集、活跃工具）、Document（读） | 不可写 Scene |
| Editor | DocumentManager（通过 Document 只读 Scene）、Core | 不可直接写 Scene |
| Document | Scene（写）、Core | Editor、Render、UI |
| Scene | Core | Document、Editor、Render、UI |
| Core | 无 | 所有其他层 |

**核心不变量：Scene 对 Document 以外的所有层都只读。所有修改必须经过 `ICommand` → `CommandStack`。**

## 实体类型系统

`Object`（根）→ `Entity`（基类）→ 10 个具体实体：Line、Point、Circle、Arc、Ellipse、Rectangle、Polyline、Spline、Text、MText。

- **运行时类型**：`DECLARE_RUNTIME_TYPE` 宏提供编译期 RTTI，每个实体类有 `static const RuntimeTypeInfo TypeInfo`，可通过 `IsKindOf<T>()` 做类型判断
- **实体属性**：所有实体持有 `EntityAttr`（Color、LayerID、LineType、LineWidth、Visible）
- **绘制接口**：实体通过 `Draw(IDrawSink&, isSelected, isHovered)` 向 sink 发射几何（`DrawLine`）或文字（`EmitText`/`EmitMText`），不直接依赖渲染器
- **克隆**：`Clone(newId)` 用于复制操作

## Command 模式

`ICommand` 接口：`Execute(Scene&) → bool`、`Undo(Scene&)`、`GetName() → string`。

- `Execute()` 返回 false 时命令不入栈（校验失败）
- **所有权语义**：Execute 后 Scene 持有实体；Undo 后 Command 回收实体所有权
- `CommandStack` 维护独立的 undo/redo 栈；执行新命令时清空 redo 栈
- `Push()` 仅入栈不执行（用于拖拽等已发生的操作）

## 工具系统

`ITool` 接口：`OnInput(InputEvent&) → bool`、`Cancel()`、`OnSceneChanged()`、`OnFocusLost/Restored()`、`HasAnchor() → bool`、`GetAnchor() → Point3`。

- **有状态**：工具跨帧累积输入（如画线收集点位）
- `OnInput()` 返回 true 表示事件已消费
- `OnFinished` 回调通知工具完成（右键、ESC）
- `OnSceneChanged()` 在 Undo/Redo/Delete 后被调用以重置工具状态
- **注册机制**：`EditorContext::RegisterTool(toolId, factory)` 注册工厂函数，`RegisterAlias(alias, toolId)` 支持键盘快捷别名（如 "L" → "Line"）

## 输入处理管线（Resolver）

`EditorContext` 通过 `Resolver` 将原始输入转化为语义化结果：

1. **Raw**：屏幕坐标 → 世界坐标（通过 Camera）
2. **Snap**：查询 SnapEngine（支持端点、中点、最近点、象限点、网格吸附）
3. **Constraint**：应用正交/极轴约束（需要锚点，来自 Tool 或 Grip）
4. **Picking**：在原始坐标上做 HitTest（必须用未约束的坐标）
5. **输出**：`ResolvedInput` 结构体，含 `hasPoint`、`hasSnap`、`hasConstraint`、`pickedObject`

`InputContext` 打包所有输入相关状态（InputEvent、Scene、Viewport、SnapEngine、ConstraintEngine、Picking、ITool*、GripEditor*）。

## Grip 编辑

`GripEditor` 管理实体控制点拖拽：

- MouseDown on grip → 待激活 → MouseUp 确认激活 → MouseMove 实时跟随 → MouseDown(左键) 确认 / MouseDown(右键) 取消
- `IEntityGripHandler`：按 RuntimeTypeInfo 注册的实体类型特定处理器

## 约束系统

`ConstraintEngine` 管理互斥的 `OrthoConstraint`（轴对齐）和 `PolarConstraint`（角度约束）。`IConstraint::Apply(ConstraintContext, ConstraintResult&) → bool`，结果包含约束点和引导线。

## 渲染管线

1. `Entity::Draw(IDrawSink&)` 发射几何到 `DrawContext`（IDrawSink 实现）
2. `DrawContext` 路由到 `m_verts`（场景线段，`Vertex_P3_C4`）、`m_textVerts`（文字纹理四边形，`Vertex_P3_C4_UV`）、`m_overlay`（临时预览）
3. `IRenderer::Submit()` 提交线段，`SubmitTextured()` 提交文字
4. D3D11/WebGL 后端各自实现 IRenderer

**Overlay**：工具预览等临时几何通过 `overlay.AddLine()`/`AddPoint()` 绘制，不进入 Scene。

## 数据流示例：画一条线

1. 鼠标点击 → `ViewportInputAdapter` → `InputEvent`
2. `EditorContext.OnInput()` → `Resolver` 产生 `ResolvedInput` → 分派到当前 `LineTool`
3. `LineTool` 累积点位 → 产生 `AddEntityCommand`
4. Command 执行：`Execute()` 写入 `EntityDatabase`
5. `Scene::MarkDirty()` → 触发重绘
6. `DrawContext` 收集所有实体几何 → `IRenderer.Submit()` 提交绘制

## 关键入口文件

- **桌面端** — `src/App/Main.cpp`
- **WASM** — `src/App/WebMain.cpp`（通过 `ccall` 导出 `_MiniCAD_*` C 函数）
- **文档根对象** — `src/Document/Document.h`（持有 Scene、CommandStack、EventBus、DirtyTracker）
- **编辑器上下文** — `src/Editor/Context/EditorContext.h`（工具管理、选择、输入分派）
- **UI 管理器** — `src/UI/UIManager.h`（菜单、工具栏、文档选项卡）
- **渲染器接口** — `src/Render/IRenderer.h`

## Web 端注意事项

- CMake 通过 `EMSCRIPTEN` 宏区分桌面和 Web 源文件集
- WASM 构建中被排除的文件：`Editor/Input/InputSystem.cpp`、`Editor/Input/KeyCodeUtils.cpp`、`Editor/Input/ViewportInputAdapter.cpp`
- 导出的 C 函数遵循 `_MiniCAD_*` 命名规范，由 JavaScript 通过 `ccall` 调用
- WASM 构建统一定义预处理宏 `MINICAD_WEB`

## C++ 规范

- **MSVC 编译选项** — `/utf-8`、`/UNICODE`、`NOMINMAX`、`WIN32_LEAN_AND_MEAN`
- **头文件根路径** — `src/` 和 `3rd/`，使用相对于这两个根的包含路径
- **第三方库** — ImGui（Windows 使用 Win32 + D3D11 后端）、stb 头文件库
- **CMakeLists.txt** 是编译配置的唯一来源，src/ 下以代码为准
