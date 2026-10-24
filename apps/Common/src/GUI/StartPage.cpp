// 起始页（docs/MiniGUI/UI-REFRESH-PLAN.md P2）。外观与尺寸照 SolidCraft 的起始页
// （D:\solidcraft\crates\ui-egui\src\home.rs、ui_kit/file_view.rs、assets/styles/ui/palettes.rs 的 file_* 令牌）：
// 占据标题栏以下的整个窗口——左侧蓝色导航列（返回当前图纸、开始、新建、打开、保存…，右侧是快捷键），
// 右侧是“新建”卡片、最近打开的图纸、示例节点图，卡片按宽度自动分列。
// 没有打开的文档时显示；有文档时经标题栏首页键（view.start）显示，Esc 或“返回”回到图纸。
// 界面描述文件里以面板 "start" 放在标题栏下面；显示时隐藏同一列里除标题栏以外的其他部分（ApplyStartPage）。
#include "GUI/MainFrame.h"
#include "GUI/NodeEditor.h"
#include "Core/UIContext.h"
#include "NodeGraph/Examples.h"
#include "Style/Theme.hpp"
#include "Text/TextSystem.h"
#include "Widgets/Button.h"
#include "Widgets/Label.h"
#include "Widgets/Panel.h"
#include "Widgets/ScrollView.h"
#include "Widgets/TitleBar.h"
#include "Widgets/ViewportHost.h"
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>

namespace MiniCAD
{
    namespace
    {
        namespace Theme = MiniGUI::Theme;
        using namespace MiniGUI;

        // SolidCraft 的 file_* 尺寸令牌
        constexpr float kNavWidth       = 248.0f;   // file_nav_width
        constexpr float kNavRowH        = 38.0f;    // file_nav_height
        constexpr float kBackRowH       = 40.0f;    // file_back_height
        constexpr float kTileH          = 84.0f;    // file_tile_height
        constexpr float kTileIcon       = 44.0f;    // file_tile_icon
        constexpr float kTileMinW       = 200.0f;   // SolidCraft 为 240（form_card_width - space_24）；中文说明短，放宽到四张一行
        constexpr float kRecentRowH     = 52.0f;    // file_recent_height
        constexpr float kSampleImageH   = 84.0f;    // file_sample_image_height
        constexpr float kSampleMinW     = 164.0f;   // file_nav_width - file_tile_height
        constexpr float kSampleCardH    = 170.0f;
        constexpr float kGridGap        = 14.0f;    // file_grid_gap
        constexpr float kInsetX         = 40.0f;    // file_content_inset_x
        constexpr float kInsetY         = 32.0f;    // file_content_inset_y
        constexpr float kSectionGap     = 28.0f;    // file_section_gap
        constexpr float kRadiusLg       = 8.0f;     // radius_lg（导航行、最近行）
        constexpr float kRadiusXl       = 10.0f;    // radius_xl（卡片、最近列表）

        std::string ParentDir(const std::string& utf8)
        {
            const std::filesystem::path p(std::u8string(utf8.begin(), utf8.end()));
            const std::u8string dir = p.parent_path().u8string();
            return std::string(dir.begin(), dir.end());
        }

        // 文件修改时间："2026-10-24 14:05"；文件不存在返回空
        std::string ModifiedText(const std::string& utf8)
        {
            std::error_code ec;
            const auto t = std::filesystem::last_write_time(std::filesystem::path(std::u8string(utf8.begin(), utf8.end())), ec);
            if (ec)
                return {};
            const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
            const std::time_t tt = std::chrono::system_clock::to_time_t(sys);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &tt);
#else
            localtime_r(&tt, &tm);
#endif
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
            return buf;
        }

        // 去掉菜单助记符："新建(&N)" → "新建"，"打开(&O)…" → "打开…"
        std::string PlainLabel(std::string s)
        {
            const size_t open = s.find("(&");
            if (open != std::string::npos)
            {
                const size_t close = s.find(')', open);
                if (close != std::string::npos)
                    s.erase(open, close - open + 1);
            }
            return s;
        }

