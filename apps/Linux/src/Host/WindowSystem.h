#pragma once
#include "Core/Clipboard.h"
#include "Core/Event.hpp"
#include "Core/Types/Rect.hpp"
#include "IGraphicsDevice.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    class LinuxInput;
}

namespace MiniCAD
{
    struct WindowCallbacks
    {
        std::function<void()> resized;                  // 像素尺寸或缩放变了
        std::function<void()> exposed;
        std::function<void(bool active)> activeChanged;
        std::function<void()> stateChanged;             // 最大化 / 还原
        std::function<void()> closeRequested;
        std::function<void(std::vector<std::string> paths)> filesDropped;
        std::function<bool(MiniGUI::Vec2 logical)> captionTest;    // 自绘标题栏时：哪些点可拖动窗口
        // 子窗口拖标题栏移动（自己移动窗口，不交给窗口管理器）：光标与窗口左上角都是根窗口像素，回调里可以改窗口位置
        std::function<void(bool done, int cursorX, int cursorY, int& windowX, int& windowY)> moving;
        std::function<void()> moved;                    // 子窗口被窗口管理器移动 / 改大小之后
    };

    // CreateSubWindow 的参数：位置、尺寸为根窗口像素
    struct SubWindowParams
    {
        std::string title;
        int  x = 0, y = 0, width = 1, height = 1;
        bool document = false;          // false：工具窗口（总在 owner 之上、不占任务栏）；true：普通顶层窗口
        class WindowSystem* owner = nullptr;
    };

    // X11 / Wayland 窗口：LinuxApp 只经它开窗口、收事件、给图形设备窗口表面；剪贴板也在这里（MiniGUI::IClipboard）。
    // 主窗口由 CreateWindowSystem 建，负责连接与事件分发；浮动窗口由主窗口的 CreateSubWindow 建（只有 X11 支持），共用连接。
    // 坐标：窗口坐标 × GetPointScale() = 物理像素；界面缩放 GetScale()（逻辑像素 = 物理像素 / GetScale()）
    class WindowSystem : public MiniGUI::IClipboard
    {
    public:
        ~WindowSystem() override = default;

        virtual const char* GetName() const = 0;

        // width / height：逻辑像素
        virtual bool Open(const char* title, int width, int height, int minWidth, int minHeight) = 0;
        virtual void Close() = 0;
        virtual void Show() = 0;
        virtual bool IsAlive() const = 0;

        void SetCallbacks(WindowCallbacks callbacks) { m_callbacks = std::move(callbacks); }
        void SetInput(MiniGUI::LinuxInput* input) { m_input = input; }
        // 本窗口的界面没处理的按键转给它（浮出的停靠面板 → 主窗口，全局快捷键照常）
        void SetKeyForward(MiniGUI::LinuxInput* input) { m_keyForward = input; }

        virtual int   PixelWidth() const = 0;
        virtual int   PixelHeight() const = 0;
        virtual float GetScale() const = 0;
        virtual float GetPointScale() const = 0;
        virtual bool  HasClientDecorations() const { return false; }

        virtual void SetTitle(const std::string& utf8) = 0;
        virtual void SetIcon(int width, int height, const uint8_t* rgba) { (void)width; (void)height; (void)rgba; }
        virtual bool IsActive() const = 0;
        virtual bool IsMaximized() const = 0;
        virtual void Minimize() = 0;
        virtual void ToggleMaximize() = 0;
        virtual void Activate() {}

        virtual void SetCursor(MiniGUI::CursorShape shape) = 0;
        virtual void SetTextInput(bool enabled) = 0;
        virtual void SetTextCaret(const MiniGUI::Rect& caret) = 0;   // 窗口坐标

        virtual int  GetFd() const = 0;
        virtual bool PrepareWait() = 0;             // true：已有排队的事件，不要阻塞
        virtual void Dispatch(bool readable) = 0;   // 每次 PrepareWait 之后必须调用一次
        virtual int  TimeoutMs() const { return -1; }
        virtual void ProcessTimers() {}

        virtual bool CanRender() const { return true; }
        virtual void BeforePresent() {}

        virtual NativeWindow GetVulkanWindow() = 0;
        virtual unsigned GetEglPlatform() const = 0;
        virtual void*    GetEglDisplay() const = 0;
        virtual void*    GetEglWindow() = 0;
        virtual int      GetEglVisualId() const { return 0; }
        virtual bool     EglVsync() const { return true; }

        virtual std::string GetDialogParent() const { return {}; }

        // ── 浮动窗口（只有能知道、能设定窗口位置的 X11 支持；Wayland 返回空）──
        virtual std::unique_ptr<WindowSystem> CreateSubWindow(const SubWindowParams& params) { (void)params; return nullptr; }
        virtual bool GetRootPosition(int& x, int& y) const { (void)x; (void)y; return false; }   // 窗口左上角，根窗口像素
        virtual void SetRootRect(int x, int y, int width, int height) { (void)x; (void)y; (void)width; (void)height; }
        virtual void SetVisible(bool visible) { (void)visible; }

    protected:
        WindowCallbacks      m_callbacks;
        MiniGUI::LinuxInput* m_input = nullptr;
        MiniGUI::LinuxInput* m_keyForward = nullptr;
    };

    // preferred：x11 / wayland / 空（有 WAYLAND_DISPLAY 时先 Wayland）；连不上时试另一个
    std::unique_ptr<WindowSystem> CreateWindowSystem(const std::string& preferred);

    std::unique_ptr<WindowSystem> CreateX11WindowSystem();
    std::unique_ptr<WindowSystem> CreateWaylandWindowSystem();

    std::vector<std::string> ParseUriList(const std::string& text);
    std::string FileUriToPath(const std::string& uri);
    const char* const* CursorNames(MiniGUI::CursorShape shape);     // XCursor 主题里的名字，nullptr 结尾
}
