#include "Host/WebHost.h"
#include <emscripten.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace MiniCAD
{
    EM_JS_DEPS(MiniCADWebHost, "$UTF8ToString");

    // 画布尺寸变化（ResizeObserver，比 window 的 resize 事件可靠：页面布局、开发者工具、嵌入的面板都能捕获）
    // 与 devicePixelRatio 变化（浏览器缩放、拖到另一块屏幕）都回调 MiniCADWebHost_OnResize
    EM_JS(void, MiniCADWebHost_Observe, (const char* selectorPtr, void* host), {
        const canvas = document.querySelector(UTF8ToString(selectorPtr));
        if (!canvas) return;
        const notify = () => _MiniCADWebHost_OnResize(host);
        if (typeof ResizeObserver !== "undefined")
            new ResizeObserver(notify).observe(canvas);
        const watchDpr = () => {
            const mq = matchMedia("(resolution: " + devicePixelRatio + "dppx)");
            mq.addEventListener("change", () => { notify(); watchDpr(); }, { once: true });
        };
        watchDpr();
    });

    extern "C" EMSCRIPTEN_KEEPALIVE void MiniCADWebHost_OnResize(void* host)
    {
        static_cast<WebHost*>(host)->UpdateSize();
    }

    WebHost::WebHost(std::string canvasSelector)
        : m_selector(std::move(canvasSelector))
    {
    }

    WebHost::~WebHost()
    {
        emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, nullptr);
        if (m_gl)
            emscripten_webgl_destroy_context(m_gl);
    }

    bool WebHost::Initialize()
    {
        EmscriptenWebGLContextAttributes attr;
        emscripten_webgl_init_context_attributes(&attr);
        attr.majorVersion = 2;
        attr.minorVersion = 0;
        attr.alpha        = false;      // 不与页面背景混合
        attr.depth        = false;
        attr.stencil      = false;
        attr.antialias    = false;      // 界面自己做抗锯齿（羽化几何）
        attr.premultipliedAlpha    = false;
        attr.preserveDrawingBuffer = false;
        attr.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;

        m_gl = emscripten_webgl_create_context(m_selector.c_str(), &attr);
        if (m_gl <= 0)
        {
            std::printf("MiniCAD: 浏览器不支持 WebGL2（错误 %d）\n", static_cast<int>(m_gl));
            m_gl = 0;
            return false;
        }
        emscripten_webgl_make_context_current(m_gl);

        // 窗口尺寸变化、页面缩放（devicePixelRatio 变化）都会触发 resize
        emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, &WebHost::OnResizeEvent);
        MiniCADWebHost_Observe(m_selector.c_str(), this);
        UpdateSize();
        return true;
    }

    bool WebHost::OnResizeEvent(int, const EmscriptenUiEvent*, void* user)
    {
        static_cast<WebHost*>(user)->UpdateSize();
        return false;
    }

    void WebHost::UpdateSize()
    {
        double cssW = 0.0, cssH = 0.0;
        emscripten_get_element_css_size(m_selector.c_str(), &cssW, &cssH);
        const float scale = static_cast<float>(emscripten_get_device_pixel_ratio());
        const int   fbW   = std::max(1, static_cast<int>(std::lround(cssW * scale)));
        const int   fbH   = std::max(1, static_cast<int>(std::lround(cssH * scale)));

        if (fbW == m_fbWidth && fbH == m_fbHeight && scale == m_pixelScale &&
            static_cast<float>(cssW) == m_cssWidth && static_cast<float>(cssH) == m_cssHeight)
            return;

        m_cssWidth   = static_cast<float>(cssW);
        m_cssHeight  = static_cast<float>(cssH);
        m_pixelScale = scale;
        m_fbWidth    = fbW;
        m_fbHeight   = fbH;
        emscripten_set_canvas_element_size(m_selector.c_str(), fbW, fbH);

        if (m_onResize)
            m_onResize(m_cssWidth, m_cssHeight, m_pixelScale);
        RequestFrame();
    }

    void WebHost::RequestFrame()
    {
        if (m_framePending)
            return;
        m_framePending = true;
        emscripten_request_animation_frame(&WebHost::OnAnimationFrame, this);
    }

    bool WebHost::OnAnimationFrame(double, void* user)
    {
        auto* self = static_cast<WebHost*>(user);
        self->m_framePending = false;
        if (self->m_onFrame)
            self->m_onFrame();
        return false;   // 不自动续帧：下一帧由 RequestFrame 安排
    }
}
