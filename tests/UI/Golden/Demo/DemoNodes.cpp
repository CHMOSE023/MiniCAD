#include "DemoNodes.h"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include "Render/IRenderBackend.h"
#include "Widgets/Label.h"
#include "Widgets/TextBox.h"
#include "Style/Theme.hpp"
#include <cmath>
#include <cstdio>
#include <numbers>

namespace MiniGUI
{
    namespace
    {
        constexpr float kPi = std::numbers::pi_v<float>;

        // 深色主题配色（暂时写死，M7 再换成主题变量）
        constexpr ColorRef kPanel      = Theme::Panel;
        constexpr ColorRef kSidebar    = Theme::PanelAlt;
        constexpr ColorRef kSeparator  = Theme::BorderSubtle;
        constexpr ColorRef kAccent     = Theme::Accent;
        constexpr ColorRef kAccentSoft = Theme::AccentSoft;
        constexpr Color32 kGreen      = ColorFromHex(0x5FB865);
        constexpr Color32 kOrange     = ColorFromHex(0xE08855);
        constexpr Color32 kRed        = ColorFromHex(0xDB5C5C);
        constexpr ColorRef kText       = Theme::Text;
        constexpr ColorRef kTextDim    = Theme::TextDim;

        constexpr float kToolbarH = 40.0f;
        constexpr float kStatusH  = 24.0f;
        constexpr float kSidebarW = 220.0f;

        bool g_logEnabled = true;

        void Log(const char* text)
        {
            if (!g_logEnabled)
                return;
            std::printf("%s\n", text);
            std::fflush(stdout);
        }
    }

    void SetDemoLogEnabled(bool enabled)
    {
        g_logEnabled = enabled;
    }

    Image CreateCheckerImage()
    {
        constexpr int kSize = 64;
        Image img;
        img.width  = kSize;
        img.height = kSize;
        img.pixels.resize(kSize * kSize);
        for (int y = 0; y < kSize; ++y)
            for (int x = 0; x < kSize; ++x)
                img.pixels[y * kSize + x] = (((x / 8) + (y / 8)) & 1) ? ColorFromHex(0x4A4D52) : ColorFromHex(0x9DA0A8);
        return img;
    }

    TextureId CreateCheckerTexture(IRenderBackend& backend)
    {
        const Image img = CreateCheckerImage();
        return backend.CreateTexture(img.width, img.height, TextureFormat::RGBA8, img.pixels.data());
    }

    // =========================================================
    // IconNode
    // =========================================================
    IconNode::IconNode(Kind kind)
        : m_kind(kind)
    {
        SetHitTestVisible(false);   // 点击穿透到按钮
    }

    void IconNode::OnPaint(DrawList& dl, const Rect& screenRect)
    {
        const ColorRef color = IsEnabled() ? kText : kTextDim;
        const Vec2    c     = screenRect.Center();
        const float   s     = 6.0f;

        switch (m_kind)
        {
        case Kind::Plus:
            dl.AddLine({ c.x - s, c.y }, { c.x + s, c.y }, color, 1.5f);
            dl.AddLine({ c.x, c.y - s }, { c.x, c.y + s }, color, 1.5f);
            break;
        case Kind::Minus:
            dl.AddLine({ c.x - s, c.y }, { c.x + s, c.y }, color, 1.5f);
            break;
        case Kind::Clip:
            dl.AddRect({ c.x - s, c.y - s, c.x + s, c.y + s }, color, 2.0f, 1.5f);
            dl.AddCircleFilled({ c.x + s, c.y + s }, 3.5f, color);
            break;
        case Kind::Power:
            dl.PathArcTo(c, s, -kPi * 0.25f, kPi * 1.25f);
            dl.PathStroke(color, false, 1.5f);
            dl.AddLine({ c.x, c.y - s - 1.0f }, { c.x, c.y - 1.0f }, color, 1.5f);
            break;
        case Kind::Circle:
            dl.AddCircle(c, s, color, 1.5f);
            break;
        case Kind::Sidebar:
            dl.AddRect({ c.x - s - 1, c.y - s, c.x + s + 1, c.y + s }, color, 2.0f, 1.5f);
            dl.AddLine({ c.x - 1.5f, c.y - s }, { c.x - 1.5f, c.y + s }, color, 1.5f);
            break;
        }
    }

