#include "Core/UIContext.h"
#include "Layout/FlexLayout.h"
#include "Paint/Image.h"
#include "Render/IRenderBackend.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>

namespace MiniGUI
{
    namespace
    {
        // 把每个子节点拉伸到整个区域：用作树根（各图层铺满窗口）和主界面层
        class StretchNode : public Node
        {
        protected:
            void OnLayout() override
            {
                const Rect full{ { 0, 0 }, GetSize() };
                for (auto& c : GetChildren())
                    c->SetBounds(full);
            }
        };

        // 悬浮提示：自动换行，最宽 360
        class TooltipNode : public Node
        {
        public:
            TooltipNode(std::string text, Vec2 point) : m_text(std::move(text)), m_point(point)
            {
                SetHitTestVisible(false);
            }

            Vec2 GetPoint() const { return m_point; }

        protected:
            TextParams Params() const
            {
                TextParams p;
                p.size  = 12.0f;
                p.color = Theme::Text;
                p.wrap  = true;
                return p;
            }

            Vec2 MeasureContent(Vec2 available) override
            {
                (void)available;
                UIContext* ctx = GetContext();
                if (!ctx)
                    return {};
                const Vec2 s = ctx->GetTextSystem().Measure(m_text, Params(), 360.0f);
                return { s.x + 16.0f, s.y + 10.0f };
            }

            void OnPaint(DrawList& dl, const Rect& r) override
            {
                dl.AddRectFilled(Rect{ r.min.x, r.min.y + 1.0f, r.max.x, r.max.y + 2.0f }, ColorRef(ThemeColor::Shadow, 50), 4.0f);
                dl.AddRectFilled(r, Theme::TooltipBackground, 4.0f);
                dl.AddRect(r, Theme::TooltipBorder, 4.0f, 1.0f);
                if (UIContext* ctx = GetContext())
                    ctx->GetTextSystem().Draw(dl, Rect{ r.min.x + 8.0f, r.min.y + 5.0f, r.max.x - 8.0f, r.max.y - 5.0f }, m_text, Params());
            }

        private:
            std::string m_text;
            Vec2        m_point;
        };

        class TooltipLayer : public Node
        {
        public:
            TooltipLayer() { SetHitTestVisible(false); }

        protected:
            void OnLayout() override
            {
                const Vec2 display = GetSize();
                for (const auto& c : GetChildren())
                {
                    auto* tip = static_cast<TooltipNode*>(c.get());
                    tip->SetBounds(PlacePopup(tip->Measure(display), PopupPlacement::AtPoint, Rect{}, tip->GetPoint(), display));
                }
            }
        };

        uint32_t SteadyClockMs()
        {
            using namespace std::chrono;
            return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
        }
    }

    UIContext::UIContext(IRenderBackend* backend)
        : m_backend(backend)
        , m_drawList(&m_drawShared)
    {
        assert(backend != nullptr);

        // 纯色图元采样字形图集里预留的白色像素，文字和图形共用一张纹理，可以合并批次
        m_text = std::make_unique<TextSystem>(backend);
        m_drawShared.whiteTexture = m_text->GetAtlas().GetTexture();
        m_drawShared.whitePixelUV = m_text->GetAtlas().GetWhitePixelUV();
        m_drawShared.palette      = m_theme.Data();

        // 图层：主界面 → 弹层 → 悬浮提示，后面的盖在前面上面
        m_root = std::make_unique<StretchNode>();
        m_root->m_context = this;
        m_mainLayer    = m_root->AddChild<StretchNode>();
        m_popupLayer   = m_root->AddChild<PopupLayer>();
        m_tooltipLayer = m_root->AddChild<TooltipLayer>();
    }

    UIContext::~UIContext()
    {
        m_timers.clear();
        m_hovered      = nullptr;
        m_capture      = nullptr;
        m_focus        = nullptr;

        // 先关闭并销毁所有弹层（此时它们的所有者都还活着，关闭回调可以正常清理），再销毁节点树
        m_timerScheduler = nullptr;
        m_redrawCallback = nullptr;
        CloseAllPopups();
        m_focus = nullptr;
        m_graveyard.clear();
        m_timers.clear();
        m_tooltipNode  = nullptr;
        m_tooltipOwner = nullptr;
        m_graveyard.clear();
        m_root.reset();
        m_text.reset();

        for (const auto& [id, size] : m_textureSizes)
            m_backend->DestroyTexture(id);
    }

    // =========================================================
    // 纹理
    // =========================================================
    TextureId UIContext::CreateTexture(const Image& image)
    {
        if (image.Empty())
            return InvalidTextureId;
        const TextureId id = m_backend->CreateTexture(image.width, image.height, TextureFormat::RGBA8, image.pixels.data());
        if (id != InvalidTextureId)
            m_textureSizes[id] = Vec2{ static_cast<float>(image.width), static_cast<float>(image.height) };
        return id;
    }

    TextureId UIContext::LoadTexture(const std::string& utf8Path)
    {
        auto it = m_textureCache.find(utf8Path);
        if (it != m_textureCache.end())
            return it->second;

        Image image;
        const TextureId id = LoadImageFromFile(utf8Path, image) ? CreateTexture(image) : InvalidTextureId;
        m_textureCache.emplace(utf8Path, id);   // 失败也缓存，避免反复读盘
        return id;
    }

