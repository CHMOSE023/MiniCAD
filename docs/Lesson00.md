# MiniCAD 初级教程课程规划

## 课程定位

本教程面向会一点 C++、能使用 Visual Studio/CMake，但不熟悉 CAD、渲染或命令模式的初级开发者。课程目标是帮助学习者理解 MiniCAD 的整体架构，能构建运行项目，读懂一个绘图工具的输入流程，并尝试添加简单功能。

建议采用"从使用到源码"的顺序，而不是按源码目录逐个讲。主线是：用户输入如何进入系统、工具如何响应、实体如何创建、命令如何支持撤销、场景如何绘制到屏幕。

开始学习前，建议先阅读《C++ 基础课》（`Lesson00_Cpp.md`）和本文的"前置知识简介"节，了解课程中会涉及的语言特性、数学概念和图形 API。课程末尾的"知识点分布总览"按课次列出各领域重点，方便提前预习或课后查阅。

---

## 前置知识简介

### CMakeLists.txt

CMake 是跨平台构建系统。MiniCAD 用一份 `CMakeLists.txt` 同时管理 Windows 桌面目标（`MiniCAD`）和 WebAssembly 目标（`MiniCADWeb`）。

核心概念：

- **`cmake_minimum_required` / `project`**：声明最低版本与项目名。
- **`add_executable`**：创建可执行目标，列举源文件。MiniCAD 按目录显式列举，不用 GLOB，避免新增文件漏编译。
- **`target_include_directories`**：设置头文件搜索路径。MiniCAD 使用 `src/` 和 `3rd/` 作为根路径，所有 `#include` 从这两个根写相对路径。
- **`target_link_libraries`**：链接库。桌面版链接 `d3d11`、`dxgi`、`D3DCompiler`；Web 版通过链接选项设置 Emscripten 标志。
- **`if(EMSCRIPTEN) ... else() ... endif()`**：平台分支，控制哪些源文件和链接选项参与编译。
- **`target_compile_options` / `target_compile_definitions`**：设置编译选项（`/utf-8`、`NOMINMAX`）和宏定义（`MINICAD_WEB`）。
- **`add_custom_command`**：构建后复制 assets 到输出目录；Web 版用 `--preload-file` 打包字体资源。
- **生成器表达式**：`$<CONFIG:Debug>` 等根据构建配置动态求值，用于条件性编译选项。

读 CMakeLists.txt 时，关注 `if(EMSCRIPTEN)` 分支——这是理解哪些代码只编译到桌面、哪些只编译到 Web 的关键入口。

---

### Emscripten SDK（emsdk）与 WebAssembly 编译

Emscripten 是把 C/C++ 编译成 WebAssembly 的工具链，本质上是一套以 LLVM/Clang 为后端的编译器前端。

**安装与激活**：emsdk 提供 `emsdk install` / `emsdk activate` 命令管理工具链版本。`build_web.bat` 在每次构建时自动调用 `emsdk_env.bat` 激活环境变量，无需手动操作。

**关键编译概念**：

- **工具链文件**：Emscripten 提供 `Emscripten.cmake`，通过 `-DCMAKE_TOOLCHAIN_FILE` 告知 CMake 用 `emcc`/`em++` 代替 MSVC。
- **输出文件**：编译产物是 `.wasm`（字节码）+ `.js`（胶水代码）+ `.html`（入口页面）。`.js` 负责初始化运行时、加载 `.wasm`、暴露 JS 接口。
- **`--preload-file`**：把本地文件打包进虚拟文件系统，C 代码可以用标准 `fopen` 读取，MiniCAD 用此打包字体文件。
- **`EXPORTED_FUNCTIONS`**：指定哪些 C 函数可从 JS 调用（用 `ccall`/`cwrap`）。MiniCAD 在 CMakeLists.txt 中用列表变量维护导出函数名。
- **`EMSCRIPTEN_KEEPALIVE`**：标记函数避免被死代码消除，与 `EXPORTED_FUNCTIONS` 配合使用。
- **`emscripten_set_main_loop`**：浏览器主线程不可阻塞，不能用 `while(true)` 做主循环，必须注册一个每帧回调函数，由浏览器在 `requestAnimationFrame` 时机调用。
- **`ASSERTIONS` / `SAFE_HEAP`**：调试选项，开启后运行时检查内存越界和未对齐访问，用 `MINICAD_WEB_DEBUG=ON` 启用。

---

### C++ 知识要求

本教程涉及的 C++ 特性按难度分层，详细示例见《C++ 基础课》（`Lesson00_Cpp.md`）。