        TextParams Text(float size, ColorRef color, TextAlign h = TextAlign::Start, TextAlign v = TextAlign::Center)
        {
            TextParams p;
            p.size     = size;
            p.color    = color;
            p.hAlign   = h;
            p.vAlign   = v;
            p.ellipsis = true;
            return p;
        }

        void DrawIcon(Node& node, DrawList& dl, const std::string& path, const Rect& r, bool enabled = true)
        {
            UIContext* ctx = node.GetContext();
            if (!ctx || path.empty())
                return;
            const TextureId tex = ctx->LoadTexture(path, r.Size());
            if (tex != InvalidTextureId)
                dl.AddImage(tex, r, { 0, 0 }, { 1, 1 }, enabled ? Colors::White : ColorRef(Color32(0x73FFFFFFu)));
        }

        // 自绘按钮的公共部分：点击、悬停、按下由 Button 处理，外观全部由子类画
        class PaintedButton : public Button
        {
        public:
            explicit PaintedButton(std::function<void()> onClick) : Button(std::move(onClick)) {}

        protected:
            bool Hot() const { return IsEnabled() && IsHovered(); }
        };

        // 导航列的一行：名称 + 右侧等宽快捷键；selected 为当前页
        class NavRow : public PaintedButton
        {
        public:
            NavRow(std::string text, std::string shortcut, bool selected, std::function<void()> onClick)
                : PaintedButton(std::move(onClick)), m_text(std::move(text)), m_shortcut(std::move(shortcut)), m_selected(selected)
            {
                SetFocusable(false);
                EditLayoutStyle().height = kNavRowH;
            }

        protected:
            void OnPaint(DrawList& dl, const Rect& r) override
            {
                if (m_selected || Hot())
                    dl.AddRectFilled(r, Theme::RailHover, kRadiusLg);
                UIContext* ctx = GetContext();
                if (!ctx)
                    return;
                const ColorRef ink = IsEnabled() ? Theme::RailInk : ColorRef(ThemeColor::RailInk, 128);
                const Rect inner{ r.min.x + 12.0f, r.min.y, r.max.x - 12.0f, r.max.y };
                const float keyW = m_shortcut.empty() ? 0.0f : ctx->GetTextSystem().Measure(m_shortcut, Text(11.0f, ink)).x;
                ctx->GetTextSystem().Draw(dl, Rect{ inner.min.x, inner.min.y, inner.max.x - keyW - 8.0f, inner.max.y }, m_text, Text(14.0f, ink));
                if (keyW > 0.0f)
                    ctx->GetTextSystem().Draw(dl, inner, m_shortcut, Text(11.0f, ink, TextAlign::End));
            }

        private:
            std::string m_text, m_shortcut;
            bool        m_selected;
        };

        // 导航列顶部的“‹ 返回 文档名 [Esc]”
        class BackRow : public PaintedButton
        {
        public:
            explicit BackRow(std::function<void()> onClick) : PaintedButton(std::move(onClick))
            {
                SetFocusable(false);
                LayoutStyle& s = EditLayoutStyle();
                s.height = kBackRowH;
                s.margin.bottom = 10.0f;
            }
            void SetDocument(std::string name) { if (name != m_doc) { m_doc = std::move(name); Invalidate(); } }

        protected:
            void OnPaint(DrawList& dl, const Rect& r) override
            {
                if (Hot())
                    dl.AddRectFilled(r, Theme::RailHover, kRadiusLg);
                UIContext* ctx = GetContext();
                if (!ctx)
                    return;
                const Rect inner{ r.min.x + 12.0f, r.min.y, r.max.x - 12.0f, r.max.y };
                // 左箭头
                const Vec2 c{ inner.min.x + 5.0f, inner.Center().y };
                dl.AddLine({ c.x + 3.0f, c.y - 5.0f }, { c.x - 2.0f, c.y }, Theme::RailInk, 1.6f);
                dl.AddLine({ c.x - 2.0f, c.y }, { c.x + 3.0f, c.y + 5.0f }, Theme::RailInk, 1.6f);
                // 右侧键帽
                const float kw = ctx->GetTextSystem().Measure("Esc", Text(11.0f, Theme::RailInk)).x + 10.0f;
                const Rect key{ inner.max.x - kw, inner.Center().y - 9.0f, inner.max.x, inner.Center().y + 9.0f };
                dl.AddRect(key, ColorRef(ThemeColor::RailInk, 128), Theme::RadiusSm, 1.0f);
                ctx->GetTextSystem().Draw(dl, key, "Esc", Text(11.0f, Theme::RailInk, TextAlign::Center));
                ctx->GetTextSystem().Draw(dl, Rect{ inner.min.x + 20.0f, inner.min.y, key.min.x - 8.0f, inner.max.y },
                                          "返回 " + m_doc, Text(13.5f, Theme::RailInk));
            }