    // =========================================================
    // StatusBar
    // =========================================================
    void StatusBar::OnPaint(DrawList& dl, const Rect& r)
    {
        dl.AddRectFilled(r, kPanel);

        const float cy = r.Center().y;
        const Color32 lights[3] = { kGreen, kOrange, kRed };
        for (int i = 0; i < 3; ++i)
            dl.AddCircleFilled({ r.max.x - 20.0f - i * 16.0f, cy }, 4.0f, lights[i]);

        // 文字直接用 TextSystem 绘制：左侧状态，右侧图集统计（观察字形按需加载）
        UIContext* ctx = GetContext();
        if (!ctx)
            return;
        TextSystem& text = ctx->GetTextSystem();

        TextParams p;
        p.size   = 12.0f;
        p.color  = kTextDim;
        p.vAlign = TextAlign::Center;

        char left[256];
        std::snprintf(left, sizeof(left), "%s  ·  侧栏 %d 项", m_message.c_str(), m_count);
        text.Draw(dl, Rect{ r.min.x + 12.0f, r.min.y, r.max.x, r.max.y }, left, p);

        const GlyphAtlas& atlas = text.GetAtlas();
        char right[96];
        std::snprintf(right, sizeof(right), "字形 %zu 个  ·  图集 %d×%d  ·  缩放 %d%%",
                      atlas.GetGlyphCount(), atlas.GetWidth(), atlas.GetHeight(),
                      static_cast<int>(std::lround(ctx->GetPixelScale() * 100.0f)));
        p.hAlign = TextAlign::End;
        text.Draw(dl, Rect{ r.min.x, r.min.y, r.max.x - 72.0f, r.max.y }, right, p);
    }

