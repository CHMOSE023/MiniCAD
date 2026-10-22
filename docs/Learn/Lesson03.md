# Lesson03：程序入口与主循环

## 学习目标

读懂 MiniCAD 两套入口的完整启动流程，以及消息（输入事件）如何从操作系统/浏览器一路转发到 `Editor::OnInput()`。完成后应理解：Windows 与 Web 的主循环模型差异、每帧的执行顺序、输入事件的两条转发路径。

---

## Windows 入口：WinMain / main()

`src/App/Win/Main.cpp` 只有几行，职责是创建 `MainWindow` 并调用 `Run()`：

```cpp
// USE_WIN32 宏控制用哪个入口签名
#ifdef USE_WIN32
int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE, PSTR, int)
#else
int main()   // 控制台模式，方便 printf 调试
#endif
{
    MainWindow mainWindow;
    mainWindow.Initialize(L"MiniCAD", 600, 400);
    mainWindow.Run();
    return 0;
}
```

`USE_WIN32` 宏决定入口函数签名：`WinMain` 不显示控制台窗口，`main` 版本会弹出控制台（便于调试日志）。Release 构建用 `WinMain`；开发期间用 `main` 方便 `printf`。

---

## Windows 初始化顺序

`MainWindow::Initialize()` 的四步必须严格按顺序：

```
1. InitWindow()         → 注册窗口类，CreateWindowEx，设置 DWM 阴影，居中显示
2. InitD3D11()          → 创建 Device + SwapChain + D3D11Renderer
3. m_fontSystem.Initialize() + PreloadDefaultFonts()
                        → 必须在 InitDocument 之前，因为 Document 注册字体样式需要 FontSystem
4. InitDocument()       → DocumentManager.InitViewport + 注册 FontStyle + Create() 创建首个文档
5. m_uiManager.Init()   → ImGui Win32/D3D11 后端初始化
```

顺序约束说明：
- **字体先于文档**：`InitDocument` 内部调用 `RegisterFontStyle`，这需要 `FontSystem` 已就绪。
- **D3D11 先于视口**：`InitViewport` 内部创建 `D3D11RenderTarget`，需要 `IRenderer` 已存在。
- **ImGui 最后**：`m_uiManager.Init` 把 ImGui 的 Win32 窗口句柄和 D3D11 设备注入后端，必须两者都已创建。

---

## Windows 消息循环（Run）

```cpp
void MainWindow::Run()
{
    MSG msg = {};
    bool needsRedraw = true;

    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);  // 将 WM_KEYDOWN 转换为 WM_CHAR
            DispatchMessage(&msg);   // 路由到 WndProc
            needsRedraw = true;      // 收到任何消息都标记需要重绘
        }
        else
        {
            if (needsRedraw)
            {
                RenderFrame();       // 消息队列空了才渲染
                needsRedraw = false;
            }
            else
            {
                WaitMessage();       // 无消息、无需渲染：让出 CPU
            }
        }
    }
}
```

**关键设计**：这是一个"事件驱动"的主循环，而不是游戏常见的"无限渲染"循环。

- `PeekMessage`（非阻塞）：有消息就取走并分发，**但不在此处渲染**。
- 消息队列清空后，才调用 `RenderFrame()`，渲染一帧。
- 渲染后若没有新消息，`WaitMessage()` 让线程睡眠，完全不占 CPU。
- CAD 软件大部分时间静止，这个设计比游戏引擎的忙等循环节省大量 CPU。

---

## Windows 消息路由：WndProc → EventProc

```
OS 产生消息（WM_MOUSEMOVE、WM_LBUTTONDOWN、WM_KEYDOWN …）
  ↓
DispatchMessage → WndProc（静态函数）
  ↓
通过 GWLP_USERDATA 取回 this 指针
  ↓
pThis->EventProc(hwnd, msg, wParam, lParam)
  ↓
① ImGui_ImplWin32_WndProcHandler(hwnd, msg, ...)  ← ImGui 先吃一遍
  如果 ImGui 消耗（返回 true），直接 return，不往下走
  ↓
② switch(msg) 处理窗口级消息：
   WM_SIZE      → SwapChain::Resize
   WM_NCHITTEST → 自定义标题栏/边框拖拽区域
   WM_DESTROY   → PostQuitMessage(0)
   其余         → DefWindowProc
```

