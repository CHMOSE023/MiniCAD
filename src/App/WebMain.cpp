#include "Document/Document.h"
#include "Editor/Input/InputEvent.h"
#include "Editor/Input/KeyCode.h"
#include "Render/RendererFactory.hpp"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <memory>

namespace
{
    constexpr const char* kCanvas = "#minicad-canvas";

    std::unique_ptr<MiniCAD::IRenderer> g_renderer;
    std::unique_ptr<MiniCAD::Document> g_document;

    int g_width = 1;
    int g_height = 1;
    double g_cssWidth = 1.0;
    double g_cssHeight = 1.0;
    double g_devicePixelRatio = 1.0;
    int g_lastX = 0;
    int g_lastY = 0;
    int g_pressX = 0;
    int g_pressY = 0;
    unsigned g_mouseButtons = 0;

    int CanvasX(double targetX)
    {
        return static_cast<int>(targetX * static_cast<double>(g_width) / g_cssWidth);
    }

    int CanvasY(double targetY)
    {
        return static_cast<int>(targetY * static_cast<double>(g_height) / g_cssHeight);
    }

    uint8_t Modifiers(bool shift, bool ctrl, bool alt)
    {
        uint8_t result = 0;
        if (shift)
            result |= static_cast<uint8_t>(MiniCAD::ModifierKey::Shift);
        if (ctrl)
            result |= static_cast<uint8_t>(MiniCAD::ModifierKey::Ctrl);
        if (alt)
            result |= static_cast<uint8_t>(MiniCAD::ModifierKey::Alt);
        return result;
    }

    MiniCAD::MouseButton ToMouseButton(short button)
    {
        switch (button)
        {
        case 0: return MiniCAD::MouseButton::Left;
        case 1: return MiniCAD::MouseButton::Middle;
        case 2: return MiniCAD::MouseButton::Right;
        default: return MiniCAD::MouseButton::None;
        }
    }

    unsigned ButtonMask(MiniCAD::MouseButton button)
    {
        switch (button)
        {
        case MiniCAD::MouseButton::Left:
            return static_cast<unsigned>(MiniCAD::MouseButtonState::Left);
        case MiniCAD::MouseButton::Middle:
            return static_cast<unsigned>(MiniCAD::MouseButtonState::Middle);
        case MiniCAD::MouseButton::Right:
            return static_cast<unsigned>(MiniCAD::MouseButtonState::Right);
        default:
            return 0;
        }
    }

    MiniCAD::KeyCode ToKeyCode(const EmscriptenKeyboardEvent* e)
    {
        if (e->key[0] >= 'a' && e->key[0] <= 'z' && e->key[1] == '\0')
            return static_cast<MiniCAD::KeyCode>(
                static_cast<int>(MiniCAD::KeyCode::A) + (e->key[0] - 'a'));

        if (e->key[0] >= 'A' && e->key[0] <= 'Z' && e->key[1] == '\0')
            return static_cast<MiniCAD::KeyCode>(
                static_cast<int>(MiniCAD::KeyCode::A) + (e->key[0] - 'A'));

        if (e->key[0] >= '0' && e->key[0] <= '9' && e->key[1] == '\0')
            return static_cast<MiniCAD::KeyCode>(
                static_cast<int>(MiniCAD::KeyCode::Num0) + (e->key[0] - '0'));

        const auto code = e->keyCode;
        if (code >= 112 && code <= 123)
            return static_cast<MiniCAD::KeyCode>(
                static_cast<int>(MiniCAD::KeyCode::F1) + (code - 112));

        switch (code)
        {
        case 8: return MiniCAD::KeyCode::Backspace;
        case 9: return MiniCAD::KeyCode::Tab;
        case 13: return MiniCAD::KeyCode::Enter;
        case 16: return MiniCAD::KeyCode::Shift;
        case 17: return MiniCAD::KeyCode::Ctrl;
        case 18: return MiniCAD::KeyCode::Alt;
        case 27: return MiniCAD::KeyCode::Escape;
        case 32: return MiniCAD::KeyCode::Space;
        case 33: return MiniCAD::KeyCode::PageUp;
        case 34: return MiniCAD::KeyCode::PageDown;
        case 35: return MiniCAD::KeyCode::End;
        case 36: return MiniCAD::KeyCode::Home;
        case 37: return MiniCAD::KeyCode::Left;
        case 38: return MiniCAD::KeyCode::Up;
        case 39: return MiniCAD::KeyCode::Right;
        case 40: return MiniCAD::KeyCode::Down;
        case 45: return MiniCAD::KeyCode::Insert;
        case 46: return MiniCAD::KeyCode::Delete;
        default: return MiniCAD::KeyCode::Unknown;
        }
    }

    void Dispatch(const MiniCAD::InputEvent& e)
    {
        if (g_document)
            g_document->OnInput(e);
    }

