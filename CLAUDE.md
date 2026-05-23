# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目简介

MiniCAD 是一个轻量级跨平台二维 CAD 编辑器框架（C++20），目标平台为 Windows 桌面端（D3D11）和 WebAssembly（WebGL 2.0）。代码采用严格的分层架构，每一层都有明确的访问规则。

## 构建命令

### 桌面端（Windows）

```bash
# 配置（Visual Studio 2022）
cmake -S . -B out/win -G "Visual Studio 17 2022"

# 编译 Debug
cmake --build out/win --config Debug

# 编译 Release
cmake --build out/win --config Release

# 运行
out/win/MiniCAD/MiniCAD.exe
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

CMake 选项 `MINICAD_WEB_DEBUG=ON` 可启用 WASM 调试构建（`-g3 -O0`、ASSERTIONS=2、SAFE_HEAP）。

**无测试框架**：项目暂无单元测试。验证靠编译 + 手动运行。

## 源文件组织（平台隔离）

平台专属文件放在 `Win/` 或 `Web/` 子目录，CMakeLists.txt 按目录显式列举，无 REGEX 排除。

```
src/App/
    Win/            # 桌面入口：Main.cpp、MainWindow.cpp
    Web/            # WASM 入口：WebMain.cpp
src/Editor/Input/
    Win/            # Win32 输入系统：InputSystem、KeyCodeUtils、ViewportInputAdapter
    *.h             # 输入接口头文件（跨平台，原地不动）
src/Render/
    D3D11/          # 桌面渲染后端
    WebGL/          # WASM 渲染后端