**注意**：鼠标移动、点击、按键这些**输入事件不在 WndProc 里转发给 Editor**。ImGui 的 Win32 后端把它们存进 `ImGuiIO`，在下一帧 `RenderFrame()` 中读取。

---

## Windows 每帧执行顺序（RenderFrame）

```cpp
void MainWindow::RenderFrame()
{
    // 1. 清空后备缓冲区
    ClearRenderTargetView(rtv, { 0.1, 0.1, 0.1, 1 });
    OMSetRenderTargets(1, &rtv, nullptr);

    // 2. ImGui 帧开始（必须在 Editor::Render 之前）
    m_uiManager.BeginFrame();
    //   └─ ImGui_ImplDX11_NewFrame + ImGui_ImplWin32_NewFrame + ImGui::NewFrame

    // 3. 同步字体纹理（BeginFrame 之后、Render 之前）
    m_uiManager.SyncFonts(m_docManager);
    //   └─ 把 ImGui 字体图集 SRV 注入 Editor，供 DrawContext::EmitText 使用

    // 4. 从 ImGui IO 读取本帧视口输入并转发给 Editor
    DocumentInput();

    // 5. Editor 收集顶点并提交给 Viewport 渲染
    m_docManager.GetEditor().Render();

    // 6. 重新绑定 RTV（Editor::Render 内部可能改变渲染目标）
    OMSetRenderTargets(1, &rtv, nullptr);

    // 7. 渲染 ImGui UI（菜单、工具栏、状态栏）
    m_uiManager.Render(m_docManager);

    // 8. ImGui 帧结束，提交 ImGui 绘制数据到 D3D11
    m_uiManager.EndFrame();

    // 9. 呈现（交换前后缓冲区）
    m_swapChain->Present();
}
```

**顺序约束**：
- `BeginFrame` 必须在 `Editor::Render` 之前，`EmitText` 内部调用 ImGui 字形接口需要帧已开始。
- `SyncFonts` 必须在 `BeginFrame` 之后（字体 atlas SRV 在 NewFrame 时才确定）。
- `Editor::Render` 必须在 `UIManager::Render` 之前，否则视口内容会被 ImGui 的绘制覆盖。

---

## Windows 输入转发路径（DocumentInput）

消息循环只负责把 OS 事件喂给 ImGui；每帧由 `DocumentInput()` 把 ImGui 状态转换成 `InputEvent` 列表：

```cpp
void MainWindow::DocumentInput()
{
    const auto& uiInput = m_uiManager.GetViewportInput();
    // ViewportInput 是 UIManager 每帧从 ImGui::IsItemHovered/Active 等状态构建的快照

    if (!uiInput.Valid) return;  // 视口区域未就绪

    // 视口尺寸变化时同步 Viewport
    auto& viewport = m_docManager.GetViewport();
    if (viewport.GetWidth() != uiInput.Size.x || ...)
        viewport.Resize(uiInput.Size.x, uiInput.Size.y);

    // ViewportInputAdapter 把快照差分成事件列表
    for (const auto& e : m_viewportInputAdapter.BuildEvents(uiInput))
    {
        m_docManager.GetEditor().OnInput(e);
    }
}
```

完整的 Windows 输入路径：

```
OS 鼠标/键盘事件
  → WndProc → ImGui_ImplWin32_WndProcHandler → 存入 ImGuiIO
  → 下一帧 UIManager::BeginFrame (ImGui::NewFrame)
  → UIManager 从 ImGui::IsItemHovered/Active/IO 构建 ViewportInput 快照
  → ViewportInputAdapter::BuildEvents(ViewportInput) → InputEvent 列表
  → Editor::OnInput(e)
```

---

## Web 入口：main()

Web 版没有 `MainWindow`，全局变量就是运行环境：