        private:
            std::string m_doc;
        };

        // 操作卡片（新建 / 打开…）：左侧 44 的图标，右侧标题 + 两行以内的说明；primary 为强调样式
        class ActionTile : public PaintedButton
        {
        public:
            ActionTile(std::string icon, std::string title, std::string hint, bool primary, std::function<void()> onClick)
                : PaintedButton(std::move(onClick)), m_icon(std::move(icon)), m_title(std::move(title)), m_hint(std::move(hint)), m_primary(primary)
            {
                SetTooltip(m_title + "\n" + m_hint);
            }

        protected:
            void OnPaint(DrawList& dl, const Rect& r) override
            {
                PaintCard(dl, r);
                UIContext* ctx = GetContext();
                if (!ctx)
                    return;
                const Rect inner = r.Deflated(16.0f);
                const float is = std::min({ kTileIcon, inner.Height(), inner.Width() });
                const Rect icon = Rect::FromXYWH(inner.min.x, inner.Center().y - is * 0.5f, is, is);
                DrawIcon(*this, dl, m_icon, icon, IsEnabled());
                const Rect text{ std::min(icon.max.x + 14.0f, inner.max.x), inner.min.y, inner.max.x, inner.max.y };
                const ColorRef ink = m_primary ? Theme::CardPrimaryInk : Theme::Text;
                TextParams hp = Text(12.5f, Theme::TextDim, TextAlign::Start, TextAlign::Start);
                hp.wrap = true;
                const float titleH = 20.0f;
                const float hintH  = std::min(ctx->GetTextSystem().Measure(m_hint, hp, text.Width()).y, 34.0f);
                const float top    = text.Center().y - (titleH + 2.0f + hintH) * 0.5f;
                ctx->GetTextSystem().Draw(dl, Rect{ text.min.x, top, text.max.x, top + titleH }, m_title, Text(14.5f, ink));
                const Rect hintRect{ text.min.x, top + titleH + 2.0f, text.max.x, top + titleH + 2.0f + hintH };
                dl.PushClipRect(hintRect);          // 最多两行
                ctx->GetTextSystem().Draw(dl, hintRect, m_hint, hp);
                dl.PopClipRect();
            }

            void PaintCard(DrawList& dl, const Rect& r)
            {
                const bool hot = Hot();
                ColorRef fill   = m_primary ? Theme::CardPrimary : (hot ? Theme::Control : Theme::Panel);
                ColorRef border = m_primary ? Theme::CardPrimaryBorder : (hot ? Theme::BorderHover : Theme::BorderSubtle);
                if (IsPressedVisual())
                    fill = m_primary ? ColorRef(ThemeColor::CardPrimaryBorder, 90) : Theme::ControlHover;
                else if (m_primary && hot)
                    fill = ColorRef(ThemeColor::CardPrimaryBorder, 60);
                dl.AddRectFilled(r, fill, kRadiusXl);
                dl.AddRect(r, border, kRadiusXl, m_primary ? 1.5f : 1.0f);
            }

        private:
            std::string m_icon, m_title, m_hint;
            bool        m_primary;
        };

        // 最近打开的一行：文件图标、名称、所在目录（等宽小字）、修改时间；行间有分隔线
        class RecentRow : public PaintedButton
        {
        public:
            RecentRow(std::string icon, std::string title, std::string path, std::string when, bool first, std::function<void()> onClick)
                : PaintedButton(std::move(onClick)), m_icon(std::move(icon)), m_title(std::move(title)), m_path(std::move(path)),
                  m_when(std::move(when)), m_first(first)
            {
                EditLayoutStyle().height = kRecentRowH;
            }

