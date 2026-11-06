#include "Host/WindowSystem.h"
#include "Core/Log.h"
#include "Platform/Linux/LinuxInput.h"
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#ifdef MINICAD_X11_SHAPE
#include <X11/extensions/shape.h>
#endif
#undef CursorShape
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <climits>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#ifdef MINICAD_LINUX_VULKAN
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>
#endif

namespace MiniCAD
{
    namespace
    {
        constexpr unsigned kEglPlatform = 0x31D5;   // EGL_PLATFORM_X11_KHR

        enum AtomId
        {
            WM_PROTOCOLS, WM_DELETE_WINDOW, NET_WM_PING, NET_WM_PID, NET_WM_NAME, NET_WM_ICON, UTF8_STRING,
            NET_WM_STATE, NET_WM_STATE_MAXIMIZED_VERT, NET_WM_STATE_MAXIMIZED_HORZ,
            CLIPBOARD, TARGETS, TEXT, TEXT_PLAIN_UTF8, MINICAD_SELECTION,
            XdndAware, XdndEnter, XdndPosition, XdndStatus, XdndLeave, XdndDrop, XdndFinished,
            XdndActionCopy, XdndTypeList, XdndSelection, TEXT_URI_LIST,
            MOTIF_WM_HINTS, NET_WM_MOVERESIZE, NET_SUPPORTED,
            NET_WM_WINDOW_TYPE, NET_WM_WINDOW_TYPE_UTILITY, NET_WM_WINDOW_TYPE_NORMAL, NET_WM_STATE_SKIP_TASKBAR, NET_ACTIVE_WINDOW,
            NET_WM_USER_TIME,
            kAtomCount
        };

        constexpr const char* kAtomNames[kAtomCount] =
        {
            "WM_PROTOCOLS", "WM_DELETE_WINDOW", "_NET_WM_PING", "_NET_WM_PID", "_NET_WM_NAME", "_NET_WM_ICON", "UTF8_STRING",
            "_NET_WM_STATE", "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ",
            "CLIPBOARD", "TARGETS", "TEXT", "text/plain;charset=utf-8", "MINICAD_SELECTION",
            "XdndAware", "XdndEnter", "XdndPosition", "XdndStatus", "XdndLeave", "XdndDrop", "XdndFinished",
            "XdndActionCopy", "XdndTypeList", "XdndSelection", "text/uri-list",
            "_MOTIF_WM_HINTS", "_NET_WM_MOVERESIZE", "_NET_SUPPORTED",
            "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_UTILITY", "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_STATE_SKIP_TASKBAR", "_NET_ACTIVE_WINDOW",
            "_NET_WM_USER_TIME",
        };

        constexpr int kResizeBorder = 6;
        enum : unsigned { EdgeTop = 1, EdgeBottom = 2, EdgeLeft = 4, EdgeRight = 8 };
        constexpr long kMoveResizeMove = 8;

        uint8_t ToModifiers(unsigned state)
        {
            uint8_t m = 0;
            if (state & ShiftMask)   m |= static_cast<uint8_t>(MiniGUI::ModifierKey::Shift);
            if (state & ControlMask) m |= static_cast<uint8_t>(MiniGUI::ModifierKey::Ctrl);
            if (state & Mod1Mask)    m |= static_cast<uint8_t>(MiniGUI::ModifierKey::Alt);
            return m;
        }