**必须掌握（课程入门前）**：

- 类、继承、虚函数、纯虚函数（接口）
- 指针与引用的区别
- `std::vector`、`std::string` 基本使用
- `#include`、头文件保护（`#pragma once`）
- 命名空间（`namespace`）

**课程中会学到（随课推进）**：

- `std::unique_ptr` 与所有权语义、`std::move`
- `std::function` 与 lambda 表达式
- `enum class` 强类型枚举
- 模板基础（函数模板、类模板）
- `std::span`（C++20 零拷贝视图）
- `std::unordered_map` / `std::unordered_set`
- 宏（`#define`）与宏展开（`DECLARE_RUNTIME_TYPE`）
- 预处理条件编译（`#ifdef` / `#ifndef`）
- `override` / `final` 关键字
- 协变返回类型（`Clone()` 返回 `unique_ptr<Derived>`）

---

### 数学基础

CAD 系统的数学核心是**二维线性代数**，不需要三维透视，但需要理解坐标变换。

**坐标与向量**：

- 点（Point）表示位置，向量（Vector）表示方向或偏移，两者不可混用。
- MiniCAD 工作在 XY 平面（Z 固定为 0），`Point3` 中 z 通常为 0。

**常用运算**：

- 向量加减、标量乘法
- 点积（dot product）：判断夹角、计算投影
- 向量长度（`sqrt(x²+y²)`）与归一化
- 线性插值：`lerp(a, b, t) = a + t*(b-a)`

**几何算法**：

- 点到线段的最近点（参数化 t，clamp 到 [0,1]）
- 矩形（AABB）与点、矩形的相交测试
- 圆的象限点（0°/90°/180°/270°处的点）
- 极坐标转直角坐标：`x = r·cosθ`，`y = r·sinθ`

**矩阵与坐标变换**（渲染课程重点）：

- 4×4 齐次矩阵：平移、旋转、缩放合并为一次矩阵乘法
- 正交投影矩阵：把世界坐标映射到 NDC（归一化设备坐标）
- 逆矩阵：`ScreenToWorld` 的数学基础
- 行优先 vs 列优先存储约定（DirectXMath 列优先，GLSL 列优先，注意转置）

**旋转**（文字渲染）：

- 2D 旋转：`x' = x·cosθ - y·sinθ`，`y' = x·sinθ + y·cosθ`

---

### Direct3D 11

Direct3D 11（D3D11）是 Windows 平台的图形 API，MiniCAD 桌面版的所有渲染都通过它完成。

**核心对象**：

| 对象                                           | 职责                                        |
| ---------------------------------------------- | ------------------------------------------- |
| `ID3D11Device`                                 | GPU 资源创建工厂（不直接绘制）              |
| `ID3D11DeviceContext`                          | 发出绘制命令（Immediate Context）           |
| `IDXGISwapChain`                               | 管理前后缓冲区，`Present()` 翻转显示        |
| `ID3D11RenderTargetView`                       | 指定渲染目标（通常是后缓冲区）              |
| `ID3D11Buffer`                                 | 顶点缓冲区（VB）或常量缓冲区（CB）          |
| `ID3D11InputLayout`                            | 描述顶点格式（位置、颜色、UV 的偏移和类型） |
| `ID3D11VertexShader` / `ID3D11PixelShader`     | GPU 着色器程序                              |
| `ID3D11Texture2D` + `ID3D11ShaderResourceView` | 纹理资源（字体图集）                        |
| `ID3D11SamplerState`                           | 纹理采样方式（线性/最近邻）                 |

**渲染流程**（每帧）：

```text
ClearRenderTargetView()          // 清屏
IASetVertexBuffers()             // 绑定顶点缓冲
IASetInputLayout()               // 绑定顶点格式
VSSetConstantBuffers()           // 上传 MVP 矩阵
VSSetShader() / PSSetShader()    // 绑定着色器
Draw() / DrawIndexed()           // 发出绘制调用
Present()                        // 翻转显示
```

**坐标系约定**：左手坐标系，Y 轴向上，NDC 中 Z ∈ [0, 1]（深度范围），与 OpenGL 不同（Z ∈ [-1, 1]）。

**HLSL 着色器**（Shader）：D3D11 使用 HLSL 编写顶点和像素着色器，MiniCAD 使用预编译的 `.cso` 字节码文件打包在 assets 中。顶点着色器接收顶点位置，乘以 MVP 矩阵输出裁剪坐标；像素着色器输出最终颜色。

---

### WebGL / OpenGL ES

MiniCAD 的 Web 版使用 WebGL 2.0（基于 OpenGL ES 3.0），通过 Emscripten 在浏览器中运行。

