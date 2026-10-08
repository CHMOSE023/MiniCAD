#pragma once
#include <emscripten/html5.h>
#include <functional>
#include <string>

namespace MiniCAD
{
    // 网页宿主：画布 + WebGL2 上下文 + 尺寸 / DPI 跟踪 + 按需刷新，与 Win32 的窗口 + D3D11 设备对应。
    //   - 画布由页面用 CSS 撑满（100vw × 100vh），绘图缓冲 = CSS 尺寸 × devicePixelRatio
    //   - RequestFrame 用 requestAnimationFrame 安排下一帧，多次调用只渲染一次；空闲时不渲染
    class WebHost
    {
    public:
        using ResizeFunc = std::function<void(float logicalWidth, float logicalHeight, float pixelScale)>;
        using FrameFunc  = std::function<void()>;

        explicit WebHost(std::string canvasSelector = "#canvas");
        ~WebHost();

        WebHost(const WebHost&) = delete;
        WebHost& operator=(const WebHost&) = delete;

        bool Initialize();                  // 创建 WebGL2 上下文并设为当前
        void SetOnResize(ResizeFunc f) { m_onResize = std::move(f); }
        void SetOnFrame (FrameFunc  f) { m_onFrame  = std::move(f); }

        void RequestFrame();
        void UpdateSize();                  // 重新读取画布 CSS 尺寸与 DPI，变化时回调 OnResize

        const std::string& GetCanvasSelector() const { return m_selector; }
        float GetLogicalWidth()  const { return m_cssWidth; }
        float GetLogicalHeight() const { return m_cssHeight; }
        float GetPixelScale()    const { return m_pixelScale; }
        int   GetFramebufferWidth()  const { return m_fbWidth; }
        int   GetFramebufferHeight() const { return m_fbHeight; }

    private:
        static bool OnResizeEvent(int type, const EmscriptenUiEvent* e, void* user);
        static bool OnAnimationFrame(double time, void* user);

    private:
        std::string m_selector;
        EMSCRIPTEN_WEBGL_CONTEXT_HANDLE m_gl = 0;
        float m_cssWidth   = 0.0f;
        float m_cssHeight  = 0.0f;
        float m_pixelScale = 1.0f;
        int   m_fbWidth    = 0;
        int   m_fbHeight   = 0;
        bool  m_framePending = false;
        ResizeFunc m_onResize;
        FrameFunc  m_onFrame;
    };
}
