# Lesson01：项目能跑起来

## 学习目标

能在本机把 MiniCAD 跑起来，画一条线并撤销。同时理解为什么一份代码能同时构建出 Windows 桌面版和浏览器 WebAssembly 版。

---

## 前置要求

开始之前确认本机已安装：

| 工具 | 用途 | 最低版本 |
|---|---|---|
| Visual Studio | C++ 编译器 + IDE | 2022（含 C++ 工作负载） |
| CMake | 构建系统 | 3.20 |
| Emscripten SDK（emsdk） | 编译 WebAssembly | 3.x |
| Ninja | Web 构建用 | 1.10 |
| Python 3 | `build_web.bat` 脚本依赖 | 3.8 |

Windows 桌面版只需要 Visual Studio + CMake，不需要 Emscripten。

---

## 第一步：构建 Windows 桌面版

**1. 生成 Visual Studio 工程**

在项目根目录执行：

```powershell
cmake -S . -B out/win -G "Visual Studio 17 2022"
```

这一步不编译代码，只生成 `.sln` 和 `.vcxproj`，输出到 `out/win/`。

**2. 编译**

```powershell
cmake --build out/win --config Release
```

也可以直接用 Visual Studio 打开 `out/win/MiniCAD.sln` 编译。

**3. 运行**

```powershell
out/win/MiniCAD/Release/MiniCAD.exe
```

程序启动后应看到带工具栏的 CAD 界面。如果提示找不到字体或 shader 文件，说明 assets 没有复制过去——检查 `out/win/MiniCAD/Release/` 目录下是否有 `fonts/`、`shader/` 子目录。

> **assets 从哪里来**：CMakeLists.txt 通过 `add_custom_command` 在编译后自动把 `assets/` 复制到可执行文件旁边。首次构建就会复制，重新构建不会重复复制（除非 assets 内容有变化）。

---

## 第二步：构建 Web 版

**1. 增量构建并启动本地服务器**

```bat
build_web.bat serve
```

脚本会自动激活 emsdk 环境、调用 CMake + Ninja 编译、然后在 `http://localhost:8080` 启动 HTTP 服务器。

第一次运行会比较慢（完整编译所有源文件）。后续只修改了少量文件时，增量构建只重新编译有变化的部分。

**2. 在浏览器打开**

Chrome / Edge 访问 `http://localhost:8080`，看到和桌面版类似的界面。

> **强制刷新**：每次重新构建后必须按 `Ctrl+F5` 清除浏览器缓存，否则仍会运行旧版本的 `.wasm` 文件。

**其他 build_web.bat 子命令**：

```bat
build_web.bat             # 只构建，不启动服务器
build_web.bat configure   # 清空 out/web 并重新生成 CMake 工程（改了 CMakeLists.txt 后用）
build_web.bat clean       # 清理所有输出后全量重建
```

---

## 第三步：验证功能

两个版本启动后，依次操作验证基本功能正常：

1. 点击工具栏"直线"（或按 `L` + `Enter`），在视口内点两下画一条线。
2. 点击工具栏"圆"（或按 `C` + `Enter`），画一个圆。
3. 按 `Ctrl+Z` 撤销，按 `Ctrl+Y` 重做，确认两者都生效。
4. 鼠标滚轮缩放，中键按住拖动平移。
5. 选中一条线（左键单击），观察端点夹点出现，拖动端点改变线段长度。

如果以上都正常，说明渲染、输入、命令栈、吸附、夹点整条链路都通了。

---

## CMakeLists.txt 如何控制两套构建

`CMakeLists.txt` 用一个 `if(EMSCRIPTEN)` 分支把两套构建区分开：

```cmake
if(EMSCRIPTEN)
    # ── Web 目标 ─────────────────────────────────────────────
    add_executable(MiniCADWeb
        src/App/Web/WebMain.cpp
        src/Render/WebGL/WebGLRenderer.cpp
        # ... 共享模块源文件 ...
    )
    target_link_options(MiniCADWeb PRIVATE
        -sEXPORTED_FUNCTIONS=${MINICAD_EXPORTS}  # 暴露给 JS 的 C 函数列表
        --preload-file=assets/fonts@/fonts        # 把字体打包进虚拟文件系统
    )
else()
    # ── Windows 目标 ─────────────────────────────────────────
    add_executable(MiniCAD WIN32
        src/App/Win/Main.cpp
        src/App/Win/MainWindow.cpp
        src/Render/D3D11/D3D11Renderer.cpp
        src/Editor/Input/Win/InputSystem.cpp
        src/Editor/Input/Win/KeyCodeUtils.cpp
        src/Editor/Input/Win/ViewportInputAdapter.cpp
        # ... 共享模块源文件 ...
    )
    target_link_libraries(MiniCAD PRIVATE d3d11 dxgi D3DCompiler)
endif()
```

两个目标都包含的**共享模块**（不在 `if/else` 里）：

```
src/Core/        src/Scene/      src/Document/
src/Editor/      src/Viewport/   src/Text/
```

这是 MiniCAD 的关键设计原则：**CAD 业务逻辑与平台渲染完全分离**。工具系统、命令栈、实体、吸附、约束——这些核心代码一行都不需要改，就能同时跑在 Windows 和浏览器上。平台差异只体现在三个地方：入口文件、渲染后端、输入适配层。

---

## 构建产物说明

**Windows 构建输出** (`out/win/MiniCAD/Release/`)：

```
MiniCAD.exe        ← 可执行文件
fonts/             ← 从 assets/fonts/ 复制过来
icons/             ← 从 assets/icons/ 复制过来
shader/            ← 预编译 HLSL shader 字节码（.cso）
```

**Web 构建输出** (`out/web/MiniCADWeb/`)：

```
index.html         ← 入口页面
index.js           ← Emscripten 胶水代码（初始化 WASM 运行时）
index.wasm         ← 实际的 WebAssembly 字节码
index.data         ← 打包的虚拟文件系统（含 fonts/ 字体数据）
```

浏览器加载顺序：`index.html` → 加载 `index.js` → `index.js` 下载并实例化 `index.wasm` → 从 `index.data` 恢复虚拟文件系统 → 调用 C++ `main()`。

---

## 拓展练习

1. 找到 `CMakeLists.txt` 中 `MINICAD_EXPORTS` 列表，数一数导出了多少个函数，每个对应 Web 端的哪个操作。
2. 修改 `CMakeLists.txt` 里 `WIN32` 关键字（`add_executable(MiniCAD WIN32 ...)`），改成不带 `WIN32`，重新构建，观察启动后多了什么。
3. 在 `out/win/MiniCAD/Release/` 目录手动删除 `fonts/` 文件夹，重新启动程序，观察报错位置在哪个文件里。