**WebGL 与 D3D11 的对应关系**：

| D3D11 概念                       | WebGL 等价                                                       |
| -------------------------------- | ---------------------------------------------------------------- |
| `ID3D11Device` + `DeviceContext` | `WebGLRenderingContext`（`gl`）                                  |
| `ID3D11Buffer`（顶点缓冲）       | `gl.createBuffer()` + `gl.ARRAY_BUFFER`                          |
| `ID3D11InputLayout`              | `gl.vertexAttribPointer()` + VAO                                 |
| `ID3D11VertexShader`             | GLSL `attribute`/`in` + `gl.createShader(gl.VERTEX_SHADER)`      |
| `ID3D11PixelShader`              | GLSL `uniform sampler2D` + `gl.createShader(gl.FRAGMENT_SHADER)` |
| `ID3D11Texture2D` + SRV          | `gl.createTexture()` + `gl.texImage2D()`                         |
| `VSSetConstantBuffers`           | `gl.uniformMatrix4fv()`                                          |
| `DrawInstanced(n)`               | `gl.drawArrays(gl.LINES, 0, n)`                                  |
| `Present()`                      | 浏览器自动显示（无需显式调用）                                   |

**GLSL 着色器**：WebGL 使用 GLSL ES 300（`#version 300 es`）编写着色器，语法与 HLSL 类似但不同。顶点着色器用 `in`/`out` 声明输入输出，`uniform` 声明矩阵等常量；片段着色器输出 `out vec4 fragColor`。

**坐标系约定**：右手坐标系（默认），NDC 中 Z ∈ [-1, 1]。MiniCAD 的 `Camera` 针对两个平台生成不同的正交投影矩阵。

---

### WebAssembly

WebAssembly（WASM）是一种二进制指令格式，可以在浏览器中以接近原生速度运行。MiniCAD 通过 Emscripten 将 C++ 编译为 WASM。

**关键概念**：

- **模块（Module）**：`.wasm` 文件是一个二进制模块，包含函数、内存、导入/导出表。
- **线性内存（Linear Memory）**：WASM 拥有一块连续的字节数组（Heap），C/C++ 的栈和堆都在这里。JS 可以通过 `Module.HEAPU8` 等视图直接读写这块内存。
- **JS ↔ C++ 互操作**：
  - C++ → JS：通过 `emscripten_run_script` 或导出函数（`EXPORTED_FUNCTIONS`）实现，JS 用 `Module.ccall()` 调用。
  - MiniCAD 的 `WebMain.cpp` 导出 `_MiniCAD_Init`、`_MiniCAD_SubmitText` 等函数，JS 层通过 `ccall` 触发文字输入等操作。
- **虚拟文件系统（FS）**：Emscripten 模拟了一套文件系统，`--preload-file` 把字体文件打包进去，C 代码用 `fopen("/fonts/xxx.shx")` 直接读取。
- **单线程模型**：浏览器 JS 主线程是单线程的，所有 WASM 代码也在同一线程运行，这是为什么主循环必须用 `emscripten_set_main_loop` 而不能阻塞。
- **调试**：`MINICAD_WEB_DEBUG=ON` 开启 `-g3`（DWARF 调试信息）、`ASSERTIONS=2`（运行时断言）、`SAFE_HEAP`（越界检测），Chrome DevTools 可以直接调试 C++ 源码。

---

## 第 1 课：项目能跑起来

阅读路径：`README.md`、`CMakeLists.txt`、`docs/BUILD_WASM.md`

MiniCAD 的定位、Windows/D3D11 与 WebAssembly/WebGL 两种构建方式、顶层目录说明，以及基本运行验证。

```powershell
cmake -S . -B out/win -G "Visual Studio 17 2022"
cmake --build out/win --config Release
build_web.bat serve
```

练习：运行桌面版或 Web 版，画一条线、一个圆，测试撤销/重做。

---

## 第 2 课：整体架构地图

阅读路径：`src/App`、`src/Document`、`src/Editor`、`src/Viewport`、`src/Render`

各模块职责与核心数据流：

```text
Input → Editor → Tool → CommandStack → Scene
Scene → Entity::Draw → DrawContext → ViewState
ViewState → Viewport → IRenderer → D3D11/WebGL
```

练习：画出 MiniCAD 的模块关系图，用一句话解释 Document、Scene、Editor 的区别。

---

## 第 3 课：程序入口与主循环

阅读路径：`src/App/Win/Main.cpp`、`src/App/Win/MainWindow.cpp`、`src/App/Web/WebMain.cpp`

