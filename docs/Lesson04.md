# Lesson04：输入系统与消息映射

## 学习目标

本课讲清楚 MiniCAD 把原始平台事件转换成内部 `InputEvent` 的**完整两条路径**：Windows（Win32 消息 + ImGui ViewportInput）和 Web（Emscripten 浏览器回调）。完成后学习者应能准确说出一次鼠标左键点击经过几层转换才能到达工具的 `OnInput()`，并理解每一层解决了什么问题。

---

## 相关源码

- `src/Editor/Input/InputEvent.h` — 平台无关的统一事件结构
- `src/Editor/Input/ViewportInput.h` — ImGui 视口快照（桌面端中间层）
- `src/Editor/Input/Win/InputSystem.cpp` — Win32 消息 → InputEvent
- `src/Editor/Input/Win/KeyCodeUtils.cpp` — VK 虚拟键码 → KeyCode
- `src/Editor/Input/Win/ViewportInputAdapter.cpp` — ViewportInput → InputEvent
- `src/App/Web/WebMain.cpp` — Emscripten 回调 → InputEvent
- `src/UI/UIManager.h` — 桌面端 UI 管理器（读 ImGui 状态，填 ViewportInput）
- `src/Editor/Editor.cpp` — `OnInput()` 最终消费者

---

## 核心数据结构：InputEvent

`InputEvent` 是所有平台共用的统一事件包，包含一帧里关于"发生了什么"的完整快照：

```cpp
struct InputEvent
{
    InputEventType Type;          // 事件类型（MouseButtonDown/Move/Wheel/KeyDown/...）
    MouseButton    Button;        // 触发的鼠标按键（Left/Middle/Right）
    uint8_t        Modifiers;     // 修饰键位掩码（Shift|Ctrl|Alt）
    uint8_t        MouseButtons;  // 当前所有按键状态位掩码（同时按多键时有值）
    int            MouseX, MouseY;           // 当前鼠标位置（视口像素坐标）
    int            LastMouseX, LastMouseY;   // 上帧鼠标位置（MouseMove delta 来源）
    int            PressMouseX, PressMouseY; // 鼠标按下时的位置（框选起点）
    float          WheelDelta;    // 滚轮量（+1 向上，-1 向下，归一化）
    KeyCode        Key;           // 按键枚举
};
```

设计要点：
- **快照语义**：每个事件是某时刻状态的完整副本，工具不需要查询全局状态。
- **`MouseButtons` vs `Button`**：`Button` 是"这次触发的键"，`MouseButtons` 是"此刻还有哪些键按着"——中键平移时同时左键也可能按着。
- **按键事件里也有鼠标坐标**：`WM_KEYDOWN` 本身没有鼠标位置，`InputSystem` 用上次记录的 `m_lastMousePos` 填充，保证工具拿到的 `MouseX/Y` 始终有意义。

语义化查询方法（工具用这些，不手写条件）：

```cpp
e.IsLeftClick()    // Type==MouseButtonDown && Button==Left
e.IsRightClick()   // Type==MouseButtonDown && Button==Right
e.IsCancel()       // Type==KeyDown && Key==Escape
e.IsUndo()         // Type==KeyDown && Ctrl && Key==Z
e.IsMiddleDrag()   // Type==MouseMove && Middle 键按着
e.IsZoom()         // Type==MouseWheel
e.IsDrag(2)        // 鼠标偏离 PressPos 超过 2 像素（用于区分点击和拖拽）
```

---

## 路径一：Windows 桌面端

桌面端有两套并行的输入通道，分别处理全窗口事件和视口内事件。

### 通道 A：Win32 消息 → InputSystem（全窗口）

`InputSystem::Dispatch(hwnd, msg, wParam, lParam)` 直接挂在 `WndProc` 上，收到 Win32 消息后调用 `BuildEvent()` 转换：

