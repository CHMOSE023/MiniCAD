#include "Host/WindowSystem.h"
#include "Core/Log.h"
#include "Platform/Linux/LinuxInput.h"
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-egl.h>
#include <xkbcommon/xkbcommon.h>
#include "fractional-scale-v1-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>
#ifdef MINICAD_LINUX_VULKAN
#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>
#endif

namespace MiniCAD
{
    namespace
    {
        constexpr unsigned kEglPlatform = 0x31D8;   // EGL_PLATFORM_WAYLAND_KHR

        using Clock = std::chrono::steady_clock;

        constexpr uint32_t kBtnLeft   = 0x110;
        constexpr uint32_t kBtnRight  = 0x111;
        constexpr uint32_t kBtnMiddle = 0x112;
        constexpr float    kResizeBorder = 6.0f;

        const char* const kTextMimes[] = { "text/plain;charset=utf-8", "UTF8_STRING", "text/plain", "TEXT", "STRING" };

        struct Output
        {
            wl_output* output = nullptr;
            uint32_t   name   = 0;
            int        scale  = 1;
        };

        int CountCodepoints(std::string_view text)
        {
            int n = 0;
            for (unsigned char c : text)
                if ((c & 0xC0) != 0x80)
                    ++n;
            return n;
        }

        class WaylandWindow final : public WindowSystem
        {
        public:
            explicit WaylandWindow(wl_display* display);
            ~WaylandWindow() override;

            using WindowSystem::m_callbacks;
            using WindowSystem::m_input;

            bool Connect();

            const char* GetName() const override { return "Wayland"; }
            bool Open(const char* title, int width, int height, int minWidth, int minHeight) override;
            void Close() override;
            void Show() override {}
            bool IsAlive() const override { return m_alive; }

            int   PixelWidth() const override  { return m_pixelWidth; }
            int   PixelHeight() const override { return m_pixelHeight; }
            float GetScale() const override    { return m_scale; }
            float GetPointScale() const override { return m_scale; }
            bool  HasClientDecorations() const override { return m_csd; }

            void SetTitle(const std::string& utf8) override;
            bool IsActive() const override    { return m_activated; }
            bool IsMaximized() const override { return m_maximized; }
            void Minimize() override;
            void ToggleMaximize() override;

            void SetCursor(MiniGUI::CursorShape shape) override;
            void SetTextInput(bool enabled) override;
            void SetTextCaret(const MiniGUI::Rect& caret) override;

            int  GetFd() const override { return wl_display_get_fd(m_display); }
            bool PrepareWait() override;
            void Dispatch(bool readable) override;
            int  TimeoutMs() const override;
            void ProcessTimers() override;

            bool CanRender() const override;
            void BeforePresent() override;

            NativeWindow GetVulkanWindow() override;
            unsigned GetEglPlatform() const override { return kEglPlatform; }
            void*    GetEglDisplay() const override  { return m_display; }
            void*    GetEglWindow() override;
            bool     EglVsync() const override       { return false; }

            std::string GetText() override;
            void        SetText(std::string_view text) override;

            // ── 协议事件（监听表在类外）──
            void OnGlobal(wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
            void OnGlobalRemove(uint32_t name);
            void OnSeatCapabilities(uint32_t caps);
            void OnToplevelConfigure(int32_t width, int32_t height, wl_array* states);
            void OnSurfaceConfigure(uint32_t serial);
            void OnDecorationMode(uint32_t mode);
            void OnPointerEnter(uint32_t serial, wl_fixed_t x, wl_fixed_t y);
            void OnPointerLeave();
            void OnPointerMotion(wl_fixed_t x, wl_fixed_t y);
            void OnPointerButton(uint32_t serial, uint32_t button, uint32_t state);
            void OnPointerAxis(uint32_t axis, wl_fixed_t value);
            void OnPointerDiscrete(uint32_t axis, int32_t discrete);
            void OnPointerFrame();
            void OnKeymap(uint32_t format, int fd, uint32_t size);
            void OnKeyboardLeave();
            void OnKey(uint32_t serial, uint32_t key, uint32_t state);
            void OnModifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
            void OnTextInputDone();
            void OnDataOffer(wl_data_offer* offer);
            void OnDndEnter(uint32_t serial, wl_data_offer* offer);
            void OnDndLeave();
            void OnDndDrop();
            void OnSelection(wl_data_offer* offer);
            void OnSourceSend(wl_data_source* source, int fd);
            void OnSourceCancelled(wl_data_source* source);
            void UpdateScale();

            wl_display* m_display = nullptr;
            wl_registry* m_registry = nullptr;
            wl_compositor* m_compositor = nullptr;
            uint32_t m_compositorVersion = 0;
            wl_shm* m_shm = nullptr;
            wl_seat* m_seat = nullptr;
            uint32_t m_seatVersion = 0;
            xdg_wm_base* m_wmBase = nullptr;
            zxdg_decoration_manager_v1* m_decorationManager = nullptr;
            wp_viewporter* m_viewporter = nullptr;
            wp_fractional_scale_manager_v1* m_fractionalManager = nullptr;
            zwp_text_input_manager_v3* m_textInputManager = nullptr;
            wl_data_device_manager* m_dataDeviceManager = nullptr;
            uint32_t m_dataDeviceVersion = 0;
            std::vector<Output> m_outputs;

            wl_surface* m_surface = nullptr;
            xdg_surface* m_xdgSurface = nullptr;
            xdg_toplevel* m_toplevel = nullptr;
            zxdg_toplevel_decoration_v1* m_decoration = nullptr;
            wp_viewport* m_viewport = nullptr;
            wp_fractional_scale_v1* m_fractional = nullptr;
            wl_egl_window* m_eglWindow = nullptr;
            wl_callback* m_frameCallback = nullptr;
            Clock::time_point m_frameRequested{};
            std::vector<wl_output*> m_entered;

            bool  m_alive = true;
            bool  m_reading = false;
            bool  m_configured = false;
            bool  m_csd = false;
            bool  m_maximized = false;
            bool  m_activated = false;
            int   m_width = 0;
            int   m_height = 0;
            int   m_pendingWidth = 0;
            int   m_pendingHeight = 0;
            bool  m_pendingMaximized = false;
            bool  m_pendingActivated = false;
            int   m_pixelWidth = 0;
            int   m_pixelHeight = 0;
            float m_scale = 1.0f;
            int   m_bufferScale = 1;
            uint32_t m_fractionalScale = 0;
            int   m_preferredBufferScale = 0;
            bool  m_geometryDirty = true;

            wl_pointer* m_pointer = nullptr;
            uint32_t m_pointerSerial = 0;
            uint32_t m_lastSerial = 0;
            float m_px = 0.0f;
            float m_py = 0.0f;
            uint32_t m_edges = 0;
            double m_axis[2] = {};
            int32_t m_discrete[2] = {};
            bool m_hasAxis = false;
            bool m_hasDiscrete[2] = {};
            Clock::time_point m_captionClick{};
            float m_captionX = 0.0f;
            float m_captionY = 0.0f;
            bool     m_pendingMove = false;     // 按在标题区域上、还没拖动：拖过几像素才交给合成器移动，双击才认得出来
            uint32_t m_moveSerial = 0;
            MiniGUI::CursorShape m_uiCursor = MiniGUI::CursorShape::Arrow;
            int   m_appliedCursor = -1;
            wl_cursor_theme* m_cursorTheme = nullptr;
            int   m_cursorThemeScale = 0;
            wl_surface* m_cursorSurface = nullptr;

            wl_keyboard* m_keyboard = nullptr;
            xkb_context* m_xkb = nullptr;
            xkb_keymap* m_keymap = nullptr;
            xkb_state* m_xkbState = nullptr;
            int32_t m_repeatRate = 25;
            int32_t m_repeatDelay = 400;
            uint32_t m_repeatCode = 0;
            Clock::time_point m_repeatNext{};

            zwp_text_input_v3* m_textInput = nullptr;
            bool m_textMode = false;
            bool m_textEntered = false;
            MiniGUI::Rect m_caret{ 0.0f, 0.0f, 1.0f, 20.0f };
            std::string m_preedit;
            int32_t m_preeditCursor = -1;
            std::string m_commit;

            wl_data_device* m_dataDevice = nullptr;
            std::map<wl_data_offer*, std::vector<std::string>> m_offers;
            wl_data_offer* m_selectionOffer = nullptr;
            wl_data_offer* m_dndOffer = nullptr;
            bool m_dndAccept = false;
            wl_data_source* m_source = nullptr;
            std::string m_clipboard;

        private:
            void ApplySize();
            void ApplyCursor(MiniGUI::CursorShape shape);
            uint32_t EdgesAt(float x, float y) const;
            void EnableTextInput();
            void CreateSeatObjects();
            void DestroyOffer(wl_data_offer* offer);
            std::string ReceiveOffer(wl_data_offer* offer, const char* mime);
            void EmitKey(uint32_t code, bool down, bool repeat);
        };