Windows 主循环（消息驱动）与 WebAssembly 主循环（`emscripten_set_main_loop`）的差异；`Editor::Bind(Document&, Viewport&)` 的作用。

练习：在入口处添加启动日志，确认初始化顺序。

---

## 第 4 课：输入系统与编辑器核心

阅读路径：`src/Editor/Input/InputEvent.h`、`src/Editor/Input/Win/ViewportInputAdapter.cpp`、`src/Editor/Editor.cpp`、`src/Editor/EditorContext.h`

鼠标/键盘/滚轮统一为 `InputEvent`；`Editor::HandleGlobal()` 处理全局快捷键与命令缓冲；工具别名系统（`"L" → Line`）。

练习：增加命令别名 `"R" → Rectangle`，验证 R 键可启动矩形工具。

---

## 第 5 课：绘图工具如何工作

阅读路径：`src/Editor/Tools/ITool.h`、`src/Editor/Tools/LineTool.h`、`src/Editor/Overlay/Overlay.h`

以 `LineTool` 为案例讲解工具状态机、Overlay 临时预览、`AddEntityCommand` 正式提交的完整流程。

练习：修改 `LineTool::GetPrompt()` 提示文字；为 LineTool 增加第一次点击日志。

---

## 第 6 课：Core 核心层：数学、几何、对象与实体

阅读路径：`src/Core/Math`、`src/Core/GeomKernel`、`src/Core/Object`、`src/Core/Entity`、`src/Core/Draw`

`Math`（坐标/向量/矩阵/颜色）、`GeomKernel`（几何数据）、`Object`（ID + 运行时类型）、`Entity`（三个核心虚函数：GetBoundingBox/Draw/Clone）、`IDrawSink`（绘制发射接口）。

练习：阅读 `LineEntity`，找出它使用的所有 Core 类型；比较 `Line`（几何）和 `LineEntity`（CAD 对象）的区别。

---

## 第 7 课：命令模式与撤销重做

阅读路径：`src/Document/CommandStack/ICommand.h`、`CommandStack.cpp`、`src/Document/Command/AddEntityCommand.h`、`MoveCommand.cpp`

`Execute/Undo` 接口设计；undo/redo 双栈机制；实体所有权的转移规则。

练习：在 CommandStack 关键方法中加日志，观察栈变化。

---

## 第 8 课：吸附与约束系统 _(新)_

阅读路径：`src/Editor/Snap/SnapEngine.h`、`src/Editor/Constraint/OrthoConstraint.h`、`src/Editor/Constraint/PolarConstraint.h`、`src/Editor/Resolver/InputResolver.h`、`src/Editor/Resolver/InputResult.h`

吸附管线（端点/中点/最近点/象限点/网格）；正交约束（F8）与极轴约束的数学原理；`InputResolver` 把 Raw → Snap → Constraint → `InputResult` 的完整流程。

练习：修改吸附半径；为正交约束加 45° 方向。

---

## 第 9 课：命令行系统 _(新)_

阅读路径：`src/Editor/CommandLine/CommandLine.h`、`src/Editor/Editor.cpp`（`RunCommand`、`HandleGlobal`）、`src/Editor/Tools/ITool.h`（`GetPrompt`）、`src/UI/UIManager.cpp`

`CommandLine` 提示缓冲与回显历史；`RunCommand()` 统一入口；工具 `SetPrompt()` 每帧刷新；重复上次命令（空缓冲回车）。

练习：新增别名 `"TR" → Trim`；修改命令行面板最大行数。

---

## 第 10 课：选择、拾取与夹点编辑

阅读路径：`src/Editor/Picking/Picking.h`、`src/Editor/Grip/GripEditor.h`、`src/Editor/Grip/LineGripHandler.h`

点选/框选/hover 机制；HitTest 算法；夹点状态机（命中 → 待激活 → 激活 → 确认/取消）；`IEntityGripHandler` 按类型注册。

练习：修改夹点颜色；阅读 `PolylineGripHandler`，理解多个夹点的管理方式。

---

## 第 11 课：视口、相机与坐标转换

阅读路径：`src/Viewport/Viewport.h`、`src/Viewport/Camera.h`、`src/Viewport/ViewState.h`

屏幕坐标与世界坐标；正交投影矩阵；`ScreenToWorld()`/`WorldToScreen()`；鼠标下点不动的缩放算法；D3D11 与 WebGL 坐标系差异。

练习：打印鼠标屏幕坐标和世界坐标；调整 Zoom 速度系数。

---

## 第 12 课：渲染层入门

