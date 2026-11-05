#pragma once

namespace MiniCAD
{
    // OpenGL ES 3 上下文（EGL）：X11 / Wayland 共用，建好后设为当前，GLES3GraphicsDevice 直接使用
    class EglContext
    {
    public:
        EglContext() = default;
        ~EglContext();

        EglContext(const EglContext&) = delete;
        EglContext& operator=(const EglContext&) = delete;

        // nativeWindow：X11 为 Window 变量的地址，Wayland 为 wl_egl_window*；visualId 不为 0 时优先选同一视觉的配置
        bool Create(unsigned platform, void* nativeDisplay, void* nativeWindow, int visualId, bool vsync);
        void Swap();

        // 浮动窗口：同一个上下文、各自的窗口表面（交换间隔 0，几个窗口不轮流等垂直同步）。surface 为空 = 主窗口的
        void* CreateSurface(void* nativeWindow);
        void  DestroySurface(void* surface);
        void  MakeCurrent(void* surface);
        void  Swap(void* surface);

    private:
        void* m_display = nullptr;
        void* m_context = nullptr;
        void* m_surface = nullptr;
        void* m_config  = nullptr;
        void* m_current = nullptr;
    };
}
