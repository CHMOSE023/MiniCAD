#include <emscripten.h>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "Document/DocumentManager.h"
#include "Document/GlyphTypes.h"
#include "Editor/Editor.h"
#include "Editor/Input/InputEvent.h"
#include "Editor/Input/KeyCode.h"
#include "Viewport/Viewport.h"
#include "Text/FontSystem.h"

#include "WebRenderer.h"
#include "WebRenderTarget.h"

using namespace MiniCAD;

// ── Global application state ────────────────────────────────────────────────
static std::unique_ptr<WebRenderer>     g_renderer;
static std::unique_ptr<WebRenderTarget> g_renderTarget;
static std::unique_ptr<DocumentManager> g_docManager;
static FontSystem                       g_fontSystem;

// Input state (track button/modifier state across events)
static int     g_lastMouseX   = 0;
static int     g_lastMouseY   = 0;
static int     g_pressMouseX  = 0;
static int     g_pressMouseY  = 0;
static uint8_t g_mouseButtons = 0;

// ── KeyCode mapping from browser KeyboardEvent.code strings ─────────────────
static KeyCode MapBrowserKey(const char* code)
{
    if (!code) return KeyCode::Unknown;
#define K(str, kc) if (strcmp(code, str) == 0) return KeyCode::kc
    K("KeyA",A); K("KeyB",B); K("KeyC",C); K("KeyD",D); K("KeyE",E);
    K("KeyF",F); K("KeyG",G); K("KeyH",H); K("KeyI",I); K("KeyJ",J);
    K("KeyK",K); K("KeyL",L); K("KeyM",M); K("KeyN",N); K("KeyO",O);
    K("KeyP",P); K("KeyQ",Q); K("KeyR",R); K("KeyS",S); K("KeyT",T);
    K("KeyU",U); K("KeyV",V); K("KeyW",W); K("KeyX",X); K("KeyY",Y);
    K("KeyZ",Z);
    K("Digit0",Num0); K("Digit1",Num1); K("Digit2",Num2); K("Digit3",Num3); K("Digit4",Num4);
    K("Digit5",Num5); K("Digit6",Num6); K("Digit7",Num7); K("Digit8",Num8); K("Digit9",Num9);
    K("Escape",Escape); K("Enter",Enter); K("Tab",Tab); K("Backspace",Backspace);
    K("Space",Space); K("Delete",Delete); K("Insert",Insert);
    K("ArrowLeft",Left); K("ArrowRight",Right); K("ArrowUp",Up); K("ArrowDown",Down);
    K("ShiftLeft",LShift); K("ShiftRight",RShift);
    K("ControlLeft",LCtrl); K("ControlRight",RCtrl);
    K("AltLeft",LAlt); K("AltRight",RAlt);
    K("F1",F1); K("F2",F2); K("F3",F3); K("F4",F4); K("F5",F5); K("F6",F6);
    K("F7",F7); K("F8",F8); K("F9",F9); K("F10",F10); K("F11",F11); K("F12",F12);
    K("Home",Home); K("End",End); K("PageUp",PageUp); K("PageDown",PageDown);
    K("Minus",OemMinus); K("Equal",OemPlus); K("Comma",OemComma); K("Period",OemPeriod);
    K("Slash",OemSlash); K("Backslash",OemBackslash); K("Semicolon",OemSemicolon);
    K("Quote",OemQuotes); K("BracketLeft",OemOpenBracket); K("BracketRight",OemCloseBracket);
    K("Backquote",OemGrave);
#undef K
    return KeyCode::Unknown;
}

// ── Exported C API ──────────────────────────────────────────────────────────