        float ReadXftScale(Display* dpy)
        {
            XrmInitialize();
            const char* rms = XResourceManagerString(dpy);
            if (!rms)
                return 1.0f;
            float scale = 1.0f;
            if (XrmDatabase db = XrmGetStringDatabase(rms))
            {
                char* type = nullptr;
                XrmValue value{};
                if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &value) && value.addr)
                {
                    const double dpi = std::atof(value.addr);
                    if (dpi > 0.0)
                        scale = static_cast<float>(dpi / 96.0);
                }
                XrmDestroyDatabase(db);
            }
            return scale;
        }

        class X11Window;

        // 一个 X 连接：主窗口与浮动窗口共用（atom、输入法、光标、窗口表）
        struct X11Display
        {
            Display*  dpy = nullptr;
            int       screen = 0;
            ::Window  root = 0;
            Atom      atoms[kAtomCount] = {};
            float     scale = 1.0f;
            bool      wmMoveResize = false;
            bool      shape = false;
            XIM       im = nullptr;
            XIMStyle  imStyle = 0;
            Time      lastTime = CurrentTime;
            std::map<int, ::Cursor> cursors;
            std::vector<X11Window*> windows;
            std::vector<unsigned long> icon;     // _NET_WM_ICON，之后建的浮动窗口也用

            explicit X11Display(Display* d);
            ~X11Display();
            void     OpenInputMethod();
            ::Cursor LoadCursor(MiniGUI::CursorShape shape);
            X11Window* Find(::Window w) const;
        };

        X11Display::X11Display(Display* d)
            : dpy(d), screen(DefaultScreen(d)), root(RootWindow(d, DefaultScreen(d)))
        {
            XInternAtoms(dpy, const_cast<char**>(kAtomNames), kAtomCount, False, atoms);
            scale = ReadXftScale(dpy);
            Bool supported = False;
            XkbSetDetectableAutoRepeat(dpy, True, &supported);

            Atom type = None;
            int format = 0;
            unsigned long count = 0, after = 0;
            unsigned char* data = nullptr;
            if (XGetWindowProperty(dpy, root, atoms[NET_SUPPORTED], 0, 4096, False, XA_ATOM, &type, &format, &count, &after, &data) == Success && data)
            {
                const Atom* list = reinterpret_cast<const Atom*>(data);
                for (unsigned long i = 0; i < count; ++i)
                    wmMoveResize = wmMoveResize || list[i] == atoms[NET_WM_MOVERESIZE];
                XFree(data);
            }
#ifdef MINICAD_X11_SHAPE
            int event = 0, error = 0;
            shape = XShapeQueryExtension(dpy, &event, &error);
#endif
            OpenInputMethod();
        }

        X11Display::~X11Display()
        {
            for (auto& [_, c] : cursors)
                XFreeCursor(dpy, c);
            if (im)
                XCloseIM(im);
            XCloseDisplay(dpy);
        }

        void X11Display::OpenInputMethod()
        {
            const std::string previous = std::setlocale(LC_CTYPE, nullptr);
            std::setlocale(LC_CTYPE, "");
            XSetLocaleModifiers("");
            im = XOpenIM(dpy, nullptr, nullptr, nullptr);
            if (!im)
            {
                XSetLocaleModifiers("@im=none");
                im = XOpenIM(dpy, nullptr, nullptr, nullptr);
            }
            if (im)
            {
                XIMStyles* styles = nullptr;
                if (!XGetIMValues(im, XNQueryInputStyle, &styles, nullptr) && styles)
                {
                    for (unsigned short i = 0; i < styles->count_styles; ++i)
                        if (styles->supported_styles[i] == (XIMPreeditPosition | XIMStatusNothing))
                            imStyle = styles->supported_styles[i];
                    if (!imStyle)
                        for (unsigned short i = 0; i < styles->count_styles; ++i)
                            if (styles->supported_styles[i] == (XIMPreeditNothing | XIMStatusNothing))
                                imStyle = styles->supported_styles[i];
                    XFree(styles);
                }
            }
            std::setlocale(LC_CTYPE, previous.c_str());
        }

        ::Cursor X11Display::LoadCursor(MiniGUI::CursorShape shape)
        {
            const int key = static_cast<int>(shape);
            if (auto it = cursors.find(key); it != cursors.end())
                return it->second;
            ::Cursor cursor = 0;
            if (shape == MiniGUI::CursorShape::Hidden)
            {
                // xrdp 把全透明光标转给 mstsc 后显示成默认箭头；留一个 alpha=1 的像素才真正看不见
                if (XcursorImage* image = XcursorImageCreate(32, 32))
                {
                    std::memset(image->pixels, 0, 32 * 32 * sizeof(XcursorPixel));
                    image->pixels[0] = 0x01000000;
                    image->xhot = image->yhot = 0;
                    cursor = XcursorImageLoadCursor(dpy, image);
                    XcursorImageDestroy(image);
                }
            }
            if (shape == MiniGUI::CursorShape::Hidden && !cursor)
            {
                char zero = 0;
                const Pixmap pixmap = XCreateBitmapFromData(dpy, root, &zero, 1, 1);
                XColor black{};
                cursor = XCreatePixmapCursor(dpy, pixmap, pixmap, &black, &black, 0, 0);
                XFreePixmap(dpy, pixmap);
            }
            else if (shape != MiniGUI::CursorShape::Hidden)
            {
                for (const char* const* name = CursorNames(shape); *name && !cursor; ++name)
                    cursor = XcursorLibraryLoadCursor(dpy, *name);
                if (!cursor)
                    cursor = XCreateFontCursor(dpy, shape == MiniGUI::CursorShape::IBeam ? XC_xterm : XC_left_ptr);
            }
            cursors[key] = cursor;
            return cursor;
        }

        // 主窗口（m_main 为空，负责事件分发与剪贴板）或浮动窗口（CreateSubWindow）
        class X11Window final : public WindowSystem
        {
        public:
            X11Window(std::shared_ptr<X11Display> x, X11Window* main);
            ~X11Window() override;

            const char* GetName() const override { return "X11"; }
            bool Open(const char* title, int width, int height, int minWidth, int minHeight) override;
            bool OpenSub(const SubWindowParams& params);
            void Close() override;
            void Show() override;
            bool IsAlive() const override { return m_window != 0; }

            int   PixelWidth() const override  { return m_width; }
            int   PixelHeight() const override { return m_height; }
            float GetScale() const override    { return m_x->scale; }
            float GetPointScale() const override { return 1.0f; }
            bool  HasClientDecorations() const override { return m_csd; }

            void SetTitle(const std::string& utf8) override;
            void SetIcon(int width, int height, const uint8_t* rgba) override;
            bool IsActive() const override    { return m_active; }
            bool IsMaximized() const override { return m_maximized; }
            void Minimize() override;
            void ToggleMaximize() override;
            void Activate() override;

            void SetCursor(MiniGUI::CursorShape shape) override;
            void SetTextInput(bool enabled) override;
            void SetTextCaret(const MiniGUI::Rect& caret) override;

            int  GetFd() const override { return ConnectionNumber(m_x->dpy); }
            bool PrepareWait() override;
            void Dispatch(bool readable) override;

            NativeWindow GetVulkanWindow() override;
            unsigned GetEglPlatform() const override { return kEglPlatform; }
            void*    GetEglDisplay() const override  { return m_x->dpy; }
            void*    GetEglWindow() override         { return &m_window; }
            int      GetEglVisualId() const override { return static_cast<int>(XVisualIDFromVisual(DefaultVisual(m_x->dpy, m_x->screen))); }

            std::string GetDialogParent() const override;

            std::string GetText() override;
            void        SetText(std::string_view text) override;

            std::unique_ptr<WindowSystem> CreateSubWindow(const SubWindowParams& params) override;
            bool GetRootPosition(int& x, int& y) const override;
            void SetRootRect(int x, int y, int width, int height) override;
            void SetVisible(bool visible) override;

            ::Window GetWindow() const { return m_window; }
            void HandleEvent(XEvent& e);

        private:
            bool CreateNativeWindow(const char* title, int x, int y, int width, int height, int minWidth, int minHeight);
            void HandleKey(XKeyEvent& e, bool down);
            void HandleClientMessage(const XClientMessageEvent& e);
            void HandleSelectionRequest(const XSelectionRequestEvent& e);
            void HandleDropData(const XSelectionEvent& e);
            void SendXdnd(::Window target, Atom type, long l1, long l2, long l3, long l4);
            void SendNetWmState(long action, Atom a, Atom b);
            void UpdateWmState();
            void UpdateShape();
            void CreateInputContext();
            void UpdateIcFocus();
            std::string ReadProperty(::Window w, Atom property, Atom* type = nullptr);
            unsigned EdgesAt(int x, int y) const;
            void ApplyCursor();
            bool HandleFramePress(const XButtonEvent& e);
            void StartMoveResize(unsigned edges, int rootX, int rootY);
            void DragTo(int rootX, int rootY);
            void PointerPos(int x, int y, int rootX, int rootY, float& px, float& py) const;

            std::shared_ptr<X11Display> m_x;
            X11Window* m_main = nullptr;
            Atom*     m_atoms = nullptr;
            ::Window  m_window = 0;
            Colormap  m_colormap = 0;
            int       m_width = 0;
            int       m_height = 0;
            bool      m_active = false;
            bool      m_maximized = false;
            bool      m_document = false;

            XIC       m_ic = nullptr;
            bool      m_spotStyle = false;
            bool      m_textMode = false;

            std::bitset<256> m_pressed;
            std::array<KeySym, 256> m_pressedSym{};

            std::string m_clipboard;

            bool      m_csd = false;
            bool      m_clientMove = false;     // 子窗口：拖标题栏时自己移动窗口并回报（宿主据此找合并的落点）
            unsigned  m_edges = 0;
            MiniGUI::CursorShape m_uiCursor = MiniGUI::CursorShape::Arrow;
            Time      m_captionTime = 0;
            int       m_captionX = 0;
            int       m_captionY = 0;
            bool      m_pendingMove = false;    // 按在标题区域上、还没拖动：拖过几像素才开始移动，双击才认得出来
            int       m_pressRootX = 0;
            int       m_pressRootY = 0;
            struct
            {
                bool     active = false;
                unsigned edges = 0;
                int      rootX = 0, rootY = 0;
                int      x = 0, y = 0, w = 0, h = 0;
            } m_drag;
            int       m_minWidth = 1;
            int       m_minHeight = 1;
            int       m_shapeRadius = 0;
            int       m_shapeW = 0;
            int       m_shapeH = 0;

            // 子窗口在根窗口上的位置：SetRootRect 后立即生效，之后只认在那次请求之后产生的 ConfigureNotify（serial），
            // 指针坐标按它换算，与宿主看到的窗口位置一致（移动中不抖）
            int           m_rootX = 0;
            int           m_rootY = 0;
            unsigned long m_rectSerial = 0;
            int           m_expectW = 0;
            int           m_expectH = 0;

            ::Window  m_dndSource = 0;
            int       m_dndVersion = 0;
            bool      m_dndAccept = false;
        };

        X11Window* X11Display::Find(::Window w) const
        {
            for (X11Window* win : windows)
                if (win->GetWindow() == w)
                    return win;
            return nullptr;
        }

        X11Window::X11Window(std::shared_ptr<X11Display> x, X11Window* main)
            : m_x(std::move(x)), m_main(main)
        {
            m_atoms = m_x->atoms;
            m_x->windows.push_back(this);
        }

        X11Window::~X11Window()
        {
            Close();
            auto& list = m_x->windows;
            list.erase(std::remove(list.begin(), list.end(), this), list.end());
        }

        bool X11Window::CreateNativeWindow(const char* title, int x, int y, int width, int height, int minWidth, int minHeight)
        {
            Display* dpy = m_x->dpy;
            Visual* visual = DefaultVisual(dpy, m_x->screen);
            m_colormap = XCreateColormap(dpy, m_x->root, visual, AllocNone);
            m_width  = std::max(width, 1);
            m_height = std::max(height, 1);

            XSetWindowAttributes a{};
            a.colormap   = m_colormap;
            a.background_pixmap = None;
            a.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                           PointerMotionMask | EnterWindowMask | LeaveWindowMask | FocusChangeMask | PropertyChangeMask;
            m_window = XCreateWindow(dpy, m_x->root, x, y, m_width, m_height, 0, DefaultDepth(dpy, m_x->screen), InputOutput, visual,
                                     CWColormap | CWBackPixmap | CWEventMask, &a);
            if (!m_window)
            {
                LOG_ERROR_TR("创建窗口失败：{0}", "XCreateWindow");
                return false;
            }

            Atom protocols[] = { m_atoms[WM_DELETE_WINDOW], m_atoms[NET_WM_PING] };
            XSetWMProtocols(dpy, m_window, protocols, 2);

            XSizeHints* size = XAllocSizeHints();
            size->flags      = PMinSize | (m_main ? (USPosition | PPosition) : 0);
            size->x          = x;
            size->y          = y;
            size->min_width  = minWidth;
            size->min_height = minHeight;
            XClassHint* cls  = XAllocClassHint();
            cls->res_name    = const_cast<char*>("MiniCADLinux");
            cls->res_class   = const_cast<char*>("MiniCAD");
            XWMHints* wm     = XAllocWMHints();
            wm->flags        = InputHint | StateHint;
            wm->input        = True;
            wm->initial_state = NormalState;
            Xutf8SetWMProperties(dpy, m_window, title, title, nullptr, 0, size, wm, cls);
            XFree(size);
            XFree(cls);
            XFree(wm);
            SetTitle(title);

            m_minWidth  = minWidth;
            m_minHeight = minHeight;
            const char* deco = std::getenv("MINICAD_X11_DECORATIONS");
            m_csd = m_main || !(deco && std::strcmp(deco, "system") == 0);
            if (m_csd)
            {
                const long hints[5] = { 2, 0, 0, 0, 0 };    // MWM_HINTS_DECORATIONS，不要装饰
                XChangeProperty(dpy, m_window, m_atoms[MOTIF_WM_HINTS], m_atoms[MOTIF_WM_HINTS], 32, PropModeReplace,
                                reinterpret_cast<const unsigned char*>(hints), 5);
            }

            const long pid = static_cast<long>(getpid());
            XChangeProperty(dpy, m_window, m_atoms[NET_WM_PID], XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<const unsigned char*>(&pid), 1);
            if (!m_x->icon.empty())
                XChangeProperty(dpy, m_window, m_atoms[NET_WM_ICON], XA_CARDINAL, 32, PropModeReplace,
                                reinterpret_cast<const unsigned char*>(m_x->icon.data()), static_cast<int>(m_x->icon.size()));
            CreateInputContext();
            UpdateShape();
            return true;
        }

        bool X11Window::Open(const char* title, int width, int height, int minWidth, int minHeight)
        {
            const float s = m_x->scale;
            if (!CreateNativeWindow(title, 0, 0, static_cast<int>(width * s), static_cast<int>(height * s),
                                    static_cast<int>(minWidth * s), static_cast<int>(minHeight * s)))
                return false;
            const Atom version = 5;
            XChangeProperty(m_x->dpy, m_window, m_atoms[XdndAware], XA_ATOM, 32, PropModeReplace, reinterpret_cast<const unsigned char*>(&version), 1);
            return true;
        }

        bool X11Window::OpenSub(const SubWindowParams& p)
        {
            const float s = m_x->scale;
            if (!CreateNativeWindow(p.title.c_str(), p.x, p.y, p.width, p.height, static_cast<int>(160 * s), static_cast<int>(100 * s)))
                return false;
            Display* dpy = m_x->dpy;
            m_document   = p.document;
            m_clientMove = true;
            m_rootX = p.x;
            m_rootY = p.y;
            m_expectW = m_width;
            m_expectH = m_height;
            m_rectSerial = NextRequest(dpy);
            const Atom type = m_atoms[p.document ? NET_WM_WINDOW_TYPE_NORMAL : NET_WM_WINDOW_TYPE_UTILITY];
            XChangeProperty(dpy, m_window, m_atoms[NET_WM_WINDOW_TYPE], XA_ATOM, 32, PropModeReplace, reinterpret_cast<const unsigned char*>(&type), 1);
            if (!p.document)
            {
                const auto* owner = static_cast<const X11Window*>(p.owner ? p.owner : m_main);
                XSetTransientForHint(dpy, m_window, owner->m_window);
                const Atom state = m_atoms[NET_WM_STATE_SKIP_TASKBAR];
                XChangeProperty(dpy, m_window, m_atoms[NET_WM_STATE], XA_ATOM, 32, PropModeReplace, reinterpret_cast<const unsigned char*>(&state), 1);
            }
            return true;
        }

        void X11Window::Close()
        {
            if (m_ic)
                XDestroyIC(m_ic);
            m_ic = nullptr;
            if (m_window)
                XDestroyWindow(m_x->dpy, m_window);
            if (m_colormap)
                XFreeColormap(m_x->dpy, m_colormap);
            m_window   = 0;
            m_colormap = 0;
            XFlush(m_x->dpy);
        }

        void X11Window::Show()
        {
            XMapRaised(m_x->dpy, m_window);
            XFlush(m_x->dpy);
        }

        void X11Window::SetVisible(bool visible)
        {
            if (visible)
            {
                // 用户时间 0：窗口管理器映射时不给焦点（拖出标签时主窗口失焦会取消拖动）；要激活的随后经 Activate
                const long zero = 0;
                XChangeProperty(m_x->dpy, m_window, m_atoms[NET_WM_USER_TIME], XA_CARDINAL, 32, PropModeReplace,
                                reinterpret_cast<const unsigned char*>(&zero), 1);
                XMapWindow(m_x->dpy, m_window);
                XMoveWindow(m_x->dpy, m_window, m_rootX, m_rootY);   // 有的窗口管理器映射时不管 USPosition
            }
            else
                XUnmapWindow(m_x->dpy, m_window);
            m_rectSerial = NextRequest(m_x->dpy);
            XFlush(m_x->dpy);
        }

        void X11Window::CreateInputContext()
        {
            XIM im = m_x->im;
            const XIMStyle style = m_x->imStyle;
            if (!im || !style)
                return;
            m_spotStyle = (style & XIMPreeditPosition) != 0;
            if (m_spotStyle)
            {
                XPoint spot{ 0, 0 };
                XVaNestedList list = XVaCreateNestedList(0, XNSpotLocation, &spot, nullptr);
                m_ic = XCreateIC(im, XNInputStyle, style, XNClientWindow, m_window, XNFocusWindow, m_window, XNPreeditAttributes, list, nullptr);
                XFree(list);
            }
            else
                m_ic = XCreateIC(im, XNInputStyle, style, XNClientWindow, m_window, XNFocusWindow, m_window, nullptr);
            if (m_ic)
            {
                long filter = 0;
                XGetICValues(m_ic, XNFilterEvents, &filter, nullptr);
                XWindowAttributes attrs{};
                XGetWindowAttributes(m_x->dpy, m_window, &attrs);
                XSelectInput(m_x->dpy, m_window, attrs.your_event_mask | filter);
                XUnsetICFocus(m_ic);
            }
        }

        void X11Window::UpdateIcFocus()
        {
            if (!m_ic)
                return;
            if (m_textMode && m_active)
                XSetICFocus(m_ic);
            else
                XUnsetICFocus(m_ic);
        }

        void X11Window::SetTitle(const std::string& utf8)
        {
            Display* dpy = m_x->dpy;
            XChangeProperty(dpy, m_window, m_atoms[NET_WM_NAME], m_atoms[UTF8_STRING], 8, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(utf8.data()), static_cast<int>(utf8.size()));
            XTextProperty text{};
            char* list[] = { const_cast<char*>(utf8.c_str()) };
            if (Xutf8TextListToTextProperty(dpy, list, 1, XUTF8StringStyle, &text) == Success)
            {
                XSetWMName(dpy, m_window, &text);
                XFree(text.value);
            }
            XFlush(dpy);
        }

        void X11Window::SetIcon(int width, int height, const uint8_t* rgba)
        {
            std::vector<unsigned long>& data = m_x->icon;
            data.clear();
            data.reserve(2 + static_cast<size_t>(width) * height);
            data.push_back(static_cast<unsigned long>(width));
            data.push_back(static_cast<unsigned long>(height));
            for (int i = 0; i < width * height; ++i)
            {
                const uint8_t* p = rgba + i * 4;
                data.push_back((static_cast<unsigned long>(p[3]) << 24) | (static_cast<unsigned long>(p[0]) << 16) |
                               (static_cast<unsigned long>(p[1]) << 8) | p[2]);
            }
            for (X11Window* w : m_x->windows)
                XChangeProperty(m_x->dpy, w->m_window, m_atoms[NET_WM_ICON], XA_CARDINAL, 32, PropModeReplace,
                                reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()));
        }

        void X11Window::Minimize()
        {
            XIconifyWindow(m_x->dpy, m_window, m_x->screen);
            XFlush(m_x->dpy);
        }

        void X11Window::SendNetWmState(long action, Atom a, Atom b)
        {
            XEvent e{};
            e.xclient.type         = ClientMessage;
            e.xclient.window       = m_window;
            e.xclient.message_type = m_atoms[NET_WM_STATE];
            e.xclient.format       = 32;
            e.xclient.data.l[0]    = action;
            e.xclient.data.l[1]    = static_cast<long>(a);
            e.xclient.data.l[2]    = static_cast<long>(b);
            e.xclient.data.l[3]    = 1;
            XSendEvent(m_x->dpy, m_x->root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
            XFlush(m_x->dpy);
        }

        void X11Window::ToggleMaximize()
        {
            SendNetWmState(m_maximized ? 0 : 1, m_atoms[NET_WM_STATE_MAXIMIZED_VERT], m_atoms[NET_WM_STATE_MAXIMIZED_HORZ]);
        }

        void X11Window::Activate()
        {
            Display* dpy = m_x->dpy;
            const long time = static_cast<long>(m_x->lastTime);
            XChangeProperty(dpy, m_window, m_atoms[NET_WM_USER_TIME], XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<const unsigned char*>(&time), 1);
            XEvent e{};
            e.xclient.type         = ClientMessage;
            e.xclient.window       = m_window;
            e.xclient.message_type = m_atoms[NET_ACTIVE_WINDOW];
            e.xclient.format       = 32;
            e.xclient.data.l[0]    = 1;
            e.xclient.data.l[1]    = static_cast<long>(m_x->lastTime);
            XSendEvent(dpy, m_x->root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
            XRaiseWindow(dpy, m_window);
            XWindowAttributes attrs{};
            if (XGetWindowAttributes(dpy, m_window, &attrs) && attrs.map_state == IsViewable)
                XSetInputFocus(dpy, m_window, RevertToParent, CurrentTime);
            XFlush(dpy);
        }

        void X11Window::UpdateWmState()
        {
            Atom type = None;
            int format = 0;
            unsigned long count = 0, after = 0;
            unsigned char* data = nullptr;
            bool vert = false, horz = false;
            if (XGetWindowProperty(m_x->dpy, m_window, m_atoms[NET_WM_STATE], 0, 1024, False, XA_ATOM, &type, &format, &count, &after, &data) == Success && data)
            {
                const Atom* atoms = reinterpret_cast<const Atom*>(data);
                for (unsigned long i = 0; i < count; ++i)
                {
                    vert = vert || atoms[i] == m_atoms[NET_WM_STATE_MAXIMIZED_VERT];
                    horz = horz || atoms[i] == m_atoms[NET_WM_STATE_MAXIMIZED_HORZ];
                }
                XFree(data);
            }
            const bool maximized = vert && horz;
            if (maximized != m_maximized)
            {
                m_maximized = maximized;
                UpdateShape();
                if (m_callbacks.stateChanged)
                    m_callbacks.stateChanged();
            }
        }

        // 自绘标题栏的窗口用 Shape 扩展切出圆角（最大化时恢复成矩形）
        void X11Window::UpdateShape()
        {
#ifdef MINICAD_X11_SHAPE
            if (!m_x->shape || !m_window)
                return;
            const int w = m_width, h = m_height;
            int r = (m_csd && !m_maximized) ? static_cast<int>(std::lround(8.0f * m_x->scale)) : 0;
            r = std::max(0, std::min({ r, w / 2, h / 2 }));
            if (r == m_shapeRadius && (r == 0 || (w == m_shapeW && h == m_shapeH)))
                return;
            m_shapeRadius = r;
            m_shapeW = w;
            m_shapeH = h;
            if (r == 0)
            {
                XShapeCombineMask(m_x->dpy, m_window, ShapeBounding, 0, 0, None, ShapeSet);
                return;
            }
            std::vector<XRectangle> rects;
            rects.reserve(2 * r + 1);
            for (int y = 0; y < r; ++y)
            {
                const double dy = r - y - 0.5;
                const int dx = std::max(0, static_cast<int>(std::ceil(r - 0.5 - std::sqrt(double(r) * r - dy * dy))));
                const auto width = static_cast<unsigned short>(std::max(0, w - 2 * dx));
                rects.push_back({ static_cast<short>(dx), static_cast<short>(y), width, 1 });
                rects.push_back({ static_cast<short>(dx), static_cast<short>(h - 1 - y), width, 1 });
            }
            rects.push_back({ 0, static_cast<short>(r), static_cast<unsigned short>(w), static_cast<unsigned short>(h - 2 * r) });
            XShapeCombineRectangles(m_x->dpy, m_window, ShapeBounding, 0, 0, rects.data(), static_cast<int>(rects.size()), ShapeSet, Unsorted);
#endif
        }

        void X11Window::SetCursor(MiniGUI::CursorShape shape)
        {
            if (shape == m_uiCursor)
                return;
            m_uiCursor = shape;
            ApplyCursor();
        }

        void X11Window::ApplyCursor()
        {
            using MiniGUI::CursorShape;
            const unsigned edges = m_drag.active ? m_drag.edges : m_edges;
            CursorShape shape = m_uiCursor;
            if (edges)
            {
                const bool v = edges & (EdgeTop | EdgeBottom), h = edges & (EdgeLeft | EdgeRight);
                const bool nwse = edges == (EdgeTop | EdgeLeft) || edges == (EdgeBottom | EdgeRight);
                shape = v && h ? (nwse ? CursorShape::SizeNWSE : CursorShape::SizeNESW) : v ? CursorShape::SizeNS : CursorShape::SizeWE;
            }
            XDefineCursor(m_x->dpy, m_window, m_x->LoadCursor(shape));
        }

        // ── 自绘标题栏：缩放边框与标题区域 ─────────────────────────
        unsigned X11Window::EdgesAt(int x, int y) const
        {
            if (!m_csd || m_maximized)
                return 0;
            unsigned edges = 0;
            if (y < kResizeBorder)                  edges |= EdgeTop;
            else if (y >= m_height - kResizeBorder) edges |= EdgeBottom;
            if (x < kResizeBorder)                  edges |= EdgeLeft;
            else if (x >= m_width - kResizeBorder)  edges |= EdgeRight;
            return edges;
        }

        bool X11Window::HandleFramePress(const XButtonEvent& e)
        {
            if (!m_csd || e.button != Button1)
                return false;
            if (m_edges)
            {
                StartMoveResize(m_edges, e.x_root, e.y_root);
                return true;
            }
            float px = 0.0f, py = 0.0f;
            PointerPos(e.x, e.y, e.x_root, e.y_root, px, py);
            const MiniGUI::Vec2 logical{ px / m_x->scale, py / m_x->scale };
            if (!m_callbacks.captionTest || !m_callbacks.captionTest(logical))
                return false;
            if (m_captionTime && e.time - m_captionTime < 400 && std::abs(e.x - m_captionX) < 5 && std::abs(e.y - m_captionY) < 5)
            {
                m_captionTime = 0;
                ToggleMaximize();
                return true;
            }
            m_captionTime = e.time;
            m_captionX = e.x;
            m_captionY = e.y;
            m_pendingMove = true;
            m_pressRootX  = e.x_root;
            m_pressRootY  = e.y_root;
            return true;
        }

        void X11Window::StartMoveResize(unsigned edges, int rootX, int rootY)
        {
            if (m_x->wmMoveResize && !(m_clientMove && edges == 0))
            {
                // edges（上 1 下 2 左 4 右 8）→ _NET_WM_MOVERESIZE 的方向
                static constexpr long kDirection[16] = { kMoveResizeMove, 1, 5, -1, 7, 0, 6, -1, 3, 2, 4 };
                XUngrabPointer(m_x->dpy, CurrentTime);
                XEvent e{};
                e.xclient.type         = ClientMessage;
                e.xclient.window       = m_window;
                e.xclient.message_type = m_atoms[NET_WM_MOVERESIZE];
                e.xclient.format       = 32;
                e.xclient.data.l[0]    = rootX;
                e.xclient.data.l[1]    = rootY;
                e.xclient.data.l[2]    = kDirection[edges & 15];
                e.xclient.data.l[3]    = Button1;
                e.xclient.data.l[4]    = 1;
                XSendEvent(m_x->dpy, m_x->root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
                XFlush(m_x->dpy);
                return;
            }
            int x = 0, y = 0;
            GetRootPosition(x, y);
            m_drag = { true, edges, rootX, rootY, x, y, m_width, m_height };
        }

        void X11Window::DragTo(int rootX, int rootY)
        {
            const int dx = rootX - m_drag.rootX, dy = rootY - m_drag.rootY;
            if (!m_drag.edges)
            {
                int x = m_drag.x + dx, y = m_drag.y + dy;
                if (m_callbacks.moving)
                    m_callbacks.moving(false, rootX, rootY, x, y);
                SetRootRect(x, y, m_width, m_height);
                return;
            }
            int x = m_drag.x, y = m_drag.y, w = m_drag.w, h = m_drag.h;
            if (m_drag.edges & EdgeRight)  w = std::max(m_minWidth, m_drag.w + dx);
            if (m_drag.edges & EdgeBottom) h = std::max(m_minHeight, m_drag.h + dy);
            if (m_drag.edges & EdgeLeft)   { w = std::max(m_minWidth, m_drag.w - dx);  x = m_drag.x + m_drag.w - w; }
            if (m_drag.edges & EdgeTop)    { h = std::max(m_minHeight, m_drag.h - dy); y = m_drag.y + m_drag.h - h; }
            SetRootRect(x, y, w, h);
        }

        bool X11Window::GetRootPosition(int& x, int& y) const
        {
            if (m_main)
            {
                x = m_rootX;
                y = m_rootY;
                return true;
            }
            ::Window child = 0;
            return XTranslateCoordinates(m_x->dpy, m_window, m_x->root, 0, 0, &x, &y, &child);
        }

        void X11Window::SetRootRect(int x, int y, int width, int height)
        {
            width  = std::max(width, 1);
            height = std::max(height, 1);
            if (m_main)
            {
                m_rootX = x;
                m_rootY = y;
                m_expectW = width;
                m_expectH = height;
                m_rectSerial = NextRequest(m_x->dpy);
            }
            XMoveResizeWindow(m_x->dpy, m_window, x, y, static_cast<unsigned>(width), static_cast<unsigned>(height));
            XFlush(m_x->dpy);
        }

        void X11Window::PointerPos(int x, int y, int rootX, int rootY, float& px, float& py) const
        {
            if (m_main)
            {
                px = static_cast<float>(rootX - m_rootX);
                py = static_cast<float>(rootY - m_rootY);
            }
            else
            {
                px = static_cast<float>(x);
                py = static_cast<float>(y);
            }
        }

        void X11Window::SetTextInput(bool enabled)
        {
            m_textMode = enabled;
            UpdateIcFocus();
        }

        void X11Window::SetTextCaret(const MiniGUI::Rect& caret)
        {
            if (!m_ic || !m_spotStyle)
                return;
            XPoint spot{ static_cast<short>(caret.min.x), static_cast<short>(caret.max.y) };
            XVaNestedList list = XVaCreateNestedList(0, XNSpotLocation, &spot, nullptr);
            XSetICValues(m_ic, XNPreeditAttributes, list, nullptr);
            XFree(list);
        }

        std::string X11Window::GetDialogParent() const
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "x11:%lx", static_cast<unsigned long>(m_window));
            return buf;
        }

        std::unique_ptr<WindowSystem> X11Window::CreateSubWindow(const SubWindowParams& params)
        {
            X11Window* main = m_main ? m_main : this;
            auto w = std::make_unique<X11Window>(m_x, main);
            if (!w->OpenSub(params))
                return nullptr;
            return w;
        }

        // ── 事件：只有主窗口分发，按窗口转给对应的 X11Window ─────────────
        bool X11Window::PrepareWait()
        {
            XFlush(m_x->dpy);
            return XPending(m_x->dpy) > 0;
        }

        void X11Window::Dispatch(bool)
        {
            Display* dpy = m_x->dpy;
            while (XPending(dpy) > 0)
            {
                XEvent e;
                XNextEvent(dpy, &e);
                if (XFilterEvent(&e, None))
                    continue;
                if (e.type == SelectionRequest || e.type == SelectionClear)
                {
                    HandleEvent(e);
                    continue;
                }
                if (X11Window* target = m_x->Find(e.xany.window))
                    target->HandleEvent(e);
            }
        }

        void X11Window::HandleEvent(XEvent& e)
        {
            switch (e.type)
            {
            case KeyPress:
                m_x->lastTime = e.xkey.time;
                HandleKey(e.xkey, true);
                break;
            case KeyRelease:
                m_x->lastTime = e.xkey.time;
                HandleKey(e.xkey, false);
                break;
            case ButtonPress:
            case ButtonRelease:
            {
                m_x->lastTime = e.xbutton.time;
                const bool down = e.type == ButtonPress;
                if (m_pendingMove && e.xbutton.button == Button1)
                {
                    m_pendingMove = false;
                    if (!down)
                        break;
                }
                if (m_drag.active)
                {
                    if (!down && e.xbutton.button == Button1)
                    {
                        m_drag.active = false;
                        ApplyCursor();
                        if (m_callbacks.moving && !m_drag.edges)
                        {
                            int x = m_rootX, y = m_rootY;
                            m_callbacks.moving(true, e.xbutton.x_root, e.xbutton.y_root, x, y);
                        }
                        if (m_callbacks.moved)
                            m_callbacks.moved();
                    }
                    break;
                }
                if (down && HandleFramePress(e.xbutton))
                    break;
                if (!m_input)
                    break;
                m_input->SetModifiers(ToModifiers(e.xbutton.state));
                float x = 0.0f, y = 0.0f;
                PointerPos(e.xbutton.x, e.xbutton.y, e.xbutton.x_root, e.xbutton.y_root, x, y);
                switch (e.xbutton.button)
                {
                case Button1: m_input->PointerButton(x, y, MiniGUI::MouseButton::Left, down);   break;
                case Button2: m_input->PointerButton(x, y, MiniGUI::MouseButton::Middle, down); break;
                case Button3: m_input->PointerButton(x, y, MiniGUI::MouseButton::Right, down);  break;
                case Button4: if (down) m_input->PointerWheel(x, y, 0.0f, 1.0f);  break;
                case Button5: if (down) m_input->PointerWheel(x, y, 0.0f, -1.0f); break;
                case 6:       if (down) m_input->PointerWheel(x, y, -1.0f, 0.0f); break;
                case 7:       if (down) m_input->PointerWheel(x, y, 1.0f, 0.0f);  break;
                default: break;
                }
                break;
            }
            case MotionNotify:
            {
                if (m_pendingMove)
                {
                    if (!(e.xmotion.state & Button1Mask))
                        m_pendingMove = false;
                    else if (std::abs(e.xmotion.x_root - m_pressRootX) >= 4 || std::abs(e.xmotion.y_root - m_pressRootY) >= 4)
                    {
                        m_pendingMove = false;
                        StartMoveResize(0, m_pressRootX, m_pressRootY);
                        if (m_drag.active)
                            DragTo(e.xmotion.x_root, e.xmotion.y_root);
                    }
                    break;
                }
                if (m_drag.active)
                {
                    DragTo(e.xmotion.x_root, e.xmotion.y_root);
                    break;
                }
                float x = 0.0f, y = 0.0f;
                PointerPos(e.xmotion.x, e.xmotion.y, e.xmotion.x_root, e.xmotion.y_root, x, y);
                if (!(e.xmotion.state & (Button1Mask | Button2Mask | Button3Mask)))
                    if (const unsigned edges = EdgesAt(static_cast<int>(x), static_cast<int>(y)); edges != m_edges)
                    {
                        m_edges = edges;
                        ApplyCursor();
                    }
                if (m_edges)
                    break;
                if (m_input)
                {
                    m_input->SetModifiers(ToModifiers(e.xmotion.state));
                    m_input->PointerMove(x, y);
                }
                break;
            }
            case LeaveNotify:
                if (m_input && e.xcrossing.mode == NotifyNormal && !(e.xcrossing.state & (Button1Mask | Button2Mask | Button3Mask)))
                    m_input->PointerLeave();
                break;
            case FocusIn:
            case FocusOut:
            {
                if (e.xfocus.mode == NotifyGrab || e.xfocus.mode == NotifyUngrab || e.xfocus.detail == NotifyPointer)
                    break;
                const bool active = e.type == FocusIn;
                if (active == m_active)
                    break;
                m_active = active;
                UpdateIcFocus();
                if (!active)
                {
                    m_pressed.reset();
                    if (m_input)
                        m_input->FocusLost();
                }
                if (m_callbacks.activeChanged)
                    m_callbacks.activeChanged(active);
                break;
            }
            case ConfigureNotify:
            {
                bool moved = false;
                if (m_main && e.xconfigure.serial >= m_rectSerial)
                {
                    int x = 0, y = 0;
                    ::Window child = 0;
                    if (XTranslateCoordinates(m_x->dpy, m_window, m_x->root, 0, 0, &x, &y, &child) && (x != m_rootX || y != m_rootY))
                    {
                        m_rootX = x;
                        m_rootY = y;
                        moved = !m_drag.active;
                    }
                }
                if (e.xconfigure.width != m_width || e.xconfigure.height != m_height)
                {
                    m_width  = e.xconfigure.width;
                    m_height = e.xconfigure.height;
                    UpdateShape();
                    moved = moved || (m_main && !m_drag.active && (m_width != m_expectW || m_height != m_expectH));
                    if (m_callbacks.resized)
                        m_callbacks.resized();
                }
                if (moved && m_callbacks.moved)
                    m_callbacks.moved();
                break;
            }
            case Expose:
                if (e.xexpose.count == 0 && m_callbacks.exposed)
                    m_callbacks.exposed();
                break;
            case PropertyNotify:
                if (e.xproperty.atom == m_atoms[NET_WM_STATE])
                    UpdateWmState();
                break;
            case ClientMessage:
                HandleClientMessage(e.xclient);
                break;
            case SelectionRequest:
                HandleSelectionRequest(e.xselectionrequest);
                break;
            case SelectionClear:
                if (e.xselectionclear.selection == m_atoms[CLIPBOARD])
                    m_clipboard.clear();
                break;
            case SelectionNotify:
                if (e.xselection.selection == m_atoms[XdndSelection])
                    HandleDropData(e.xselection);
                break;
            default:
                break;
            }
        }

        void X11Window::HandleKey(XKeyEvent& e, bool down)
        {
            const unsigned code = e.keycode & 0xff;
            if (!down)
            {
                const KeySym sym = m_pressedSym[code];
                m_pressed[code] = false;
                if (m_input && sym)
                {
                    m_input->SetModifiers(ToModifiers(e.state));
                    if (!m_input->Key(static_cast<uint32_t>(sym), code - 8, false, false, {}) && m_keyForward)
                    {
                        m_keyForward->SetModifiers(ToModifiers(e.state));
                        m_keyForward->Key(static_cast<uint32_t>(sym), code - 8, false, false, {});
                    }
                }
                return;
            }

            char stack[64];
            std::string heap;
            char* buf = stack;
            KeySym sym = NoSymbol;
            Status status = XLookupNone;
            int n = 0;
            if (m_ic)
            {
                n = Xutf8LookupString(m_ic, &e, buf, sizeof(stack) - 1, &sym, &status);
                if (status == XBufferOverflow)
                {
                    heap.resize(static_cast<size_t>(n) + 1);
                    buf = heap.data();
                    n = Xutf8LookupString(m_ic, &e, buf, n, &sym, &status);
                }
            }
            else
            {
                n = XLookupString(&e, buf, sizeof(stack) - 1, &sym, nullptr);
                status = n > 0 ? XLookupBoth : XLookupKeySym;
                if (n > 0 && static_cast<unsigned char>(buf[0]) >= 0x80)
                    n = 0;
            }
            const bool hasChars = (status == XLookupChars || status == XLookupBoth) && n > 0;
            const bool hasSym   = status == XLookupKeySym || status == XLookupBoth;
            const std::string_view text = hasChars ? std::string_view(buf, static_cast<size_t>(n)) : std::string_view();

            if (!m_input)
                return;
            m_input->SetModifiers(ToModifiers(e.state));
            if (code == 0 || !hasSym)
            {
                if (!text.empty())
                    m_input->Text(text);
                return;
            }
            const bool repeat = m_pressed[code];
            m_pressed[code]    = true;
            m_pressedSym[code] = sym;
            if (!m_input->Key(static_cast<uint32_t>(sym), code - 8, true, repeat, text) && m_keyForward)
            {
                m_keyForward->SetModifiers(ToModifiers(e.state));
                m_keyForward->Key(static_cast<uint32_t>(sym), code - 8, true, repeat, text);
            }
        }

        void X11Window::SendXdnd(::Window target, Atom type, long l1, long l2, long l3, long l4)
        {
            XEvent e{};
            e.xclient.type         = ClientMessage;
            e.xclient.window       = target;
            e.xclient.message_type = type;
            e.xclient.format       = 32;
            e.xclient.data.l[0]    = static_cast<long>(m_window);
            e.xclient.data.l[1]    = l1;
            e.xclient.data.l[2]    = l2;
            e.xclient.data.l[3]    = l3;
            e.xclient.data.l[4]    = l4;
            XSendEvent(m_x->dpy, target, False, NoEventMask, &e);
            XFlush(m_x->dpy);
        }

        void X11Window::HandleClientMessage(const XClientMessageEvent& e)
        {
            Display* dpy = m_x->dpy;
            const Atom type = e.message_type;
            if (type == m_atoms[WM_PROTOCOLS])
            {
                const Atom protocol = static_cast<Atom>(e.data.l[0]);
                if (protocol == m_atoms[WM_DELETE_WINDOW])
                {
                    if (m_callbacks.closeRequested)
                        m_callbacks.closeRequested();
                }
                else if (protocol == m_atoms[NET_WM_PING])
                {
                    XEvent reply{};
                    reply.xclient = e;
                    reply.xclient.window = m_x->root;
                    XSendEvent(dpy, reply.xclient.window, False, SubstructureNotifyMask | SubstructureRedirectMask, &reply);
                }
            }
            else if (type == m_atoms[XdndEnter])
            {
                m_dndSource  = static_cast<::Window>(e.data.l[0]);
                m_dndVersion = static_cast<int>(static_cast<unsigned long>(e.data.l[1]) >> 24);
                m_dndAccept  = false;
                if (e.data.l[1] & 1)
                {
                    Atom actual = None;
                    int format = 0;
                    unsigned long count = 0, after = 0;
                    unsigned char* data = nullptr;
                    if (XGetWindowProperty(dpy, m_dndSource, m_atoms[XdndTypeList], 0, 1024, False, XA_ATOM, &actual, &format, &count, &after, &data) == Success && data)
                    {
                        const Atom* atoms = reinterpret_cast<const Atom*>(data);
                        for (unsigned long i = 0; i < count; ++i)
                            m_dndAccept = m_dndAccept || atoms[i] == m_atoms[TEXT_URI_LIST];
                        XFree(data);
                    }
                }
                else
                    for (int i = 2; i < 5; ++i)
                        m_dndAccept = m_dndAccept || static_cast<Atom>(e.data.l[i]) == m_atoms[TEXT_URI_LIST];
            }
            else if (type == m_atoms[XdndPosition])
            {
                SendXdnd(static_cast<::Window>(e.data.l[0]), m_atoms[XdndStatus], m_dndAccept ? 1 : 0, 0, 0,
                         m_dndAccept ? static_cast<long>(m_atoms[XdndActionCopy]) : 0);
            }
            else if (type == m_atoms[XdndLeave])
            {
                m_dndSource = 0;
                m_dndAccept = false;
            }
            else if (type == m_atoms[XdndDrop])
            {
                const ::Window source = static_cast<::Window>(e.data.l[0]);
                if (!m_dndAccept)
                {
                    SendXdnd(source, m_atoms[XdndFinished], 0, 0, 0, 0);
                    return;
                }
                m_dndSource = source;
                const Time time = m_dndVersion >= 1 ? static_cast<Time>(e.data.l[2]) : CurrentTime;
                XConvertSelection(dpy, m_atoms[XdndSelection], m_atoms[TEXT_URI_LIST], m_atoms[XdndSelection], m_window, time);
                XFlush(dpy);
            }
        }

        void X11Window::HandleDropData(const XSelectionEvent& e)
        {
            std::vector<std::string> paths;
            if (e.property != None)
                paths = ParseUriList(ReadProperty(m_window, e.property));
            if (m_dndSource)
                SendXdnd(m_dndSource, m_atoms[XdndFinished], paths.empty() ? 0 : 1, paths.empty() ? 0 : static_cast<long>(m_atoms[XdndActionCopy]), 0, 0);
            m_dndSource = 0;
            m_dndAccept = false;
            if (!paths.empty() && m_callbacks.filesDropped)
                m_callbacks.filesDropped(std::move(paths));
        }

        std::string X11Window::ReadProperty(::Window w, Atom property, Atom* typeOut)
        {
            Atom type = None;
            int format = 0;
            unsigned long count = 0, after = 0;
            unsigned char* data = nullptr;
            std::string result;
            if (XGetWindowProperty(m_x->dpy, w, property, 0, LONG_MAX / 4, True, AnyPropertyType, &type, &format, &count, &after, &data) == Success && data)
            {
                if (format == 8)
                    result.assign(reinterpret_cast<const char*>(data), count);
                XFree(data);
            }
            if (typeOut)
                *typeOut = type;
            return result;
        }

        // ── 剪贴板：归主窗口（浮动窗口转给它）─────────────────────────
        void X11Window::HandleSelectionRequest(const XSelectionRequestEvent& r)
        {
            Display* dpy = m_x->dpy;
            XEvent reply{};
            reply.xselection.type      = SelectionNotify;
            reply.xselection.requestor = r.requestor;
            reply.xselection.selection = r.selection;
            reply.xselection.target    = r.target;
            reply.xselection.property  = None;
            reply.xselection.time      = r.time;

            const Atom property = r.property != None ? r.property : r.target;
            if (r.selection == m_atoms[CLIPBOARD] && XGetSelectionOwner(dpy, m_atoms[CLIPBOARD]) == m_window)
            {
                if (r.target == m_atoms[TARGETS])
                {
                    const Atom targets[] = { m_atoms[TARGETS], m_atoms[UTF8_STRING], m_atoms[TEXT_PLAIN_UTF8], m_atoms[TEXT], XA_STRING };
                    XChangeProperty(dpy, r.requestor, property, XA_ATOM, 32, PropModeReplace,
                                    reinterpret_cast<const unsigned char*>(targets), static_cast<int>(std::size(targets)));
                    reply.xselection.property = property;
                }
                else if (r.target == m_atoms[UTF8_STRING] || r.target == m_atoms[TEXT_PLAIN_UTF8] || r.target == m_atoms[TEXT] || r.target == XA_STRING)
                {
                    XChangeProperty(dpy, r.requestor, property, r.target == m_atoms[TEXT] ? m_atoms[UTF8_STRING] : r.target, 8, PropModeReplace,
                                    reinterpret_cast<const unsigned char*>(m_clipboard.data()), static_cast<int>(m_clipboard.size()));
                    reply.xselection.property = property;
                }
            }
            XSendEvent(dpy, r.requestor, False, NoEventMask, &reply);
            XFlush(dpy);
        }

        std::string X11Window::GetText()
        {
            if (m_main)
                return m_main->GetText();
            Display* dpy = m_x->dpy;
            const ::Window owner = XGetSelectionOwner(dpy, m_atoms[CLIPBOARD]);
            if (owner == m_window)
                return m_clipboard;
            if (owner == None)
                return {};
            for (const Atom target : { m_atoms[UTF8_STRING], XA_STRING })
            {
                XDeleteProperty(dpy, m_window, m_atoms[MINICAD_SELECTION]);
                XConvertSelection(dpy, m_atoms[CLIPBOARD], target, m_atoms[MINICAD_SELECTION], m_window, m_x->lastTime);
                XFlush(dpy);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
                for (;;)
                {
                    XEvent e;
                    if (XCheckTypedWindowEvent(dpy, m_window, SelectionNotify, &e))
                    {
                        if (e.xselection.selection == m_atoms[XdndSelection])
                        {
                            HandleDropData(e.xselection);
                            continue;
                        }
                        if (e.xselection.property == None)
                            break;
                        Atom type = None;
                        std::string text = ReadProperty(m_window, e.xselection.property, &type);
                        if (type == XInternAtom(dpy, "INCR", False))
                            return {};
                        return text;
                    }
                    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
                    if (left <= 0)
                        return {};
                    pollfd fd{ ConnectionNumber(dpy), POLLIN, 0 };
                    poll(&fd, 1, static_cast<int>(left));
                }
            }
            return {};
        }

        void X11Window::SetText(std::string_view text)
        {
            if (m_main)
            {
                m_main->SetText(text);
                return;
            }
            m_clipboard.assign(text);
            XSetSelectionOwner(m_x->dpy, m_atoms[CLIPBOARD], m_window, m_x->lastTime);
            XFlush(m_x->dpy);
        }

        NativeWindow X11Window::GetVulkanWindow()
        {
            NativeWindow window;
#ifdef MINICAD_LINUX_VULKAN
            window.vulkanInstanceExtensions = { "VK_KHR_surface", "VK_KHR_xlib_surface" };
            window.createVulkanSurface = [dpy = m_x->dpy, win = m_window](void* instance, uint64_t* surface)
            {
                const auto inst   = static_cast<VkInstance>(instance);
                const auto create = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(vkGetInstanceProcAddr(inst, "vkCreateXlibSurfaceKHR"));
                VkXlibSurfaceCreateInfoKHR ci{ VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR };
                ci.dpy    = dpy;
                ci.window = win;
                VkSurfaceKHR s = VK_NULL_HANDLE;
                if (!create || create(inst, &ci, nullptr, &s) != VK_SUCCESS)
                {
                    LOG_WARN_TR("创建 Vulkan 窗口表面失败：{0}", "vkCreateXlibSurfaceKHR");
                    return false;
                }
                *surface = reinterpret_cast<uint64_t>(s);
                return true;
            };
#endif
            return window;
        }
    }

    std::unique_ptr<WindowSystem> CreateX11WindowSystem()
    {
        XInitThreads();
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy)
            return nullptr;
        return std::make_unique<X11Window>(std::make_shared<X11Display>(dpy), nullptr);
    }
}