    TextureId UIContext::LoadTexture(const std::string& utf8Path, Vec2 logicalSize)
    {
        const int w = static_cast<int>(std::lround(logicalSize.x * m_pixelScale));
        const int h = static_cast<int>(std::lround(logicalSize.y * m_pixelScale));
        if (w <= 0 || h <= 0)
            return LoadTexture(utf8Path);

        const std::string key = utf8Path + "@" + std::to_string(w) + "x" + std::to_string(h);
        auto it = m_textureCache.find(key);
        if (it != m_textureCache.end())
            return it->second;

        Image image;
        TextureId id = InvalidTextureId;
        if (LoadImageFromFile(utf8Path, image))
            id = CreateTexture((image.width == w && image.height == h) ? image : ResizeImage(image, w, h));
        m_textureCache.emplace(key, id);
        return id;
    }

    Vec2 UIContext::GetTextureSize(TextureId id) const
    {
        auto it = m_textureSizes.find(id);
        return it != m_textureSizes.end() ? it->second : Vec2{};
    }

    void UIContext::SetDisplaySize(Vec2 logicalSize, float pixelScale)
    {
        if (logicalSize == m_displaySize && pixelScale == m_pixelScale)
            return;

        // 缩放系数变化时，像素对齐的结果全部要重算
        if (pixelScale != m_pixelScale)
            MarkAllLayoutDirty(m_root.get());

        m_displaySize             = logicalSize;
        m_pixelScale              = pixelScale;
        m_drawShared.pixelScale   = pixelScale;
        m_text->SetPixelScale(pixelScale);
        m_root->SetBounds(Rect{ { 0, 0 }, logicalSize });
        RequestRedraw();
    }

    void UIContext::SetLayoutEngine(std::unique_ptr<ILayoutEngine> engine)
    {
        m_layoutEngine = std::move(engine);
        MarkAllLayoutDirty(m_root.get());
        RequestRedraw();
    }

    ILayoutEngine& UIContext::GetLayoutEngine() const
    {
        return m_layoutEngine ? *m_layoutEngine : FlexLayout::Default();
    }

    void UIContext::MarkAllLayoutDirty(Node* node)
    {
        node->m_needsLayout  = true;
        node->m_measureValid = false;
        for (auto& c : node->m_children)
            MarkAllLayoutDirty(c.get());
    }

    // =========================================================
    // 帧
    // =========================================================
    void UIContext::RequestRedraw()
    {
        if (m_needsRedraw)
            return;
        m_needsRedraw = true;
        if (m_redrawCallback)
            m_redrawCallback();
    }

    bool UIContext::Update()
    {
        if (m_root->m_needsLayout || m_root->m_subtreeNeedsLayout)
        {
            m_inLayout = true;
            LayoutNode(m_root.get());
            m_inLayout = false;

            // 布局变了，指针下方的节点可能也变了
            UpdateHoverFromPointer();
        }
        return m_needsRedraw;
    }

    void UIContext::LayoutNode(Node* node)
    {
        if (node->m_needsLayout)
        {
            node->m_needsLayout = false;
            node->OnLayout();
        }
        node->m_subtreeNeedsLayout = false;

        for (auto& c : node->m_children)
        {
            if (c->m_needsLayout || c->m_subtreeNeedsLayout)
                LayoutNode(c.get());
        }
    }

    // =========================================================
    // 主题
    // =========================================================
    void UIContext::NotifyThemeChanged(Node* node)
    {
        node->OnThemeChanged();
        for (auto& c : node->m_children)
            NotifyThemeChanged(c.get());
    }

    void UIContext::SetTheme(const ThemeColors& theme)
    {
        m_theme              = theme;
        m_drawShared.palette = m_theme.Data();
        // 颜色在绘制时才解析，重绘即可；个别控件缓存了与颜色相关的资源时在 OnThemeChanged 里更新
        NotifyThemeChanged(m_root.get());
        RequestRedraw();
    }

    void UIContext::Render()
    {
        // 之前事件中关闭的节点此时已不在任何调用栈上，可以安全销毁。
        // 只在 Render 里做：Update 可能在事件处理中被调用（例如列表滚动到可见位置前先完成布局）
        m_graveyard.clear();

        Update();

        // 绘制过程中可能有新字形加入图集；若图集因此扩容或清空，本帧已生成的 UV 失效，重新绘制一遍。
        // 第二遍只需要本帧用到的字形，一定放得下（除非单帧字形就超过整个图集）
        GlyphAtlas& atlas = m_text->GetAtlas();
        for (int pass = 0; pass < 2; ++pass)
        {
            atlas.BeginFrame();
            m_drawShared.whiteTexture = atlas.GetTexture();
            m_drawShared.whitePixelUV = atlas.GetWhitePixelUV();

            m_drawList.Reset(Rect{ { 0, 0 }, m_displaySize });
            PaintNode(m_root.get(), Vec2{});

            if (!atlas.Changed())
                break;
        }
        atlas.Upload();

        DrawData data;
        data.lists.push_back(&m_drawList);
        data.displaySize      = m_displaySize;
        data.framebufferScale = m_pixelScale;
        m_backend->Render(data);

        m_needsRedraw = false;
    }