extern "C" {

EMSCRIPTEN_KEEPALIVE
void MiniCAD_Init(int width, int height)
{
    g_renderer      = std::make_unique<WebRenderer>();
    g_renderTarget  = std::make_unique<WebRenderTarget>(width, height);
    g_docManager    = std::make_unique<DocumentManager>();

    g_docManager->SetRenderer(g_renderer.get());
    g_docManager->SetRenderTarget(g_renderTarget.get());
    g_docManager->InitViewport(*g_renderer, (float)width, (float)height);

    g_fontSystem.Initialize();
    g_fontSystem.PreloadDefaultFonts();

    if (g_fontSystem.IsReady())
    {
        g_docManager->SetFontSystem(&g_fontSystem);     // 文字样式由各文档的文字样式表定义
        g_fontSystem.GetFontEngine().SetFontDir("/fonts/");
        g_docManager->Create();
    }
    else
    {
        g_docManager->Create();
    }
}

EMSCRIPTEN_KEEPALIVE
void MiniCAD_Resize(int width, int height)
{
    if (!g_docManager) return;
    g_renderTarget->Resize(width, height);
    auto& vp = g_docManager->GetViewport();
    vp.Resize((float)width, (float)height);
}

EMSCRIPTEN_KEEPALIVE
void MiniCAD_Tick()
{
    if (!g_docManager || !g_docManager->GetActive()) return;

    auto& editor   = g_docManager->GetEditor();
    auto& viewport = g_docManager->GetViewport();

    editor.Render();
    ViewState vs = editor.BuildViewState();
    viewport.Render(*g_renderer, *g_renderTarget, vs);
}

// ── Input ────────────────────────────────────────────────────────────────────

EMSCRIPTEN_KEEPALIVE
void MiniCAD_MouseMove(int x, int y, uint8_t btns, uint8_t mods)
{
    if (!g_docManager) return;
    g_mouseButtons = btns;

    InputEvent e;
    e.Type         = InputEventType::MouseMove;
    e.MouseX       = x;
    e.MouseY       = y;
    e.LastMouseX   = g_lastMouseX;
    e.LastMouseY   = g_lastMouseY;
    e.PressMouseX  = g_pressMouseX;
    e.PressMouseY  = g_pressMouseY;
    e.MouseButtons = btns;
    e.Modifiers    = mods;

    g_lastMouseX = x;
    g_lastMouseY = y;

    if (g_docManager->GetActive())
        g_docManager->GetEditor().OnInput(e);
}

EMSCRIPTEN_KEEPALIVE
void MiniCAD_MouseDown(int x, int y, int btn, uint8_t mods)
{
    if (!g_docManager) return;

    MouseButton mb = MouseButton::None;
    uint8_t     mask = 0;
    switch (btn) {
        case 0: mb = MouseButton::Left;   mask = (uint8_t)MouseButtonState::Left;   break;
        case 1: mb = MouseButton::Middle; mask = (uint8_t)MouseButtonState::Middle; break;
        case 2: mb = MouseButton::Right;  mask = (uint8_t)MouseButtonState::Right;  break;
    }
    g_mouseButtons |= mask;
    g_pressMouseX = x;
    g_pressMouseY = y;
    g_lastMouseX  = x;
    g_lastMouseY  = y;

    InputEvent e;
    e.Type         = InputEventType::MouseButtonDown;
    e.Button       = mb;
    e.MouseX       = x;
    e.MouseY       = y;
    e.LastMouseX   = x;
    e.LastMouseY   = y;
    e.PressMouseX  = x;
    e.PressMouseY  = y;
    e.MouseButtons = g_mouseButtons;
    e.Modifiers    = mods;

    if (g_docManager->GetActive())
        g_docManager->GetEditor().OnInput(e);
}

EMSCRIPTEN_KEEPALIVE
void MiniCAD_MouseUp(int x, int y, int btn, uint8_t mods)
{
    if (!g_docManager) return;

    MouseButton mb = MouseButton::None;
    uint8_t     mask = 0;
    switch (btn) {
        case 0: mb = MouseButton::Left;   mask = (uint8_t)MouseButtonState::Left;   break;
        case 1: mb = MouseButton::Middle; mask = (uint8_t)MouseButtonState::Middle; break;
        case 2: mb = MouseButton::Right;  mask = (uint8_t)MouseButtonState::Right;  break;
    }
    g_mouseButtons &= ~mask;

    InputEvent e;
    e.Type         = InputEventType::MouseButtonUp;
    e.Button       = mb;
    e.MouseX       = x;
    e.MouseY       = y;
    e.LastMouseX   = g_lastMouseX;
    e.LastMouseY   = g_lastMouseY;
    e.PressMouseX  = g_pressMouseX;
    e.PressMouseY  = g_pressMouseY;
    e.MouseButtons = g_mouseButtons;
    e.Modifiers    = mods;

    g_lastMouseX = x;
    g_lastMouseY = y;

    if (g_docManager->GetActive())
        g_docManager->GetEditor().OnInput(e);
}

EMSCRIPTEN_KEEPALIVE
void MiniCAD_Wheel(float delta, uint8_t mods)
{
    if (!g_docManager) return;

    InputEvent e;
    e.Type         = InputEventType::MouseWheel;
    e.WheelDelta   = delta;
    e.MouseX       = g_lastMouseX;
    e.MouseY       = g_lastMouseY;
    e.MouseButtons = g_mouseButtons;
    e.Modifiers    = mods;

    if (g_docManager->GetActive())
        g_docManager->GetEditor().OnInput(e);
}

EMSCRIPTEN_KEEPALIVE
void MiniCAD_KeyDown(const char* code, uint8_t mods)
{
    if (!g_docManager) return;

    InputEvent e;
    e.Type      = InputEventType::KeyDown;
    e.Key       = MapBrowserKey(code);
    e.Modifiers = mods;
    e.MouseX    = g_lastMouseX;
    e.MouseY    = g_lastMouseY;

    if (g_docManager->GetActive())
        g_docManager->GetEditor().OnInput(e);
}

EMSCRIPTEN_KEEPALIVE
void MiniCAD_KeyUp(const char* code, uint8_t mods)
{
    if (!g_docManager) return;

    InputEvent e;
    e.Type      = InputEventType::KeyUp;
    e.Key       = MapBrowserKey(code);
    e.Modifiers = mods;

    if (g_docManager->GetActive())
        g_docManager->GetEditor().OnInput(e);
}

EMSCRIPTEN_KEEPALIVE
void MiniCAD_RunCommand(const char* text)
{
    if (!g_docManager || !g_docManager->GetActive()) return;
    g_docManager->GetEditor().RunCommand(text ? text : "");
}

// ── Vertex read-back ────────────────────────────────────────────────────────

EMSCRIPTEN_KEEPALIVE
const float* MiniCAD_GetScreenLineData()  { return g_renderer ? g_renderer->ScreenLineData()  : nullptr; }
EMSCRIPTEN_KEEPALIVE
int          MiniCAD_GetScreenLineCount() { return g_renderer ? g_renderer->ScreenLineCount() : 0; }

EMSCRIPTEN_KEEPALIVE
const float* MiniCAD_GetScreenTriData()   { return g_renderer ? g_renderer->ScreenTriData()   : nullptr; }
EMSCRIPTEN_KEEPALIVE
int          MiniCAD_GetScreenTriCount()  { return g_renderer ? g_renderer->ScreenTriCount()  : 0; }

EMSCRIPTEN_KEEPALIVE
const float* MiniCAD_GetWorldLineData()   { return g_renderer ? g_renderer->WorldLineData()   : nullptr; }
EMSCRIPTEN_KEEPALIVE
int          MiniCAD_GetWorldLineCount()  { return g_renderer ? g_renderer->WorldLineCount()  : 0; }

EMSCRIPTEN_KEEPALIVE
const float* MiniCAD_GetWorldTriData()    { return g_renderer ? g_renderer->WorldTriData()    : nullptr; }
EMSCRIPTEN_KEEPALIVE
int          MiniCAD_GetWorldTriCount()   { return g_renderer ? g_renderer->WorldTriCount()   : 0; }

EMSCRIPTEN_KEEPALIVE
const float* MiniCAD_GetTextData()        { return g_renderer ? g_renderer->TextData()        : nullptr; }
EMSCRIPTEN_KEEPALIVE
int          MiniCAD_GetTextCount()       { return g_renderer ? g_renderer->TextCount()       : 0; }

EMSCRIPTEN_KEEPALIVE
const float* MiniCAD_GetScreenVP()        { return g_renderer ? g_renderer->ScreenVP()        : nullptr; }
EMSCRIPTEN_KEEPALIVE
const float* MiniCAD_GetWorldVP()         { return g_renderer ? g_renderer->WorldVP()         : nullptr; }

EMSCRIPTEN_KEEPALIVE
void MiniCAD_SetFontTexId(int id)         { if (g_renderer) g_renderer->SetFontTexId(id); }
EMSCRIPTEN_KEEPALIVE
int  MiniCAD_GetFontTexId()               { return g_renderer ? g_renderer->GetFontTexId() : 0; }

// ── UI state ─────────────────────────────────────────────────────────────────

EMSCRIPTEN_KEEPALIVE
const char* MiniCAD_GetPrompt()
{
    if (!g_docManager || !g_docManager->GetActive()) return "";
    return g_docManager->GetEditor().GetCmdLine().Prompt().c_str();
}

EMSCRIPTEN_KEEPALIVE
int MiniCAD_GetLineCount()
{
    if (!g_docManager || !g_docManager->GetActive()) return 0;
    return (int)g_docManager->GetEditor().GetCmdLine().Lines().size();
}

EMSCRIPTEN_KEEPALIVE
const char* MiniCAD_GetLine(int i)
{
    if (!g_docManager || !g_docManager->GetActive()) return "";
    const auto& lines = g_docManager->GetEditor().GetCmdLine().Lines();
    if (i < 0 || i >= (int)lines.size()) return "";
    return lines[i].c_str();
}

EMSCRIPTEN_KEEPALIVE
int MiniCAD_ConsumeScrollToBottom()
{
    if (!g_docManager || !g_docManager->GetActive()) return 0;
    return g_docManager->GetEditor().GetCmdLine().ConsumeScrollToBottom() ? 1 : 0;
}

// ── Tool shortcuts ────────────────────────────────────────────────────────────

EMSCRIPTEN_KEEPALIVE void MiniCAD_StartLine()      { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().StartLineTool(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_StartPoint()     { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().StartPointTool(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_StartRect()      { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().StartRectangleTool(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_StartCircle()    { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().StartCircleTool(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_StartArc()       { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().StartArcTool(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_StartEllipse()   { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().StartEllipseTool(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_StartPolyline()  { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().StartPolylineTool(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_StartSpline()    { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().StartSplineTool(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_StartText()      { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().StartTextTool(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_StartMText()     { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().StartMTextTool(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_StartMove()      { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().StartMoveTool(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_StartCopy()      { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().StartCopyTool(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_StartMirror()    { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().StartMirrorTool(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_StartRotate()    { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().StartRotateTool(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_DeleteSelected() { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().DeleteSelected(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_Undo()           { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().Undo(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_Redo()           { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().Redo(); }

EMSCRIPTEN_KEEPALIVE
int MiniCAD_IsSnapEnabled()  { return g_docManager && g_docManager->GetActive() ? (g_docManager->GetEditor().IsSnapEnabled()  ? 1 : 0) : 0; }
EMSCRIPTEN_KEEPALIVE
int MiniCAD_IsOrthoEnabled() { return g_docManager && g_docManager->GetActive() ? (g_docManager->GetEditor().IsOrthoEnabled() ? 1 : 0) : 0; }
EMSCRIPTEN_KEEPALIVE
void MiniCAD_ToggleSnap()    { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().ToggleSnap(); }
EMSCRIPTEN_KEEPALIVE
void MiniCAD_ToggleOrtho()   { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().ToggleOrtho(); }

// ── 对象捕捉模式（SnapMode 位掩码，见 SnapEngine.h）──────────────────────────
EMSCRIPTEN_KEEPALIVE
unsigned MiniCAD_GetSnapModes() { return g_docManager ? g_docManager->GetEditor().GetSnapEngine().GetSnapModes() : 0u; }
EMSCRIPTEN_KEEPALIVE
void MiniCAD_SetSnapModes(unsigned modes) { if (g_docManager) g_docManager->GetEditor().GetSnapEngine().SetSnapModes(modes); }
EMSCRIPTEN_KEEPALIVE
void MiniCAD_SetSnapRadius(double px) { if (g_docManager) g_docManager->GetEditor().GetSnapEngine().SetSnapRadiusPx(px); }

// ── Text input requests ───────────────────────────────────────────────────────

EMSCRIPTEN_KEEPALIVE
int MiniCAD_IsTextInputActive()  { return g_docManager && g_docManager->GetActive() ? (g_docManager->GetEditor().GetTextInputRequest().Active  ? 1 : 0) : 0; }
EMSCRIPTEN_KEEPALIVE
int MiniCAD_IsMTextInputActive() { return g_docManager && g_docManager->GetActive() ? (g_docManager->GetEditor().GetMTextInputRequest().Active ? 1 : 0) : 0; }
EMSCRIPTEN_KEEPALIVE
void MiniCAD_SubmitTextInput (const char* utf8) { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().SubmitTextInput (utf8 ? utf8 : ""); }
EMSCRIPTEN_KEEPALIVE
void MiniCAD_SubmitMTextInput(const char* utf8) { if (g_docManager && g_docManager->GetActive()) g_docManager->GetEditor().SubmitMTextInput(utf8 ? utf8 : ""); }

// ── Clipboard (entity copy / cut / paste) ────────────────────────────────────

EMSCRIPTEN_KEEPALIVE void MiniCAD_CopySelected() { if (g_docManager) g_docManager->CopySelected(); }
EMSCRIPTEN_KEEPALIVE void MiniCAD_CutSelected()  { if (g_docManager) g_docManager->CutSelected();  }
EMSCRIPTEN_KEEPALIVE void MiniCAD_Paste()        { if (g_docManager) g_docManager->Paste();        }

// ── Multi-document management ────────────────────────────────────────────────

EMSCRIPTEN_KEEPALIVE
void MiniCAD_NewDocument() { if (g_docManager) g_docManager->New(); }

EMSCRIPTEN_KEEPALIVE
int MiniCAD_GetDocumentCount() { return g_docManager ? (int)g_docManager->GetAll().size() : 0; }

EMSCRIPTEN_KEEPALIVE
const char* MiniCAD_GetDocumentName(int i)
{
    if (!g_docManager) return "";
    auto& docs = g_docManager->GetAll();
    if (i < 0 || i >= (int)docs.size()) return "";
    return docs[i]->GetName().c_str();
}

EMSCRIPTEN_KEEPALIVE
int MiniCAD_IsDocumentDirty(int i)
{
    if (!g_docManager) return 0;
    auto& docs = g_docManager->GetAll();
    if (i < 0 || i >= (int)docs.size()) return 0;
    return docs[i]->IsDirty() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int MiniCAD_GetActiveDocument()
{
    if (!g_docManager) return -1;
    auto& docs = g_docManager->GetAll();
    for (int i = 0; i < (int)docs.size(); ++i)
        if (docs[i].get() == g_docManager->GetActive()) return i;
    return -1;
}

EMSCRIPTEN_KEEPALIVE
void MiniCAD_SetActiveDocument(int i)
{
    if (!g_docManager) return;
    auto& docs = g_docManager->GetAll();
    if (i < 0 || i >= (int)docs.size()) return;
    g_docManager->SetActive(docs[i].get());
}

EMSCRIPTEN_KEEPALIVE
void MiniCAD_CloseDocument(int i)
{
    if (!g_docManager) return;
    auto& docs = g_docManager->GetAll();
    if (i < 0 || i >= (int)docs.size()) return;
    g_docManager->Close(docs[i].get());
}

// ── Document save / load via strings (JS 负责下载/上传) ─────────────────────

// 保存缓冲:返回指针 + 单独取长度(二进制内容含 \0,不能依赖 C 字符串结尾)
static std::string g_saveBuffer;

EMSCRIPTEN_KEEPALIVE
const char* MiniCAD_SaveActiveDocument(int binary)
{
    g_saveBuffer.clear();
    if (g_docManager && g_docManager->GetActive())
    {
        g_saveBuffer = g_docManager->GetActive()->SaveToString(binary != 0);
        g_docManager->GetActive()->MarkSaved();
    }
    return g_saveBuffer.data();
}

EMSCRIPTEN_KEEPALIVE
int MiniCAD_GetSaveDataSize() { return (int)g_saveBuffer.size(); }

// 数据导入为新文档并激活;格式按内容自动嗅探(二进制魔数/JSON)。成功返回 1。
EMSCRIPTEN_KEEPALIVE
int MiniCAD_LoadDocument(const char* data, int size, const char* name)
{
    if (!g_docManager || !data || size <= 0) return 0;

    auto& doc = g_docManager->Create();
    if (!doc.LoadFromString(std::string(data, (size_t)size)))
    {
        g_docManager->Close(&doc);
        return 0;
    }

    if (name && name[0])
        doc.SetName(name);
    return 1;
}

// ── View toggles ─────────────────────────────────────────────────────────────

EMSCRIPTEN_KEEPALIVE int  MiniCAD_IsGridShown()  { return g_docManager ? (g_docManager->GetViewport().IsGridShown()  ? 1 : 0) : 0; }
EMSCRIPTEN_KEEPALIVE int  MiniCAD_IsAxisShown()  { return g_docManager ? (g_docManager->GetViewport().IsAxisShown()  ? 1 : 0) : 0; }
EMSCRIPTEN_KEEPALIVE int  MiniCAD_IsGizmoShown() { return g_docManager ? (g_docManager->GetViewport().IsGizmoShown() ? 1 : 0) : 0; }
EMSCRIPTEN_KEEPALIVE void MiniCAD_ToggleGrid()   { if (g_docManager) g_docManager->GetViewport().ShowGridToggle();  }
EMSCRIPTEN_KEEPALIVE void MiniCAD_ToggleAxis()   { if (g_docManager) g_docManager->GetViewport().ShowAxisToggle();  }
EMSCRIPTEN_KEEPALIVE void MiniCAD_ToggleGizmo()  { if (g_docManager) g_docManager->GetViewport().ShowGizmoToggle(); }

// ── Generic tool start (覆盖 Trim/Extend/Offset/XLine/Ray/Dimension/Hatch 等) ──

EMSCRIPTEN_KEEPALIVE
void MiniCAD_StartTool(const char* toolId)
{
    if (g_docManager && g_docManager->GetActive() && toolId)
        g_docManager->GetEditor().ActivateToolById(toolId);
}

// Mouse position for status bar
EMSCRIPTEN_KEEPALIVE
float MiniCAD_GetMouseWorldX()
{
    if (!g_docManager || !g_docManager->GetActive()) return 0.f;
    auto wp = g_docManager->GetViewport().GetCamera().ScreenToWorld(g_lastMouseX, g_lastMouseY);
    return (float)wp.x;
}

EMSCRIPTEN_KEEPALIVE
float MiniCAD_GetMouseWorldY()
{
    if (!g_docManager || !g_docManager->GetActive()) return 0.f;
    auto wp = g_docManager->GetViewport().GetCamera().ScreenToWorld(g_lastMouseX, g_lastMouseY);
    return (float)wp.y;
}

} // extern "C"
