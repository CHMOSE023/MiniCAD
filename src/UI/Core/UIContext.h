#pragma once
#include "Core/Clipboard.h"
#include "Core/Node.h"
#include "Core/Popup.h"
#include "Core/ShortcutTable.h"
#include "Layout/ILayoutEngine.h"
#include "Paint/DrawList.h"
#include "Style/ThemeColors.h"
#include "Text/TextSystem.h"
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MiniGUI
{
    class IRenderBackend;
    struct Image;

    // 菜单栏在核心层的接口（菜单栏控件在 Widgets 里实现）
    class IMenuBarHost
    {
    public:
        virtual ~IMenuBarHost() = default;
        virtual Node* GetMenuBarNode() = 0;
        virtual bool  ActivateFromKeyboard() = 0;          // Alt / F10：进入菜单栏键盘模式
        virtual bool  OpenByMnemonic(char letter) = 0;     // Alt+字母：打开助记符匹配的菜单
    };

    // 界面上下文：持有节点树，负责事件分发、布局、绘制。
    // 主循环由宿主掌控，典型用法：
    //   平台消息 → PointerXxx(...)                （分发事件，修改节点，标记脏）
    //   Update()      → 返回 true 表示需要重绘      （执行布局）
    //   Render()      → 生成 DrawList 并交给后端     （不 Present，由宿主决定）
    // 需要重绘时会调用 SetRedrawCallback 设置的回调，宿主可以在回调里 InvalidateRect，
    // 这样空闲时主循环可以一直阻塞在 GetMessage / WaitMessage 上。
    class UIContext
    {
    public:
        explicit UIContext(IRenderBackend* backend);
        ~UIContext();

        UIContext(const UIContext&) = delete;
        UIContext& operator=(const UIContext&) = delete;

        // 主界面层：它的子节点会被拉伸到整个显示区域。
        // 实际的树根下依次是：主界面层 → 弹层（菜单、下拉、对话框）→ 悬浮提示层
        Node* GetRoot() const { return m_mainLayer; }

        // ── 弹层 ────────────────────────────────────────────────
        // takeFocus：打开后把焦点移到弹层（菜单、对话框）；输入建议列表传 false，焦点留在输入框
        Popup* OpenPopup(std::unique_ptr<Popup> popup, bool takeFocus = true);

        template<typename T, typename... Args>
        T* OpenPopup(Args&&... args)
        {
            auto p = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = p.get();
            OpenPopup(std::move(p));
            return raw;
        }

        // 摘下的节点推迟到下一次 Render 销毁（例如在编辑框自己的回调里移除编辑框）
        void   DeferDelete(std::unique_ptr<Node> node) { if (node) m_graveyard.push_back(std::move(node)); }

        // 在 target 及其祖先上依次尝试打开右键菜单
        bool   OpenContextMenu(Node* target, Vec2 windowPos);

        // 菜单栏：注册后 Alt / F10 激活菜单栏，Alt+字母按助记符打开菜单
        void   SetMenuBarHost(IMenuBarHost* host) { m_menuBarHost = host; }
        IMenuBarHost* GetMenuBarHost() const { return m_menuBarHost; }
        // 菜单栏退出键盘模式时调用：焦点回到按 Alt / F10 之前的控件（例如命令行）
        void   RestoreFocusAfterMenuBar();

        void   ClosePopup(Popup* popup);    // 同时关闭它上面的所有弹层（例如子菜单）
        void   CloseAllPopups();
        Popup* GetTopPopup() const;
        bool   HasModal() const;

        // ── 定时器（悬浮提示延迟、光标闪烁等）───────────────────
        // 时间由平台层提供（SetClock），需要唤醒时通过 SetTimerScheduler 通知平台层设置系统定时器
        using TimerId = uint32_t;
        TimerId  StartTimer(uint32_t delayMs, std::function<void()> callback, bool repeat = false);
        void     StopTimer(TimerId id);
        void     Tick();                                         // 执行到期的定时器
        bool     GetNextTimerDelay(uint32_t& delayMs) const;
        uint32_t Now() const;
        void     SetClock(std::function<uint32_t()> clock) { m_clock = std::move(clock); }
        void     SetTimerScheduler(std::function<void(bool active, uint32_t delayMs)> scheduler);

        // ── 悬浮提示与光标 ──────────────────────────────────────
        void        SetTooltipDelay(uint32_t ms) { m_tooltipDelay = ms; }
        bool        IsTooltipVisible() const { return m_tooltipNode != nullptr; }
        const std::string& GetVisibleTooltip() const { return m_tooltipText; }
        CursorShape GetCursor() const;

        // ── 显示区域 ────────────────────────────────────────────
        void  SetDisplaySize(Vec2 logicalSize, float pixelScale);
        // ── 主题：控件的颜色引用主题槽位，切换后整棵树在下一帧按新颜色绘制 ──
        void               SetTheme(const ThemeColors& theme);
        const ThemeColors& GetTheme() const { return m_theme; }
        Color32            ResolveColor(ColorRef color) const { return m_theme.Resolve(color); }

        Vec2  GetDisplaySize()  const { return m_displaySize; }
        float GetPixelScale()   const { return m_pixelScale; }

        // ── 布局引擎：默认为 FlexLayout，可替换（传 nullptr 恢复默认）──
        void           SetLayoutEngine(std::unique_ptr<ILayoutEngine> engine);
        ILayoutEngine& GetLayoutEngine() const;

        // ── 文字：字体、字形图集、排版 ──────────────────────────
        TextSystem&    GetTextSystem() const { return *m_text; }

        // ── 纹理（图标、图片）：由上下文持有，销毁时一并释放 ────
        TextureId      CreateTexture(const Image& image);
        TextureId      LoadTexture(const std::string& utf8Path);   // 按路径缓存；失败返回 InvalidTextureId
        // 按显示尺寸加载：图片先缩放到 逻辑尺寸 × 缩放系数 的像素大小再上传（图标清晰），按路径 + 像素尺寸缓存
        TextureId      LoadTexture(const std::string& utf8Path, Vec2 logicalSize);
        Vec2           GetTextureSize(TextureId id) const;          // 只对上面两个函数创建的纹理有效
        IRenderBackend& GetBackend() const { return *m_backend; }

        // ── 指针输入（窗口坐标，逻辑像素）；返回 true 表示有节点处理了事件 ──
        // timeMs 为消息时间（毫秒，可回绕），用于判断双击；没有时间信息时传 0
        bool PointerMove  (Vec2 pos, uint8_t modifiers);
        bool PointerDown  (Vec2 pos, MouseButton button, uint8_t modifiers, uint32_t timeMs = 0);
        bool PointerUp    (Vec2 pos, MouseButton button, uint8_t modifiers);
        bool PointerWheel (Vec2 pos, Vec2 delta, uint8_t modifiers);
        void PointerLeave ();   // 指针离开窗口
        void PointerCancel();   // 系统夺走了捕获（例如切换窗口）

        void SetDoubleClickTime(uint32_t ms) { m_doubleClickTime = ms; }

        // ── 键盘与文字输入 ──────────────────────────────────────
        // 顺序：全局快捷键 → 焦点节点（捕获/目标/冒泡）→ 未处理的 Tab 用于切换焦点
        bool KeyDown  (Key key, uint8_t modifiers, bool repeat = false);
        bool KeyUp    (Key key, uint8_t modifiers);
        bool TextInput(std::string_view utf8);

        // 输入法组合串（拼音等）：只发给焦点节点
        void CompositionStart ();
        void CompositionUpdate(std::string_view utf8, int caret);
        void CompositionEnd   ();
        bool IsComposing() const { return m_composing; }

        void KeyboardFocusLost();   // 窗口失去键盘焦点：结束组合，清除按键状态

        ShortcutTable& GetShortcuts() { return m_shortcuts; }

        // ── 焦点 ────────────────────────────────────────────────
        void  SetFocus(Node* node, FocusReason reason = FocusReason::Program);
        Node* GetFocus() const { return m_focus; }
        bool  IsFocusVisible() const { return m_focusVisible; }   // 键盘导航获得焦点时显示焦点框
        bool  FocusNext(bool backward);                            // Tab / Shift+Tab

        bool  WantsTextInput() const;                              // 焦点节点是文本输入控件
        bool  GetTextCaretRect(Rect& out) const;                   // 焦点节点的文字光标（窗口坐标）

        // ── 剪贴板：平台层注入；未注入时使用进程内剪贴板 ────────
        void        SetClipboard(IClipboard* clipboard) { m_clipboard = clipboard; }
        IClipboard& GetClipboard() { return m_clipboard ? *m_clipboard : m_localClipboard; }

        // ── 指针捕获：捕获期间所有指针事件都发给捕获节点 ────────
        void  SetCapture(Node* node);
        void  ReleaseCapture();
        Node* GetCapture() const { return m_capture; }
        Node* GetHovered() const { return m_hovered; }

        Node* HitTest(Vec2 windowPos) const;

        // ── 与宿主的其他界面（例如 ImGui）共用一个窗口时的输入分配 ──
        // 指针在 MiniGUI 的控件上、有弹层打开（需要轻触关闭）或正在捕获时返回 true，宿主应把指针消息只交给 MiniGUI。
        // 用作透明容器的节点应设为 SetHitTestVisible(false)，否则整块区域都算作 MiniGUI 的
        bool  IsPointerOverUI(Vec2 windowPos) const;
        // 有焦点控件或有弹层打开时返回 true，宿主应把键盘和文字消息只交给 MiniGUI
        bool  WantsKeyboard() const;

        // ── 帧 ──────────────────────────────────────────────────
        bool Update();          // 执行布局；返回是否需要重绘
        void Render();          // 绘制整棵树并提交给后端
        void RequestRedraw();
        bool NeedsRedraw() const { return m_needsRedraw; }

        void SetRedrawCallback(std::function<void()> callback) { m_redrawCallback = std::move(callback); }

    private:
        friend class Node;

        void  OnSubtreeDetached(Node* subtree);
        bool  IsInLayout() const { return m_inLayout; }

        void  LayoutNode(Node* node);
        void  MarkAllLayoutDirty(Node* node);
        void  PaintNode (Node* node, Vec2 parentOrigin);
        Node* HitTestNode(Node* node, Vec2 posInParent) const;

        void  UpdateHover(Node* target);
        void  UpdateHoverFromPointer();
        bool  Dispatch(Node* target, PointerEvent& e);
        void  SendDirect(Node* node, PointerEvent e);  // 只发给一个节点（Enter/Leave/Cancel）
        bool  DispatchKey(KeyEvent& e);
        void  FocusFromPointer(Node* target);
        void  CollectFocusable(Node* node, std::vector<Node*>& out) const;

        bool  DismissPopupsFor(Node* target);     // 轻触关闭；返回是否关闭了弹层
        bool  IsInPopupLayer(const Node* node) const;
        void  NotifyTimerScheduler();

        void  NotifyThemeChanged(Node* node);
        void  OnHoverChangedForTooltip();
        void  ShowTooltip();
        void  HideTooltip(bool suppress);
        void  RememberFocusForMenuBar();
        Node* FindTooltipOwner(Node* node) const;

    private:
        IRenderBackend*                m_backend = nullptr;
        std::unique_ptr<ILayoutEngine> m_layoutEngine;
        std::unique_ptr<TextSystem>    m_text;          // 必须在 m_root 之前声明：节点树先销毁
        std::unique_ptr<Node>          m_root;
        Node*                          m_mainLayer    = nullptr;
        Node*                          m_popupLayer   = nullptr;
        Node*                          m_tooltipLayer = nullptr;

        // 已关闭但尚未销毁的节点：可能正在执行自己的事件处理函数，推迟到下一次 Render 再销毁
        std::vector<std::unique_ptr<Node>> m_graveyard;

        // 上下文持有的纹理
        std::unordered_map<std::string, TextureId> m_textureCache;
        std::unordered_map<TextureId, Vec2>        m_textureSizes;

        // 定时器
        struct Timer
        {
            TimerId               id;
            uint32_t              due;
            uint32_t              interval;
            bool                  repeat;
            std::function<void()> callback;
        };
        std::vector<Timer>                             m_timers;
        TimerId                                        m_nextTimerId = 1;
        std::function<uint32_t()>                      m_clock;
        std::function<void(bool, uint32_t)>            m_timerScheduler;

        // 悬浮提示
        uint32_t    m_tooltipDelay      = 500;
        TimerId     m_tooltipTimer      = 0;
        Node*       m_tooltipNode       = nullptr;   // 提示层里正在显示的节点
        Node*       m_tooltipOwner      = nullptr;   // 提示内容来自哪个节点
        Node*       m_tooltipSuppressed = nullptr;   // 点击后不再为同一节点显示，直到指针离开
        std::string m_tooltipText;
        uint32_t    m_tooltipHiddenAt   = 0;         // 刚隐藏不久时，移到另一个节点立即显示

        ThemeColors           m_theme = ThemeColors::Dark();
        DrawListSharedData    m_drawShared;
        DrawList              m_drawList;

        Vec2  m_displaySize;
        float m_pixelScale = 1.0f;

        Node*   m_hovered       = nullptr;
        Node*   m_capture       = nullptr;
        Vec2    m_pointerPos;
        bool    m_pointerInside = false;
        uint8_t m_buttons       = 0;
        uint8_t m_modifiers     = 0;

        // 双击判定
        uint32_t    m_doubleClickTime = 500;
        uint32_t    m_lastClickTime   = 0;
        Vec2        m_lastClickPos;
        MouseButton m_lastClickButton = MouseButton::None;
        int         m_clickCount      = 0;

        // 键盘
        Node*          m_focus        = nullptr;
        bool           m_focusVisible = false;
        bool           m_composing    = false;
        ShortcutTable  m_shortcuts;
        IClipboard*    m_clipboard    = nullptr;
        LocalClipboard m_localClipboard;

        // 右键：按下时如果关闭了弹层，抬起时不再弹出新的右键菜单
        bool           m_rightDownDismissed = false;

        // 菜单栏键盘激活：Alt 单独按下再抬起（中间没有其他键）视为激活
        IMenuBarHost*  m_menuBarHost = nullptr;
        Node*          m_menuBarReturnFocus = nullptr;     // 进入菜单栏键盘模式前的焦点
        bool           m_altAlone    = false;

        bool m_needsRedraw = true;
        bool m_inLayout    = false;

        // 每次有子树被移除就加一。事件处理函数里可能删除节点（例如点击按钮关闭面板），
        // 分发过程中发现版本变化就立即停止，避免访问已销毁的节点
        uint64_t m_treeVersion = 0;
        std::function<void()> m_redrawCallback;

        std::vector<Node*> m_pathScratch;   // 分发时的节点路径，复用避免分配
    };
}