```cpp
// 匿名 namespace 内的全局对象
std::unique_ptr<IRenderer> g_renderer;
std::unique_ptr<Document>  g_document;
std::unique_ptr<Viewport>  g_viewport;
Editor                     g_editor;       // 值类型，与 DocumentManager 里相同
FontSystem                 g_fontSystem;

// 手动维护的状态
int g_lastX, g_lastY;       // 上一帧鼠标位置（用于 MouseMove 的 LastMouseX/Y）
int g_pressX, g_pressY;     // 最近一次按下时的位置（用于拖拽判定）
unsigned g_mouseButtons;    // 当前按下的按键掩码（Emscripten 不维护，需要手动）
```

`main()` 的初始化顺序：

```
1. emscripten_webgl_create_context("#minicad-canvas", attrs)
   → 创建 WebGL2 上下文并激活

2. ResizeIfNeeded()
   → 同步 canvas 物理像素尺寸（CSS 尺寸 × devicePixelRatio）

3. CreateRenderer(info)       → WebGLRenderer
4. new Viewport(*g_renderer, w, h)
5. new Document()
6. g_editor.Bind(*g_document, *g_viewport)

7. g_fontSystem.Initialize() + PreloadDefaultFonts()
   → SHX 字体从 --preload-file 嵌入的虚拟 FS /fonts/ 读取

8. 注册 Emscripten 事件回调：
   emscripten_set_mousedown_callback (kCanvas, ..., OnMouse)
   emscripten_set_mouseup_callback   (kCanvas, ..., OnMouse)
   emscripten_set_mousemove_callback (kCanvas, ..., OnMouse)
   emscripten_set_wheel_callback     (kCanvas, ..., OnWheel)
   emscripten_set_keydown_callback   (EMSCRIPTEN_EVENT_TARGET_WINDOW, ..., OnKey)
   emscripten_set_keyup_callback     (EMSCRIPTEN_EVENT_TARGET_WINDOW, ..., OnKey)

9. emscripten_set_main_loop(MainLoop, 0, EM_TRUE)
   → 注册主循环，接管控制流（EM_TRUE = 阻塞，main 后面的代码不会执行）
```

---

## Web 主循环（MainLoop）

```cpp
void MainLoop()
{
    ResizeIfNeeded();     // 检查 canvas 是否因浏览器窗口变化而需要缩放
    g_editor.Render();    // 收集顶点 + 提交渲染
}
```

`emscripten_set_main_loop(MainLoop, 0, EM_TRUE)` 三个参数含义：
- `MainLoop`：每帧调用的函数指针
- `0`：帧率 0 = 跟随浏览器 `requestAnimationFrame`，通常 60fps
- `EM_TRUE`：接管控制流，`main()` 的剩余代码不执行（类似 `while(true)` 替代）

Web 主循环**远比 Windows 简单**：没有消息泵，没有 ImGui，没有 SwapChain——浏览器负责帧节奏，WebGL 上下文始终有效。

---

## Web 输入转发路径

Web 的输入路径比 Windows 直接得多——回调里直接构建 `InputEvent` 然后调用 `g_editor.OnInput()`：

```
浏览器原生事件（mousedown、mousemove、wheel、keydown …）
  → Emscripten 回调（OnMouse / OnWheel / OnKey）
  → 构建 InputEvent（含 DPI 修正、按键映射）
  → Dispatch(input) → g_editor.OnInput(e)
```

中间没有 ImGui，没有快照，没有 BuildEvents，每个事件同步触发。

**DPI 修正**：浏览器回调给的是 CSS 像素坐标，`Editor` 和 `Viewport` 需要物理像素：

```cpp
int CanvasX(double cssX) {
    return static_cast<int>(cssX * g_width / g_cssWidth);
    // g_width = CSS宽 × devicePixelRatio（物理像素）
    // g_cssWidth = CSS宽
}
```

**手动维护按键掩码**：Windows 可以通过 `GetAsyncKeyState` 查询，Web 没有等价 API，所以手动维护 `g_mouseButtons`：

```cpp
// mousedown 时置位
g_mouseButtons |= ButtonMask(button);
// mouseup 时清位
g_mouseButtons &= ~ButtonMask(button);
// 每个事件都把当前掩码写入 InputEvent.MouseButtons
input.MouseButtons = static_cast<uint8_t>(g_mouseButtons);
```