    // =========================================================
    // DemoRoot
    // =========================================================
    DemoRoot::DemoRoot(TextureId checker)
    {
        LayoutStyle rootStyle;
        rootStyle.direction = FlexDirection::Column;
        SetLayoutStyle(rootStyle);

        // ── 工具栏 ──────────────────────────────────────────────
        Panel* toolbar = AddChild<Panel>(kPanel);
        {
            LayoutStyle s;
            s.direction  = FlexDirection::Row;
            s.alignItems = Align::Center;
            s.height     = kToolbarH;
            s.padding    = Edges::Symmetric(8.0f, 0.0f);
            s.gap        = 8.0f;
            s.shrink     = 0.0f;
            toolbar->SetLayoutStyle(s);
        }

        AddToolButton(toolbar, IconNode::Kind::Plus,  [this] { AddSidebarItem(); });
        AddToolButton(toolbar, IconNode::Kind::Minus, [this] { RemoveSidebarItem(); });
        AddSeparator(toolbar, true)->EditLayoutStyle().height = 20.0f;

        m_clipButton = AddToolButton(toolbar, IconNode::Kind::Clip, [this] { ToggleClipDemo(); });
        m_clipButton->SetChecked(true);

        AddToolButton(toolbar, IconNode::Kind::Power, [this]
        {
            const bool enable = !m_sidebar->IsEnabled();
            m_sidebar->SetEnabled(enable);
            Log(enable ? "[点击] 启用侧栏" : "[点击] 禁用侧栏");
        });

        // 文字按钮：宽度由文字决定
        AddSeparator(toolbar, true)->EditLayoutStyle().height = 20.0f;
        for (const char* name : { "新建", "打开", "保存", "Export…" })
        {
            Button* b = toolbar->AddChild<Button>(name, [this, name]
            {
                char text[64];
                std::snprintf(text, sizeof(text), "[点击] %s", name);
                Log(text);
                m_statusBar->SetMessage(text);
            });
            ButtonStyle style = b->GetStyle();
            style.normal          = Colors::Transparent;
            style.borderThickness = 0.0f;
            b->SetStyle(style);
            b->EditLayoutStyle().height = 28.0f;
        }

        // 弹簧：占满中间剩余空间，把后面的按钮推到最右边
        toolbar->AddChild<Node>()->EditLayoutStyle().grow = 1.0f;

        m_sideToggle = AddToolButton(toolbar, IconNode::Kind::Sidebar, [this] { ToggleSidebar(); });
        m_sideToggle->SetChecked(true);

        AddSeparator(this, false);

        // ── 主体：侧栏 | 主区域 ─────────────────────────────────
        Node* body = AddChild<Node>();
        {
            LayoutStyle s;
            s.direction = FlexDirection::Row;
            s.grow      = 1.0f;
            body->SetLayoutStyle(s);
        }

        m_sidebar = body->AddChild<Panel>(kSidebar);
        {
            LayoutStyle s;
            s.direction = FlexDirection::Column;
            s.width     = kSidebarW;
            s.padding   = Edges::All(12.0f);
            s.gap       = 12.0f;
            s.shrink    = 0.0f;
            m_sidebar->SetLayoutStyle(s);
            m_sidebar->SetClipChildren(true);   // 项目太多时超出部分裁掉
        }

        // 侧栏上半部分是项目列表（占满剩余空间，超出裁剪），下半部分是命令输入区
        m_itemList = m_sidebar->AddChild<Node>();
        {
            LayoutStyle s;
            s.gap    = 12.0f;
            s.grow   = 1.0f;
            s.shrink = 1.0f;
            m_itemList->SetLayoutStyle(s);
            m_itemList->SetClipChildren(true);
        }
        AddCommandArea();
        m_sideSep = AddSeparator(body, true);

        m_primitives = body->AddChild<PrimitivesView>(checker);
        m_primitives->EditLayoutStyle().grow = 1.0f;
        m_primitives->SetClipChildren(true);

        AddTextShowcase();

        // 浮动角标：绝对定位在主区域右下角，窗口改变大小时跟随
        Panel* badge = m_primitives->AddChild<Panel>(kAccentSoft);
        {
            LayoutStyle s;
            s.position   = PositionType::Absolute;
            s.right      = 16.0f;
            s.bottom     = 16.0f;
            s.width      = 140.0f;
            s.height     = 32.0f;
            s.justify    = Justify::Center;
            s.alignItems = Align::Center;
            badge->SetLayoutStyle(s);
            badge->SetRounding(16.0f);
            badge->SetBorder(kAccent, 1.0f);
            badge->AddChild<Label>("MiniGUI · M5", 13.0f);
        }

        AddSeparator(this, false);

        // ── 状态栏 ──────────────────────────────────────────────
        m_statusBar = AddChild<StatusBar>();
        {
            LayoutStyle s;
            s.height = kStatusH;
            s.shrink = 0.0f;
            m_statusBar->SetLayoutStyle(s);
        }

        for (int i = 0; i < 5; ++i)
            AddSidebarItem();
        SelectSidebarItem(m_sideItems[1]);
    }

    Button* DemoRoot::AddToolButton(Node* parent, IconNode::Kind icon, std::function<void()> onClick)
    {
        Button* b = parent->AddChild<Button>(std::move(onClick));
        ButtonStyle style = b->GetStyle();
        style.normal          = Colors::Transparent;
        style.checked         = kAccentSoft;
        style.borderThickness = 0.0f;
        b->SetStyle(style);
        b->SetLayoutStyle([] { LayoutStyle s; s.width = 28.0f; s.height = 28.0f; s.shrink = 0.0f; return s; }());

        // 图标拉伸到整个按钮，自己画在中心
        b->AddChild<IconNode>(icon)->EditLayoutStyle().grow = 1.0f;
        return b;
    }

    Node* DemoRoot::AddSeparator(Node* parent, bool vertical)
    {
        Panel* sep = parent->AddChild<Panel>(kSeparator);
        LayoutStyle s;
        if (vertical) s.width = 1.0f; else s.height = 1.0f;
        s.shrink = 0.0f;
        sep->SetLayoutStyle(s);
        return sep;
    }