```
WndProc
  ├─ WM_LBUTTONDOWN  → InputEventType::MouseButtonDown, Button::Left
  ├─ WM_RBUTTONDOWN  → InputEventType::MouseButtonDown, Button::Right
  ├─ WM_MBUTTONDOWN  → InputEventType::MouseButtonDown, Button::Middle
  │                     + SetCapture(hwnd)  ← 保证拖出窗口后仍收到消息
  ├─ WM_MBUTTONUP    → InputEventType::MouseButtonUp
  │                     + ReleaseCapture()
  ├─ WM_MOUSEMOVE    → InputEventType::MouseMove
  │                     LastMouseX/Y = m_lastMousePos（上帧值）
  │                     然后 m_lastMousePos = 当前坐标
  ├─ WM_MOUSEWHEEL   → InputEventType::MouseWheel
  │                     ⚠ lParam 是屏幕坐标！需 ScreenToClient() 转换成客户区坐标
  │                     WheelDelta = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA
  ├─ WM_KEYDOWN      → InputEventType::KeyDown
  │                     Key = FromWin32Key(wParam)    ← VK 码 → KeyCode 枚举
  │                     MouseX/Y = m_lastMousePos     ← 按键无位置，复用上次鼠标位置
  └─ WM_KEYUP        → InputEventType::KeyUp
```

**修饰键获取**：`InputSystem::GetModifiers()` 用 `GetKeyState(VK_SHIFT) & 0x8000` 实时查询，而不是等 `WM_KEYDOWN`，因为 Shift/Ctrl 可能在事件前已经按住。

**`SetCapture` / `ReleaseCapture`**：Win32 默认只给有焦点的窗口发鼠标消息。中键拖拽（平移画布）时鼠标很容易滑出视口，`SetCapture` 强制把后续消息路由到本窗口，直到 `ReleaseCapture` 释放。

**`FromWin32Key()` 键码映射**：Win32 的虚拟键码（`VK_ESCAPE`=27、`VK_RETURN`=13、`'A'`=65 …）和 MiniCAD 的 `KeyCode` 枚举之间需要逐一映射。字母 A-Z、数字 0-9 用范围运算批量转换；特殊键（F1-F12、方向键、符号键）用 switch-case 逐个处理：

```cpp
// 字母 A-Z 批量转换
if (vk >= 'A' && vk <= 'Z')
    return static_cast<KeyCode>((int)KeyCode::A + (vk - 'A'));

// 特殊键
case VK_ESCAPE:  return KeyCode::Escape;
case VK_RETURN:  return KeyCode::Enter;
case VK_F3:      return KeyCode::F3;   // F3 切换吸附
case VK_F8:      return KeyCode::F8;   // F8 切换正交
```

转换后发给 `InputSystem` 的责任链（`IInputHandler` 列表），每个 handler 返回 true 则消费事件，停止向下传递。

---

### 通道 B：ImGui → ViewportInput → ViewportInputAdapter（视口内）

桌面端还有一条专门服务于 ImGui 视口窗口的通道，解决"鼠标在 ImGui 面板上时不应传给 Editor"的问题。

**第一步：UIManager 读 ImGui 状态，填 ViewportInput**

UIManager 在每帧 `Render()` 中，通过 ImGui 查询视口 Image 控件的当前帧状态，填写 `ViewportInput` 快照：

```cpp
// UIManager 内部（伪代码展示逻辑，非实际函数名）
ImGui::Image(viewportSRV, viewportSize);  // 渲染视口图像

m_viewportInput.Hovered  = ImGui::IsItemHovered();   // 鼠标在视口上方
m_viewportInput.Active   = ImGui::IsItemActive();    // 正在与视口交互
m_viewportInput.Focused  = ImGui::IsItemFocused();   // 视口有焦点

ImVec2 mousePos  = ImGui::GetIO().MousePos;
ImVec2 itemMin   = ImGui::GetItemRectMin();
m_viewportInput.MouseLocal = { mousePos.x - itemMin.x,
                                mousePos.y - itemMin.y };  // 转成视口本地坐标
m_viewportInput.MouseDelta = { mousePos.x - m_lastLocal.x,
                                mousePos.y - m_lastLocal.y };

// 三个按键的 Pressed / Released / Down 状态
for (int i = 0; i < 3; ++i)
{
    auto& btn = m_viewportInput.MouseButtons[i];
    btn.Down     = ImGui::GetIO().MouseDown[i];
    btn.Pressed  = ImGui::GetIO().MouseClicked[i];
    btn.Released = ImGui::GetIO().MouseReleased[i];
}

m_viewportInput.Wheel     = ImGui::GetIO().MouseWheel;
m_viewportInput.Modifiers = ...;  // Shift/Ctrl/Alt 同 InputSystem
```

