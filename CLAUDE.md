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

需要 Emscripten SDK（路径 `D:\dev\emsdk\`）和 Ninja（VS 2022 自带）。若环境不同，修改 `build_web.bat` 顶部的路径配置。

```bat
build_web.bat              # 增量构建
build_web.bat configure    # 重新生成 CMake 工程（会清空 out/web）
build_web.bat clean        # 清理后全量重建
build_web.bat serve        # 构建并在 http://localhost:8080 启动本地服务器
```

输出：`out/web/MiniCADWeb/index.html`。重新构建后浏览器需强制刷新（`Ctrl+F5`）以清除缓存。
 

### 层间访问规则（严格执行）

| 层 | 可访问 | 不可访问 |
|---|---|---|
| App | 所有层（仅做组装） | 不含业务逻辑 |
| Editor | DocumentManager多文档（通过 Document 只读 Scene）、Core | 不可直接写 Scene |
| DocumentManager |Document Scene（写）、Core | Editor、Render |
| Scene | Core | Document、Editor、Render |
| Core | 无 | 所有其他层 | 

**Scene 对 Document 以外的所有层只读。** 所有修改必须经过 `ICommand` → `CommandStack`，这是整个系统的核心不变量。
 

### 数据流示例：画一条线

1. 鼠标点击 → `ViewportInputAdapter` → `InputEvent`
2. `EditorContext.OnInput()` → 当前激活的 `LineTool`
3. `LineTool` 累积点位 → 经 `CommandBridge` 产生意图
4. `CommandTranslator` → `CreateEntityCommand`
5. `TwoPhaseExecutor`：`prepare()` 校验 → `commit()` 写入 `EntityDatabase`
6. `DirtyTracker` 标记实体变脏 → `DirtyPropagator` 触发 `RenderBuilder`
7. `RenderBuilder` 将实体转换为 `RenderEntity` 写入 back buffer
8. 帧末：`RenderSyncBuffer` 交换 → `IRenderer.submit()` 提交绘制

  
  
### 关键入口文件：

- `src/App/Main.cpp` — 桌面端入口
- `src/App/WebMain.cpp` — WASM 入口，通过 `ccall` 导出 `_MiniCAD_*` C 函数
- `src/Document/Document.h` — 根对象，持有 Scene、CommandStack、EventBus、DirtyTracker
- `src/Editor/Context/EditorContext.h` — 当前激活工具、选择集、图层状态
- `src/Render/Core/IRenderer.h` — 所有渲染后端实现的接口

## Web 端注意事项

- CMake 以 `EMSCRIPTEN` 宏区分桌面和 Web 源文件集。
- 以下桌面输入文件在 WASM 构建中被排除：`InputSystem.cpp`、`KeyCodeUtils.cpp`、`ViewportInputAdapter.cpp`。
- 导出的 C 函数遵循 `_MiniCAD_*` 命名规范，由 JavaScript 通过 `ccall` 调用。
- Emscripten 默认无线程支持，依赖 `std::thread` 的代码需要额外配置。
- WASM 构建统一定义预处理宏 `MINICAD_WEB`。

## C++ 规范
 
- MSVC 编译选项：`/utf-8`、`UNICODE`、`NOMINMAX`、`WIN32_LEAN_AND_MEAN`
- 头文件包含根路径：`src/` 和 `thirdParty/`，使用相对于这两个根的路径
- 第三方库：ImGui（桌面端使用 Win32 + D3D11 后端）、stb 头文件库

## 文件和目录

- CMakeLists.txt 包括编译的所有文件
- src文件下为准，README.md 文件夹目录和项目介绍不准确
- WASM 构建 Web入口 web/index.html ；src/App/WebMain.cpp 