阅读路径：`src/Render/IRenderer.h`、`src/Render/VertexTypes.hpp`、`src/Render/D3D11/D3D11Renderer.cpp`、`src/Render/WebGL/WebGLRenderer.cpp`、`src/Viewport/Viewport.cpp`

`IRenderer` 接口；`Vertex_P3_C4` 与 `Vertex_P3_C4_UV`；`Submit`（线段）/ `SubmitTextured`（文字贴图）；顶点变换流水线；D3D11 与 WebGL 渲染对等实现。

练习：找到线段从 LineEntity 到 `DrawInstanced` 的完整调用链。

---

## 第 13 课：文字与字体系统

阅读路径：`src/Core/Entity/TextEntity.hpp`、`src/Core/Entity/MTextEntity.hpp`、`src/Text/FontSystem.h`、`src/Text/Font/SHXFont.cpp`、`src/Text/Layout/TextLayoutEngine.cpp`、`src/Document/DrawContext.hpp`

单行文字（ImGui 纹理路径）与多行文字（SHX/TTF 矢量路径）；`GlyphProvider`/`FontResolver` 回调；字形 UV、旋转变换、字符步进。

练习：创建文字并比较与普通几何实体的绘制路径差异。

---

## 第 14 课：多文档管理

阅读路径：`src/Document/DocumentManager.h`、`src/Document/DocumentManager.cpp`、`src/Editor/Editor.h`（`Bind/Unbind`）、`src/Text/FontSystem.h`

`DocumentManager` 所有权模型（唯一 Editor + Viewport，多文档）；`Bind/Unbind` 延迟绑定模式；FontStyle 全局注册与跨文档共享；选项卡切换文档的完整流程。

练习：实现关闭 dirty 文档时的保存提示。

---

## 第 15 课：综合实战

推荐选题：

- 添加命令别名组（AutoCAD 风格）
- 添加新绘图工具（如简化两点矩形）
- 属性面板：显示选中对象 ID、图层、类型、包围盒

实现路线：

```text
ITool → Editor 注册 → Entity → ICommand → CommandStack → Scene → Viewport
```

综合检查清单：是否遵守模块边界、是否通过 CommandStack 修改场景、是否支持 Undo/Redo、是否兼容 Windows 和 Web 共享核心。

---

## 知识点分布总览

| 课次 | 编译重点                                    | C++ 重点                                 | 数学重点                       | 图形 API 重点                                       |
| ---- | ------------------------------------------- | ---------------------------------------- | ------------------------------ | --------------------------------------------------- |
| 1    | CMake 生成器、Emscripten 工具链             | 预处理宏                                 | 无                             | 无                                                  |
| 2    | 分层编译依赖                                | 纯虚接口、前向声明                       | 无                             | 无                                                  |
| 3    | WinMain/SUBSYSTEM、emscripten_set_main_loop | unique_ptr 全局对象                      | 无                             | D3D11 设备创建；WebGL 上下文                        |
| 4    | 平台目录隔离                                | enum class、适配器模式                   | 无                             | Win32 消息；浏览器事件回调                          |
| 5    | header-only 工具                            | 多态、状态机、lambda                     | 平面坐标点                     | 无                                                  |
| 6    | .hpp 扩展名、宏展开                         | 模板、纯虚、RTTI 宏                      | AABB、点到线段距离             | 无                                                  |
| 7    | 无                                          | unique_ptr 所有权转移、双栈              | 无                             | 无                                                  |
| 8    | 无                                          | 策略模式、bool+value 可选值              | 最近点算法、极坐标、向量投影   | 无                                                  |
| 9    | 无                                          | 环形缓冲、字符串处理                     | 无                             | ImGui InputText/TextUnformatted                     |
| 10   | 无                                          | unordered_map 注册表                     | HitTest 距离算法、框选相交测试 | 夹点屏幕正交矩阵渲染                                |
| 11   | 无                                          | 无                                       | 正交投影矩阵、NDC、逆矩阵      | D3D11 vs WebGL 坐标约定                             |
| 12   | 无                                          | std::span、接口隔离                      | 顶点变换流水线                 | D3D11 VB/InputLayout/Draw；WebGL VAO/VBO/drawArrays |
| 13   | 无                                          | std::function 回调、LRU 缓存、UTF-8 解码 | 旋转变换、字符步进、MText 换行 | D3D11/WebGL 纹理采样                                |
| 14   | 无                                          | unique_ptr vector 所有权、延迟绑定       | 无                             | ImGui TabBar/TabItem                                |
| 15   | 综合                                        | 综合                                     | 依选题                         | ImGui                                               |