    MiniCAD::InputEvent MakePointerEvent(MiniCAD::InputEventType type,
                                         MiniCAD::MouseButton button,
                                         double x,
                                         double y,
                                         bool shift,
                                         bool ctrl,
                                         bool alt)
    {
        MiniCAD::InputEvent input = {};
        input.Type = type;
        input.Button = button;
        input.MouseX = CanvasX(x);
        input.MouseY = CanvasY(y);
        input.LastMouseX = g_lastX;
        input.LastMouseY = g_lastY;
        input.PressMouseX = g_pressX;
        input.PressMouseY = g_pressY;
        input.Modifiers = Modifiers(shift, ctrl, alt);
        input.MouseButtons = static_cast<uint8_t>(g_mouseButtons);
        return input;
    }

    EM_BOOL OnMouse(int eventType, const EmscriptenMouseEvent* e, void*)
    {
        MiniCAD::InputEvent input = {};
        const auto button = ToMouseButton(e->button);

        if (eventType == EMSCRIPTEN_EVENT_MOUSEDOWN)
        {
            input = MakePointerEvent(MiniCAD::InputEventType::MouseButtonDown,
                                     button,
                                     e->targetX,
                                     e->targetY,
                                     e->shiftKey,
                                     e->ctrlKey,
                                     e->altKey);
            g_mouseButtons |= ButtonMask(button);
            g_pressX = input.MouseX;
            g_pressY = input.MouseY;
            input.PressMouseX = g_pressX;
            input.PressMouseY = g_pressY;
            input.MouseButtons = static_cast<uint8_t>(g_mouseButtons);
        }
        else if (eventType == EMSCRIPTEN_EVENT_MOUSEUP)
        {
            input = MakePointerEvent(MiniCAD::InputEventType::MouseButtonUp,
                                     button,
                                     e->targetX,
                                     e->targetY,
                                     e->shiftKey,
                                     e->ctrlKey,
                                     e->altKey);
            g_mouseButtons &= ~ButtonMask(button);
        }
        else if (eventType == EMSCRIPTEN_EVENT_MOUSEMOVE)
        {
            input = MakePointerEvent(MiniCAD::InputEventType::MouseMove,
                                     MiniCAD::MouseButton::None,
                                     e->targetX,
                                     e->targetY,
                                     e->shiftKey,
                                     e->ctrlKey,
                                     e->altKey);
        }
        else
        {
            return EM_FALSE;
        }

        if (eventType != EMSCRIPTEN_EVENT_MOUSEDOWN)
            input.MouseButtons = static_cast<uint8_t>(g_mouseButtons);

        Dispatch(input);

        g_lastX = input.MouseX;
        g_lastY = input.MouseY;
        return EM_TRUE;
    }

    EM_BOOL OnWheel(int, const EmscriptenWheelEvent* e, void*)
    {
        MiniCAD::InputEvent input = {};
        input.Type = MiniCAD::InputEventType::MouseWheel;
        input.MouseX = CanvasX(e->mouse.targetX);
        input.MouseY = CanvasY(e->mouse.targetY);
        input.LastMouseX = g_lastX;
        input.LastMouseY = g_lastY;
        input.PressMouseX = g_pressX;
        input.PressMouseY = g_pressY;
        input.Modifiers = Modifiers(e->mouse.shiftKey, e->mouse.ctrlKey, e->mouse.altKey);
        input.MouseButtons = static_cast<uint8_t>(g_mouseButtons);
        input.WheelDelta = e->deltaY < 0.0 ? 1.0f : -1.0f;
        Dispatch(input);
        return EM_TRUE;
    }

    EM_BOOL OnKey(int eventType, const EmscriptenKeyboardEvent* e, void*)
    {
        MiniCAD::InputEvent input = {};
        input.Type = eventType == EMSCRIPTEN_EVENT_KEYDOWN
            ? MiniCAD::InputEventType::KeyDown
            : MiniCAD::InputEventType::KeyUp;
        input.Key = ToKeyCode(e);
        input.MouseX = g_lastX;
        input.MouseY = g_lastY;
        input.LastMouseX = g_lastX;
        input.LastMouseY = g_lastY;
        input.PressMouseX = g_pressX;
        input.PressMouseY = g_pressY;
        input.Modifiers = Modifiers(e->shiftKey, e->ctrlKey, e->altKey);
        input.MouseButtons = static_cast<uint8_t>(g_mouseButtons);
        Dispatch(input);
        return input.Key != MiniCAD::KeyCode::Unknown ? EM_TRUE : EM_FALSE;
    }

    void ResizeIfNeeded()
    {
        double cssW = 0.0;
        double cssH = 0.0;
        emscripten_get_element_css_size(kCanvas, &cssW, &cssH);
        g_devicePixelRatio = emscripten_get_device_pixel_ratio();
        if (g_devicePixelRatio < 1.0)
            g_devicePixelRatio = 1.0;

        g_cssWidth = cssW > 1.0 ? cssW : 1.0;
        g_cssHeight = cssH > 1.0 ? cssH : 1.0;

        const int width = static_cast<int>(g_cssWidth * g_devicePixelRatio);
        const int height = static_cast<int>(g_cssHeight * g_devicePixelRatio);

        if (width == g_width && height == g_height)
            return;

        g_width = width;
        g_height = height;
        emscripten_set_canvas_element_size(kCanvas, g_width, g_height);

        if (g_document)
            g_document->Resize(static_cast<float>(g_width), static_cast<float>(g_height));
    }