`ViewportInput` 是**一帧状态快照**，不是事件流——它记录的是"这一帧 ImGui 里的鼠标处于什么状态"。

**第二步：ViewportInputAdapter 把快照拆成事件列表**

`ViewportInputAdapter::BuildEvents(ViewportInput)` 的核心逻辑：

```cpp
// 派发条件：鼠标在视口上、视口活跃/聚焦、或已捕获（按键后移出视口仍继续）
bool ShouldDispatch(const ViewportInput& input) const
{
    return input.Hovered || input.Active || input.Focused || m_captured || ...;
}
```

满足条件后，把快照拆成零个或多个 `InputEvent`：

```
ViewportInput（一帧快照）
  ├─ MouseButtons[0].Pressed  → MouseButtonDown (Left)
  │   m_captured = true        ← 按下后开始捕获，离开视口也继续派发
  │   m_pressX/Y = MouseLocal  ← 记录按下位置
  ├─ MouseDelta != 0           → MouseMove
  │   LastMouseX = Local - Delta
  ├─ MouseButtons[i].Released  → MouseButtonUp
  │   所有按键都释放时 m_captured = false
  └─ Wheel != 0                → MouseWheel
```

键盘事件由 `ViewportInput.Keys[]` 数组提供（每个 `KeyCode` 有 `Pressed`/`Released`/`Down` 三个状态）。`UIManager` 读 `ImGui::GetIO().KeysDown[]` 并映射。

**两条通道的关系**：Win32 消息通道（通道 A）处理快捷键和全局按键；ImGui 通道（通道 B）处理视口内的鼠标和键盘，并加了视口焦点过滤。两者最终都调用同一个 `Editor::OnInput()`。

---

## 路径二：Web 端（Emscripten）

Web 版没有 ImGui，也没有 Win32 消息，直接注册浏览器事件回调，手动管理所有状态。

### 回调注册

```cpp
// main() 中注册，目标是 canvas 元素或 window
emscripten_set_mousedown_callback(kCanvas, nullptr, EM_TRUE, OnMouse);
emscripten_set_mouseup_callback  (kCanvas, nullptr, EM_TRUE, OnMouse);
emscripten_set_mousemove_callback(kCanvas, nullptr, EM_TRUE, OnMouse);
emscripten_set_wheel_callback    (kCanvas, nullptr, EM_TRUE, OnWheel);
emscripten_set_keydown_callback  (EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, OnKey);
emscripten_set_keyup_callback    (EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, OnKey);
```

鼠标事件注册在 canvas 上；键盘事件注册在 `window` 上（canvas 默认不接收键盘事件）。

### 鼠标坐标的 DPI 缩放

```cpp
int CanvasX(double cssX)
{
    // CSS 坐标 × (物理像素/CSS像素) = 物理像素坐标
    return static_cast<int>(cssX * g_width / g_cssWidth);
}
```

浏览器给出的是 CSS 坐标（逻辑像素），在高 DPI 屏幕（`devicePixelRatio = 2`）上，canvas 的物理分辨率是 CSS 尺寸的两倍，必须换算，否则鼠标点击位置偏差 2 倍。Win32 版不需要这步，因为 Win32 消息已经是物理像素。

### OnMouse — 鼠标事件回调

```cpp
EM_BOOL OnMouse(int eventType, const EmscriptenMouseEvent* e, void*)
{
    const auto button = ToMouseButton(e->button);
    // 浏览器 button: 0=左键 1=中键 2=右键
    // MiniCAD:       Left   Middle  Right

    if (eventType == EMSCRIPTEN_EVENT_MOUSEDOWN)
    {
        g_mouseButtons |= ButtonMask(button);   // 手动维护按键状态位掩码
        g_pressX = CanvasX(e->targetX);         // 记录框选起点
        g_pressY = CanvasY(e->targetY);
        // 构造 MouseButtonDown 事件 → Dispatch(e)
    }
    else if (eventType == EMSCRIPTEN_EVENT_MOUSEUP)
    {
        g_mouseButtons &= ~ButtonMask(button);  // 释放对应位
        // 构造 MouseButtonUp 事件 → Dispatch(e)
    }
    else if (eventType == EMSCRIPTEN_EVENT_MOUSEMOVE)
    {
        // 构造 MouseMove 事件，LastMouseX/Y = g_lastX/Y → Dispatch(e)
    }

    g_lastX = input.MouseX;   // 更新"上次位置"供下一帧 delta 计算
    g_lastY = input.MouseY;
    return EM_TRUE;           // 消费事件，阻止浏览器默认行为（如右键菜单）
}
```

