# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目简介

MiniCAD 是一个轻量级跨平台二维 CAD 编辑器框架（C++20），目标平台为 Windows 桌面端（D3D11）和 WebAssembly（WebGL 2.0）。代码采用严格的分层架构，每一层都有明确的访问规则。

## 核心架构

代码分为八层，从底到顶：

- **Core** — 纯计算库，无 I/O 无副作用：Math（向量、矩阵）、Geom（几何基元）
- **Scene** — 只读运行时快照，持有 EntityDatabase、LayerManager
- **Document** — 数据库内核，唯一的 Scene 写入口，持有 CommandStack
- **Editor** — 交互层，包含工具系统、输入处理、吸附、拾取、Grip 编辑、视口系统
- **Render** — 渲染层，D3D11（Windows）和 WebGL（WASM）后端
- **Text** — 文本与字体系统：TTF/SHX 字体、字形缓存、虚拟机执行、排版引擎
- **UI** — ImGui 界面层，包含菜单栏、工具栏、文档选项卡、状态栏、工具图标管理
- **App** — 启动层，仅做组装，无业务逻辑

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

## 层间访问规则（严格执行）

| 层 | 可访问 | 不可访问 |
|---|---|---|
| App | 所有层（仅做组装） | 不含业务逻辑 |
| UI | DocumentManager（读）、Editor（读选择集、活跃工具）、Document（读） | 不可写 Scene |
| Editor | DocumentManager（通过 Document 只读 Scene）、Core | 不可直接写 Scene |
| Document | Scene（写）、Core | Editor、Render、UI |
| Scene | Core | Document、Editor、Render、UI |
| Core | 无 | 所有其他层 |

**核心不变量：Scene 对 Document 以外的所有层都只读。所有修改必须经过 `ICommand` → `CommandStack`。**

## 数据流示例：画一条线

1. 鼠标点击 → `ViewportInputAdapter` → `InputEvent`
2. `EditorContext.OnInput()` → 当前激活的 `LineTool`
3. `LineTool` 累积点位 → 产生 `CreateEntityCommand`
4. Command 执行：`prepare()` 校验 → `commit()` 写入 `EntityDatabase`
5. `DirtyTracker` 标记实体变脏 → 触发 `RenderBuilder`
6. `RenderBuilder` 将实体转换为 `RenderEntity` 写入 back buffer
7. 帧末：`RenderSyncBuffer` 交换 → `IRenderer.submit()` 提交绘制

## 关键入口文件

- **桌面端** — `src/App/Main.cpp`
- **WASM** — `src/App/WebMain.cpp`（通过 `ccall` 导出 `_MiniCAD_*` C 函数）
- **文档根对象** — `src/Document/Document.h`（持有 Scene、CommandStack、EventBus、DirtyTracker）
- **编辑器上下文** — `src/Editor/Context/EditorContext.h`（当前工具、选择集、图层状态）
- **UI 管理器** — `src/UI/UIManager.h`（菜单、工具栏、文档选项卡、获取活跃工具、同步字体）
- **渲染器接口** — `src/Render/IRenderer.h`（所有渲染后端实现的接口）

## Web 端注意事项

- CMake 通过 `EMSCRIPTEN` 宏区分桌面和 Web 源文件集
- WASM 构建中被排除的文件：`Editor/Input/InputSystem.cpp`、`Editor/Input/KeyCodeUtils.cpp`、`Editor/Input/ViewportInputAdapter.cpp`
- 导出的 C 函数遵循 `_MiniCAD_*` 命名规范（如 `_MiniCAD_StartLine`、`_MiniCAD_Undo` 等），由 JavaScript 通过 `ccall` 调用
- Emscripten 默认无线程支持，依赖 `std::thread` 的代码需要额外配置
- WASM 构建统一定义预处理宏 `MINICAD_WEB`
- 编译链接选项启用异常、完整调试信息、内存安全检查

## C++ 规范

- **MSVC 编译选项** — `/utf-8`、`/UNICODE`、`NOMINMAX`、`WIN32_LEAN_AND_MEAN`
- **头文件根路径** — `src/` 和 `3rd/`，使用相对于这两个根的包含路径
- **第三方库** — ImGui（Windows 使用 Win32 + D3D11 后端）、stb 头文件库
- **CMakeLists.txt** 是编译配置的唯一来源，src/ 下以代码为准，README.md 目录结构可能不同步

## 关键文件位置速查

- **UI 界面** — `src/UI/`（UIManager、ImGuiLayer、工具栏、菜单栏、文档选项卡、状态栏）
- 工具系统 — `src/Editor/Tools/`（8 个绘制工具 + 4 个修改工具）
- 几何基元 — `src/Core/Geom/`（Point, Line, Arc, Circle, Ellipse, Polyline, Spline, AABB）
- Command 模式 — `src/Document/Command/`（ICommand、CommandStack、各种具体 Command）
- 字体系统 — `src/Text/Font/`（TTF、SHX、SHX 虚拟机、字形缓存）
- 输入系统 — `src/Editor/Input/`（事件、键码、吸附引擎）
- 吸附与拾取 — `src/Editor/Snap/`、`src/Editor/Picking/`
- Grip 编辑 — `src/Editor/Grip/`（各实体类型的 Grip 处理）
- 视口与渲染 — `src/Editor/Viewport/`（Camera、Grid、Gizmo、Cursor、Axis）、`src/Render/D3D11/`、`src/Render/WebGL/`