    void DemoRoot::ToggleClipDemo()
    {
        const bool show = !m_primitives->GetShowClipDemo();
        m_primitives->SetShowClipDemo(show);
        m_clipButton->SetChecked(show);
        Log(show ? "[点击] 显示裁剪示例" : "[点击] 隐藏裁剪示例");
        m_statusBar->SetMessage(show ? "显示裁剪示例" : "隐藏裁剪示例");
    }

    void DemoRoot::ToggleSidebar()
    {
        const bool show = !m_sidebar->IsVisible();
        m_sidebar->SetVisible(show);
        m_sideSep->SetVisible(show);
        m_sideToggle->SetChecked(show);
        Log(show ? "[点击] 显示侧栏" : "[点击] 隐藏侧栏");
    }

    void DemoRoot::RegisterShortcuts(ShortcutTable& shortcuts)
    {
        const uint8_t ctrl = Mods(ModifierKey::Ctrl);
        shortcuts.Register(Key::N, ctrl, [this] { AddSidebarItem(); });
        shortcuts.Register(Key::W, ctrl, [this] { RemoveSidebarItem(); });
        shortcuts.Register(Key::B, ctrl, [this] { ToggleSidebar(); });
        shortcuts.Register(Key::S, ctrl, [this]
        {
            Log("[快捷键] Ctrl+S 保存");
            m_statusBar->SetMessage("Ctrl+S：已保存");
        });

        // 功能键不参与文字编辑，输入框持有焦点时也生效
        ShortcutTable::Options fnKey;
        fnKey.allowInTextInput = true;
        shortcuts.Register(Key::F8, 0, [this] { ToggleClipDemo(); }, fnKey);
    }

    void DemoRoot::AddCommandArea()
    {
        m_sidebar->AddChild<Label>("命令行（Enter 执行）", 12.0f, kTextDim);

        TextBox* command = m_sidebar->AddChild<TextBox>();
        command->SetPlaceholder("输入命令，例如 LINE / 直线");
        command->SetOnSubmit([this, command](const std::string& text)
        {
            if (text.empty())
                return;
            std::string msg = "执行命令：" + text;
            Log(("[命令] " + text).c_str());
            m_statusBar->SetMessage(msg);
            command->SetText({});
        });

        m_sidebar->AddChild<Label>("备注（多行，自动换行）", 12.0f, kTextDim);
        TextBox* notes = m_sidebar->AddChild<TextBox>();
        notes->SetMultiline(true);
        notes->SetRows(4);
        notes->SetPlaceholder("支持中文输入法、Ctrl+Z 撤销、双击选词");
    }

    void DemoRoot::AddTextShowcase()
    {
        // 文字展示面板：绝对定位在主区域底部，左列不同字号，右列自动换行与省略号
        Panel* panel = m_primitives->AddChild<Panel>(kPanel);
        {
            LayoutStyle s;
            s.position   = PositionType::Absolute;
            s.left       = 16.0f;
            s.right      = 172.0f;
            s.bottom     = 16.0f;
            s.height     = 190.0f;
            s.direction  = FlexDirection::Row;
            s.alignItems = Align::Start;
            s.padding    = Edges::All(12.0f);
            s.gap        = 20.0f;
            panel->SetLayoutStyle(s);
            panel->SetRounding(8.0f);
            panel->SetBorder(kSeparator, 1.0f);
            panel->SetClipChildren(true);
        }

        Node* sizes = panel->AddChild<Node>();
        {
            LayoutStyle s;
            s.gap    = 4.0f;
            s.shrink = 0.0f;
            sizes->SetLayoutStyle(s);
        }
        sizes->AddChild<Label>("12 号 · 保留模式界面 Retained UI", 12.0f);
        sizes->AddChild<Label>("14 号 · 保留模式界面 Retained UI", 14.0f);
        sizes->AddChild<Label>("18 号 · 中文输入 IME", 18.0f);
        sizes->AddChild<Label>("24 号 · 抗锯齿 Anti-aliasing", 24.0f);
        sizes->AddChild<Label>("32 号 · 你好 Hello", 32.0f, kAccent);

        Node* right = panel->AddChild<Node>();
        {
            LayoutStyle s;
            s.gap    = 8.0f;
            s.grow   = 1.0f;
            s.shrink = 1.0f;
            right->SetLayoutStyle(s);
        }
        right->AddChild<Label>("自动换行 / 省略号", 12.0f, kTextDim);

        Label* paragraph = right->AddChild<Label>(
            "MiniGUI 是为 MiniCAD 编写的保留模式界面库：界面是一棵长期存在的节点树，"
            "只在数据变化时重新布局和绘制。English words wrap at spaces, "
            "而中文可以在任意两个字之间换行。", 14.0f);
        paragraph->SetWrap(true);

        Label* path = right->AddChild<Label>(
            "单行超出宽度时显示省略号：D:\\Drawings\\建筑平面图_最终版_第三次修改稿_2026.mcad", 14.0f, kOrange);
        path->SetEllipsis(true);
    }