    void UIContext::PaintNode(Node* node, Vec2 parentOrigin)
    {
        if (!node->m_visible)
            return;

        const Rect screen{ parentOrigin + node->m_bounds.min, parentOrigin + node->m_bounds.max };
        const bool selfVisible = screen.Overlaps(m_drawList.GetClipRect());

        // 自己不在裁剪区域内时跳过自己的绘制；子节点可能超出父节点范围，仍然逐个判断
        if (selfVisible)
            node->OnPaint(m_drawList, screen);

        if (!node->m_children.empty())
        {
            if (node->m_clipChildren)
            {
                if (!selfVisible)
                    return;
                m_drawList.PushClipRect(screen);
            }

            for (auto& c : node->m_children)
                PaintNode(c.get(), screen.min);

            if (node->m_clipChildren)
                m_drawList.PopClipRect();
        }

        if (selfVisible)
            node->OnPaintOverlay(m_drawList, screen);
    }

    // =========================================================
    // 命中测试
    // =========================================================
    Node* UIContext::HitTest(Vec2 windowPos) const
    {
        return HitTestNode(m_root.get(), windowPos);
    }

    bool UIContext::IsPointerOverUI(Vec2 windowPos) const
    {
        if (m_capture || GetTopPopup())
            return true;
        const Node* hit = HitTest(windowPos);
        return hit && hit != m_root.get() && hit != m_mainLayer && hit != m_popupLayer;
    }

    bool UIContext::WantsKeyboard() const
    {
        return m_focus != nullptr || GetTopPopup() != nullptr;
    }

    Node* UIContext::HitTestNode(Node* node, Vec2 posInParent) const
    {
        if (!node->m_visible)
            return nullptr;

        const Vec2 local  = posInParent - node->m_bounds.min;
        const bool inside = Rect{ { 0, 0 }, node->GetSize() }.Contains(local);
        if (!inside && node->m_clipChildren)
            return nullptr;

        // 节点声明由自己处理这个位置（例如滚动条区域盖在内容上）
        if (inside && node->m_hitTestVisible && node->InterceptsHit(local))
            return node;

        // 后绘制的子节点在上面，先测试
        for (auto it = node->m_children.rbegin(); it != node->m_children.rend(); ++it)
        {
            if (Node* hit = HitTestNode(it->get(), local))
                return hit;
        }

        return (inside && node->m_hitTestVisible) ? node : nullptr;
    }

    // =========================================================
    // 悬停：维护 Enter / Leave
    // =========================================================
    void UIContext::UpdateHoverFromPointer()
    {
        if (!m_pointerInside)
        {
            if (!m_capture)
                UpdateHover(nullptr);
            return;
        }

        Node* hit = HitTest(m_pointerPos);

        // 捕获期间只有捕获节点（及其子节点）能处于悬停状态，
        // 这样按钮可以根据 IsHovered 判断按下后指针是否还在自己上面
        if (m_capture && hit && !m_capture->IsAncestorOf(hit))
            hit = nullptr;

        UpdateHover(hit);
    }

    void UIContext::UpdateHover(Node* target)
    {
        if (target == m_hovered)
            return;

        const uint64_t version = m_treeVersion;
        Node* old = m_hovered;
        m_hovered = target;
        OnHoverChangedForTooltip();

        PointerEvent e;
        e.position  = m_pointerPos;
        e.buttons   = m_buttons;
        e.modifiers = m_modifiers;

        // Leave：从旧目标往上，直到遇到同时也是新目标祖先的节点
        for (Node* n = old; n && !(target && n->IsAncestorOf(target)); n = n->m_parent)
        {
            n->m_hovered = false;
            e.type = PointerEventType::Leave;
            SendDirect(n, e);
            if (version != m_treeVersion)
                return;
        }

        // Enter：新目标链上尚未悬停的节点，按从根到叶的顺序
        m_pathScratch.clear();
        for (Node* n = target; n && !n->m_hovered; n = n->m_parent)
            m_pathScratch.push_back(n);

        std::vector<Node*> entering(m_pathScratch.rbegin(), m_pathScratch.rend());
        for (Node* n : entering)
        {
            n->m_hovered = true;
            e.type = PointerEventType::Enter;
            SendDirect(n, e);
            if (version != m_treeVersion)
                return;
        }
    }

    // =========================================================
    // 事件分发
    // =========================================================
    void UIContext::SendDirect(Node* node, PointerEvent e)
    {
        e.phase         = EventPhase::Target;
        e.localPosition = e.position - node->GetScreenBounds().min;
        node->OnPointerEvent(e);
    }