        protected:
            void OnPaint(DrawList& dl, const Rect& r) override
            {
                if (!m_first)
                    dl.AddLine({ r.min.x, r.min.y }, { r.max.x, r.min.y }, Theme::BorderSubtle, 1.0f);
                if (Hot())
                    dl.AddRectFilled(r, Theme::RowHover, kRadiusLg);
                UIContext* ctx = GetContext();
                if (!ctx)
                    return;
                const Rect inner{ r.min.x + 14.0f, r.min.y + 8.0f, r.max.x - 14.0f, r.max.y - 8.0f };
                const Rect preview{ inner.min.x, inner.min.y, inner.min.x + 48.0f, inner.max.y };
                dl.AddRectFilled(preview, Theme::Background, Theme::RadiusMd);
                const float is = std::min(24.0f, preview.Height());
                DrawIcon(*this, dl, m_icon, Rect::FromXYWH(preview.Center().x - is * 0.5f, preview.Center().y - is * 0.5f, is, is));
                const float left = preview.max.x + 14.0f;
                const bool  wide = inner.max.x - left > 528.0f;
                const float whenW = wide && !m_when.empty() ? 140.0f : 0.0f;
                const Rect content{ left, inner.min.y, inner.max.x - whenW, inner.max.y };
                const float half = content.Center().y;
                ctx->GetTextSystem().Draw(dl, Rect{ content.min.x, content.min.y, content.max.x, half }, m_title, Text(13.5f, Theme::Text));
                ctx->GetTextSystem().Draw(dl, Rect{ content.min.x, half, content.max.x, content.max.y }, m_path, Text(12.0f, Theme::TextDim));
                if (whenW > 0.0f)
                    ctx->GetTextSystem().Draw(dl, Rect{ content.max.x, inner.min.y, inner.max.x, inner.max.y }, m_when, Text(12.0f, Theme::TextDim, TextAlign::End));
            }

        private:
            std::string m_icon, m_title, m_path, m_when;
            bool        m_first;
        };

        // 示例卡片：上方 84 高的图片区（没有缩略图时画图标），下面名称和两行说明
        class SampleCard : public ActionTile
        {
        public:
            SampleCard(std::string icon, std::string title, std::string description, std::function<void()> onClick)
                : ActionTile(icon, title, description, false, std::move(onClick)), m_icon(std::move(icon)), m_title(std::move(title)),
                  m_desc(std::move(description)) {}

        protected:
            void OnPaint(DrawList& dl, const Rect& r) override
            {
                PaintCard(dl, r);
                UIContext* ctx = GetContext();
                if (!ctx)
                    return;
                const Rect inner = r.Deflated(10.0f);
                const Rect image{ inner.min.x, inner.min.y, inner.max.x, inner.min.y + kSampleImageH };
                dl.AddRectFilled(image, Theme::Background, Theme::RadiusMd);
                dl.AddRect(image, Theme::BorderSubtle, Theme::RadiusMd, 1.0f);
                DrawIcon(*this, dl, m_icon, Rect::FromXYWH(image.Center().x - 22.0f, image.Center().y - 22.0f, 44.0f, 44.0f), IsEnabled());
                const ColorRef ink = IsEnabled() ? Theme::Text : Theme::TextDisabled;
                const float y = image.max.y + 10.0f;
                ctx->GetTextSystem().Draw(dl, Rect{ inner.min.x, y, inner.max.x, y + 18.0f }, m_title, Text(13.5f, ink));
                TextParams dp = Text(12.0f, Theme::TextDim, TextAlign::Start, TextAlign::Start);
                dp.wrap = true;
                const Rect descRect{ inner.min.x, y + 24.0f, inner.max.x, std::min(inner.max.y, y + 24.0f + 34.0f) };
                dl.PushClipRect(descRect);          // 最多两行
                ctx->GetTextSystem().Draw(dl, descRect, m_desc, dp);
                dl.PopClipRect();
            }