```

CMakeLists.txt 中 `MINICAD_EXPORTS` 是 CMake 列表变量，通过 `list(JOIN ...)` 拼成 `sEXPORTED_FUNCTIONS` 字符串，新增导出函数只需在列表加一行。

## 核心架构（分层 + 三模块）

| 层 | 职责 |
|---|---|
| **Core** | 纯计算库，无 I/O 无副作用：Math、Geom、Entity 定义 |
| **Scene** | 只读运行时快照，持有 EntityDatabase、LayerManager |
| **Document** | 数据库内核，唯一的 Scene 写入口，持有 CommandStack |
| **Viewport** | 视图层：Camera、Grid、Axis、Cursor，接收 ViewState 并驱动 IRenderer |
| **Editor** | 交互层：工具系统、输入处理、吸附、拾取、Grip 编辑、约束、顶点缓冲 |
| **Render** | D3D11（Windows）和 WebGL（WASM）后端，各自实现 IRenderer |
| **Text** | TTF/SHX 字体、字形缓存、虚拟机执行、排版引擎 |
| **UI** | ImGui 界面层：菜单栏、工具栏、文档选项卡、状态栏（仅桌面） |
| **App** | 启动层，仅做组装，无业务逻辑 |

### 层间访问规则（严格执行）

**核心不变量：Scene 对 Document 以外的所有层都只读。所有修改必须经过 `ICommand` → `CommandStack`。**

| 层 | 可访问 | 禁止 |
|---|---|---|
| UI | DocumentManager（读）、Editor（读选择集/活跃工具） | 写 Scene |
| Editor | Document（只读 Scene）、Core | 直接写 Scene |
| Document | Scene（写）、Core | Editor、Render、UI |

### 三模块运行时分离

- **Document**（纯数据）：持有 Scene、CommandStack、名称/路径/脏标记、FontSystem 指针。不依赖 Editor 或 Viewport。
- **Viewport**（视图与渲染）：持有 Camera、Grid、Axis、Cursor、RenderTarget。接收不可变的 `ViewState` 快照，调用 `IRenderer` 绘制。
- **Editor**（交互）：持有工具系统、Picking、Snap、Overlay、GripEditor、ConstraintEngine、Resolver、**四组顶点缓冲**（scene/text/overlay/grip）。通过 `Bind(Document&, Viewport&)` / `Unbind()` 绑定。

**DocumentManager** 拥有唯一的 `Editor` 实例和 `Viewport`。切换文档时调用 `Editor::Unbind()` + `Editor::Bind(newDoc, viewport)`。

**延迟绑定模式**：Overlay、Picking、GripEditor 使用默认构造函数 + `Bind()` 指针成员，因为构造时 Document/Viewport 尚不可用。

## 实体类型系统

`Object`（根）→ `Entity`（基类）→ 10 个具体实体：Line、Point、Circle、Arc、Ellipse、Rectangle、Polyline、Spline、Text、MText。

- **运行时类型**：`DECLARE_RUNTIME_TYPE` 宏，`static const RuntimeTypeInfo TypeInfo`，`IsKindOf<T>()` 类型判断
- **绘制接口**：`Draw(IDrawSink&, isSelected, isHovered)` 向 sink 发射几何，不直接依赖渲染器
- **克隆**：`Clone(newId)` 用于复制操作

## Command 模式

`ICommand` 接口：`Execute(Scene&) → bool`、`Undo(Scene&)`、`GetName() → string`。

- `Execute()` 返回 false 时命令不入栈（校验失败）
- **所有权语义**：Execute 后 Scene 持有实体；Undo 后 Command 回收实体所有权
- `Push()` 仅入栈不执行（用于拖拽等已发生的操作）

## 工具系统

`ITool` 接口：`OnInput(InputContext&) → bool`、`Cancel()`、`OnSceneChanged()`、`OnFocusLost/Restored()`、`HasAnchor() → bool`、`GetAnchor() → Point3`。

- **有状态**：工具跨帧累积输入（如画线收集点位）
- `OnInput()` 返回 true 表示事件已消费；`OnFinished` 回调通知工具完成
- `OnSceneChanged()` 在 Undo/Redo/Delete 后调用以重置工具状态
- **注册**：`Editor::RegisterTool(toolId, factory)`，`RegisterAlias(alias, toolId)` 支持键盘别名（"L" → "Line"）
- Tools 全部是 header-only（`.h`），无对应 `.cpp`

## 输入处理管线（Resolver）

`Editor::OnInput()` 每帧构建 `InputContext`，交给 `Resolver` 产出 `ResolvedInput`：

1. **Raw**：屏幕坐标 → 世界坐标（Camera）
2. **Snap**：SnapEngine（端点、中点、最近点、象限点、网格）
3. **Constraint**：OrthoConstraint / PolarConstraint（需要锚点，来自 Tool 或 Grip）
4. **Picking**：在**未约束**坐标上做 HitTest
5. **输出**：`ResolvedInput`（hasPoint、hasSnap、hasConstraint、pickedObject）

## Grip 编辑

`GripEditor` 管理实体控制点拖拽（CAD 夹点流程）：

- MouseDown 命中 → 待激活 → MouseUp 确认激活 → MouseMove 实时跟随 → MouseDown 左键确认 / 右键取消
- `IEntityGripHandler`：按 `RuntimeTypeInfo` 注册，每种实体类型一个处理器

## 渲染管线

1. `Entity::Draw(IDrawSink&)` 发射几何到 `DrawContext`（`src/Document/DrawContext.hpp`）
2. `DrawContext` 路由到四组缓冲：`m_verts`（线段）、`m_textVerts`（文字纹理四边形）、`m_overlay`（预览）
3. `Editor::BuildViewState()` 将四组缓冲封装为 `ViewState`（用 `std::span` 零拷贝）
4. `Viewport::Render(ViewState)` 调用 `IRenderer::Submit()` / `SubmitTextured()` 完成绘制

**两条文字渲染路径**：
- `EmitText`（纹理）：`GlyphProvider` 回调查询 ImGui/WebFontAtlas 字形 UV，生成纹理四边形
- `EmitMText`（矢量）：`FontResolver` 回调获取 `IFont*`（SHX/TTF），经 `TextLayoutEngine` 排版输出线段

**脏标记优化**：`UpdateSceneVertices()` 仅在 `scene.IsDirty() || picking.IsDirty()` 时重建顶点，静态场景跨帧复用缓冲容量。

## 关键入口文件

- **桌面端** — `src/App/Win/Main.cpp`
- **WASM** — `src/App/Web/WebMain.cpp`（导出 `_MiniCAD_*` C 函数，由 JS `ccall` 调用）
- **文档根对象** — `src/Document/Document.h`（Scene、CommandStack、名称/路径/脏标记）
- **编辑器** — `src/Editor/Editor.h`（工具管理、选择、输入分派、四组顶点缓冲、Bind/Unbind）
- **文档管理器** — `src/Document/DocumentManager.h`（拥有唯一 Editor + Viewport，多文档切换）
- **输入上下文** — `src/Editor/Input/InputContext.h`（打包一帧输入所需的所有引用）
- **渲染快照** — `src/Viewport/ViewState.h`（std::span 引用 Editor 顶点缓冲，零拷贝传递给 Viewport）
- **绘制汇聚点** — `src/Document/DrawContext.hpp`（IDrawSink 实现）
- **渲染器接口** — `src/Render/IRenderer.h`
- **UI 管理器** — `src/UI/UIManager.h`（菜单、工具栏、文档选项卡，仅桌面）

## Web 端注意事项

- CMake 通过 `EMSCRIPTEN` 宏区分平台；平台专属源文件通过目录隔离，无 REGEX 过滤
- WASM 不编译 `Editor/Input/Win/` 下的三个 Win32 输入文件
- WASM 构建定义预处理宏 `MINICAD_WEB`

## C++ 规范

- **MSVC 编译选项** — `/utf-8`、`/UNICODE`、`NOMINMAX`、`WIN32_LEAN_AND_MEAN`
- **头文件根路径** — `src/` 和 `3rd/`，所有 `#include` 使用相对于这两个根的路径（含移入子目录的 `.cpp` 文件）
- **第三方库** — ImGui（桌面用 Win32 + D3D11 后端）、stb 头文件库