    bool UIContext::Dispatch(Node* target, PointerEvent& e)
    {
        if (!target)
            return false;

        // 路径：根 → 目标，同时算出每个节点的窗口坐标原点
        m_pathScratch.clear();
        for (Node* n = target; n; n = n->m_parent)
            m_pathScratch.push_back(n);
        std::reverse(m_pathScratch.begin(), m_pathScratch.end());

        std::vector<Node*> path = m_pathScratch;   // 处理函数里可能再次分发，不能直接用成员缓冲
        std::vector<Vec2>  origins(path.size());
        Vec2 origin;
        for (size_t i = 0; i < path.size(); ++i)
        {
            origin += path[i]->m_bounds.min;
            origins[i] = origin;
        }

        const uint64_t version = m_treeVersion;
        auto deliver = [&](size_t i, EventPhase phase) -> bool
        {
            Node* n = path[i];
            if (!n->IsEnabled())
                return false;
            e.phase         = phase;
            e.localPosition = e.position - origins[i];
            n->OnPointerEvent(e);
            // 节点被移除后路径失效，立即停止
            return e.handled || version != m_treeVersion;
        };

        const size_t last = path.size() - 1;
        for (size_t i = 0; i < last; ++i)
        {
            if (deliver(i, EventPhase::Capture))
                return e.handled;
        }
        if (deliver(last, EventPhase::Target))
            return e.handled;
        for (size_t i = last; i-- > 0;)
        {
            if (deliver(i, EventPhase::Bubble))
                return e.handled;
        }
        return e.handled;
    }

    bool UIContext::PointerMove(Vec2 pos, uint8_t modifiers)
    {
        m_pointerPos    = pos;
        m_pointerInside = true;
        m_modifiers     = modifiers;
        UpdateHoverFromPointer();

        PointerEvent e;
        e.type      = PointerEventType::Move;
        e.position  = pos;
        e.buttons   = m_buttons;
        e.modifiers = modifiers;
        return Dispatch(m_capture ? m_capture : HitTest(pos), e);
    }

    bool UIContext::PointerDown(Vec2 pos, MouseButton button, uint8_t modifiers, uint32_t timeMs)
    {
        m_pointerPos    = pos;
        m_pointerInside = true;
        m_modifiers     = modifiers;
        m_buttons      |= ToMask(button);
        UpdateHoverFromPointer();

        // 连击计数：同一按键、双击时间内、位置相差不超过 4 个逻辑像素
        const bool continues = button == m_lastClickButton &&
                               (timeMs - m_lastClickTime) <= m_doubleClickTime &&
                               (pos - m_lastClickPos).LengthSq() <= 16.0f;
        m_clickCount      = continues ? m_clickCount + 1 : 1;
        m_lastClickTime   = timeMs;
        m_lastClickPos    = pos;
        m_lastClickButton = button;

        HideTooltip(true);
        m_altAlone = false;
        if (button == MouseButton::Right)
            m_rightDownDismissed = false;

        Node* target = m_capture ? m_capture : HitTest(pos);

        if (!m_capture)
        {
            // 点在弹层外：关闭轻触关闭的弹层，这次点击被消费（与系统菜单行为一致）
            if (DismissPopupsFor(target))
            {
                m_rightDownDismissed = button == MouseButton::Right;
                return true;
            }

            // 按下时先转移焦点（已捕获时是同一次拖拽中的另一个按键，不改变焦点）
            FocusFromPointer(target);
        }

        PointerEvent e;
        e.type       = PointerEventType::Down;
        e.position   = pos;
        e.button     = button;
        e.buttons    = m_buttons;
        e.modifiers  = modifiers;
        e.clickCount = m_clickCount;
        return Dispatch(target, e);
    }

    bool UIContext::PointerUp(Vec2 pos, MouseButton button, uint8_t modifiers)
    {
        m_pointerPos = pos;
        m_modifiers  = modifiers;
        m_buttons   &= static_cast<uint8_t>(~ToMask(button));

        PointerEvent e;
        e.type      = PointerEventType::Up;
        e.position  = pos;
        e.button    = button;
        e.buttons   = m_buttons;
        e.modifiers = modifiers;
        const bool captured = m_capture != nullptr;
        bool handled = Dispatch(m_capture ? m_capture : HitTest(pos), e);

        // 所有按键都松开后自动释放捕获，防止节点忘记释放导致界面"卡住"
        if (m_buttons == 0)
            m_capture = nullptr;

        UpdateHoverFromPointer();

        // 右键抬起：弹出右键菜单（与 Windows 的 WM_CONTEXTMENU 时机一致）。
        // 按下时关闭了弹层、或正在拖拽（右键拖动平移视图等）时不弹出
        if (button == MouseButton::Right && !handled && !captured && !m_rightDownDismissed)
            handled = OpenContextMenu(HitTest(pos), pos);
        if (button == MouseButton::Right)
            m_rightDownDismissed = false;
        return handled;
    }

    bool UIContext::PointerWheel(Vec2 pos, Vec2 delta, uint8_t modifiers)
    {
        m_pointerPos = pos;
        m_modifiers  = modifiers;
        HideTooltip(true);

        PointerEvent e;
        e.type       = PointerEventType::Wheel;
        e.position   = pos;
        e.wheelDelta = delta;
        e.buttons    = m_buttons;
        e.modifiers  = modifiers;
        return Dispatch(m_capture ? m_capture : HitTest(pos), e);
    }

    void UIContext::PointerLeave()
    {
        m_pointerInside = false;
        UpdateHoverFromPointer();
    }

    void UIContext::PointerCancel()
    {
        m_buttons = 0;
        if (Node* cap = m_capture)
        {
            m_capture = nullptr;
            PointerEvent e;
            e.type      = PointerEventType::Cancel;
            e.position  = m_pointerPos;
            e.modifiers = m_modifiers;
            SendDirect(cap, e);
        }
        UpdateHoverFromPointer();
    }