        private:
            std::string m_icon, m_title, m_desc;
        };

        // 卡片网格：每格不窄于 minWidth，列数随宽度变化，各列等宽；行高固定（SolidCraft FileView::grid）
        class TileGrid : public Node
        {
        public:
            TileGrid(float minWidth, float rowHeight) : m_minWidth(minWidth), m_rowHeight(rowHeight) {}

        protected:
            int Columns(float width) const
            {
                const int count = static_cast<int>(GetChildren().size());
                if (count == 0)
                    return 1;
                if (!std::isfinite(width) || width <= 0.0f)
                    return std::min(count, 3);
                const int fit = std::max(1, static_cast<int>(std::floor((width + kGridGap) / (m_minWidth + kGridGap))));
                return std::min(fit, count);
            }

            Vec2 MeasureContent(Vec2 available) override
            {
                const int count = static_cast<int>(GetChildren().size());
                const int cols  = Columns(available.x);
                const int rows  = (count + cols - 1) / cols;
                const float w = std::isfinite(available.x) ? available.x : cols * (m_minWidth + kGridGap) - kGridGap;
                return { w, rows > 0 ? rows * m_rowHeight + (rows - 1) * kGridGap : 0.0f };
            }

            void OnLayout() override
            {
                const float width = GetSize().x;
                const int   cols  = Columns(width);
                const float cellW = (width - (cols - 1) * kGridGap) / static_cast<float>(cols);
                int i = 0;
                for (const auto& child : GetChildren())
                {
                    const int row = i / cols, col = i % cols;
                    child->SetBounds(Rect::FromXYWH(col * (cellW + kGridGap), row * (m_rowHeight + kGridGap), cellW, m_rowHeight));
                    ++i;
                }
            }

        private:
            float m_minWidth, m_rowHeight;
        };

        // 起始页外层：画底色，接住从子节点冒泡上来的 Esc（有文档时回到图纸）
        class StartPageView : public Node
        {
        public:
            explicit StartPageView(std::function<void()> onEscape) : m_onEscape(std::move(onEscape)) { SetFocusable(true); }

            void OnKeyEvent(KeyEvent& e) override
            {
                if (!e.handled && e.type == KeyEventType::Down && e.key == Key::Escape && e.phase != EventPhase::Capture && m_onEscape)
                {
                    e.handled = true;
                    m_onEscape();
                }
            }

            void OnPaint(DrawList& dl, const Rect& r) override { dl.AddRectFilled(r, Theme::Background); }

        private:
            std::function<void()> m_onEscape;
        };

        void AddNavSeparator(Node* parent)
        {
            auto* sep = parent->AddChild<Panel>(ColorRef(ThemeColor::RailInk, 64));
            LayoutStyle& ls = sep->EditLayoutStyle();
            ls.height = 1.0f;
            ls.margin = Edges::Symmetric(12.0f, 10.0f);
        }