Web 端没有 Win32 的 `SetCapture`，改用 `g_mouseButtons` 全局位掩码**手动跟踪**哪些键按着，每次 MOUSEDOWN 置位，MOUSEUP 清位。

### OnWheel — 滚轮

```cpp
EM_BOOL OnWheel(int, const EmscriptenWheelEvent* e, void*)
{
    input.WheelDelta = e->deltaY < 0.0 ? 1.0f : -1.0f;  // 归一化为 ±1
    // Win32 版：GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA，也归一化为 ±1
    Dispatch(input);
    return EM_TRUE;
}
```

浏览器的 `deltaY` 单位不固定（像素/行/页），MiniCAD 直接取符号归一化，与 Win32 版行为统一。

### OnKey — 键盘事件与浏览器保留键过滤

```cpp
EM_BOOL OnKey(int eventType, const EmscriptenKeyboardEvent* e, void*)
{
    // 必须先过滤浏览器保留键，否则会阻止开发者工具、刷新等
    if (IsBrowserReservedKey(e)) return EM_FALSE;  // 不消费，浏览器正常处理
    //  F5 / F11 / F12 / Ctrl+R / Ctrl+Shift+I/J/C

    input.Key = ToKeyCode(e);
    Dispatch(input);

    // 返回 EM_TRUE：消费事件（阻止 Space 滚动页面等副作用）
    // 返回 EM_FALSE：未识别按键，让浏览器处理
    return input.Key != KeyCode::Unknown ? EM_TRUE : EM_FALSE;
}
```

**键码映射 `ToKeyCode()`**：浏览器提供两个字段：
- `e->key`：字符串，如 `"a"`、`"A"`、`"Enter"`——适合字母数字
- `e->keyCode`：数字（DOM Level 3 已废弃但仍广泛支持）——适合功能键

```cpp
// 字母：e->key 是单个字母字符串
if (e->key[0] >= 'a' && e->key[0] <= 'z' && e->key[1] == '\0')
    return KeyCode::A + (e->key[0] - 'a');

// F 功能键：e->keyCode 在 112-123 范围
if (code >= 112 && code <= 123)
    return KeyCode::F1 + (code - 112);

// 其他控制键：switch-case 映射数字码
case 27: return KeyCode::Escape;
case 13: return KeyCode::Enter;
case 46: return KeyCode::Delete;
```

### Web 端 JS 调用 C++（工具栏按钮）

Web 版没有 ImGui 工具栏，工具栏由 HTML/JS 实现，通过 `ccall` 调用 C++ 导出函数：

```javascript
// JS 按钮点击
Module.ccall('MiniCAD_StartLine', null, [], []);
Module.ccall('MiniCAD_Undo',      null, [], []);
Module.ccall('MiniCAD_ToggleSnap',null, [], []);
```

对应 C++ 端：

```cpp
EMSCRIPTEN_KEEPALIVE void MiniCAD_StartLine()   { g_editor.StartLineTool(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_Undo()        { g_document->Undo(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_ToggleSnap()  { g_editor.ToggleSnap(); }
```

`EMSCRIPTEN_KEEPALIVE` 防止链接器死代码消除，`EXPORTED_FUNCTIONS` 列表在 CMakeLists.txt 中维护。这条路径是 Web 版"工具栏"的实现方式——与桌面版 `UIManager` 直接调用 `editor.StartLineTool()` 等价，只是多了一层 JS↔WASM 跨语言调用。

**文字输入特殊处理**：Web 版的文字输入弹窗是 HTML `<input>` 元素，输入完成后调用：

```javascript
Module.ccall('MiniCAD_SubmitText', null, ['string','number'], [text, height]);
```