    // =========================================================
    // 捕获
    // =========================================================
    void UIContext::SetCapture(Node* node)
    {
        m_capture = node;
    }

    void UIContext::ReleaseCapture()
    {
        // 悬停状态在本次事件分发结束后（PointerUp 末尾或下一次移动）再更新，避免在处理函数里重入
        m_capture = nullptr;
    }

    void UIContext::OnSubtreeDetached(Node* subtree)
    {
        ++m_treeVersion;

        if (m_hovered && subtree->IsAncestorOf(m_hovered))
        {
            subtree->ClearHoverRecursive();
            m_hovered = subtree->m_parent;  // 父节点及以上仍然处于悬停状态
        }
        if (m_capture && subtree->IsAncestorOf(m_capture))
            m_capture = nullptr;
        if (m_focus && subtree->IsAncestorOf(m_focus))
            SetFocus(nullptr);    // 节点此时仍然存活，可以收到失去焦点的通知

        if (m_menuBarHost && subtree->IsAncestorOf(m_menuBarHost->GetMenuBarNode()))
            m_menuBarHost = nullptr;
        if (m_menuBarReturnFocus && subtree->IsAncestorOf(m_menuBarReturnFocus))
            m_menuBarReturnFocus = nullptr;

        // 悬浮提示的来源被移除
        if (m_tooltipOwner && subtree->IsAncestorOf(m_tooltipOwner))
            HideTooltip(false);
        if (m_tooltipSuppressed && subtree->IsAncestorOf(m_tooltipSuppressed))
            m_tooltipSuppressed = nullptr;

        // 弹层里保存的指针：要恢复焦点的节点被移除就不再恢复；所有者被移除则关闭弹层
        if (m_popupLayer && subtree != m_popupLayer)
        {
            std::vector<Popup*> orphaned;
            for (auto& c : m_popupLayer->m_children)
            {
                auto* p = dynamic_cast<Popup*>(c.get());
                if (!p || p == subtree)
                    continue;
                if (p->m_restoreFocus && subtree->IsAncestorOf(p->m_restoreFocus))
                    p->m_restoreFocus = nullptr;
                if (p->m_open && p->m_owner && subtree->IsAncestorOf(p->m_owner) && !p->IsAncestorOf(subtree))
                    orphaned.push_back(p);
            }
            for (Popup* p : orphaned)
                ClosePopup(p);
        }

        RequestRedraw();
    }

    // =========================================================
    // 弹层
    // =========================================================
    Popup* UIContext::OpenPopup(std::unique_ptr<Popup> popup, bool takeFocus)
    {
        Popup* p = popup.get();
        if (p->m_modal)
        {
            auto backdrop = std::make_unique<ModalBackdrop>();
            p->m_backdrop = backdrop.get();
            m_popupLayer->AddChild(std::move(backdrop));
        }
        m_popupLayer->AddChild(std::move(popup));
        p->m_open = true;
        HideTooltip(false);

        if (takeFocus)
        {
            p->m_restoreFocus = m_focus;
            SetFocus(p, FocusReason::Program);
        }
        p->OnOpened();
        RequestRedraw();
        return p;
    }

    void UIContext::ClosePopup(Popup* popup)
    {
        if (!popup || !popup->m_open)
            return;

        // 先关闭它上面的弹层（子菜单、弹层里打开的下拉框）
        auto& children = m_popupLayer->m_children;
        for (size_t i = children.size(); i-- > 0;)
        {
            if (i >= children.size())
                continue;
            Node* n = children[i].get();
            if (n == popup)
                break;
            if (auto* above = dynamic_cast<Popup*>(n); above && above->m_open)
                ClosePopup(above);
        }

        popup->m_open = false;
        Node* restore     = popup->m_restoreFocus;
        const bool hadFocus = m_focus && popup->IsAncestorOf(m_focus);

        if (popup->m_backdrop)
        {
            m_graveyard.push_back(m_popupLayer->RemoveChild(popup->m_backdrop));
            popup->m_backdrop = nullptr;
        }
        std::unique_ptr<Node> owned = m_popupLayer->RemoveChild(popup);

        // 焦点还给打开前的节点（它可能已被移除或禁用）
        if (hadFocus && restore && restore->CanTakeFocus())
            SetFocus(restore, FocusReason::Program);

        auto onClosed = std::move(popup->m_onClosed);
        m_graveyard.push_back(std::move(owned));   // 可能正在执行自己的事件处理函数，推迟销毁
        if (onClosed)
            onClosed();
        RequestRedraw();
    }

    void UIContext::CloseAllPopups()
    {
        while (Popup* top = GetTopPopup())
            ClosePopup(top);
    }

    Popup* UIContext::GetTopPopup() const
    {
        const auto& children = m_popupLayer->m_children;
        for (auto it = children.rbegin(); it != children.rend(); ++it)
        {
            if (auto* p = dynamic_cast<Popup*>(it->get()); p && p->m_open)
                return p;
        }
        return nullptr;
    }

    bool UIContext::HasModal() const
    {
        for (const auto& c : m_popupLayer->m_children)
        {
            if (auto* p = dynamic_cast<Popup*>(c.get()); p && p->m_open && p->m_modal)
                return true;
        }
        return false;
    }