    void MainLoop()
    {
        ResizeIfNeeded();
        if (g_document)
            g_document->Render();
    }
}

extern "C"
{
    // ── 绘制工具 ───────────────────────────────────────────────────
    EMSCRIPTEN_KEEPALIVE void MiniCAD_StartLine()
    {
        if (g_document) g_document->GetEditor().StartLineTool();
    }
    EMSCRIPTEN_KEEPALIVE void MiniCAD_StartPoint()
    {
        if (g_document) g_document->GetEditor().StartPointTool();
    }
    EMSCRIPTEN_KEEPALIVE void MiniCAD_StartCircle()
    {
        if (g_document) g_document->GetEditor().StartCircleTool();
    }
    EMSCRIPTEN_KEEPALIVE void MiniCAD_StartRectangle()
    {
        if (g_document) g_document->GetEditor().StartRectangleTool();
    }
    EMSCRIPTEN_KEEPALIVE void MiniCAD_StartArc()
    {
        if (g_document) g_document->GetEditor().StartArcTool();
    }
    EMSCRIPTEN_KEEPALIVE void MiniCAD_StartEllipse()
    {
        if (g_document) g_document->GetEditor().StartEllipseTool();
    }
    EMSCRIPTEN_KEEPALIVE void MiniCAD_StartPolyline()
    {
        if (g_document) g_document->GetEditor().StartPolylineTool();
    }
    EMSCRIPTEN_KEEPALIVE void MiniCAD_StartSpline()
    {
        if (g_document) g_document->GetEditor().StartSplineTool();
    }

    // ── 编辑工具 ───────────────────────────────────────────────────
    EMSCRIPTEN_KEEPALIVE void MiniCAD_StartMove()
    {
        if (g_document) g_document->GetEditor().StartMoveTool();
    }
    EMSCRIPTEN_KEEPALIVE void MiniCAD_StartCopy()
    {
        if (g_document) g_document->GetEditor().StartCopyTool();
    }
    EMSCRIPTEN_KEEPALIVE void MiniCAD_StartMirror()
    {
        if (g_document) g_document->GetEditor().StartMirrorTool();
    }
    EMSCRIPTEN_KEEPALIVE void MiniCAD_StartRotate()
    {
        if (g_document) g_document->GetEditor().StartRotateTool();
    }

    // ── 通用操作 ───────────────────────────────────────────────────
    EMSCRIPTEN_KEEPALIVE void MiniCAD_Delete()
    {
        if (g_document) g_document->GetEditor().DeleteSelected();
    }
    EMSCRIPTEN_KEEPALIVE void MiniCAD_ToggleSnap()
    {
        if (g_document) g_document->GetEditor().ToggleSnap();
    }
    EMSCRIPTEN_KEEPALIVE void MiniCAD_ToggleOrtho()
    {
        if (g_document) g_document->GetEditor().ToggleOrtho();
    }
    EMSCRIPTEN_KEEPALIVE void MiniCAD_Undo()
    {
        if (g_document) g_document->Undo();
    }
    EMSCRIPTEN_KEEPALIVE void MiniCAD_Redo()
    {
        if (g_document) g_document->Redo();
    }
    EMSCRIPTEN_KEEPALIVE void MiniCAD_Cancel()
    {
        MiniCAD::InputEvent e = {};
        e.Type = MiniCAD::InputEventType::KeyDown;
        e.Key  = MiniCAD::KeyCode::Escape;
        Dispatch(e);
    }
}

int main()
{
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = EM_TRUE;
    attrs.depth = EM_FALSE;
    attrs.stencil = EM_FALSE;
    attrs.antialias = EM_TRUE;

    const auto context = emscripten_webgl_create_context(kCanvas, &attrs);
    if (context <= 0)
        return 1;
    emscripten_webgl_make_context_current(context);

    ResizeIfNeeded();

    MiniCAD::RendererCreateInfo info;
    g_renderer = MiniCAD::CreateRenderer(info);
    g_document = std::make_unique<MiniCAD::Document>(*g_renderer,
                                                     static_cast<float>(g_width),
                                                     static_cast<float>(g_height));

    emscripten_set_mousedown_callback(kCanvas, nullptr, EM_TRUE, OnMouse);
    emscripten_set_mouseup_callback(kCanvas, nullptr, EM_TRUE, OnMouse);
    emscripten_set_mousemove_callback(kCanvas, nullptr, EM_TRUE, OnMouse);
    emscripten_set_wheel_callback(kCanvas, nullptr, EM_TRUE, OnWheel);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, OnKey);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, OnKey);

    emscripten_set_main_loop(MainLoop, 0, EM_TRUE);
    return 0;
}