        void AddSectionTitle(Node* parent, const std::string& text)
        {
            auto* l = parent->AddChild<Label>(text, 16.0f, Theme::Text);
            l->EditLayoutStyle().margin = Edges{ 0.0f, kSectionGap, 0.0f, 12.0f };
        }
    }

    std::unique_ptr<MiniGUI::Node> MainFrame::CreateStartPage()
    {
        const std::string icons = ResourceDir() + "/icons/";

        auto view = std::make_unique<StartPageView>([this] { if (m_startShown) ShowStartPage(false); });
        {
            LayoutStyle& vs = view->EditLayoutStyle();
            vs.direction = FlexDirection::Row;
            vs.alignItems = Align::Stretch;
            vs.grow      = 1.0f;
        }

        // ── 左侧导航列 ─────────────────────────────────────────
        auto* nav = view->AddChild<Panel>(Theme::Rail);
        {
            LayoutStyle& ns = nav->EditLayoutStyle();
            ns.direction = FlexDirection::Column;
            ns.width     = kNavWidth;
            ns.shrink    = 0.0f;
            ns.padding   = Edges::Symmetric(10.0f, 14.0f);
            ns.gap       = 2.0f;
        }
        m_startNav.clear();
        m_startBack = nav->AddChild<BackRow>([this] { ShowStartPage(false); });
        nav->AddChild<NavRow>("开始", "", true, std::function<void()>{});
        auto navCommand = [&](const char* id, const char* text = nullptr)
        {
            const MiniGUI::Command* c = m_commands.Find(id);
            if (!c)
                return;
            const std::string cmd = id;
            Button* b = nav->AddChild<NavRow>(text ? std::string(text) : PlainLabel(c->label), c->shortcut, false,
                                              [this, cmd] { m_commands.Execute(cmd); });
            m_startNav.emplace_back(b, cmd);
        };
        navCommand("file.new");
        navCommand("file.open");
        AddNavSeparator(nav);
        navCommand("file.save");
        navCommand("file.saveAs");
        navCommand("file.saveAll");
        navCommand("file.close", "关闭图纸");
        AddNavSeparator(nav);
        navCommand("view.panel.nodes");
        navCommand("help.about", "关于 MiniCAD");

        // ── 右侧内容（可滚动）────────────────────────────────────
        auto* scroll = view->AddChild<ScrollView>();
        scroll->SetScrollAxes(false, true);
        scroll->EditLayoutStyle().grow = 1.0f;
        auto* col = scroll->SetContent<Node>();
        {
            LayoutStyle& cs = col->EditLayoutStyle();
            cs.direction = FlexDirection::Column;
            cs.padding   = Edges::Symmetric(kInsetX, kInsetY);
        }
        col->AddChild<Label>("开始", 28.0f, Theme::Text);
        auto* hint = col->AddChild<Label>("新建图纸、打开最近的图纸，或从示例开始。", 14.0f, Theme::TextDim);
        hint->EditLayoutStyle().margin.top = 6.0f;

        AddSectionTitle(col, "新建");
        auto* tiles = col->AddChild<TileGrid>(kTileMinW, kTileH);
        tiles->AddChild<ActionTile>(icons + "New.png", "新建图纸", "空白二维图纸 · 毫米 · DWG 2004", true,
                                    [this] { m_commands.Execute("file.new"); });
        tiles->AddChild<ActionTile>(icons + "Box3D.png", "新建三维模型", "西南等轴测、着色显示", false, [this]
        {
            m_commands.Execute("file.new");
            m_commands.Execute("view.swiso");
            m_commands.Execute("view.style.shaded");
        });
        tiles->AddChild<ActionTile>(icons + "Open.png", "打开…", "DWG、DXF 或 MCAD 图纸（Ctrl+O）", false,
                                    [this] { m_commands.Execute("file.open"); });
        tiles->AddChild<ActionTile>(icons + "Nodes.png", "节点建模", "新建图纸并打开节点编辑器", false, [this]
        {
            m_commands.Execute("file.new");
            ShowNodePanel(true);
        });

        AddSectionTitle(col, "最近");
        m_startRecent = col->AddChild<Node>();

        // 示例：随程序发布的节点图（assets/nodes/examples，顺序与名称来自 examples.json）。
        // 点击后新建图纸、打开节点编辑器并插入示例；没有实体内核时需要内核的示例禁用
        const auto examples = Nodes::ReadExampleIndex(ResourceDir() + "/nodes/examples");
        if (!examples.empty())
        {
            AddSectionTitle(col, "示例");
            const bool kernel = m_platform->GetSolidKernel() != nullptr;
            auto* grid = col->AddChild<TileGrid>(kSampleMinW, kSampleCardH);
            for (const Nodes::ExampleFile& e : examples)
            {
                const bool usable = kernel || !e.NeedsKernel;
                const std::string icon = icons + (e.NeedsKernel ? "Box3D.png" : "Nodes.png");
                auto* card = grid->AddChild<SampleCard>(icon, e.Name, usable ? e.Description : "需要三维实体内核",
                                                        [this, e] { OpenNodeExample(e.Path, e.Name, e.Description); });
                card->SetEnabled(usable);
            }
        }

        m_startPage = view.get();
        RefreshStartPage();
        return view;
    }

    void MainFrame::RefreshStartPage()
    {
        if (!m_startRecent)
            return;
        while (!m_startRecent->GetChildren().empty())
            m_startRecent->RemoveChild(m_startRecent->GetChildren().back().get());

        const auto& list = m_recentFiles.Items();
        if (list.empty())
        {
            m_startRecent->SetLayoutStyle(LayoutStyle{});
            m_startRecent->AddChild<Label>("打开或保存过的图纸会显示在这里。", 13.0f, Theme::TextDim);
            m_startRecent->Invalidate();
            return;
        }
        // 圆角卡片里一行一个（SolidCraft FileView::recent_list）
        auto* list_ = m_startRecent->AddChild<Panel>(Theme::Panel);
        list_->SetRounding(kRadiusXl);
        list_->SetBorder(Theme::BorderSubtle);
        const std::string icon = ResourceDir() + "/icons/Drawing.png";
        for (size_t i = 0; i < list.size(); ++i)
        {
            const std::string path = list[i];
            auto* row = list_->AddChild<RecentRow>(icon, m_recentFiles.DisplayName(i), ParentDir(path), ModifiedText(path), i == 0,
                                                   [this, path] { OpenDrawings({ path }); });
            row->SetTooltip(path);
        }
        m_startRecent->Invalidate();
    }

    // 导航列的可用状态与“返回”行随当前文档变化（SyncDocuments 调用）
    void MainFrame::RefreshStartNav()
    {
        for (auto& [button, cmd] : m_startNav)
            button->SetEnabled(m_commands.IsEnabled(cmd));
        if (m_startBack)
        {
            const Document* doc = m_docManager.GetActive();
            m_startBack->SetVisible(doc != nullptr);
            if (doc)
                static_cast<BackRow*>(m_startBack)->SetDocument(doc->GetName());
        }
    }

    // 起始页与其余界面互斥：显示起始页时隐藏同一列里除标题栏以外的兄弟节点（工具栏、停靠区、状态栏），
    // 记下被隐藏的，收起时只恢复这些（界面描述文件里本来就隐藏的不动）
    void MainFrame::ApplyStartPage(bool show)
    {
        if (!m_startPage)
            return;
        MiniGUI::Node* parent = m_startPage->GetParent();
        if (!parent)
        {
            m_startHidden.clear();          // 起始页不在布局里（界面描述文件没有放 "start"）
            return;
        }
        if (show)
        {
            for (const auto& child : parent->GetChildren())
            {
                MiniGUI::Node* n = child.get();
                if (n == m_startPage || dynamic_cast<MiniGUI::TitleBar*>(n) || !n->IsVisible())
                    continue;
                n->SetVisible(false);
                m_startHidden.push_back(n);
            }
        }
        else
        {
            for (MiniGUI::Node* n : m_startHidden)
                n->SetVisible(true);
            m_startHidden.clear();
        }
        m_startPage->SetVisible(show);
    }

    void MainFrame::ShowStartPage(bool show)
    {
        if (show && !m_docManager.GetActive())
            show = false;                   // 没有文档时起始页本来就在显示
        m_startShown = show;
        m_startOver  = show ? m_docManager.GetActive() : nullptr;
        SyncDocuments();
        if (show)
            m_startPage->Focus();
        else if (m_viewport->IsVisible())
            m_viewport->Focus();
        m_platform->RequestRedraw();
    }

    void MainFrame::OpenNodeExample(const std::string& path, const std::string& name, const std::string& description)
    {
        const std::string text = Nodes::ReadTextFile(path);
        m_commands.Execute("file.new");
        ShowNodePanel(true);
        if (!m_nodeEditor)
            return;
        m_ui->Update();                     // 节点面板刚显示：先布局，插入的示例才能按画布尺寸摆放
        m_nodeEditor->Focus();
        const bool ok = !text.empty() && m_nodeEditor->InsertGraph(text);
        m_docManager.GetEditor().GetCmdLine().Echo(ok ? "示例：" + name + "——" + description : "示例：读不出 " + path);
        if (ok)
            m_nodeEditor->FrameAll();
    }
}