    void DemoRoot::AddSidebarItem()
    {
        // 圆角依次变化，展示不同圆角的按钮
        const float roundings[5] = { 0.0f, 3.0f, 6.0f, 10.0f, 16.0f };
        const size_t index = m_sideItems.size();

        Button* b = m_itemList->AddChild<Button>();
        b->SetOnClick([this, b] { SelectSidebarItem(b); });
        ButtonStyle style = b->GetStyle();
        style.rounding = roundings[index % 5];
        b->SetStyle(style);

        LayoutStyle s;
        s.direction  = FlexDirection::Row;
        s.alignItems = Align::Center;
        s.height     = 32.0f;
        s.shrink     = 0.0f;               // 项目多了也不压缩，超出部分由侧栏裁剪
        s.padding    = Edges::Make(8.0f, 0.0f, 8.0f, 0.0f);
        s.justify   = Justify::Start;
        s.gap        = 8.0f;
        b->SetLayoutStyle(s);

        IconNode* icon = b->AddChild<IconNode>(IconNode::Kind::Circle);
        icon->SetLayoutStyle([] { LayoutStyle is; is.width = 16.0f; is.height = 16.0f; is.shrink = 0.0f; return is; }());

        // 名称轮换：添加项目时会出现之前没用过的汉字，字形按需加入图集（状态栏可见字形数变化）
        static const char* kNames[] =
        {
            "图层管理", "块定义", "标注样式", "文字样式", "线型设置",
            "打印布局", "外部参照", "动态输入", "对象捕捉", "极轴追踪",
            "测量面积", "阵列复制", "镜像旋转", "属性匹配", "视口缩放",
        };
        char name[64];
        std::snprintf(name, sizeof(name), "%s %zu", kNames[index % std::size(kNames)], index + 1);
        Label* label = b->AddChild<Label>(name);
        label->SetEllipsis(true);
        label->EditLayoutStyle().shrink = 1.0f;

        m_sideItems.push_back(b);
        m_statusBar->SetCount(static_cast<int>(m_sideItems.size()));

        char text[64];
        std::snprintf(text, sizeof(text), "[点击] 添加侧栏项目，共 %zu 个", m_sideItems.size());
        Log(text);
    }

    void DemoRoot::RemoveSidebarItem()
    {
        if (m_sideItems.empty())
            return;

        Button* last = m_sideItems.back();
        m_sideItems.pop_back();
        m_itemList->RemoveChild(last);  // 返回的 unique_ptr 立即销毁
        m_statusBar->SetCount(static_cast<int>(m_sideItems.size()));

        char text[64];
        std::snprintf(text, sizeof(text), "[点击] 移除侧栏项目，剩 %zu 个", m_sideItems.size());
        Log(text);
    }

    void DemoRoot::SelectSidebarItem(Button* item)
    {
        int selected = -1;
        for (int i = 0; i < static_cast<int>(m_sideItems.size()); ++i)
        {
            m_sideItems[i]->SetChecked(m_sideItems[i] == item);
            if (m_sideItems[i] == item)
                selected = i;
        }

        char text[64];
        std::snprintf(text, sizeof(text), "[点击] 选中侧栏项目 %d", selected + 1);
        Log(text);
    }