        WaylandWindow* Self(void* data) { return static_cast<WaylandWindow*>(data); }

        // ── 监听表 ────────────────────────────────────────────────
        const wl_registry_listener kRegistryListener =
        {
            [](void* d, wl_registry* r, uint32_t name, const char* iface, uint32_t version) { Self(d)->OnGlobal(r, name, iface, version); },
            [](void* d, wl_registry*, uint32_t name) { Self(d)->OnGlobalRemove(name); },
        };

        const xdg_wm_base_listener kWmBaseListener =
        {
            [](void*, xdg_wm_base* base, uint32_t serial) { xdg_wm_base_pong(base, serial); },
        };

        const wl_output_listener kOutputListener =
        {
            [](void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {},
            [](void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {},
            [](void* d, wl_output*) { Self(d)->UpdateScale(); },
            [](void* d, wl_output* o, int32_t factor)
            {
                for (Output& out : Self(d)->m_outputs)
                    if (out.output == o)
                        out.scale = std::max(1, factor);
            },
        };

        const wl_surface_listener kSurfaceListener =
        {
            [](void* d, wl_surface*, wl_output* o) { Self(d)->m_entered.push_back(o); Self(d)->UpdateScale(); },
            [](void* d, wl_surface*, wl_output* o)
            {
                auto& v = Self(d)->m_entered;
                v.erase(std::remove(v.begin(), v.end(), o), v.end());
                Self(d)->UpdateScale();
            },
#ifdef WL_SURFACE_PREFERRED_BUFFER_SCALE_SINCE_VERSION
            [](void* d, wl_surface*, int32_t factor) { Self(d)->m_preferredBufferScale = std::max(1, factor); Self(d)->UpdateScale(); },
            [](void*, wl_surface*, uint32_t) {},
#endif
        };

        const wl_callback_listener kFrameListener =
        {
            [](void* d, wl_callback* cb, uint32_t)
            {
                wl_callback_destroy(cb);
                if (Self(d)->m_frameCallback == cb)
                    Self(d)->m_frameCallback = nullptr;
            },
        };

        const xdg_surface_listener kXdgSurfaceListener =
        {
            [](void* d, xdg_surface*, uint32_t serial) { Self(d)->OnSurfaceConfigure(serial); },
        };

        const xdg_toplevel_listener kToplevelListener =
        {
            [](void* d, xdg_toplevel*, int32_t w, int32_t h, wl_array* states) { Self(d)->OnToplevelConfigure(w, h, states); },
            [](void* d, xdg_toplevel*)
            {
                if (Self(d)->m_callbacks.closeRequested)
                    Self(d)->m_callbacks.closeRequested();
            },
        };

        const zxdg_toplevel_decoration_v1_listener kDecorationListener =
        {
            [](void* d, zxdg_toplevel_decoration_v1*, uint32_t mode) { Self(d)->OnDecorationMode(mode); },
        };

        const wp_fractional_scale_v1_listener kFractionalListener =
        {
            [](void* d, wp_fractional_scale_v1*, uint32_t scale) { Self(d)->m_fractionalScale = scale; Self(d)->UpdateScale(); },
        };

        const wl_seat_listener kSeatListener =
        {
            [](void* d, wl_seat*, uint32_t caps) { Self(d)->OnSeatCapabilities(caps); },
            [](void*, wl_seat*, const char*) {},
        };

        const wl_pointer_listener kPointerListener =
        {
            [](void* d, wl_pointer*, uint32_t serial, wl_surface*, wl_fixed_t x, wl_fixed_t y) { Self(d)->OnPointerEnter(serial, x, y); },
            [](void* d, wl_pointer*, uint32_t, wl_surface*) { Self(d)->OnPointerLeave(); },
            [](void* d, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) { Self(d)->OnPointerMotion(x, y); },
            [](void* d, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state) { Self(d)->OnPointerButton(serial, button, state); },
            [](void* d, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) { Self(d)->OnPointerAxis(axis, value); },
            [](void* d, wl_pointer*) { Self(d)->OnPointerFrame(); },
            [](void*, wl_pointer*, uint32_t) {},
            [](void*, wl_pointer*, uint32_t, uint32_t) {},
            [](void* d, wl_pointer*, uint32_t axis, int32_t discrete) { Self(d)->OnPointerDiscrete(axis, discrete); },
        };

        const wl_keyboard_listener kKeyboardListener =
        {
            [](void* d, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) { Self(d)->OnKeymap(format, fd, size); },
            [](void* d, wl_keyboard*, uint32_t serial, wl_surface*, wl_array*) { Self(d)->m_lastSerial = serial; },
            [](void* d, wl_keyboard*, uint32_t, wl_surface*) { Self(d)->OnKeyboardLeave(); },
            [](void* d, wl_keyboard*, uint32_t serial, uint32_t, uint32_t key, uint32_t state) { Self(d)->OnKey(serial, key, state); },
            [](void* d, wl_keyboard*, uint32_t, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group) { Self(d)->OnModifiers(dep, lat, lock, group); },
            [](void* d, wl_keyboard*, int32_t rate, int32_t delay) { Self(d)->m_repeatRate = rate; Self(d)->m_repeatDelay = delay; },
        };

        const zwp_text_input_v3_listener kTextInputListener =
        {
            [](void* d, zwp_text_input_v3*, wl_surface*)
            {
                Self(d)->m_textEntered = true;
                if (Self(d)->m_textMode)
                    Self(d)->SetTextInput(true);
            },
            [](void* d, zwp_text_input_v3*, wl_surface*) { Self(d)->m_textEntered = false; },
            [](void* d, zwp_text_input_v3*, const char* text, int32_t begin, int32_t)
            {
                Self(d)->m_preedit       = text ? text : "";
                Self(d)->m_preeditCursor = begin;
            },
            [](void* d, zwp_text_input_v3*, const char* text) { Self(d)->m_commit = text ? text : ""; },
            [](void*, zwp_text_input_v3*, uint32_t, uint32_t) {},
            [](void* d, zwp_text_input_v3*, uint32_t) { Self(d)->OnTextInputDone(); },
        };

        const wl_data_offer_listener kOfferListener =
        {
            [](void* d, wl_data_offer* offer, const char* mime) { Self(d)->m_offers[offer].emplace_back(mime ? mime : ""); },
            [](void*, wl_data_offer*, uint32_t) {},
            [](void*, wl_data_offer*, uint32_t) {},
        };

        const wl_data_device_listener kDataDeviceListener =
        {
            [](void* d, wl_data_device*, wl_data_offer* offer) { Self(d)->OnDataOffer(offer); },
            [](void* d, wl_data_device*, uint32_t serial, wl_surface*, wl_fixed_t, wl_fixed_t, wl_data_offer* offer) { Self(d)->OnDndEnter(serial, offer); },
            [](void* d, wl_data_device*) { Self(d)->OnDndLeave(); },
            [](void*, wl_data_device*, uint32_t, wl_fixed_t, wl_fixed_t) {},
            [](void* d, wl_data_device*) { Self(d)->OnDndDrop(); },
            [](void* d, wl_data_device*, wl_data_offer* offer) { Self(d)->OnSelection(offer); },
        };

        const wl_data_source_listener kSourceListener =
        {
            [](void*, wl_data_source*, const char*) {},
            [](void* d, wl_data_source* s, const char*, int32_t fd) { Self(d)->OnSourceSend(s, fd); },
            [](void* d, wl_data_source* s) { Self(d)->OnSourceCancelled(s); },
            [](void*, wl_data_source*) {},
            [](void*, wl_data_source*) {},
            [](void*, wl_data_source*, uint32_t) {},
        };

        // ── 连接 ──────────────────────────────────────────────────
        WaylandWindow::WaylandWindow(wl_display* display)
            : m_display(display)
            , m_xkb(xkb_context_new(XKB_CONTEXT_NO_FLAGS))
        {
        }

        WaylandWindow::~WaylandWindow()
        {
            Close();
            if (m_source)
                wl_data_source_destroy(m_source);
            for (auto& [offer, _] : m_offers)
                wl_data_offer_destroy(offer);
            if (m_dataDevice)
                wl_data_device_destroy(m_dataDevice);
            if (m_textInput)
                zwp_text_input_v3_destroy(m_textInput);
            if (m_pointer)
                wl_pointer_destroy(m_pointer);
            if (m_keyboard)
                wl_keyboard_destroy(m_keyboard);
            if (m_cursorSurface)
                wl_surface_destroy(m_cursorSurface);
            if (m_cursorTheme)
                wl_cursor_theme_destroy(m_cursorTheme);
            if (m_xkbState)
                xkb_state_unref(m_xkbState);
            if (m_keymap)
                xkb_keymap_unref(m_keymap);
            if (m_xkb)
                xkb_context_unref(m_xkb);
            for (Output& o : m_outputs)
                wl_output_destroy(o.output);
            if (m_dataDeviceManager)  wl_data_device_manager_destroy(m_dataDeviceManager);
            if (m_textInputManager)   zwp_text_input_manager_v3_destroy(m_textInputManager);
            if (m_fractionalManager)  wp_fractional_scale_manager_v1_destroy(m_fractionalManager);
            if (m_viewporter)         wp_viewporter_destroy(m_viewporter);
            if (m_decorationManager)  zxdg_decoration_manager_v1_destroy(m_decorationManager);
            if (m_wmBase)             xdg_wm_base_destroy(m_wmBase);
            if (m_seat)               wl_seat_destroy(m_seat);
            if (m_shm)                wl_shm_destroy(m_shm);
            if (m_compositor)         wl_compositor_destroy(m_compositor);
            if (m_registry)           wl_registry_destroy(m_registry);
            wl_display_disconnect(m_display);
        }

        bool WaylandWindow::Connect()
        {
            m_registry = wl_display_get_registry(m_display);
            wl_registry_add_listener(m_registry, &kRegistryListener, this);
            if (wl_display_roundtrip(m_display) < 0 || wl_display_roundtrip(m_display) < 0)
                return false;
            if (!m_compositor || !m_wmBase || !m_shm)
            {
                LOG_WARN_TR("Wayland 合成器缺少 {0}，改用 X11", !m_wmBase ? "xdg_wm_base" : "wl_compositor / wl_shm");
                return false;
            }
            CreateSeatObjects();
            UpdateScale();
            return true;
        }

        void WaylandWindow::OnGlobal(wl_registry* registry, uint32_t name, const char* iface, uint32_t version)
        {
            const std::string_view i = iface;
            auto bind = [&](const wl_interface* interface, uint32_t max)
            {
                return wl_registry_bind(registry, name, interface, std::min(version, max));
            };
            if (i == wl_compositor_interface.name)
            {
#ifdef WL_SURFACE_PREFERRED_BUFFER_SCALE_SINCE_VERSION
                m_compositorVersion = std::min(version, 6u);
#else
                m_compositorVersion = std::min(version, 4u);
#endif
                m_compositor = static_cast<wl_compositor*>(bind(&wl_compositor_interface, m_compositorVersion));
            }
            else if (i == wl_shm_interface.name)
                m_shm = static_cast<wl_shm*>(bind(&wl_shm_interface, 1));
            else if (i == wl_seat_interface.name && !m_seat)
            {
                m_seatVersion = std::min(version, 5u);
                m_seat = static_cast<wl_seat*>(bind(&wl_seat_interface, m_seatVersion));
                wl_seat_add_listener(m_seat, &kSeatListener, this);
            }
            else if (i == xdg_wm_base_interface.name)
            {
                m_wmBase = static_cast<xdg_wm_base*>(bind(&xdg_wm_base_interface, 3));
                xdg_wm_base_add_listener(m_wmBase, &kWmBaseListener, this);
            }
            else if (i == zxdg_decoration_manager_v1_interface.name)
                m_decorationManager = static_cast<zxdg_decoration_manager_v1*>(bind(&zxdg_decoration_manager_v1_interface, 1));
            else if (i == wp_viewporter_interface.name)
                m_viewporter = static_cast<wp_viewporter*>(bind(&wp_viewporter_interface, 1));
            else if (i == wp_fractional_scale_manager_v1_interface.name)
                m_fractionalManager = static_cast<wp_fractional_scale_manager_v1*>(bind(&wp_fractional_scale_manager_v1_interface, 1));
            else if (i == zwp_text_input_manager_v3_interface.name)
                m_textInputManager = static_cast<zwp_text_input_manager_v3*>(bind(&zwp_text_input_manager_v3_interface, 1));
            else if (i == wl_data_device_manager_interface.name)
            {
                m_dataDeviceVersion = std::min(version, 3u);
                m_dataDeviceManager = static_cast<wl_data_device_manager*>(bind(&wl_data_device_manager_interface, m_dataDeviceVersion));
            }
            else if (i == wl_output_interface.name)
            {
                Output o;
                o.output = static_cast<wl_output*>(bind(&wl_output_interface, 2));
                o.name   = name;
                wl_output_add_listener(o.output, &kOutputListener, this);
                m_outputs.push_back(o);
            }
        }

        void WaylandWindow::OnGlobalRemove(uint32_t name)
        {
            for (auto it = m_outputs.begin(); it != m_outputs.end(); ++it)
                if (it->name == name)
                {
                    m_entered.erase(std::remove(m_entered.begin(), m_entered.end(), it->output), m_entered.end());
                    wl_output_destroy(it->output);
                    m_outputs.erase(it);
                    UpdateScale();
                    return;
                }
        }

        void WaylandWindow::CreateSeatObjects()
        {
            if (!m_seat)
                return;
            if (m_dataDeviceManager && !m_dataDevice)
            {
                m_dataDevice = wl_data_device_manager_get_data_device(m_dataDeviceManager, m_seat);
                wl_data_device_add_listener(m_dataDevice, &kDataDeviceListener, this);
            }
            if (m_textInputManager && !m_textInput)
            {
                m_textInput = zwp_text_input_manager_v3_get_text_input(m_textInputManager, m_seat);
                zwp_text_input_v3_add_listener(m_textInput, &kTextInputListener, this);
            }
        }

        void WaylandWindow::OnSeatCapabilities(uint32_t caps)
        {
            const bool pointer  = (caps & WL_SEAT_CAPABILITY_POINTER) != 0;
            const bool keyboard = (caps & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
            if (pointer && !m_pointer)
            {
                m_pointer = wl_seat_get_pointer(m_seat);
                wl_pointer_add_listener(m_pointer, &kPointerListener, this);
            }
            else if (!pointer && m_pointer)
            {
                wl_pointer_destroy(m_pointer);
                m_pointer = nullptr;
            }
            if (keyboard && !m_keyboard)
            {
                m_keyboard = wl_seat_get_keyboard(m_seat);
                wl_keyboard_add_listener(m_keyboard, &kKeyboardListener, this);
            }
            else if (!keyboard && m_keyboard)
            {
                wl_keyboard_destroy(m_keyboard);
                m_keyboard = nullptr;
            }
        }

        // ── 窗口 ──────────────────────────────────────────────────
        bool WaylandWindow::Open(const char* title, int width, int height, int minWidth, int minHeight)
        {
            m_width  = width;
            m_height = height;
            m_surface = wl_compositor_create_surface(m_compositor);
            wl_surface_add_listener(m_surface, &kSurfaceListener, this);
            if (m_viewporter && m_fractionalManager)
            {
                m_viewport   = wp_viewporter_get_viewport(m_viewporter, m_surface);
                m_fractional = wp_fractional_scale_manager_v1_get_fractional_scale(m_fractionalManager, m_surface);
                wp_fractional_scale_v1_add_listener(m_fractional, &kFractionalListener, this);
            }
            m_xdgSurface = xdg_wm_base_get_xdg_surface(m_wmBase, m_surface);
            xdg_surface_add_listener(m_xdgSurface, &kXdgSurfaceListener, this);
            m_toplevel = xdg_surface_get_toplevel(m_xdgSurface);
            xdg_toplevel_add_listener(m_toplevel, &kToplevelListener, this);
            xdg_toplevel_set_title(m_toplevel, title);
            xdg_toplevel_set_app_id(m_toplevel, "io.github.minicad.MiniCAD");
            xdg_toplevel_set_min_size(m_toplevel, minWidth, minHeight);
            m_csd = m_decorationManager == nullptr;
            if (m_decorationManager)
            {
                m_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(m_decorationManager, m_toplevel);
                zxdg_toplevel_decoration_v1_add_listener(m_decoration, &kDecorationListener, this);
                zxdg_toplevel_decoration_v1_set_mode(m_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
            }
            wl_surface_commit(m_surface);
            while (!m_configured)
                if (wl_display_dispatch(m_display) < 0)
                {
                    LOG_ERROR_TR("创建窗口失败：{0}", "xdg_toplevel");
                    m_alive = false;
                    return false;
                }
            wl_display_roundtrip(m_display);
            UpdateScale();
            ApplySize();
            return true;
        }

        void WaylandWindow::Close()
        {
            if (m_frameCallback)
                wl_callback_destroy(m_frameCallback);
            if (m_eglWindow)
                wl_egl_window_destroy(m_eglWindow);
            if (m_fractional)
                wp_fractional_scale_v1_destroy(m_fractional);
            if (m_viewport)
                wp_viewport_destroy(m_viewport);
            if (m_decoration)
                zxdg_toplevel_decoration_v1_destroy(m_decoration);
            if (m_toplevel)
                xdg_toplevel_destroy(m_toplevel);
            if (m_xdgSurface)
                xdg_surface_destroy(m_xdgSurface);
            if (m_surface)
                wl_surface_destroy(m_surface);
            m_frameCallback = nullptr;
            m_eglWindow     = nullptr;
            m_fractional    = nullptr;
            m_viewport      = nullptr;
            m_decoration    = nullptr;
            m_toplevel      = nullptr;
            m_xdgSurface    = nullptr;
            m_surface       = nullptr;
            m_configured    = false;
            m_entered.clear();
            m_fractionalScale = 0;
            m_preferredBufferScale = 0;
            m_pixelWidth = m_pixelHeight = 0;
            m_geometryDirty = true;
            if (m_display)
                wl_display_flush(m_display);
        }

        void WaylandWindow::OnDecorationMode(uint32_t mode)
        {
            m_csd = mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
        }

        void WaylandWindow::OnToplevelConfigure(int32_t width, int32_t height, wl_array* states)
        {
            m_pendingWidth     = width;
            m_pendingHeight    = height;
            m_pendingMaximized = false;
            m_pendingActivated = false;
            const auto* s   = static_cast<const uint32_t*>(states->data);
            const size_t n  = states->size / sizeof(uint32_t);
            for (size_t i = 0; i < n; ++i)
            {
                if (s[i] == XDG_TOPLEVEL_STATE_MAXIMIZED || s[i] == XDG_TOPLEVEL_STATE_FULLSCREEN)
                    m_pendingMaximized = true;
                else if (s[i] == XDG_TOPLEVEL_STATE_ACTIVATED)
                    m_pendingActivated = true;
            }
        }

        void WaylandWindow::OnSurfaceConfigure(uint32_t serial)
        {
            xdg_surface_ack_configure(m_xdgSurface, serial);
            const bool first = !m_configured;
            m_configured = true;
            if (m_pendingWidth > 0 && m_pendingHeight > 0 && (m_pendingWidth != m_width || m_pendingHeight != m_height))
            {
                m_width  = m_pendingWidth;
                m_height = m_pendingHeight;
                m_geometryDirty = true;
            }
            if (!first)
                ApplySize();
            if (m_pendingMaximized != m_maximized)
            {
                m_maximized = m_pendingMaximized;
                if (m_callbacks.stateChanged)
                    m_callbacks.stateChanged();
            }
            if (m_pendingActivated != m_activated)
            {
                m_activated = m_pendingActivated;
                if (m_callbacks.activeChanged)
                    m_callbacks.activeChanged(m_activated);
            }
            if (m_callbacks.exposed)
                m_callbacks.exposed();
        }

        void WaylandWindow::UpdateScale()
        {
            float scale = 1.0f;
            if (m_fractional && m_fractionalScale > 0)
                scale = static_cast<float>(m_fractionalScale) / 120.0f;
            else if (m_preferredBufferScale > 0)
                scale = static_cast<float>(m_preferredBufferScale);
            else
            {
                int s = 0;
                for (const Output& o : m_outputs)
                    if (m_entered.empty() || std::find(m_entered.begin(), m_entered.end(), o.output) != m_entered.end())
                        s = std::max(s, o.scale);
                scale = static_cast<float>(std::max(1, s));
            }
            const int bufferScale = m_viewport ? 1 : std::max(1, static_cast<int>(scale));
            if (!m_viewport)
                scale = static_cast<float>(bufferScale);
            if (scale == m_scale && bufferScale == m_bufferScale)
                return;
            m_scale       = scale;
            m_bufferScale = bufferScale;
            m_geometryDirty = true;
            m_appliedCursor = -1;
            if (m_configured)
                ApplySize();
        }

        void WaylandWindow::ApplySize()
        {
            const int pw = std::max(1, static_cast<int>(std::lround(m_width * m_scale)));
            const int ph = std::max(1, static_cast<int>(std::lround(m_height * m_scale)));
            if (pw == m_pixelWidth && ph == m_pixelHeight)
                return;
            m_pixelWidth  = pw;
            m_pixelHeight = ph;
            m_geometryDirty = true;
            if (m_eglWindow)
                wl_egl_window_resize(m_eglWindow, pw, ph, 0, 0);
            if (m_input)
                m_input->SetPointScale(m_scale);
            if (m_callbacks.resized)
                m_callbacks.resized();
        }

        void WaylandWindow::SetTitle(const std::string& utf8)
        {
            if (m_toplevel)
                xdg_toplevel_set_title(m_toplevel, utf8.c_str());
        }

        void WaylandWindow::Minimize()
        {
            if (m_toplevel)
                xdg_toplevel_set_minimized(m_toplevel);
        }

        void WaylandWindow::ToggleMaximize()
        {
            if (!m_toplevel)
                return;
            if (m_maximized)
                xdg_toplevel_unset_maximized(m_toplevel);
            else
                xdg_toplevel_set_maximized(m_toplevel);
        }

        bool WaylandWindow::CanRender() const
        {
            return m_configured && (!m_frameCallback || Clock::now() - m_frameRequested > std::chrono::milliseconds(250));
        }

        void WaylandWindow::BeforePresent()
        {
            if (!m_surface)
                return;
            if (m_geometryDirty)
            {
                if (m_viewport)
                    wp_viewport_set_destination(m_viewport, m_width, m_height);
                else
                    wl_surface_set_buffer_scale(m_surface, m_bufferScale);
                m_geometryDirty = false;
            }
            if (m_frameCallback)
                wl_callback_destroy(m_frameCallback);
            m_frameCallback  = wl_surface_frame(m_surface);
            m_frameRequested = Clock::now();
            wl_callback_add_listener(m_frameCallback, &kFrameListener, this);
        }

        void* WaylandWindow::GetEglWindow()
        {
            if (!m_eglWindow && m_surface)
                m_eglWindow = wl_egl_window_create(m_surface, m_pixelWidth, m_pixelHeight);
            return m_eglWindow;
        }

        NativeWindow WaylandWindow::GetVulkanWindow()
        {
            NativeWindow window;
#ifdef MINICAD_LINUX_VULKAN
            window.vulkanInstanceExtensions = { "VK_KHR_surface", "VK_KHR_wayland_surface" };
            window.createVulkanSurface = [display = m_display, surface = m_surface](void* instance, uint64_t* out)
            {
                const auto inst   = static_cast<VkInstance>(instance);
                const auto create = reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>(vkGetInstanceProcAddr(inst, "vkCreateWaylandSurfaceKHR"));
                VkWaylandSurfaceCreateInfoKHR ci{ VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR };
                ci.display = display;
                ci.surface = surface;
                VkSurfaceKHR s = VK_NULL_HANDLE;
                if (!create || create(inst, &ci, nullptr, &s) != VK_SUCCESS)
                {
                    LOG_WARN_TR("创建 Vulkan 窗口表面失败：{0}", "vkCreateWaylandSurfaceKHR");
                    return false;
                }
                *out = reinterpret_cast<uint64_t>(s);
                return true;
            };
#endif
            return window;
        }

        // ── 事件循环 ──────────────────────────────────────────────
        bool WaylandWindow::PrepareWait()
        {
            while (wl_display_prepare_read(m_display) != 0)
                if (wl_display_dispatch_pending(m_display) < 0)
                {
                    m_alive = false;
                    return true;
                }
            m_reading = true;
            if (wl_display_flush(m_display) < 0 && errno != EAGAIN)
                m_alive = false;
            return false;
        }

        void WaylandWindow::Dispatch(bool readable)
        {
            if (m_reading)
            {
                m_reading = false;
                if (readable)
                {
                    if (wl_display_read_events(m_display) < 0)
                        m_alive = false;
                }
                else
                    wl_display_cancel_read(m_display);
            }
            if (m_alive && wl_display_dispatch_pending(m_display) < 0)
                m_alive = false;
            if (!m_alive)
                LOG_ERROR_TR("与 Wayland 合成器的连接断开（错误 {0}）", wl_display_get_error(m_display));
        }

        int WaylandWindow::TimeoutMs() const
        {
            int timeout = -1;
            const auto now = Clock::now();
            if (m_repeatCode)
                timeout = static_cast<int>(std::max<long long>(0, std::chrono::duration_cast<std::chrono::milliseconds>(m_repeatNext - now).count()));
            if (m_frameCallback)
            {
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(m_frameRequested + std::chrono::milliseconds(251) - now).count();
                if (left > 0)
                    timeout = timeout < 0 ? static_cast<int>(left) : std::min(timeout, static_cast<int>(left));
            }
            return timeout;
        }

        void WaylandWindow::ProcessTimers()
        {
            if (!m_repeatCode)
                return;
            const auto now = Clock::now();
            if (now < m_repeatNext)
                return;
            EmitKey(m_repeatCode, true, true);
            const auto interval = std::chrono::milliseconds(1000 / std::max(1, m_repeatRate));
            m_repeatNext += interval;
            if (m_repeatNext < now)
                m_repeatNext = now + interval;
        }

        // ── 指针 ──────────────────────────────────────────────────
        uint32_t WaylandWindow::EdgesAt(float x, float y) const
        {
            if (!m_csd || m_maximized)
                return 0;
            uint32_t edges = 0;
            if (y < kResizeBorder)                 edges |= XDG_TOPLEVEL_RESIZE_EDGE_TOP;
            else if (y >= m_height - kResizeBorder) edges |= XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
            if (x < kResizeBorder)                 edges |= XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
            else if (x >= m_width - kResizeBorder)  edges |= XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
            return edges;
        }

        void WaylandWindow::OnPointerEnter(uint32_t serial, wl_fixed_t x, wl_fixed_t y)
        {
            m_pointerSerial = serial;
            m_appliedCursor = -1;
            OnPointerMotion(x, y);
        }

        void WaylandWindow::OnPointerLeave()
        {
            m_edges = 0;
            m_pendingMove = false;
            if (m_input)
                m_input->PointerLeave();
        }

        void WaylandWindow::OnPointerMotion(wl_fixed_t x, wl_fixed_t y)
        {
            m_px = static_cast<float>(wl_fixed_to_double(x));
            m_py = static_cast<float>(wl_fixed_to_double(y));
            if (m_pendingMove)
            {
                if (std::abs(m_px - m_captionX) >= 4.0f || std::abs(m_py - m_captionY) >= 4.0f)
                {
                    m_pendingMove = false;
                    xdg_toplevel_move(m_toplevel, m_seat, m_moveSerial);
                }
                return;
            }
            m_edges = EdgesAt(m_px, m_py);
            if (m_edges)
            {
                using MiniGUI::CursorShape;
                const bool v = m_edges & (XDG_TOPLEVEL_RESIZE_EDGE_TOP | XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM);
                const bool h = m_edges & (XDG_TOPLEVEL_RESIZE_EDGE_LEFT | XDG_TOPLEVEL_RESIZE_EDGE_RIGHT);
                const bool nwse = m_edges == XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT || m_edges == XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
                ApplyCursor(v && h ? (nwse ? CursorShape::SizeNWSE : CursorShape::SizeNESW) : v ? CursorShape::SizeNS : CursorShape::SizeWE);
                return;
            }
            if (m_input)
                m_input->PointerMove(m_px, m_py);
            ApplyCursor(m_uiCursor);
        }

        void WaylandWindow::OnPointerButton(uint32_t serial, uint32_t button, uint32_t state)
        {
            m_lastSerial = serial;
            const bool down = state == WL_POINTER_BUTTON_STATE_PRESSED;
            if (m_pendingMove && button == kBtnLeft)
            {
                m_pendingMove = false;
                if (!down)
                    return;
            }
            if (down && m_csd && m_toplevel && m_seat)
            {
                if (m_edges && button == kBtnLeft)
                {
                    xdg_toplevel_resize(m_toplevel, m_seat, serial, m_edges);
                    return;
                }
                if (m_callbacks.captionTest && m_callbacks.captionTest(MiniGUI::Vec2{ m_px, m_py }))
                {
                    if (button == kBtnLeft)
                    {
                        const auto now = Clock::now();
                        if (now - m_captionClick < std::chrono::milliseconds(400) && std::abs(m_px - m_captionX) < 5.0f && std::abs(m_py - m_captionY) < 5.0f)
                        {
                            m_captionClick = {};
                            ToggleMaximize();
                            return;
                        }
                        m_captionClick = now;
                        m_captionX = m_px;
                        m_captionY = m_py;
                        m_pendingMove = true;
                        m_moveSerial  = serial;
                        return;
                    }
                    if (button == kBtnRight)
                    {
                        xdg_toplevel_show_window_menu(m_toplevel, m_seat, serial, static_cast<int32_t>(m_px), static_cast<int32_t>(m_py));
                        return;
                    }
                }
            }
            if (!m_input)
                return;
            MiniGUI::MouseButton b = MiniGUI::MouseButton::None;
            if (button == kBtnLeft)        b = MiniGUI::MouseButton::Left;
            else if (button == kBtnRight)  b = MiniGUI::MouseButton::Right;
            else if (button == kBtnMiddle) b = MiniGUI::MouseButton::Middle;
            m_input->PointerButton(m_px, m_py, b, down);
        }

        void WaylandWindow::OnPointerAxis(uint32_t axis, wl_fixed_t value)
        {
            if (axis > 1)
                return;
            m_axis[axis] += wl_fixed_to_double(value);
            m_hasAxis = true;
            if (m_seatVersion < 5)
                OnPointerFrame();
        }

        void WaylandWindow::OnPointerDiscrete(uint32_t axis, int32_t discrete)
        {
            if (axis > 1)
                return;
            m_discrete[axis] += discrete;
            m_hasDiscrete[axis] = true;
            m_hasAxis = true;
        }

        void WaylandWindow::OnPointerFrame()
        {
            if (!m_hasAxis)
                return;
            auto amount = [&](int a)
            {
                return m_hasDiscrete[a] ? static_cast<float>(m_discrete[a]) : static_cast<float>(m_axis[a] / 10.0);
            };
            const float dy = -amount(WL_POINTER_AXIS_VERTICAL_SCROLL);
            const float dx = amount(WL_POINTER_AXIS_HORIZONTAL_SCROLL);
            m_axis[0] = m_axis[1] = 0.0;
            m_discrete[0] = m_discrete[1] = 0;
            m_hasDiscrete[0] = m_hasDiscrete[1] = false;
            m_hasAxis = false;
            if (m_input && (dx != 0.0f || dy != 0.0f))
                m_input->PointerWheel(m_px, m_py, dx, dy);
        }

        void WaylandWindow::SetCursor(MiniGUI::CursorShape shape)
        {
            m_uiCursor = shape;
            if (!m_edges)
                ApplyCursor(shape);
        }

        void WaylandWindow::ApplyCursor(MiniGUI::CursorShape shape)
        {
            if (!m_pointer || !m_pointerSerial || static_cast<int>(shape) == m_appliedCursor)
                return;
            m_appliedCursor = static_cast<int>(shape);
            if (shape == MiniGUI::CursorShape::Hidden)
            {
                wl_pointer_set_cursor(m_pointer, m_pointerSerial, nullptr, 0, 0);
                return;
            }
            const int scale = std::max(1, static_cast<int>(std::ceil(m_scale)));
            if (!m_cursorTheme || m_cursorThemeScale != scale)
            {
                if (m_cursorTheme)
                    wl_cursor_theme_destroy(m_cursorTheme);
                const char* sizeEnv = std::getenv("XCURSOR_SIZE");
                const int size = sizeEnv && std::atoi(sizeEnv) > 0 ? std::atoi(sizeEnv) : 24;
                m_cursorTheme = wl_cursor_theme_load(std::getenv("XCURSOR_THEME"), size * scale, m_shm);
                m_cursorThemeScale = scale;
            }
            if (!m_cursorTheme)
                return;
            wl_cursor* cursor = nullptr;
            for (const char* const* name = CursorNames(shape); *name && !cursor; ++name)
                cursor = wl_cursor_theme_get_cursor(m_cursorTheme, *name);
            if (!cursor)
                cursor = wl_cursor_theme_get_cursor(m_cursorTheme, "left_ptr");
            if (!cursor || cursor->image_count == 0)
                return;
            wl_cursor_image* image = cursor->images[0];
            wl_buffer* buffer = wl_cursor_image_get_buffer(image);
            if (!buffer)
                return;
            if (!m_cursorSurface)
                m_cursorSurface = wl_compositor_create_surface(m_compositor);
            wl_surface_set_buffer_scale(m_cursorSurface, scale);
            wl_surface_attach(m_cursorSurface, buffer, 0, 0);
            wl_surface_damage(m_cursorSurface, 0, 0, INT32_MAX, INT32_MAX);
            wl_surface_commit(m_cursorSurface);
            wl_pointer_set_cursor(m_pointer, m_pointerSerial, m_cursorSurface,
                                  static_cast<int32_t>(image->hotspot_x) / scale, static_cast<int32_t>(image->hotspot_y) / scale);
        }

        // ── 键盘 ──────────────────────────────────────────────────
        void WaylandWindow::OnKeymap(uint32_t format, int fd, uint32_t size)
        {
            if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || !m_xkb)
            {
                close(fd);
                return;
            }
            void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
            close(fd);
            if (map == MAP_FAILED)
                return;
            xkb_keymap* keymap = xkb_keymap_new_from_string(m_xkb, static_cast<const char*>(map), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
            munmap(map, size);
            if (!keymap)
                return;
            if (m_xkbState)
                xkb_state_unref(m_xkbState);
            if (m_keymap)
                xkb_keymap_unref(m_keymap);
            m_keymap   = keymap;
            m_xkbState = xkb_state_new(keymap);
        }

        void WaylandWindow::OnKeyboardLeave()
        {
            m_repeatCode = 0;
            if (m_input)
                m_input->FocusLost();
        }

        void WaylandWindow::EmitKey(uint32_t code, bool down, bool repeat)
        {
            if (!m_input || !m_xkbState)
                return;
            const xkb_keysym_t sym = xkb_state_key_get_one_sym(m_xkbState, code);
            char text[64] = {};
            if (down)
                xkb_state_key_get_utf8(m_xkbState, code, text, sizeof(text));
            m_input->Key(sym, code - 8, down, repeat, text);
        }

        void WaylandWindow::OnKey(uint32_t serial, uint32_t key, uint32_t state)
        {
            m_lastSerial = serial;
            const uint32_t code = key + 8;
            const bool down = state == WL_KEYBOARD_KEY_STATE_PRESSED;
            EmitKey(code, down, false);
            if (down && m_keymap && m_repeatRate > 0 && xkb_keymap_key_repeats(m_keymap, code))
            {
                m_repeatCode = code;
                m_repeatNext = Clock::now() + std::chrono::milliseconds(m_repeatDelay);
            }
            else if (!down && code == m_repeatCode)
                m_repeatCode = 0;
        }

        void WaylandWindow::OnModifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
        {
            if (!m_xkbState)
                return;
            xkb_state_update_mask(m_xkbState, depressed, latched, locked, 0, 0, group);
            uint8_t m = 0;
            if (xkb_state_mod_name_is_active(m_xkbState, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0) m |= static_cast<uint8_t>(MiniGUI::ModifierKey::Shift);
            if (xkb_state_mod_name_is_active(m_xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0)  m |= static_cast<uint8_t>(MiniGUI::ModifierKey::Ctrl);
            if (xkb_state_mod_name_is_active(m_xkbState, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0)   m |= static_cast<uint8_t>(MiniGUI::ModifierKey::Alt);
            if (m_input)
                m_input->SetModifiers(m);
        }

        // ── 输入法（text-input-v3）────────────────────────────────
        void WaylandWindow::EnableTextInput()
        {
            zwp_text_input_v3_enable(m_textInput);
            zwp_text_input_v3_set_content_type(m_textInput, ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE, ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
            zwp_text_input_v3_set_cursor_rectangle(m_textInput, static_cast<int32_t>(m_caret.min.x), static_cast<int32_t>(m_caret.min.y),
                                                   std::max(1, static_cast<int32_t>(m_caret.Width())), std::max(1, static_cast<int32_t>(m_caret.Height())));
            zwp_text_input_v3_commit(m_textInput);
        }

        void WaylandWindow::SetTextInput(bool enabled)
        {
            m_textMode = enabled;
            if (!m_textInput || !m_textEntered)
                return;
            if (enabled)
                EnableTextInput();
            else
            {
                zwp_text_input_v3_disable(m_textInput);
                zwp_text_input_v3_commit(m_textInput);
            }
        }

        void WaylandWindow::SetTextCaret(const MiniGUI::Rect& caret)
        {
            m_caret = caret;
            if (m_textInput && m_textEntered && m_textMode)
                EnableTextInput();
        }

        void WaylandWindow::OnTextInputDone()
        {
            const std::string commit  = std::exchange(m_commit, {});
            const std::string preedit = std::exchange(m_preedit, {});
            const int32_t cursor = std::exchange(m_preeditCursor, -1);
            if (!m_input)
                return;
            if (!commit.empty())
                m_input->Text(commit);
            if (preedit.empty())
            {
                m_input->Preedit({}, 0);
                return;
            }
            const size_t bytes = cursor >= 0 ? std::min(static_cast<size_t>(cursor), preedit.size()) : preedit.size();
            m_input->Preedit(preedit, CountCodepoints(std::string_view(preedit).substr(0, bytes)));
        }

        // ── 剪贴板与拖放 ──────────────────────────────────────────
        void WaylandWindow::OnDataOffer(wl_data_offer* offer)
        {
            m_offers[offer];
            wl_data_offer_add_listener(offer, &kOfferListener, this);
        }

        void WaylandWindow::DestroyOffer(wl_data_offer* offer)
        {
            if (!offer)
                return;
            m_offers.erase(offer);
            wl_data_offer_destroy(offer);
        }

        void WaylandWindow::OnSelection(wl_data_offer* offer)
        {
            if (m_selectionOffer && m_selectionOffer != offer && m_selectionOffer != m_dndOffer)
                DestroyOffer(m_selectionOffer);
            m_selectionOffer = offer;
        }

        void WaylandWindow::OnDndEnter(uint32_t serial, wl_data_offer* offer)
        {
            if (m_dndOffer && m_dndOffer != offer && m_dndOffer != m_selectionOffer)
                DestroyOffer(m_dndOffer);
            m_dndOffer  = offer;
            m_dndAccept = false;
            if (!offer)
                return;
            for (const std::string& mime : m_offers[offer])
                m_dndAccept = m_dndAccept || mime == "text/uri-list";
            wl_data_offer_accept(offer, serial, m_dndAccept ? "text/uri-list" : nullptr);
            if (m_dataDeviceVersion >= 3)
            {
                const uint32_t action = m_dndAccept ? WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY : WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
                wl_data_offer_set_actions(offer, action, action);
            }
        }

        void WaylandWindow::OnDndLeave()
        {
            if (m_dndOffer && m_dndOffer != m_selectionOffer)
                DestroyOffer(m_dndOffer);
            m_dndOffer  = nullptr;
            m_dndAccept = false;
        }

        void WaylandWindow::OnDndDrop()
        {
            wl_data_offer* offer = std::exchange(m_dndOffer, nullptr);
            if (!offer)
                return;
            std::vector<std::string> paths;
            if (m_dndAccept)
            {
                paths = ParseUriList(ReceiveOffer(offer, "text/uri-list"));
                if (m_dataDeviceVersion >= 3)
                    wl_data_offer_finish(offer);
            }
            if (offer != m_selectionOffer)
                DestroyOffer(offer);
            m_dndAccept = false;
            if (!paths.empty() && m_callbacks.filesDropped)
                m_callbacks.filesDropped(std::move(paths));
        }

        std::string WaylandWindow::ReceiveOffer(wl_data_offer* offer, const char* mime)
        {
            int fds[2];
            if (pipe2(fds, O_CLOEXEC) != 0)
                return {};
            wl_data_offer_receive(offer, mime, fds[1]);
            close(fds[1]);
            wl_display_flush(m_display);
            std::string data;
            const auto deadline = Clock::now() + std::chrono::seconds(2);
            for (;;)
            {
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
                pollfd p{ fds[0], POLLIN, 0 };
                if (left <= 0 || poll(&p, 1, static_cast<int>(left)) <= 0)
                    break;
                char buf[4096];
                const ssize_t n = read(fds[0], buf, sizeof(buf));
                if (n < 0 && errno == EINTR)
                    continue;
                if (n <= 0)
                    break;
                data.append(buf, static_cast<size_t>(n));
            }
            close(fds[0]);
            return data;
        }

        std::string WaylandWindow::GetText()
        {
            if (m_source)
                return m_clipboard;
            if (!m_selectionOffer)
                return {};
            const auto& mimes = m_offers[m_selectionOffer];
            for (const char* want : kTextMimes)
                if (std::find(mimes.begin(), mimes.end(), want) != mimes.end())
                    return ReceiveOffer(m_selectionOffer, want);
            return {};
        }

        void WaylandWindow::SetText(std::string_view text)
        {
            m_clipboard.assign(text);
            if (!m_dataDeviceManager || !m_dataDevice)
                return;
            if (m_source)
                wl_data_source_destroy(m_source);
            m_source = wl_data_device_manager_create_data_source(m_dataDeviceManager);
            wl_data_source_add_listener(m_source, &kSourceListener, this);
            for (const char* mime : kTextMimes)
                wl_data_source_offer(m_source, mime);
            wl_data_device_set_selection(m_dataDevice, m_source, m_lastSerial);
        }

        void WaylandWindow::OnSourceSend(wl_data_source* source, int fd)
        {
            if (source == m_source)
            {
                size_t done = 0;
                while (done < m_clipboard.size())
                {
                    const ssize_t n = write(fd, m_clipboard.data() + done, m_clipboard.size() - done);
                    if (n < 0 && errno == EINTR)
                        continue;
                    if (n <= 0)
                        break;
                    done += static_cast<size_t>(n);
                }
            }
            close(fd);
        }

        void WaylandWindow::OnSourceCancelled(wl_data_source* source)
        {
            if (source == m_source)
                m_source = nullptr;
            wl_data_source_destroy(source);
        }
    }

    std::unique_ptr<WindowSystem> CreateWaylandWindowSystem()
    {
        wl_display* display = wl_display_connect(nullptr);
        if (!display)
            return nullptr;
        auto window = std::make_unique<WaylandWindow>(display);
        if (!window->Connect())
            return nullptr;
        return window;
    }
}