    bool UIContext::IsInPopupLayer(const Node* node) const
    {
        return node && m_popupLayer->IsAncestorOf(node);
    }

    bool UIContext::OpenContextMenu(Node* target, Vec2 windowPos)
    {
        HideTooltip(true);
        const uint64_t version = m_treeVersion;
        for (Node* n = target; n; n = n->m_parent)
        {
            if (!n->IsEnabled())
                continue;
            if (n->OnContextMenu(windowPos))
                return true;
            if (version != m_treeVersion)
                return true;
        }
        return false;
    }

    bool UIContext::DismissPopupsFor(Node* target)
    {
        bool closed = false;
        while (Popup* top = GetTopPopup())
        {
            // 点在最上层弹层里，或点在它的所有者上（交给所有者自己处理切换）
            if (target && (top->IsAncestorOf(target) || (top->m_owner && top->m_owner->IsAncestorOf(target))))
                break;
            if (!top->m_lightDismiss)
                break;      // 模态或常驻弹层：点在外面也不关闭，更下面的弹层也不受影响
            ClosePopup(top);
            closed = true;
        }
        return closed;
    }

    // =========================================================
    // 定时器
    // =========================================================
    uint32_t UIContext::Now() const
    {
        return m_clock ? m_clock() : SteadyClockMs();
    }

    UIContext::TimerId UIContext::StartTimer(uint32_t delayMs, std::function<void()> callback, bool repeat)
    {
        const TimerId id = m_nextTimerId++;
        m_timers.push_back(Timer{ id, Now() + delayMs, std::max<uint32_t>(delayMs, 1), repeat, std::move(callback) });
        NotifyTimerScheduler();
        return id;
    }

    void UIContext::StopTimer(TimerId id)
    {
        const auto it = std::find_if(m_timers.begin(), m_timers.end(), [id](const Timer& t) { return t.id == id; });
        if (it == m_timers.end())
            return;
        m_timers.erase(it);
        NotifyTimerScheduler();
    }

    void UIContext::Tick()
    {
        const uint32_t now = Now();

        // 先收集到期的回调再执行：回调里可能启动或停止定时器
        std::vector<std::function<void()>> due;
        for (auto it = m_timers.begin(); it != m_timers.end();)
        {
            if (static_cast<int32_t>(now - it->due) >= 0)
            {
                due.push_back(it->callback);
                if (it->repeat)
                {
                    it->due = now + it->interval;
                    ++it;
                }
                else
                {
                    it = m_timers.erase(it);
                }
            }
            else
            {
                ++it;
            }
        }
        for (auto& cb : due)
            if (cb) cb();
        NotifyTimerScheduler();
    }

    bool UIContext::GetNextTimerDelay(uint32_t& delayMs) const
    {
        if (m_timers.empty())
            return false;
        const uint32_t now = Now();
        int64_t best = INT64_MAX;
        for (const Timer& t : m_timers)
            best = std::min<int64_t>(best, static_cast<int32_t>(t.due - now));
        delayMs = static_cast<uint32_t>(std::max<int64_t>(best, 0));
        return true;
    }

    void UIContext::SetTimerScheduler(std::function<void(bool, uint32_t)> scheduler)
    {
        m_timerScheduler = std::move(scheduler);
        NotifyTimerScheduler();
    }

    void UIContext::NotifyTimerScheduler()
    {
        if (!m_timerScheduler)
            return;
        uint32_t delay = 0;
        const bool active = GetNextTimerDelay(delay);
        m_timerScheduler(active, delay);
    }

    // =========================================================
    // 悬浮提示与光标
    // =========================================================
    Node* UIContext::FindTooltipOwner(Node* node) const
    {
        for (Node* n = node; n; n = n->m_parent)
        {
            if (!n->m_tooltip.empty())
                return n;
        }
        return nullptr;
    }

    void UIContext::OnHoverChangedForTooltip()
    {
        Node* owner = FindTooltipOwner(m_hovered);
        if (owner && owner == m_tooltipOwner)
            return;     // 还在同一个提示来源上（例如从按钮移到按钮里的图标）

        if (owner != m_tooltipSuppressed)
            m_tooltipSuppressed = nullptr;

        const bool wasVisible = m_tooltipNode != nullptr;
        HideTooltip(false);
        if (!owner || owner == m_tooltipSuppressed || m_capture || m_buttons)
            return;

        // 刚显示过提示时，移到相邻控件立即显示，不再等待
        const bool recent = wasVisible || (Now() - m_tooltipHiddenAt) < 400;
        m_tooltipOwner = owner;
        m_tooltipTimer = StartTimer(recent ? 60 : m_tooltipDelay, [this]
        {
            m_tooltipTimer = 0;
            ShowTooltip();
        });
    }

    void UIContext::ShowTooltip()
    {
        if (!m_tooltipOwner || m_tooltipNode)
            return;
        m_tooltipText = m_tooltipOwner->m_tooltip;
        m_tooltipNode = m_tooltipLayer->AddChild<TooltipNode>(m_tooltipText, m_pointerPos + Vec2{ 12.0f, 22.0f });
        RequestRedraw();
    }

