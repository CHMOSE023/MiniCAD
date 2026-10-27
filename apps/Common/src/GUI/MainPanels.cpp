// ── 宿主面板（文档区、特性、状态栏）与界面描述文件、用户布局的加载 ─────────
#include "GUI/MainFrame.h"
#include "GUI/FloatingPanelHost.h"
#include "GUI/StatusBarView.h"
#include "GUI/Lineweights.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Log.h"
#include "Core/UIContext.h"
#include "Data/Json.h"
#include "Document/Document.h"
#include "Document/Command/LayoutCommands.h"
#include "Style/Theme.hpp"
#include "Widgets/Button.h"
#include "Widgets/CommandConsole.h"
#include "Widgets/ComboBox.h"
#include "Widgets/Controls.h"
#include "Widgets/DockSpace.h"
#include "Widgets/Label.h"
#include "Widgets/Menu.h"
#include "Widgets/CommandUI.h"
#include "Widgets/NumberBox.h"
#include "Widgets/Panel.h"
#include "Widgets/PropertyGrid.h"
#include "Widgets/TextBox.h"
#include "Widgets/TabView.h"
#include "Widgets/Ribbon.h"
#include "Widgets/TitleBar.h"
#include "Widgets/UiLayout.h"
#include "Widgets/ViewportHost.h"
#include "GUI/ViewCube.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>

namespace MiniCAD
{
    namespace
    {
        namespace Theme = MiniGUI::Theme;

        // 在自己或任何子节点上按下时先回调（捕获阶段，早于按钮的点击）：主窗口的视图方块、导航条、命令行、状态栏
        // 据此让主视口里的文档成为当前文档，按钮的命令随后就作用于它（文档窗口由 FloatingPanel::SetOnActivate 做同样的事）
        template <class Base>
        class PressHook : public Base
        {
        public:
            template <class... Args>
            explicit PressHook(std::function<void()> onPress, Args&&... args)
                : Base(std::forward<Args>(args)...), m_onPress(std::move(onPress))
            {
            }

        protected:
            void OnPointerEvent(MiniGUI::PointerEvent& e) override
            {
                if (e.phase == MiniGUI::EventPhase::Capture && e.type == MiniGUI::PointerEventType::Down && m_onPress)
                    m_onPress();
                Base::OnPointerEvent(e);
            }

        private:
            std::function<void()> m_onPress;
        };

        std::filesystem::path ToPath(const std::string& utf8)
        {
            return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
        }

        int64_t WriteTime(const std::string& utf8Path)
        {
            std::error_code ec;
            const auto t = std::filesystem::last_write_time(ToPath(utf8Path), ec);
            return ec ? 0 : static_cast<int64_t>(t.time_since_epoch().count());
        }

        // 用户布局文件：平台的用户数据目录下的 layout.json（Win32：%LOCALAPPDATA%\MiniCAD）；没有该目录时不保存
        std::filesystem::path UserLayoutPath(const AppPlatform& platform)
        {
            const std::string dir = platform.GetUserDataDir();
            return dir.empty() ? std::filesystem::path() : ToPath(dir) / "layout.json";
        }