**保留键过滤**：浏览器默认拦截 F5（刷新）、F12（DevTools）等，`IsBrowserReservedKey()` 返回 `true` 的键直接 `return EM_FALSE`（告诉浏览器我没消费，它继续默认行为）：

```cpp
bool IsBrowserReservedKey(const EmscriptenKeyboardEvent* e) {
    if (e->keyCode == 116) return true;              // F5
    if (e->keyCode == 122) return true;              // F11
    if (e->ctrlKey && e->keyCode == 82) return true; // Ctrl+R
    // ...
}
```

---

## Web JS 工具栏：EMSCRIPTEN_KEEPALIVE 导出

Web 版没有 ImGui 工具栏，工具按钮是 HTML + JS，通过 C 导出函数驱动：

```cpp
extern "C" {
    EMSCRIPTEN_KEEPALIVE void MiniCAD_StartLine()    { g_editor.StartLineTool(); }
    EMSCRIPTEN_KEEPALIVE void MiniCAD_StartCircle()  { g_editor.StartCircleTool(); }
    EMSCRIPTEN_KEEPALIVE void MiniCAD_Undo()         { if (g_document) g_document->Undo(); }
    // ...
}
```

JS 侧调用：

```js
Module.ccall('MiniCAD_StartLine', null, [], []);
```

`EMSCRIPTEN_KEEPALIVE` = 防止链接器优化删除该函数（相当于 `__attribute__((used))`）。`extern "C"` = 关闭 C++ 名称修饰，让 JS 能按字面名称找到函数。

这条路径**完全绕过 InputEvent 系统**——工具激活不经过键盘事件，直接调用 `Editor` 的方法。

---

## 两套入口对比

| 对比项 | Windows | Web |
|---|---|---|
| 入口函数 | `WinMain` / `main` | `main` |
| 主循环驱动 | `PeekMessage` + `WaitMessage` | `requestAnimationFrame`（由 Emscripten 托管） |
| CPU 占用（静止时） | 接近 0（`WaitMessage` 睡眠） | 约 1 帧 / 60fps（浏览器唤醒） |
| 输入路径 | OS→WndProc→ImGuiIO→ViewportInput→InputAdapter→InputEvent | 浏览器→Emscripten回调→InputEvent |
| 中间层 | `ViewportInput` 快照 + `ViewportInputAdapter` 差分 | 无，直接构建 |
| 工具栏 | ImGui（C++ 内渲染） | HTML+JS 按钮 + `ccall` 导出函数 |
| 字体图集 | ImGui 字体纹理（D3D11 SRV 注入 Editor） | Web 字体（无 ImGui，另行处理） |
| DPI 处理 | Windows DPI API / DPI 感知声明 | CSS 尺寸 × `devicePixelRatio` 手动计算 |
| Resize 触发 | `WM_SIZE → SwapChain::Resize` | `MainLoop` 每帧 `ResizeIfNeeded()` 轮询 |

---

## 课堂演示

1. 在 `MainWindow::RenderFrame()` 里每个步骤加 `OutputDebugString`，观察每帧的执行顺序。
2. 在 `DocumentInput()` 的 `for` 循环里打断点，观察 `ViewportInputAdapter::BuildEvents` 一帧生成多少个事件。
3. 在 `WebMain.cpp::OnMouse` 里打 `printf`，在浏览器控制台观察鼠标事件到达的时机（每帧可能有多次，发生在 `MainLoop` 之外）。

## 拓展练习

1. Windows 版中，如果把 `WaitMessage()` 去掉，会有什么现象？为什么 CAD 软件适合这种"有事才渲染"的循环而游戏不适合？
2. Web 版的 `emscripten_set_main_loop` 第二个参数改成 `30`，会发生什么？改成 `-1` 呢？
3. 找到 `MainWindow::EventProc` 中的 `WM_NCHITTEST` 处理，这段代码实现了什么功能？为什么 CAD 软件需要自定义标题栏？
4. Web 版的 `MiniCAD_Cancel` 导出函数是如何把"取消"操作注入 Editor 的？这和直接调用 `g_editor.Cancel()` 有什么区别（假设有这个方法）？