    void UIContext::HideTooltip(bool suppress)
    {
        if (m_tooltipTimer)
        {
            StopTimer(m_tooltipTimer);
            m_tooltipTimer = 0;
        }
        if (m_tooltipNode)
        {
            Node* node = m_tooltipNode;
            m_tooltipNode = nullptr;
            m_tooltipHiddenAt = Now();
            m_tooltipLayer->RemoveChild(node);   // 提示节点不处理事件，可以立即销毁
            RequestRedraw();
        }
        if (suppress)
            m_tooltipSuppressed = m_tooltipOwner ? m_tooltipOwner : FindTooltipOwner(m_hovered);
        m_tooltipOwner = nullptr;
        m_tooltipText.clear();
    }

    CursorShape UIContext::GetCursor() const
    {
        for (Node* n = m_capture ? m_capture : m_hovered; n; n = n->m_parent)
        {
            const CursorShape c = n->GetCursor(n->ToLocal(m_pointerPos));
            if (c != CursorShape::Default)
                return c;
        }
        return CursorShape::Arrow;
    }

    // =========================================================
    // 焦点
    // =========================================================
    void UIContext::SetFocus(Node* node, FocusReason reason)
    {
        if (node == m_focus)
        {
            if (reason == FocusReason::Keyboard && !m_focusVisible)
            {
                m_focusVisible = true;
                RequestRedraw();
            }
            return;
        }

        Node* old = m_focus;
        m_focus        = node;
        m_focusVisible = reason == FocusReason::Keyboard;

        if (old)
        {
            // 组合中切换焦点：先让旧节点结束组合（平台层随后会取消输入法里的组合串）
            if (m_composing)
            {
                m_composing = false;
                old->OnComposition(CompositionEvent{ CompositionEvent::Type::End, {}, 0 });
            }
            old->OnFocusChanged(false);
            old->Invalidate();
        }
        if (node)
        {
            node->OnFocusChanged(true);
            node->Invalidate();
        }
        RequestRedraw();
    }

    void UIContext::FocusFromPointer(Node* target)
    {
        // 点击到可聚焦节点（或其子节点）时聚焦它；点在其他地方时清除焦点
        for (Node* n = target; n; n = n->m_parent)
        {
            if (n->CanTakeFocus())
            {
                SetFocus(n, FocusReason::Pointer);
                return;
            }
        }
        // 点在弹层里不可聚焦的地方（例如输入建议列表）：焦点留在原处
        if (IsInPopupLayer(target))
            return;
        SetFocus(nullptr, FocusReason::Pointer);
    }

    void UIContext::CollectFocusable(Node* node, std::vector<Node*>& out) const
    {
        if (!node->m_visible || !node->m_enabled)
            return;
        if (node->m_focusable)
            out.push_back(node);
        for (auto& c : node->m_children)
            CollectFocusable(c.get(), out);
    }

    bool UIContext::FocusNext(bool backward)
    {
        // Tab 顺序即树的深度优先顺序（与界面从上到下、从左到右的结构一致）。
        // 有模态弹层时只在最上层的模态弹层内部循环
        Node* scope = m_root.get();
        const auto& popups = m_popupLayer->m_children;
        for (auto it = popups.rbegin(); it != popups.rend(); ++it)
        {
            if (auto* p = dynamic_cast<Popup*>(it->get()); p && p->m_open && p->m_modal)
            {
                scope = p;
                break;
            }
        }

        std::vector<Node*> nodes;
        CollectFocusable(scope, nodes);
        if (nodes.empty())
            return false;

        const auto it = std::find(nodes.begin(), nodes.end(), m_focus);
        const int  n  = static_cast<int>(nodes.size());
        int index;
        if (it == nodes.end())
            index = backward ? n - 1 : 0;
        else
            index = (static_cast<int>(it - nodes.begin()) + (backward ? n - 1 : 1)) % n;

        SetFocus(nodes[static_cast<size_t>(index)], FocusReason::Keyboard);
        return true;
    }

    bool UIContext::WantsTextInput() const
    {
        return m_focus && m_focus->AcceptsTextInput();
    }

    bool UIContext::GetTextCaretRect(Rect& out) const
    {
        return m_focus && m_focus->GetTextCaretRect(out);
    }

    // =========================================================
    // 键盘
    // =========================================================
    bool UIContext::DispatchKey(KeyEvent& e)
    {
        // 没有焦点时发给根节点，界面级的按键处理（例如 Esc 关闭弹层）可以挂在根上
        Node* target = m_focus ? m_focus : m_root.get();

        std::vector<Node*> path;
        for (Node* n = target; n; n = n->m_parent)
            path.push_back(n);
        std::reverse(path.begin(), path.end());

        const uint64_t version = m_treeVersion;
        auto deliver = [&](size_t i, EventPhase phase) -> bool
        {
            Node* n = path[i];
            if (!n->IsEnabled())
                return false;
            e.phase = phase;
            n->OnKeyEvent(e);
            return e.handled || version != m_treeVersion;
        };

        const size_t last = path.size() - 1;
        for (size_t i = 0; i < last; ++i)
            if (deliver(i, EventPhase::Capture)) return e.handled;
        if (deliver(last, EventPhase::Target)) return e.handled;
        for (size_t i = last; i-- > 0;)
            if (deliver(i, EventPhase::Bubble)) return e.handled;
        return e.handled;
    }