        // 第一次加载就失败时使用的最小布局：至少能看到绘图区和状态栏里的错误信息（标题栏保证窗口能拖动、关闭）
        constexpr const char* kFallbackLayout = R"json({
            "layout": { "type": "column", "children": [
                { "type": "titlebar" },
                { "type": "panel", "name": "start", "grow": 1 },
                { "type": "column", "grow": 1, "children": [
                    { "type": "panel", "name": "canvas", "position": "absolute", "left": 0, "top": 0, "right": 0, "bottom": 0 },
                    { "type": "panel", "name": "documents", "grow": 1, "passThrough": true } ] },
                { "type": "panel", "name": "statusbar" } ] }
        })json";

        // 用户布局文件的格式版本（见 LoadUserLayout）
        constexpr int kUserLayoutVersion = 2;

        int LineweightIndex(Lineweight lw)
        {
            for (int i = 0; i < static_cast<int>(std::size(kLineweightItems)); ++i)
            {
                if (kLineweightItems[i].lw == lw)
                    return i;
            }
            return -1;
        }

        // 颜色下拉项：随层、随块、ACI 1..7，最后一项"其他"表示别的 ACI / 真彩（只用于显示，选中它不做修改）
        struct ColorItem { const char* label; EntityColor color; };
        const std::vector<ColorItem>& ColorItems()
        {
            static const std::vector<ColorItem> items =
            {
                { "ByLayer", EntityColor::ByLayer() }, { "ByBlock", EntityColor::ByBlock() },
                { "红", EntityColor::FromAci(1) }, { "黄", EntityColor::FromAci(2) }, { "绿", EntityColor::FromAci(3) },
                { "青", EntityColor::FromAci(4) }, { "蓝", EntityColor::FromAci(5) }, { "品红", EntityColor::FromAci(6) },
                { "白", EntityColor::FromAci(7) },
            };
            return items;
        }

        int ColorIndex(const EntityColor& c)
        {
            const auto& items = ColorItems();
            for (int i = 0; i < static_cast<int>(items.size()); ++i)
            {
                const EntityColor& it = items[static_cast<size_t>(i)].color;
                if (it.Method == c.Method && (c.Method != ColorMethod::ByAci || it.Aci == c.Aci))
                    return i;
            }
            return static_cast<int>(items.size());      // "其他"
        }

        std::vector<std::string> ColorLabels()
        {
            std::vector<std::string> labels;
            for (const auto& it : ColorItems())
                labels.emplace_back(it.label);
            labels.emplace_back("其他");
            return labels;
        }

        std::vector<std::string> LineweightLabels()
        {
            std::vector<std::string> labels;
            for (const auto& it : kLineweightItems)
                labels.emplace_back(it.label);
            return labels;
        }
    }

    const MainFrame::SelectionSummary& MainFrame::SelectionInfo()
    {
        Document* doc    = m_docManager.GetActive();
        Editor&   editor = m_docManager.GetEditor();
        const uint64_t sv = editor.GetSelectionVersion();
        const uint64_t gv = doc ? doc->GetScene().GeometryVersion() : 0;
        const uint64_t dv = doc ? doc->GetScene().DisplayVersion()  : 0;
        SelectionSummary& s = m_selectionSummary;
        if (s.valid && s.doc == doc && s.selVersion == sv && s.sceneVersion == gv && s.displayVersion == dv)
            return s;

        s = SelectionSummary{};
        s.valid = true;
        s.doc = doc;
        s.selVersion = sv;
        s.sceneVersion = gv;
        s.displayVersion = dv;
        s.layer = s.color = s.lineType = s.lineweight = -1;
        s.lineTypeScale = 1.0;
        if (!doc)
            return s;

        const auto& ids = SortedLayerIds(*doc);
        auto layerIndex = [&ids](uint32_t id)
        {
            const auto it = std::find(ids.begin(), ids.end(), id);
            return it == ids.end() ? -1 : static_cast<int>(it - ids.begin());
        };
        std::vector<const Entity*> entities;
        bool first = true;
        auto merge = [&first](std::optional<int>& slot, int v)
        {
            if (first) slot = v;
            else if (slot && *slot != v) slot.reset();
        };
        for (Object* obj : editor.GetSelectedObjects())
        {
            const auto* e = dynamic_cast<const Entity*>(obj);
            if (!e)
                continue;
            entities.push_back(e);
            const EntityAttr& a = e->GetAttr();
            merge(s.layer,      layerIndex(a.LayerId));
            merge(s.color,      ColorIndex(a.Color));
            merge(s.lineType,   static_cast<int>(a.LineType));
            merge(s.lineweight, LineweightIndex(a.Lineweight));
            if (first)
                s.lineTypeScale = a.LinetypeScale;
            else if (s.lineTypeScale && *s.lineTypeScale != a.LinetypeScale)
                s.lineTypeScale.reset();
            first = false;
        }
        s.count      = entities.size();
        s.properties = CommonProperties(entities);
        return s;
    }

    // =========================================================
    // 布局页签条：模型 / 各张图纸空间布局
    // =========================================================
    // 由 CreateStatusBar 放在浮动状态栏的左侧（照 SolidCraft：状态栏是一张圆角卡片）。
    // 宽度取各标签的理想宽度之和，标签多了才缩窄
    std::unique_ptr<MiniGUI::Node> MainFrame::CreateLayoutTabs(ViewChrome& c)
    {
        using namespace MiniGUI;
        auto bar = std::make_unique<Node>();
        LayoutStyle bs;
        bs.direction = FlexDirection::Row;
        bs.height    = TabView::kStripHeight;
        bs.shrink    = 1.0f;
        bs.minWidth  = 120.0f;
        bar->SetLayoutStyle(bs);

        ViewChrome* pc = &c;
        c.layoutTabs = bar->AddChild<TabView>();
        c.layoutTabs->SetBarStyle(true);
        c.layoutTabs->SetFitToTabs(true);
        c.layoutTabs->EditLayoutStyle().shrink   = 1.0f;
        c.layoutTabs->EditLayoutStyle().minWidth = 0.0f;
        c.layoutTabs->SetFocusable(false);            // 点完标签键盘仍回绘图区
        c.layoutTabs->SetShowNewTabButton(true);      // + 新建布局
        c.layoutTabs->SetOnNewTabRequested([this, pc] { UseChrome(*pc); m_commands.Execute("layout.new"); });
        c.layoutTabs->SetOnSelectionChanged([this, pc](int i)
        {
            if (pc->syncingLayouts || i < 0)
                return;
            UseChrome(*pc);
            ActivateLayout(i);
            if (pc->host && pc->host->IsVisible())
                pc->host->Focus();
        });
        // 右键：新建 / 删除布局（布局标签不能关闭，不要默认的「关闭…」几项）
        c.layoutTabs->SetTabContextMenu([this, pc](int i)
        {
            const Document* doc = DocOf(*pc);
            bool canDelete = false;
            if (doc && i >= 0 && i < static_cast<int>(doc->GetScene().Layouts().size()))
                canDelete = !doc->GetScene().Layouts()[static_cast<size_t>(i)].Model &&
                            RemoveLayoutCommand::PaperCount(doc->GetScene()) > 1;
            return std::vector<MenuItem>{
                MenuItem("新建布局(&N)", [this, pc] { UseChrome(*pc); m_commands.Execute("layout.new"); }),
                MenuItem("删除布局(&D)", [this, pc, i] { UseChrome(*pc); DeleteLayout(i); }).Enabled(canDelete),
            };
        });

        // 没有文档时整条隐藏，打开 / 新建文档后由 SyncLayoutTabs 显示
        bar->SetVisible(false);
        c.layoutTabsBar = bar.get();
        return bar;
    }

    // =========================================================
    // 文档标签条（面板 "doctabs"）：界面描述文件把它放在标题栏里（SolidCraft 式），
    // 也可以放在别处；切换标签即切换文档，起始页显示时不选中任何标签
    // =========================================================
    std::unique_ptr<MiniGUI::Node> MainFrame::CreateDocTabs()
    {
        using namespace MiniGUI;
        auto tabs = std::make_unique<TabView>();
        m_docTabs = tabs.get();
        m_docTabs->EditLayoutStyle().height = TabView::kStripHeight;
        // 宽度按全部标签需要的宽度测量：标题栏有空位时标签名完整显示（不与右边的空白平分），
        // 实在放不下时再缩窄、出现溢出按钮
        m_docTabs->SetFitToTabs(true);
        m_docTabs->EditLayoutStyle().shrink   = 1.0f;
        m_docTabs->EditLayoutStyle().minWidth = 120.0f;
        m_docTabs->SetFocusable(false);             // 点击标签后键盘仍在绘图区
        m_docTabs->SetShowNewTabButton(true);
        m_docTabs->SetBarStyle(true);
        m_docTabs->SetOnSelectionChanged([this](int i)
        {
            if (m_syncing || i < 0)
                return;
            ActivateDocument(reinterpret_cast<Document*>(m_docTabs->GetTabData(i)));
            if (m_startShown)
                ShowStartPage(false);               // 起始页上点当前文档的标签也回到图纸
            m_viewport->Focus();
        });
        m_docTabs->SetOnCloseRequested([this](int i) { CloseDocument(reinterpret_cast<Document*>(m_docTabs->GetTabData(i))); });
        m_docTabs->SetOnNewTabRequested([this] { m_commands.Execute("file.new"); });
        // 标签一拖到标签条之外：文档立即移到独立的系统窗口，继续拖时窗口跟着光标走，松开在标签条上则合并回来
        //（平台给不出窗口时标签留在条里）
        m_docTabs->SetOnTabDragOut([this](int i, Vec2 windowPos)
        {
            Document* doc = reinterpret_cast<Document*>(m_docTabs->GetTabData(i));
            if (!FloatDocument(doc, windowPos, false))
                return false;
            m_tornDoc = doc;
            return true;
        });
        m_docTabs->SetOnTornTabDrag([this](TabView::TornDragPhase phase, Vec2 windowPos)
        {
            using Phase = FloatingPanel::DragPhase;
            OnDocTabTornDrag(phase == TabView::TornDragPhase::Move ? Phase::Update
                           : phase == TabView::TornDragPhase::End  ? Phase::End : Phase::Cancel, windowPos);
        });

        m_docTabs->SetTabContextMenuExtra([this](int i)
        {
            Document* doc = reinterpret_cast<Document*>(m_docTabs->GetTabData(i));
            return std::vector<MenuItem>{
                MenuItem("保存(&S)",    [this, doc] { ActivateDocument(doc); m_commands.Execute("file.save"); }),
                MenuItem("另存为(&A)…", [this, doc] { ActivateDocument(doc); m_commands.Execute("file.saveAs"); }),
                MenuItem("在独立窗口中打开(&W)", [this, doc]
                {
                    const Rect r = m_docTabs->GetScreenBounds();
                    FloatDocument(doc, Vec2{ r.min.x + 120.0f, r.max.y + 80.0f });
                }),
            };
        });

        return tabs;
    }

    // =========================================================
    // 绘图区（面板 "canvas"）：所有文档共用一个视口，切换标签时换相机状态。
    // 界面描述文件把它铺满标题栏以下的整个窗口（照 SolidCraft），功能区、停靠面板、状态栏都浮在它上面
    // =========================================================
    std::unique_ptr<MiniGUI::Node> MainFrame::CreateCanvas()
    {
        using namespace MiniGUI;
        auto vp = std::make_unique<ViewportHost>();
        vp->SetCursor(CursorShape::Hidden);       // 视口自己绘制十字光标
        vp->SetOnRender ([this](int w, int h) { RenderViewport(w, h); });
        vp->SetOnPointer([this](const ViewportPointerEvent& e) { OnViewportPointer(e); });
        // 在主视口里敲键：主视口里的文档成为当前文档（字母进的是主窗口的命令行）
        vp->SetOnKey    ([this](const KeyEvent& e) { ActivateDocumentOf(m_docManager.GetMainDocument()); return OnViewportKey(e); });
        m_viewport = vp.get();
        m_main.host = m_viewport;
        return vp;
    }

    // =========================================================
    // 文档区（主窗口的面板 "documents"）：停靠区里没有标题栏的组。浮动停靠区里它是透明、点击穿透的“洞”，
    // 范围就是没被功能区和面板挡住的绘图区：右上角视图方块、下面竖排的导航条、底部居中的命令行都放在这里，
    // “全部显示”也按它留边（UpdateFitInsets）。文档窗口里同样建一个，铺满它的视口
    // =========================================================
    std::unique_ptr<MiniGUI::Node> MainFrame::CreateDocumentArea(ViewChrome& c)
    {
        using namespace MiniGUI;
        ViewChrome* pc = &c;
        // 主窗口：在这里按下（视图方块、导航条、命令行）先让主视口里的文档成为当前文档
        std::unique_ptr<Node> area = c.main ? std::make_unique<PressHook<Node>>([this, pc] { UseChrome(*pc); })
                                            : std::make_unique<Node>();
        area->SetHitTestVisible(false);
        c.area = area.get();

        // 视图方块（M6）：右上角，跟着这个窗口里的文档
        ViewCube::Callbacks cubeCallbacks;
        cubeCallbacks.camera = [this, pc]() -> const Camera*
        {
            Viewport* vp = m_docManager.ViewportOf(DocOf(*pc));
            return vp ? &vp->GetCamera() : nullptr;
        };
        cubeCallbacks.setView = [this, pc](double az, double el)
        {
            Document* doc = DocOf(*pc);
            if (!doc)
                return;
            UseChrome(*pc);
            m_docManager.GetEditor(*doc).SetViewDirection(az, el);
            RequestRenderAll();
            StateChanged();
        };
        cubeCallbacks.orbit = [this, pc](double dx, double dy)
        {
            if (Viewport* vp = m_docManager.ViewportOf(DocOf(*pc)))
                vp->GetCamera().Orbit(dx, dy);
            RequestRenderAll();
        };
        auto* cube = area->AddChild<ViewCube>(std::move(cubeCallbacks));
        LayoutStyle cs;
        cs.position = PositionType::Absolute;
        cs.right    = 6.0f;
        cs.top      = 6.0f;
        cs.width    = 140.0f;
        cs.height   = 140.0f;
        cube->SetLayoutStyle(cs);
        cube->SetVisible(m_viewCubeVisible);
        // 右键：常用视图与隐藏（隐藏后经“视图 > 显示视图方块”或命令 NAVVCUBE 再显示）
        cube->SetContextMenuHandler([this, cube](Vec2 pos)
        {
            std::vector<MenuItem> items = {
                CommandMenuItem(m_commands, "view.top"),   CommandMenuItem(m_commands, "view.front"),
                CommandMenuItem(m_commands, "view.right"), CommandMenuItem(m_commands, "view.swiso"),
                MenuItem::Separator(),
                MenuItem("隐藏视图方块(&H)", [this] { SetViewCubeVisible(false); }),
            };
            auto popup = std::make_unique<MenuPopup>(std::move(items));
            popup->SetAnchor(Rect{ pos.x, pos.y, pos.x, pos.y }, PopupPlacement::Below);
            popup->SetOwner(cube);
            (cube->GetContext() ? *cube->GetContext() : *m_ui).OpenPopup(std::move(popup));     // 开在方块所在的窗口里
            return true;
        });
        c.cube = cube;

        // 导航条：竖排在视图方块下面，与方块的中线对齐
        area->AddChild(CreateViewNavBar(c));

        // 命令行：底部居中的浮动卡片（窄窗口时缩窄）
        {
            auto* row = area->AddChild<Node>();
            LayoutStyle rs;
            rs.position  = PositionType::Absolute;
            rs.left      = 0.0f;
            rs.right     = 0.0f;
            rs.bottom    = 0.0f;
            rs.direction = FlexDirection::Row;
            rs.justify   = Justify::Center;
            rs.padding   = Edges::Make(8.0f, 0.0f, 8.0f, 0.0f);
            row->SetLayoutStyle(rs);
            row->SetHitTestVisible(false);

            auto* card = row->AddChild<Panel>(Theme::Panel);
            card->SetRounding(Theme::RadiusLg);
            card->SetBorder(Theme::BorderSubtle);
            LayoutStyle ks;
            ks.direction = FlexDirection::Column;
            ks.width     = 760.0f;
            ks.shrink    = 1.0f;
            ks.minWidth  = 260.0f;
            ks.padding   = Edges::All(4.0f);
            card->SetLayoutStyle(ks);
            std::unique_ptr<Node> console = CreateCommandLine(c);
            console->EditLayoutStyle().grow = 1.0f;
            card->AddChild(std::move(console));
            // 卡片高度随内容：平时只有一行输入，展开历史时回显区向上长出
            c.console->GetLogView()->EditLayoutStyle().height = 96.0f;
            c.console->SetLogExpanded(false);
            card->SetVisible(m_commandLineVisible);
            c.cmdCard = card;
        }
        return area;
    }

    void MainFrame::SetCommandLineVisible(bool visible)
    {
        m_commandLineVisible = visible;
        ForEachChrome([visible](ViewChrome& c)
        {
            if (c.cmdCard)
                c.cmdCard->SetVisible(visible);
        });
        SaveUserLayout();
    }

    bool MainFrame::IsCommandLineShown(const ViewChrome& c) const
    {
        if (!c.cmdCard || !c.cmdCard->GetContext())
            return false;           // 不在界面树上（界面描述里没有文档区）
        for (const MiniGUI::Node* n = c.cmdCard; n; n = n->GetParent())
            if (!n->IsVisible())
                return false;
        return true;
    }

    // “全部显示”留边：绘图区里被功能区、面板、状态栏、命令行挡住的部分不算可见范围。pixelScale 为那个窗口的物理像素 / 逻辑像素
    void MainFrame::UpdateFitInsets(const ViewChrome& c, Camera& cam, float pixelScale)
    {
        if (!c.host || !c.area || !c.area->GetContext() || !c.area->IsVisible())
        {
            cam.SetFitInsets(0.0, 0.0, 0.0, 0.0);
            return;
        }
        const MiniGUI::Rect vp   = c.host->GetScreenBounds();
        MiniGUI::Rect       free = c.area->GetScreenBounds();
        if (IsCommandLineShown(c))
            free.max.y = std::min(free.max.y, c.cmdCard->GetScreenBounds().min.y);
        if (free.Width() < 50.0f || free.Height() < 50.0f || vp.Width() < 1.0f)
        {
            cam.SetFitInsets(0.0, 0.0, 0.0, 0.0);
            return;
        }
        const double s = pixelScale;
        cam.SetFitInsets(std::max(0.0f, free.min.x - vp.min.x) * s, std::max(0.0f, free.min.y - vp.min.y) * s,
                         std::max(0.0f, vp.max.x - free.max.x) * s, std::max(0.0f, vp.max.y - free.max.y) * s);
    }

    // =========================================================
    // 特性面板：控件通过 BindingSet 绑定到 MiniCAD 的数据
    // =========================================================
    std::unique_ptr<MiniGUI::Node> MainFrame::CreatePropertiesPanel()
    {
        using namespace MiniGUI;
        DocumentManager& dm = m_docManager;

        auto panel = std::make_unique<Panel>(Theme::PanelAlt);
        LayoutStyle ps;
        ps.direction = FlexDirection::Column;
        panel->SetLayoutStyle(ps);
        auto* grid = panel->AddChild<PropertyGrid>();
        grid->EditLayoutStyle().grow = 1.0f;
        grid->SetNameWidth(90.0f);

        auto doc = [&dm]() -> Document* { return dm.GetActive(); };
        auto layerNames = [doc]
        {
            std::vector<std::string> names;
            if (Document* d = doc())
                for (uint32_t id : SortedLayerIds(*d))
                    names.push_back(d->GetLayerManager().GetLayer(id)->GetName());
            return names;
        };

        // ── 当前设置（新建对象使用）─────────────────────────────
        Expander* current = grid->AddGroup("当前（新建对象）");
        auto* layer = grid->AddProperty<ComboBox>(current, "图层");
        m_bindings.BindChoice(layer,
            [doc]() -> std::optional<int>
            {
                Document* d = doc();
                if (!d) return -1;
                const auto& ids = SortedLayerIds(*d);
                const auto it  = std::find(ids.begin(), ids.end(), d->GetLayerManager().GetActiveLayerID());
                return it == ids.end() ? -1 : static_cast<int>(it - ids.begin());
            },
            [doc](const int& i)
            {
                if (Document* d = doc())
                    d->GetLayerManager().SetActiveLayerID(SortedLayerIds(*d)[static_cast<size_t>(i)]);
            },
            layerNames);

        auto* linetype = grid->AddProperty<ComboBox>(current, "线型");
        m_bindings.BindChoice(linetype,
            [doc]() -> std::optional<int> { Document* d = doc(); return d ? static_cast<int>(d->GetScene().GetCurrentLineType()) : -1; },
            [doc](const int& i) { if (Document* d = doc()) d->GetScene().SetCurrentLineType(static_cast<LineTypeID>(i)); },
            [doc]
            {
                std::vector<std::string> names;
                if (Document* d = doc())
                    for (const auto& r : d->GetScene().GetLineTypeTable().Records())     // 索引即 ID
                        names.push_back(r.Name);
                return names;
            });

        auto* textStyle = grid->AddProperty<ComboBox>(current, "文字样式");
        m_bindings.BindChoice(textStyle,
            [doc]() -> std::optional<int>
            {
                Document* d = doc();
                if (!d) return -1;
                const auto& recs = d->GetScene().GetTextStyleTable().Records();
                for (size_t i = 0; i < recs.size(); ++i)
                    if (recs[i].Id == d->GetScene().GetCurrentTextStyle())
                        return static_cast<int>(i);
                return -1;
            },
            [doc](const int& i)
            {
                if (Document* d = doc())
                    d->GetScene().SetCurrentTextStyle(d->GetScene().GetTextStyleTable().Records()[static_cast<size_t>(i)].Id);
            },
            [doc]
            {
                std::vector<std::string> names;
                if (Document* d = doc())
                    for (const auto& r : d->GetScene().GetTextStyleTable().Records())
                        names.push_back(r.Name);
                return names;
            });

        auto* textHeight = grid->AddProperty<NumberBox>(current, "文字高度", 2.5, 0.001, 10000.0, 0.5, 3);
        textHeight->SetTooltip("新建文字的字高；当前文字样式设了固定字高时以样式为准");
        m_bindings.BindNumber(textHeight,
            [&dm] { return std::optional<double>(dm.GetEditor().GetTextHeight()); },
            [&dm](const double& v) { dm.GetEditor().SetTextHeight(v); });

        auto* lineweight = grid->AddProperty<ComboBox>(current, "线宽", LineweightLabels());
        m_bindings.BindChoice(lineweight,
            [doc]() -> std::optional<int> { Document* d = doc(); return d ? LineweightIndex(d->GetScene().GetCurrentLineweight()) : -1; },
            [doc](const int& i) { if (Document* d = doc()) d->GetScene().SetCurrentLineweight(kLineweightItems[i].lw); });

        // 每个文档有自己的编辑器：绑定是长期存在的，不能保存引用，每次取当前文档的
        auto editor = [&dm]() -> Editor& { return dm.GetEditor(); };

        // ── 绘图辅助（与工具栏、状态栏、F3 / F8 是同一份数据，任何一处修改都同步）────
        //Expander* aux = grid->AddGroup("绘图辅助");
        //auto bindToggle = [this](CheckBox* box, std::function<bool()> get, std::function<void()> toggle)
        //{
        //    m_bindings.BindCheck(box, [get] { return std::optional<bool>(get()); },
        //                              [get, toggle](const bool& v) { if (v != get()) toggle(); });
        //};
        //bindToggle(grid->AddProperty<CheckBox>(aux, "对象捕捉"), [editor] { return editor().IsSnapEnabled(); },  [editor] { editor().ToggleSnap(); });
        //bindToggle(grid->AddProperty<CheckBox>(aux, "正交"),     [editor] { return editor().IsOrthoEnabled(); }, [editor] { editor().ToggleOrtho(); });
        //bindToggle(grid->AddProperty<CheckBox>(aux, "悬停高亮"), [editor] { return editor().IsHoverEnabled(); }, [editor] { editor().ToggleHover(); });
        //auto* radius = grid->AddProperty<NumberBox>(aux, "捕捉孔径", 10.0, 1.0, 50.0, 1.0, 0);
        //m_bindings.BindNumber(radius,
        //    [editor] { return std::optional<double>(editor().GetSnapEngine().GetSnapRadiusPx()); },
        //    [editor](const double& v) { editor().GetSnapEngine().SetSnapRadiusPx(v); });

        // ── 选择集（可编辑：多个对象取值不同时显示"*多种*"；修改走 ChangeAttrCommand，可撤销）────────────
        Expander* sel = grid->AddGroup("选择集");
        auto* count = grid->AddProperty<Label>(sel, "数量", std::string(), 13.0f);
        m_bindings.BindLabel(count, [this, doc]
        {
            const size_t n = doc() ? SelectionInfo().count : 0;
            return n == 0 ? std::string("未选择") : std::to_string(n) + " 个对象";
        });
        auto* selLayer = grid->AddProperty<ComboBox>(sel, "图层");
        selLayer->SetPlaceholder("—");
        m_bindings.BindChoice(selLayer,
            [this, doc]() -> std::optional<int>
            {
                if (!doc()) return -1;
                return SelectionInfo().layer;
            },
            [editor, doc](const int& i)
            {
                Document* d = doc();
                if (!d) return;
                const auto& ids = SortedLayerIds(*d);
                if (i >= 0 && static_cast<size_t>(i) < ids.size())
                    editor().ChangeSelectionAttr(AttrChange{ .Layer = ids[static_cast<size_t>(i)] });
            },
            layerNames);

        auto* selColor = grid->AddProperty<ComboBox>(sel, "颜色", ColorLabels());
        selColor->SetPlaceholder("—");
        m_bindings.BindChoice(selColor,
            [this, editor, doc]() -> std::optional<int>
            {
                if (!doc()) return -1;
                return SelectionInfo().color;
            },
            [editor](const int& i)
            {
                const auto& items = ColorItems();
                if (i >= 0 && static_cast<size_t>(i) < items.size())      // "其他"不可设置
                    editor().ChangeSelectionAttr(AttrChange{ .Color = items[static_cast<size_t>(i)].color });
            });

        auto* selLinetype = grid->AddProperty<ComboBox>(sel, "线型");
        selLinetype->SetPlaceholder("—");
        m_bindings.BindChoice(selLinetype,
            [this, editor, doc]() -> std::optional<int>
            {
                if (!doc()) return -1;
                return SelectionInfo().lineType;
            },
            [editor](const int& i)
            {
                if (i >= 0)
                    editor().ChangeSelectionAttr(AttrChange{ .LineType = static_cast<LineTypeID>(i) });
            },
            [doc]
            {
                std::vector<std::string> names;
                if (Document* d = doc())
                    for (const auto& r : d->GetScene().GetLineTypeTable().Records())     // 索引即 ID
                        names.push_back(r.Name);
                return names;
            });

        auto* selScale = grid->AddProperty<NumberBox>(sel, "线型比例", 1.0, 0.001, 10000.0, 0.1, 3);
        m_bindings.BindNumber(selScale,
            [this, doc]() -> std::optional<double>
            {
                if (!doc()) return std::nullopt;
                return SelectionInfo().lineTypeScale;
            },
            [editor](const double& v) { editor().ChangeSelectionAttr(AttrChange{ .LinetypeScale = v }); });

        auto* selWeight = grid->AddProperty<ComboBox>(sel, "线宽", LineweightLabels());
        selWeight->SetPlaceholder("—");
        m_bindings.BindChoice(selWeight,
            [this, editor, doc]() -> std::optional<int>
            {
                if (!doc()) return -1;
                return SelectionInfo().lineweight;
            },
            [editor](const int& i)
            {
                if (i >= 0 && static_cast<size_t>(i) < std::size(kLineweightItems))
                    editor().ChangeSelectionAttr(AttrChange{ .Lineweight = kLineweightItems[i].lw });
            });

        // ── 几何（属性描述表驱动）────────────────────────────────
        // 面板里预建固定数量的"槽"（一行 = 名称 + 编辑控件）；选择集变化时按公共特性重新分配：
        // 用到的槽显示名称并绑定对应特性，没用到的整行隐藏。没有选择 / 没有描述表时整组隐藏
        Expander* geom = grid->AddGroup("几何");
        struct Slot { std::string name; PropKind kind = PropKind::Number; bool editable = false; std::optional<PropValue> value; };
        constexpr size_t kNumberSlots = 8;          // 目前各实体最多 7 个数值特性（圆弧）
        auto slots = std::make_shared<std::vector<Slot>>(kNumberSlots + 1);     // 下标 0 是文字槽，其余是数值槽

        auto* textSlot = grid->AddProperty<TextBox>(geom, "内容");
        m_bindings.BindText(textSlot,
            [slots]() -> std::optional<std::string>
            {
                const Slot& s = (*slots)[0];
                if (s.name.empty()) return std::string();
                if (!s.value) return std::nullopt;
                return std::get<std::string>(*s.value);
            },
            [slots, editor](const std::string& v)
            {
                const Slot& s = (*slots)[0];
                if (!s.name.empty() && s.editable)
                    editor().SetSelectionProperty(s.name, PropValue(v));
            });

        std::vector<NumberBox*> numberBoxes;
        for (size_t i = 1; i <= kNumberSlots; ++i)
        {
            auto* box = grid->AddProperty<NumberBox>(geom, "", 0.0, -1.0e12, 1.0e12, 1.0, 3);
            numberBoxes.push_back(box);
            m_bindings.BindNumber(box,
                [slots, i]() -> std::optional<double>
                {
                    const Slot& s = (*slots)[i];
                    if (s.name.empty() || !s.value) return s.name.empty() ? std::optional<double>(0.0) : std::nullopt;
                    return std::get<double>(*s.value);
                },
                [slots, i, editor](const double& v)
                {
                    const Slot& s = (*slots)[i];
                    if (!s.name.empty() && s.editable)
                        editor().SetSelectionProperty(s.name, PropValue(v));
                });
        }

        // 选择集变化后：重新分配槽（在 BindingSet::Refresh 之前调用）
        m_syncGeometryRows = [this, slots, geom, textSlot, numberBoxes, doc]
        {
            for (Slot& s : *slots) s = Slot{};
            if (doc())
            {
                size_t nextNumber = 1;
                for (const CommonProperty& p : SelectionInfo().properties)
                {
                    Slot* target = nullptr;
                    if (p.kind == PropKind::String)
                        target = &(*slots)[0];
                    else if (nextNumber <= kNumberSlots)
                        target = &(*slots)[nextNumber++];
                    if (target && target->name.empty())
                        *target = Slot{ p.name, p.kind, p.editable, p.value };
                }
            }

            bool any = false;
            auto apply = [&](MiniGUI::Node* box, const Slot& s)
            {
                MiniGUI::Node* row = box->GetParent();
                const bool used = !s.name.empty();
                any |= used;
                row->SetVisible(used);
                if (!used) return;
                if (auto* label = dynamic_cast<MiniGUI::Label*>(row->GetChildren().front().get()))
                    label->SetText(s.name);
                box->SetEnabled(s.editable);
            };
            apply(textSlot, (*slots)[0]);
            for (size_t i = 0; i < numberBoxes.size(); ++i)
                apply(numberBoxes[i], (*slots)[i + 1]);
            geom->SetVisible(any);
        };
        m_syncGeometryRows();

        // 用户在特性面板（或工具栏下拉框）上修改后：刷新命令状态（工具栏、菜单勾选）并重绘视口
        m_bindings.SetOnChanged([this]
        {
            m_commands.NotifyStateChanged();
            RequestRenderAll();
        });
        return panel;
    }

    // =========================================================
    // 状态栏：MiniCAD 的状态栏（靠右的捕捉 正交 悬停 细线）+ 界面描述文件的加载结果（主窗口的）
    // =========================================================
    std::unique_ptr<MiniGUI::Node> MainFrame::CreateStatusBar(ViewChrome& c)
    {
        using namespace MiniGUI;
        // 照 SolidCraft 底部的 History 条：一张浮在绘图区上的圆角卡片；左侧是模型 / 布局标签
        constexpr float kHeight = TabView::kStripHeight;
        ViewChrome* pc = &c;
        // 主窗口：在状态栏上按下（布局标签、捕捉等开关）先让主视口里的文档成为当前文档
        std::unique_ptr<Panel> status = c.main ? std::make_unique<PressHook<Panel>>([this, pc] { UseChrome(*pc); }, Theme::Panel)
                                               : std::make_unique<Panel>(Theme::Panel);
        status->SetRounding(Theme::RadiusLg);
        status->SetBorder(Theme::BorderSubtle);
        LayoutStyle s;
        s.direction  = FlexDirection::Row;
        s.alignItems = Align::Center;
        s.height     = kHeight;
        s.shrink     = 0.0f;
        s.padding    = Edges::Make(2.0f, 0.0f, 10.0f, 0.0f);
        status->SetLayoutStyle(s);

        status->AddChild(CreateLayoutTabs(c));
        c.statusBar = status->AddChild<StatusBarView>(m_docManager, [this, pc] { return DocOf(*pc); }, kHeight - 2.0f);
        c.statusBar->EditLayoutStyle().grow     = 1.0f;
        c.statusBar->EditLayoutStyle().minWidth = 0.0f;
        c.statusBar->SetOnToggled([this] { StateChanged(); });
        if (!c.main)
            return status;
        c.statusBar->AddRightItem(CreateOpenProgress());     // 读取进度：开关左边，平时隐藏
        c.statusBar->AddRightItem(CreateSaveProgress());     // 保存进度：同上

        m_uiStatus = status->AddChild<Label>("", 12.0f, Theme::TextDim);
        m_uiStatus->SetEllipsis(true);
        m_uiStatus->EditLayoutStyle().maxWidth = 420.0f;
        m_uiStatus->EditLayoutStyle().shrink   = 1.0f;
        m_uiStatus->EditLayoutStyle().minWidth = 0.0f;
        return status;
    }

    // =========================================================
    // 界面描述文件：加载、监视
    // =========================================================
    void MainFrame::ReloadUi()
    {
        using namespace MiniGUI;
        const bool firstLoad = m_uiHost->GetChildren().empty();
        const bool hadFocus  = !m_ui->GetFocus() || (m_viewport && m_viewport->HasFocus());
        const std::u8string fileNameU8 = ToPath(m_uiPath).filename().u8string();
        const std::string fileName(fileNameU8.begin(), fileNameU8.end());

        m_uiWriteTime = WriteTime(m_uiPath);
        ApplyStartPage(false);      // 先恢复被起始页隐藏的部分（重建失败时保留的是旧界面）；重建后 StateChanged 重新应用
        const bool ok = m_layout->ApplyFile(m_uiHost, m_uiPath);
        // 重建成功后旧界面随即销毁，指向它的指针都要丢掉：
        // - 动态输入框挂在视口的父节点（界面描述生成的容器）上：下一帧 SyncDynamicInput 重新创建
        // - 起始页隐藏的兄弟节点：应用命令覆盖时会通知状态变化，SyncDocuments 可能又在旧界面上记下了一批；
        //   宿主面板的显示状态已由 UiLayout 恢复，下面的 StateChanged 在新界面上重新应用起始页
        if (ok)
        {
            m_dynInput = nullptr;
            m_startHidden.clear();
        }
        if (!ok)
        {
            LOG_ERROR("界面描述：%s", m_layout->GetError().c_str());
            if (firstLoad)
                m_layout->ApplyText(m_uiHost, kFallbackLayout);     // 第一次就失败：至少显示绘图区和状态栏
            m_uiStatus->SetText(m_layout->GetError() + "（保留当前界面）");
            m_uiStatus->SetColor(Theme::Danger);
        }
        else
        {
            for (const std::string& w : m_layout->GetWarnings())
                LOG_WARN("界面描述：%s", w.c_str());
            // 界面描述文件正常时状态栏不显示它（右侧是发布版本），有警告时才显示
            const size_t n = m_layout->GetWarnings().size();
            std::string text;
            if (n > 0)
                text = "界面：" + fileName + "，" + std::to_string(n) + " 条警告：" + m_layout->GetWarnings().front();
            m_uiStatus->SetText(text);
            m_uiStatus->SetColor(n > 0 ? Theme::Danger : Theme::TextDim);
            m_uiStatus->SetTooltip(m_uiPath);
            LOG_INFO("界面描述已加载：%s（%zu 条警告）", m_uiPath.c_str(), n);
        }

        // 标题栏是重新生成的：恢复标题和激活状态
        if (TitleBar* bar = m_layout->GetTitleBar())
        {
            bar->SetTitle(m_title);
            bar->SetWindowActive(m_platform->IsWindowActive());
        }

        // 功能区也是重新生成的：命令搜索框执行或关闭后把焦点还给绘图区
        if (Ribbon* ribbon = m_layout->GetRibbon())
            ribbon->SetOnSearchDone([this]
            {
                if (m_viewport && m_viewport->IsVisible())
                    m_viewport->Focus();
                else
                    m_ui->SetFocus(nullptr);
            });

        // 浮出的面板放进独立的系统窗口（平台给不出时停靠区退回窗口内浮动）；要在恢复用户布局之前设置
        if (DockSpace* dock = m_layout->GetDock("main"))
            dock->SetFloatHost(m_floatHost.get());
        // 用户布局只在启动时恢复：之后修改界面描述文件里的 dock，重新加载后立即看到效果
        if (firstLoad)
            LoadUserLayout();
        if (DockSpace* dock = m_layout->GetDock("main"))
            dock->SetOnLayoutChanged([this] { SaveUserLayout(); });

        m_ui->Update();
        if (hadFocus && m_viewport->GetParent() && m_viewport->IsVisible())
            m_viewport->Focus();
        StateChanged();
    }

    void MainFrame::CheckUiFileChanged()
    {
        // 目录里任何文件变化都会通知，只在本文件的修改时间变了时重新加载。
        // 编辑器保存时可能分几次写入：写到一半解析失败会保留当前界面，写完后的下一次通知再加载成功
        if (WriteTime(m_uiPath) != m_uiWriteTime)
            ReloadUi();
    }

    // =========================================================
    // 用户布局：拖动、关闭、调整面板后保存，下次启动恢复
    // =========================================================
    void MainFrame::LoadUserLayout()
    {
        using namespace MiniGUI;
        DockSpace* dock = m_layout->GetDock("main");
        const std::filesystem::path path = UserLayoutPath(*m_platform);
        if (!dock || !m_useUserLayout || path.empty())
            return;

        const std::u8string u8 = path.u8string();
        std::string text;
        if (!ReadTextFile(std::string(u8.begin(), u8.end()), text))
            return;     // 还没有保存过

        JsonValue saved;
        JsonError err;
        if (!ParseJson(text, saved, err) || !saved["dock"].IsObject())
        {
            LOG_WARN("用户布局文件无法解析，使用默认布局：%s", err.ToString().c_str());
            return;
        }
        m_viewCubeVisible = saved["viewCube"].AsBool(true);
        UpdateViewCubeVisibility();
        m_commandLineVisible = saved["commandLine"].AsBool(true);
        ForEachChrome([this](ViewChrome& c)
        {
            if (c.cmdCard)
                c.cmdCard->SetVisible(m_commandLineVisible);
        });
        // 布局格式 2：绘图区铺满窗口、停靠区浮在上面、命令行不在停靠区里。更早保存的停靠布局不再适用
        if (saved["version"].AsNumber(1) < kUserLayoutVersion)
        {
            LOG_INFO("用户布局是旧版界面保存的，使用默认布局");
            return;
        }
        // 界面描述文件新增了面板（保存时还没有）：旧布局里没有它，用默认布局，免得新面板一直看不到
        const JsonValue& known = saved["panels"];
        for (const std::string& id : dock->GetPanelIds())
        {
            const auto& list = known.GetArray();
            if (std::none_of(list.begin(), list.end(), [&id](const JsonValue& v) { return v.AsString() == id; }))
            {
                LOG_INFO("界面新增了面板 %s，使用默认布局", id.c_str());
                return;
            }
        }

        const JsonValue defaults = dock->SaveLayout();
        std::vector<std::string> warnings;
        // 文档区必须在布局里；文件损坏或界面描述换了面板时回到默认布局
        if (!dock->LoadLayout(saved["dock"], &warnings) || !dock->IsPanelVisible("documents"))
        {
            LOG_WARN("用户布局与当前界面不符，使用默认布局");
            dock->LoadLayout(defaults);
            return;
        }
        for (const std::string& w : warnings)
            LOG_WARN("用户布局：%s", w.c_str());
    }

    void MainFrame::SetViewCubeVisible(bool visible)
    {
        m_viewCubeVisible = visible;
        UpdateViewCubeVisibility();
        SaveUserLayout();
    }

    void MainFrame::UpdateViewCubeVisibility()
    {
        ForEachChrome([this](ViewChrome& c) { UpdateViewCubeVisibility(c); });
    }

    void MainFrame::UpdateViewCubeVisibility(ViewChrome& c)
    {
        if (!c.cube)
            return;
        const Document* doc = DocOf(c);
        c.cube->SetVisible(m_viewCubeVisible && !(doc && doc->ActiveLayoutIsPaper()));
    }

    void MainFrame::SaveUserLayout()
    {
        using namespace MiniGUI;
        DockSpace* dock = m_layout->GetDock("main");
        const std::filesystem::path path = UserLayoutPath(*m_platform);
        if (!dock || !m_useUserLayout || path.empty())
            return;

        JsonValue root = JsonValue::MakeObject();
        root.Set("comment", JsonValue("MiniCAD 面板布局，由程序自动保存；删除本文件或使用“视图 > 重置面板布局”恢复默认"));
        root.Set("dock", dock->SaveLayout());
        root.Set("version", JsonValue(static_cast<double>(kUserLayoutVersion)));
        root.Set("viewCube", JsonValue(m_viewCubeVisible));
        root.Set("commandLine", JsonValue(m_commandLineVisible));
        JsonValue panels = JsonValue::MakeArray();      // 保存时已有的面板：之后新增的面板据此识别
        for (const std::string& id : dock->GetPanelIds())
            panels.Push(id);
        root.Set("panels", std::move(panels));

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << root.Dump(2);
        if (!out)
        {
            const std::u8string u8 = path.u8string();
            LOG_WARN("无法保存用户布局：%s", std::string(u8.begin(), u8.end()).c_str());
        }
    }

    void MainFrame::ResetLayout()
    {
        std::error_code ec;
        const std::filesystem::path path = UserLayoutPath(*m_platform);
        if (!path.empty())
            std::filesystem::remove(path, ec);
        ReloadUi();         // 按界面描述文件里的默认布局重建（不再读取用户布局）
    }

    // =========================================================
    // 最近打开：exe 所在目录下的 recent.txt（UTF-8，每行一个路径），随程序一起走（绿色版友好）。
    // 面板布局仍在用户数据目录（%LOCALAPPDATA%\MiniCAD\layout.json），两者位置不同
    // =========================================================
    std::filesystem::path MainFrame::RecentStorePath() const
    {
        const std::string dir = m_platform->GetAppDir();
        return dir.empty() ? std::filesystem::path() : ToPath(dir) / "recent.txt";
    }

    void MainFrame::LoadRecentFiles()
    {
        const std::filesystem::path path = RecentStorePath();
        if (path.empty())
            return;                     // 该平台不保存（网页版）：列表只在本次会话里有效
        const std::u8string u8 = path.u8string();
        m_recentFiles.Load(std::string(u8.begin(), u8.end()));
    }

    void MainFrame::SaveRecentFiles()
    {
        const std::filesystem::path store = RecentStorePath();
        if (store.empty())
            return;
        const std::u8string u8 = store.u8string();
        m_recentFiles.Save(std::string(u8.begin(), u8.end()));
    }

    void MainFrame::RememberRecentFile(const std::string& path)
    {
        if (path.empty())
            return;
        m_recentFiles.Add(path);
        SaveRecentFiles();
        RefreshStartPage();
    }

    void MainFrame::ForgetRecentFile(const std::string& path)
    {
        m_recentFiles.Remove(path);
        SaveRecentFiles();
        RefreshStartPage();
    }

    void MainFrame::ClearRecentFiles()
    {
        m_recentFiles.Clear();
        SaveRecentFiles();
        RefreshStartPage();
    }

    std::vector<MiniGUI::MenuItem> MainFrame::BuildRecentMenuItems()
    {
        using namespace MiniGUI;
        std::vector<MenuItem> items;
        const auto& list = m_recentFiles.Items();
        for (size_t i = 0; i < list.size(); ++i)
        {
            const std::string path = list[i];           // 按值捕获：菜单项可能比这次调用活得久
            // 前 9 条带 &1～&9 助记符（同 AutoCAD："文件 > 最近打开 > 1 xxx"）
            const std::string prefix = i < 9 ? ("&" + std::to_string(i + 1) + " ") : "";
            MenuItem item(prefix + m_recentFiles.DisplayName(i), [this, path] { OpenDrawings({ path }); });
            items.push_back(std::move(item));
        }

        if (items.empty())
        {
            // 一条记录都没有：仍返回一个（禁用的）提示项，让「最近打开」本身保持可展开——
            // 否则菜单项会被 MenuPopup 判为空而整个灰掉，看起来像没有这个功能
            items.push_back(MenuItem("（无最近打开的文件）").Enabled(false));
            return items;
        }

        items.push_back(MenuItem::Separator());
        items.push_back(MenuItem("清除记录(&C)", [this] { ClearRecentFiles(); }));
        return items;
    }
}