    // =========================================================
    // PrimitivesView：M0 的图元展示，全部相对自身矩形绘制
    // =========================================================
    void PrimitivesView::OnPaint(DrawList& dl, const Rect& area)
    {
        // 1) 放射线：每 7.5° 一条，线宽 1 / 2.25 / 3.5 交替，检查斜线抗锯齿
        const Vec2 fanCenter{ area.min.x + 170.0f, area.min.y + 170.0f };
        for (int i = 0; i < 48; ++i)
        {
            const float a = i * kPi / 24.0f;
            const float thickness = 1.0f + (i % 3) * 1.25f;
            const Vec2  dir{ std::cos(a), std::sin(a) };
            dl.AddLine(fanCenter + dir * 30.0f, fanCenter + dir * 140.0f, kText, thickness);
        }
        dl.AddCircle(fanCenter, 150.0f, kAccent, 2.0f);

        // 2) 水平细线：线宽 0.5～4，y 落在像素中心（+0.5）时 1 像素线最清晰
        const float thickness[6] = { 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
        for (int i = 0; i < 6; ++i)
        {
            const float y = std::floor(area.min.y + 350.0f + i * 14.0f) + 0.5f;
            dl.AddLine({ area.min.x + 30.0f, y }, { area.min.x + 310.0f, y }, kTextDim, thickness[i]);
        }

        // 3) 实心圆与空心圆
        const float radii[5] = { 3.0f, 6.0f, 12.0f, 24.0f, 40.0f };
        float cx = area.min.x + 360.0f;
        for (float r : radii)
        {
            cx += r;
            dl.AddCircleFilled({ cx, area.min.y + 70.0f }, r, kAccent);
            dl.AddCircle({ cx, area.min.y + 170.0f }, r, kGreen, 1.5f);
            cx += r + 16.0f;
        }

        // 4) 圆角矩形描边：线宽 1 / 2.5 / 4
        for (int i = 0; i < 3; ++i)
        {
            const Rect r = Rect::FromXYWH(area.min.x + 360.0f + i * 110.0f, area.min.y + 240.0f, 90.0f, 60.0f);
            dl.AddRectFilled(r, ColorRef(ThemeColor::Accent, 40), 12.0f);
            dl.AddRect(r, kAccent, 12.0f, 1.0f + i * 1.5f);
        }

        // 5) 凸多边形：六边形（填充 + 描边）、三角形
        {
            Vec2 hex[6];
            const Vec2 c{ area.min.x + 410.0f, area.min.y + 380.0f };
            for (int i = 0; i < 6; ++i)
            {
                const float a = i * kPi / 3.0f + kPi / 6.0f;
                hex[i] = c + Vec2{ std::cos(a), std::sin(a) } * 44.0f;
            }
            dl.AddConvexPolyFilled(hex, kOrange);
            dl.AddPolyline(hex, kText, true, 2.0f);

            dl.AddTriangleFilled({ area.min.x + 490.0f, area.min.y + 420.0f },
                                 { area.min.x + 540.0f, area.min.y + 340.0f },
                                 { area.min.x + 590.0f, area.min.y + 420.0f }, kRed);
        }

        // 6) 裁剪矩形：大圆只露出框内的部分
        if (m_showClipDemo)
        {
            const Rect box{ area.max.x - 260.0f, area.min.y + 20.0f, area.max.x - 20.0f, area.min.y + 200.0f };
            dl.AddRectFilled(box, kPanel, 6.0f);
            dl.PushClipRect(box.Deflated(1.0f));
            dl.AddCircleFilled(box.max, 150.0f, kAccentSoft);
            dl.AddCircle(box.max, 120.0f, kGreen, 3.0f);
            dl.AddCircle(box.min, 60.0f, kOrange, 3.0f);
            dl.PopClipRect();
            dl.AddRect(box, kSeparator, 6.0f, 1.0f);
        }

        // 7) 贴图：棋盘格纹理（与纯色图元之间会切分命令）
        {
            const Rect img{ area.max.x - 260.0f, area.min.y + 220.0f, area.max.x - 20.0f, area.min.y + 380.0f };
            dl.AddImage(m_checker, img);
            dl.AddRect(img, kAccent, 0.0f, 2.0f);
        }
    }
}