    bool UIContext::KeyDown(Key key, uint8_t modifiers, bool repeat)
    {
        m_modifiers = modifiers;
        HideTooltip(true);

        // Alt 单独按下再抬起才激活菜单栏；按下期间有别的键就不算
        if (key == Key::Alt)
        {
            if (!repeat)
                m_altAlone = true;
        }
        else if (key != Key::Shift && key != Key::Ctrl)
        {
            m_altAlone = false;
        }

        // 模态对话框打开时全局快捷键不生效（避免在对话框下面执行命令）
        if (!HasModal() && m_shortcuts.Process(key, modifiers, repeat, WantsTextInput()))
            return true;

        KeyEvent e;
        e.type      = KeyEventType::Down;
        e.key       = key;
        e.modifiers = modifiers;
        e.repeat    = repeat;
        if (DispatchKey(e))
            return true;

        const uint8_t shift = static_cast<uint8_t>(ModifierKey::Shift);
        const uint8_t alt   = static_cast<uint8_t>(ModifierKey::Alt);

        // 没有节点处理的 Tab：切换焦点
        if (key == Key::Tab && (modifiers & ~shift) == 0)
            return FocusNext((modifiers & shift) != 0);

        // 菜单键 / Shift+F10：在焦点控件处打开右键菜单
        if ((key == Key::Menu && modifiers == 0) || (key == Key::F10 && modifiers == shift))
        {
            Node* target = m_focus ? m_focus : m_hovered;
            if (!target)
                return false;
            Rect caret;
            const Rect bounds = target->GetScreenBounds();
            const Vec2 pos = target->GetTextCaretRect(caret) ? Vec2{ caret.min.x, caret.max.y }
                                                              : Vec2{ bounds.min.x + 8.0f, std::min(bounds.max.y, bounds.min.y + 24.0f) };
            return OpenContextMenu(target, pos);
        }

        if (m_menuBarHost && !HasModal())
        {
            // F10：激活菜单栏
            if (key == Key::F10 && modifiers == 0)
            {
                RememberFocusForMenuBar();
                return m_menuBarHost->ActivateFromKeyboard();
            }
            // Alt+字母：按助记符打开菜单
            if (modifiers == alt && key >= Key::A && key <= Key::Z)
            {
                RememberFocusForMenuBar();
                return m_menuBarHost->OpenByMnemonic(static_cast<char>('A' + (static_cast<int>(key) - static_cast<int>(Key::A))));
            }
        }
        return false;
    }

    void UIContext::RememberFocusForMenuBar()
    {
        // 焦点已经在菜单栏或菜单里时保留原来记录的节点
        Node* bar = m_menuBarHost ? m_menuBarHost->GetMenuBarNode() : nullptr;
        if (m_focus && !(bar && bar->IsAncestorOf(m_focus)) && !(m_popupLayer && m_popupLayer->IsAncestorOf(m_focus)))
            m_menuBarReturnFocus = m_focus;
        else if (!m_focus)
            m_menuBarReturnFocus = nullptr;
    }

    void UIContext::RestoreFocusAfterMenuBar()
    {
        Node* target = m_menuBarReturnFocus;
        m_menuBarReturnFocus = nullptr;
        SetFocus(target && target->CanTakeFocus() ? target : nullptr, FocusReason::Program);
    }

    bool UIContext::KeyUp(Key key, uint8_t modifiers)
    {
        m_modifiers = modifiers;

        if (key == Key::Alt && m_altAlone)
        {
            m_altAlone = false;
            if (m_menuBarHost && !HasModal() && !m_composing)
            {
                RememberFocusForMenuBar();
                return m_menuBarHost->ActivateFromKeyboard();
            }
        }

        KeyEvent e;
        e.type      = KeyEventType::Up;
        e.key       = key;
        e.modifiers = modifiers;
        return DispatchKey(e);
    }

    bool UIContext::TextInput(std::string_view utf8)
    {
        if (!m_focus || utf8.empty())
            return false;

        TextInputEvent e;
        e.text = std::string(utf8);

        const uint64_t version = m_treeVersion;
        for (Node* n = m_focus; n; n = n->m_parent)
        {
            if (!n->IsEnabled())
                continue;
            e.phase = (n == m_focus) ? EventPhase::Target : EventPhase::Bubble;
            n->OnTextInput(e);
            if (e.handled || version != m_treeVersion)
                break;
        }
        return e.handled;
    }

    void UIContext::CompositionStart()
    {
        m_composing = true;
        if (m_focus)
            m_focus->OnComposition(CompositionEvent{ CompositionEvent::Type::Start, {}, 0 });
    }

    void UIContext::CompositionUpdate(std::string_view utf8, int caret)
    {
        m_composing = true;
        if (m_focus)
            m_focus->OnComposition(CompositionEvent{ CompositionEvent::Type::Update, std::string(utf8), caret });
    }

    void UIContext::CompositionEnd()
    {
        if (!m_composing)
            return;
        m_composing = false;
        if (m_focus)
            m_focus->OnComposition(CompositionEvent{ CompositionEvent::Type::End, {}, 0 });
    }

    void UIContext::KeyboardFocusLost()
    {
        CompositionEnd();
        m_modifiers = 0;
    }
}