C++ 端直接设置 `TextInputRequest` 并调用 `SubmitTextInput()`，绕过了 `InputEvent` 通道。

---

## 两条路径对比

| 问题 | Windows (Win32) | Web (Emscripten) |
|---|---|---|
| 事件来源 | WndProc WM_* 消息 | 浏览器事件回调 |
| 鼠标坐标单位 | 客户区物理像素（直接用） | CSS 逻辑像素（需 ×devicePixelRatio） |
| "鼠标移出后继续接收" | `SetCapture` / `ReleaseCapture` | 手动维护 `g_mouseButtons` 位掩码 |
| 滚轮归一化 | `GET_WHEEL_DELTA_WPARAM / WHEEL_DELTA` | `deltaY < 0 ? 1 : -1` |
| 键码来源 | `wParam`（VK_ 虚拟键码） | `e->key`（字符串）+ `e->keyCode`（数字） |
| 修饰键 | `GetKeyState(VK_SHIFT)` 实时查询 | `e->shiftKey / ctrlKey / altKey` 字段 |
| 保留键处理 | 无需特殊处理 | `IsBrowserReservedKey()` 过滤 F5/Ctrl+R 等 |
| 工具栏激活 | `UIManager` 直接调 `editor.StartXxx()` | JS `ccall` → `MiniCAD_StartXxx()` |
| 文字输入 | ImGui 弹窗 → `SubmitTextInput()` | HTML `<input>` → JS `ccall` → `SubmitTextInput()` |

---

## Editor::OnInput() 最终处理

无论哪条路径，事件最终都到达 `Editor::OnInput(const InputEvent& e)`：

```text
Editor::OnInput(e)
  ├─ HandleGlobal(e)
  │   ├─ Ctrl+Z → Undo()
  │   ├─ Ctrl+Y → Redo()
  │   ├─ Delete → DeleteSelected()
  │   ├─ 滚轮   → Viewport::Zoom()
  │   ├─ 中键拖 → Viewport::Pan()
  │   ├─ F3     → ToggleSnap()
  │   ├─ F8     → ToggleOrtho()
  │   └─ A-Z    → m_cmdBuffer 累积，Enter/Space 触发 ActivateToolByAlias()
  │
  ├─ GripEditor::OnInput(e)     ← 夹点激活中时优先处理
  │
  ├─ 当前 Tool::OnInput(ctx)    ← 工具处理（已有活跃工具时）
  │
  └─ Picking::OnInput(e)        ← 默认选择/悬停（无活跃工具时）
```

`HandleGlobal()` 先于工具处理，所以 `Ctrl+Z` 无论当前哪个工具激活都能响应。

---

## 课堂演示

**演示 1：跟踪 Win32 按键到工具**

在 `InputSystem::BuildEvent()` 中的 `WM_KEYDOWN` 分支加断点，按 `L` 键，单步跟踪：
```
WM_KEYDOWN wParam='L'(76)
FromWin32Key(76) → KeyCode::L
InputSystem::Dispatch() → HandleGlobal()
m_cmdBuffer = "L"
→ 再按 Enter → ActivateToolByAlias("L") → LineTool 激活
```

**演示 2：为什么 WM_MOUSEWHEEL 要 ScreenToClient()**

在 `BuildEvent()` 中临时注释掉 `ScreenToClient()`，在多显示器环境下滚动，观察缩放中心偏移。

**演示 3：Web 端 DPI 缩放**

在 `CanvasX()` 中打印 `g_cssWidth` 和 `g_width`，在 150% DPI 屏幕的浏览器中运行，观察两者比值为 1.5。

---

## 拓展练习

1. 给 `InputSystem::BuildEvent()` 增加对 `WM_LBUTTONDBLCLK` 的支持，映射为一个新的 `InputEventType::MouseDoubleClick`。
2. 在 Web 端 `OnKey()` 的 `IsBrowserReservedKey()` 里添加 `Ctrl+S`（保存），让 MiniCAD 在 Web 端也能响应保存快捷键。
3. 思考：Win32 的 `SetCapture` 和 Web 端的 `g_mouseButtons` 位掩码解决的是同一个问题（鼠标移出后继续跟踪），但机制完全不同——分别由谁来维护状态？
