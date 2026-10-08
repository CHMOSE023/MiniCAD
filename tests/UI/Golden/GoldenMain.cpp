// 截图对比测试
//   MiniGUIGolden               软件光栅结果与基准图片比较；D3D11 结果与软件光栅比较
//   MiniGUIGolden --update      重新生成基准图片（需要人工检查后再提交）
//   MiniGUIGolden --no-d3d11    跳过 D3D11 比较（没有 D3D11 的环境）
//   MiniGUIGolden --save-all    所有场景都输出实际图片，不只是失败的
//   MiniGUIGolden --warp        强制使用 WARP（微软软件实现的 D3D11），模拟没有显卡的环境
//   MiniGUIGolden <名称片段>     只运行名称包含该片段的场景
// 失败时在构建目录的 golden_out/ 下输出实际图片和差异图（差异像素标红）
#include "D3D11Capture.h"
#include "PngIO.h"
#include "DemoNodes.h"
#include "Core/UIContext.h"
#include "Platform/Win32/Win32Fonts.h"
#include "Text/Font.h"
#include "Gallery.h"
#include "Widgets/Button.h"
#include "Widgets/ComboBox.h"
#include "Widgets/Label.h"
#include "Widgets/ListView.h"
#include "Widgets/Menu.h"
#include "Widgets/Panel.h"
#include "Widgets/TabView.h"
#include "Widgets/TextBox.h"
#include "Widgets/CommandUI.h"
#include "Widgets/UiLayout.h"
#include "Widgets/DockSpace.h"
#include "Widgets/TitleBar.h"
#include "Widgets/CommandConsole.h"
#include "Widgets/AutoComplete.h"
#include "Data/CommandRegistry.h"
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <windows.h>
#include <crtdbg.h>

using namespace MiniGUI;
using namespace MiniGUI::Test;

namespace
{
    constexpr Color32 kClearColor = ColorFromHex(0x1E1F22);

    // ── 字体 ─────────────────────────────────────────────────────
    // 文字场景依赖系统字体（微软雅黑 UI + Segoe UI Symbol）。不同 Windows 版本的字体文件可能不同，
    // 所以基准目录里记录生成基准时字体文件的指纹；指纹不一致时跳过文字场景，而不是报失败
    struct Fonts
    {
        std::shared_ptr<Font> ui;
        std::shared_ptr<Font> symbol;

        bool Available() const { return ui != nullptr; }

        std::string Fingerprint() const
        {
            uint64_t h = 1469598103934665603ull;   // FNV-1a 64
            auto feed = [&h](const std::shared_ptr<Font>& f)
            {
                if (!f) return;
                for (uint8_t b : f->GetData()) { h ^= b; h *= 1099511628211ull; }
                h ^= f->GetData().size();
                h *= 1099511628211ull;
            };
            feed(ui);
            feed(symbol);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
            return buf;
        }
    };

    Fonts g_fonts;

    // ── 场景 ─────────────────────────────────────────────────────
    struct Scene
    {
        const char* name;
        Vec2        size;       // 逻辑像素
        float       scale;      // DPI 缩放
        bool        needsFont;  // 含文字，依赖系统字体
        std::function<void(UIContext&, IRenderBackend&, std::vector<TextureId>&)> build;
    };

    void BuildPrimitives(UIContext& ui, IRenderBackend& backend, std::vector<TextureId>& textures)
    {
        textures.push_back(CreateCheckerTexture(backend));
        ui.GetRoot()->AddChild<PrimitivesView>(textures.back());
    }

    DemoRoot* BuildDemo(UIContext& ui, IRenderBackend& backend, std::vector<TextureId>& textures)
    {
        textures.push_back(CreateCheckerTexture(backend));
        DemoRoot* root = ui.GetRoot()->AddChild<DemoRoot>(textures.back());
        ui.Update();   // 先布局，之后才能按坐标模拟指针事件
        return root;
    }

    // 深度优先查找第一个满足条件的 T 类型节点（场景里定位要操作的控件）
    template<typename T, typename Pred>
    T* FindNode(Node* node, Pred pred)
    {
        if (auto* t = dynamic_cast<T*>(node); t && pred(t))
            return t;
        for (const auto& c : node->GetChildren())
            if (T* found = FindNode<T>(c.get(), pred))
                return found;
        return nullptr;
    }

    GalleryRoot* BuildGalleryScene(UIContext& ui, int page)
    {
        GalleryRoot* g = BuildGallery(ui, MINIGUI_ASSETS_DIR);
        g->GetTabs()->SetSelected(page);
        ui.Update();
        return g;
    }

    void Click(UIContext& ui, Vec2 pos);

    // M10.1：停靠区。左侧"图层 / 特性"两个标签，中间无标题栏的文档区，右侧上下拆分"块"与"命令历史"。
    // dragToDocs：拖动"块"的标签到文档区左侧，显示落点预览（不松开）
    DockSpace* BuildDockScene(UIContext& ui, bool dragToDocs)
    {
        auto* dock = ui.GetRoot()->AddChild<DockSpace>();
        auto panel = [](const char* text, ColorRef bg)
        {
            auto p = std::make_unique<Panel>(bg);
            p->EditLayoutStyle().padding = Edges::All(10.0f);
            p->AddChild<Label>(text, 13.0f, Theme::TextDim);
            return p;
        };
        dock->AddPanel("layers", "图层", panel("图层列表", Theme::Panel));
        dock->AddPanel("props", "特性", panel("特性面板", Theme::Panel));
        dock->AddPanel("blocks", "块", panel("块库", Theme::Panel));
        dock->AddPanel("history", "命令历史", panel("命令: LINE", Theme::Panel));
        dock->AddPanel("docs", "文档", panel("文档区（无标题栏，不能合并标签）", Theme::Background),
                       { .closable = false, .showHeader = false });
        JsonValue layout;
        JsonError err;
        ParseJson(R"json({ "split": "row", "children": [
            { "tabs": [ "layers", "props" ], "active": 1, "size": 220 },
            { "tabs": [ { "panel": "docs", "header": false, "closable": false } ] },
            { "split": "column", "size": 240, "children": [
                { "tabs": [ "blocks" ] },
                { "tabs": [ "history" ], "size": 140 } ] } ] })json", layout, err);
        dock->LoadLayout(layout);
        ui.Update();
        if (dragToDocs)
        {
            const Rect blocks = dock->GetPanelRect("blocks");
            const Rect docs   = dock->GetPanelRect("docs");
            const Vec2 from{ blocks.min.x + 14.0f, blocks.min.y - DockSpace::kHeaderHeight * 0.5f };
            const Vec2 to{ docs.min.x + 40.0f, docs.Center().y };
            ui.PointerMove(from, 0);
            ui.PointerDown(from, MouseButton::Left, 0, 1);
            ui.PointerMove(from + Vec2{ -20.0f, 10.0f }, 0);
            ui.PointerMove(to, 0);
            ui.Update();
        }
        return dock;
    }

    // M9：界面描述文件生成的菜单栏与工具栏。命令注册表必须活到渲染结束，放在静态变量里（每次构建时重建）
    void BuildJsonUi(UIContext& ui, bool openOverflow)
    {
        static std::unique_ptr<CommandRegistry> reg;
        static std::unique_ptr<UiLayout>        layout;
        static bool ortho = true;
        reg    = std::make_unique<CommandRegistry>();
        layout = std::make_unique<UiLayout>(*reg, std::string(MINIGUI_ASSETS_DIR) + "/icons");

        const struct { const char* id; const char* label; const char* icon; } cmds[] =
        {
            { "draw.line", "直线", "Line.png" }, { "draw.circle", "圆", "Circle.png" }, { "draw.arc", "圆弧", "Arc.png" },
            { "draw.rect", "矩形", "Rect.png" }, { "draw.pline", "多段线", "Pline.png" }, { "draw.spline", "样条曲线", "Spline.png" },
            { "draw.text", "文字", "Text.png" }, { "modify.move", "移动", "Move.png" }, { "modify.copy", "复制", "Copy.png" },
            { "modify.rotate", "旋转", "Rotate.png" }, { "modify.mirror", "镜像", "Mirror.png" },
            { "edit.undo", "撤销(&U)", "Undo.png" }, { "edit.redo", "重做(&R)", "Redo.png" },
        };
        for (const auto& c : cmds)
            reg->Register({ .id = c.id, .label = c.label, .icon = c.icon, .execute = [] {} });
        reg->Find("edit.redo")->canExecute = [] { return false; };
        reg->Register({ .id = "aux.ortho", .label = "正交(&O)", .shortcut = "F8", .execute = [] { ortho = !ortho; },
                        .isChecked = [] { return ortho; } });
        reg->Find("draw.line")->isChecked = [] { return true; };     // 当前工具

        Panel* host = ui.GetRoot()->AddChild<Panel>(Theme::Background);
        layout->RegisterPanel("canvas", std::make_unique<Panel>(Theme::Background));
        layout->ApplyText(host, R"json({
            "menus": [ { "title": "绘图(&D)", "items": [ "draw.line", "draw.circle", "-", "aux.ortho" ] },
                       { "title": "修改(&M)", "items": [ "modify.move", "modify.copy" ] } ],
            "toolbars": {
                "main": [ "edit.undo", "edit.redo", "|", "aux.ortho", { "command": "draw.text", "text": true }, "|",
                          "modify.move", "modify.copy", "modify.rotate", "modify.mirror" ],
                "draw": { "items": [ "draw.line", "draw.circle", "draw.arc", "draw.rect", "|", "draw.pline", "draw.spline" ], "iconSize": 22 }
            },
            "layout": { "type": "column", "children": [
                { "type": "menubar" },
                { "type": "toolbar", "id": "main", "background": "Panel", "width": 300 },
                { "type": "separator" },
                { "type": "row", "grow": 1, "children": [
                    { "type": "toolbar", "id": "draw", "vertical": true, "background": "PanelAlt" },
                    { "type": "separator" },
                    { "type": "panel", "name": "canvas", "grow": 1 }
                ] }
            ] }
        })json");
        ui.Update();
        if (openOverflow)
        {
            if (ToolBar* tb = layout->GetToolBar("main"))
                Click(ui, tb->GetMoreButton()->GetScreenBounds().Center());
        }
    }

    // M10.2：无边框主窗口的标题栏（图标 + 菜单栏 + 居中标题 + 窗口按钮）+ 只有标签条的多文档标签。
    // maximized：最大化按钮显示"还原"图标；hoverClose：指针停在关闭按钮上（红底白叉）
    void BuildMainWindowUi(UIContext& ui, bool maximized, bool hoverClose)
    {
        static std::unique_ptr<CommandRegistry> reg;
        static std::unique_ptr<UiLayout>        layout;
        static bool zoomed = false;
        zoomed = maximized;
        reg    = std::make_unique<CommandRegistry>();
        layout = std::make_unique<UiLayout>(*reg, std::string(MINIGUI_ASSETS_DIR) + "/icons");
        reg->Register({ .id = "file.new", .label = "新建(&N)", .execute = [] {} });
        reg->Register({ .id = TitleBar::kMinimizeCommand, .execute = [] {} });
        reg->Register({ .id = TitleBar::kMaximizeCommand, .execute = [] {}, .isChecked = [] { return zoomed; } });
        reg->Register({ .id = TitleBar::kCloseCommand,    .execute = [] {} });

        auto docs = std::make_unique<Panel>(Theme::Background);
        docs->EditLayoutStyle().direction = FlexDirection::Column;
        TabView* tabs = docs->AddChild<TabView>();
        tabs->EditLayoutStyle().height = TabView::kStripHeight;
        tabs->SetShowNewTabButton(true);
        tabs->AddTab("图纸1.mcad", nullptr, true);
        tabs->AddTab("Untitled", nullptr, true);
        tabs->AddTab("厂房平面图.mcad", nullptr, true);
        tabs->SetTabModified(1, true);
        tabs->SetSelected(1);
        layout->RegisterPanel("documents", std::move(docs));

        Panel* host = ui.GetRoot()->AddChild<Panel>(Theme::Background);
        layout->ApplyText(host, R"json({
            "menus": [ { "title": "文件(&F)", "items": [ "file.new" ] }, { "title": "编辑(&E)", "items": [] },
                       { "title": "视图(&V)", "items": [] }, { "title": "帮助(&H)", "items": [] } ],
            "layout": { "type": "column", "children": [
                { "type": "titlebar", "icon": "Circle.png", "children": [ { "type": "menubar" } ] },
                { "type": "panel", "name": "documents", "grow": 1 }
            ] }
        })json");
        if (TitleBar* bar = layout->GetTitleBar())
            bar->SetTitle("Untitled * - MiniCAD");
        ui.Update();
        if (hoverClose)
        {
            const Rect r = layout->GetTitleBar()->GetScreenBounds();
            ui.PointerMove({ r.max.x - TitleBar::kButtonWidth * 0.5f, r.Center().y }, 0);
            ui.Update();
        }
    }

    // M10.3：命令行。回显若干行（滚到最后），提示为当前工具的提示，输入 "c" 弹出候选（默认选中第一项，在输入框上方）
    void BuildConsoleScene(UIContext& ui)
    {
        Node* col = ui.GetRoot()->AddChild<Node>();
        col->EditLayoutStyle().direction = FlexDirection::Column;
        col->AddChild<Panel>(Theme::Background)->EditLayoutStyle().grow = 1.0f;
        auto* console = col->AddChild<CommandConsole>();
        console->EditLayoutStyle().height = 130.0f;
        console->SetLog({ "命令: Line", "命令: Circle", "未知命令: XYZ", "命令: Move", "Move: 需要先选择对象", "命令: Rectangle" });
        console->SetPrompt("指定第一个角点或 [倒角(C)/圆角(F)]:");
        console->SetCompletion([](const std::string&)
        {
            return std::vector<ListItem>{ { "Circle", "圆" }, { "Copy", "复制" } };
        });
        ui.Update();
        console->FocusInput();
        ui.TextInput("c");
        ui.Update();
    }

    void Click(UIContext& ui, Vec2 pos)
    {
        ui.PointerMove(pos, 0);
        ui.PointerDown(pos, MouseButton::Left, 0);
        ui.PointerUp(pos, MouseButton::Left, 0);
        ui.Update();
    }

    std::vector<Scene> MakeScenes()
    {
        return
        {
            // M0：DrawList 图元（抗锯齿线、圆、圆角矩形、多边形、裁剪、贴图）
            { "primitives_100", { 1060, 440 }, 1.0f, false,
              [](UIContext& ui, IRenderBackend& b, auto& t) { BuildPrimitives(ui, b, t); } },
            { "primitives_150", { 1060, 440 }, 1.5f, false,
              [](UIContext& ui, IRenderBackend& b, auto& t) { BuildPrimitives(ui, b, t); } },

            // M1 + M2 + M4：节点树、按钮、Flexbox 布局、文字
            { "demo_ui_100", { 1280, 720 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend& b, auto& t) { BuildDemo(ui, b, t); } },
            { "demo_ui_150", { 1024, 640 }, 1.5f, true,
              [](UIContext& ui, IRenderBackend& b, auto& t) { BuildDemo(ui, b, t); } },

            // M4：1200 个不同汉字，迫使字形图集在一帧内从 512 扩容到 1024（验证扩容后重绘本帧）
            { "atlas_grow", { 1000, 720 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  std::string text;
                  for (uint32_t cp = 0x4E00; cp < 0x4E00 + 1200; ++cp)
                  {
                      text.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                      text.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                      text.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                  }
                  Label* label = ui.GetRoot()->AddChild<Label>(text, 20.0f);
                  label->SetWrap(true);
                  label->EditLayoutStyle().padding = Edges::All(10.0f);
              } },

            // M5：输入框的各种状态（占位文字、非焦点选区、输入法组合串下划线、多行自动换行）
            { "textbox_states", { 520, 300 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  Node* col = ui.GetRoot()->AddChild<Node>();
                  col->SetLayoutStyle([] { LayoutStyle s; s.padding = Edges::All(16); s.gap = 12; return s; }());

                  TextBox* empty = col->AddChild<TextBox>();
                  empty->SetPlaceholder("占位文字：请输入名称");

                  TextBox* selected = col->AddChild<TextBox>("选区 selection 在失去焦点后变灰");
                  selected->SetSelection(0, 18);

                  TextBox* composing = col->AddChild<TextBox>("输入：");
                  TextBox* multi = col->AddChild<TextBox>(
                      "多行输入框会自动换行。Multi-line text wraps at the box width, "
                      "中文可以在任意两个字之间换行。");
                  multi->SetMultiline(true);
                  multi->SetRows(3);

                  ui.Update();
                  composing->Focus();
                  ui.CompositionStart();
                  ui.CompositionUpdate("zhong'wen", 9);
              } },

            // M5：Tab 键导航到第二个按钮，显示焦点框
            { "focus_ring", { 420, 80 }, 1.5f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  Node* row = ui.GetRoot()->AddChild<Node>();
                  row->SetLayoutStyle([] { LayoutStyle s; s.direction = FlexDirection::Row; s.alignItems = Align::Center;
                                           s.padding = Edges::All(20); s.gap = 16; return s; }());
                  for (const char* name : { "取消", "确定", "应用" })
                      row->AddChild<Button>(name, [] {})->EditLayoutStyle().height = 32.0f;
                  ui.Update();
                  ui.KeyDown(Key::Tab, 0);
                  ui.KeyDown(Key::Tab, 0);
              } },

            // ── 控件展示（Gallery）───────────────────────────────
            { "gallery_basic", { 1280, 780 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&) { BuildGalleryScene(ui, GalleryRoot::PageBasic); } },
            { "gallery_basic_150", { 1280, 780 }, 1.5f, true,
              [](UIContext& ui, IRenderBackend&, auto&) { BuildGalleryScene(ui, GalleryRoot::PageBasic); } },
            { "gallery_list", { 1100, 600 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PageList);
                  if (auto* list = FindNode<ListView>(g, [](ListView* l) { return l->GetItemCount() == 10000; }))
                  {
                      list->Focus();
                      list->SetCurrent(3);
                      ui.KeyDown(Key::Down, Mods(ModifierKey::Shift));
                      ui.KeyDown(Key::Down, Mods(ModifierKey::Shift));
                  }
                  if (auto* tree = FindNode<TreeView>(g, [](TreeView*) { return true; }))
                      tree->GetRootItem()->GetChildren()[1]->SetExpanded(true);
              } },
            // 树的在位编辑：编辑框覆盖文字，全选原名称后输入新名称
            { "gallery_tree_edit", { 900, 500 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PageList);
                  auto* tree = FindNode<TreeView>(g, [](TreeView*) { return true; });
                  TreeItem* door = tree->GetRootItem()->GetChildren()[0]->GetChildren()[0].get();
                  tree->Focus();
                  tree->SetSelected(door);
                  ui.Update();
                  tree->BeginEdit(door);
                  ui.Update();
                  ui.TextInput("防火门");
                  ui.Update();
              } },
            { "gallery_menu", { 900, 500 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PageBasic);
                  g->GetMenuBar()->OpenMenu(2, true);      // 视图
                  ui.Update();
                  ui.KeyDown(Key::Up, 0);                  // 最后一项"页面"（有子菜单）
                  ui.KeyDown(Key::Right, 0);               // 打开子菜单
                  ui.KeyDown(Key::Down, 0);
                  ui.Update();
              } },
            // 单独按下再松开 Alt：菜单栏进入键盘模式（显示助记符下划线），→ 移到"编辑"
            { "gallery_menubar_keyboard", { 700, 200 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  BuildGalleryScene(ui, GalleryRoot::PageBasic);
                  ui.KeyDown(Key::Alt, Mods(ModifierKey::Alt));
                  ui.KeyUp(Key::Alt, 0);
                  ui.KeyDown(Key::Right, 0);
                  ui.Update();
              } },
            { "gallery_dialog", { 900, 560 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PagePopup);
                  g->OpenLayerDialog();
                  ui.Update();
              } },
            { "gallery_context_menu", { 900, 560 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PagePopup);
                  // 在右键菜单区域里点右键，再把鼠标移到"绘图"子菜单上
                  Node* area = FindNode<Panel>(g, [](Panel* p) { return p->GetLayoutStyle().height == 150.0f; });
                  const Vec2 p = area->GetScreenBounds().Center() + Vec2{ -200.0f, -40.0f };
                  ui.PointerMove(p, 0);
                  ui.PointerDown(p, MouseButton::Right, 0, 1);
                  ui.PointerUp(p, MouseButton::Right, 0);
                  ui.Update();
                  auto* menu = static_cast<MenuPopup*>(ui.GetTopPopup());
                  const Rect mr = menu->GetScreenBounds();
                  ui.PointerMove({ mr.min.x + 60.0f, mr.min.y + 4.0f + 26.0f * 4 + 9.0f * 2 + 13.0f }, 0);
                  ui.Update();
              } },
            // 输入框的编辑菜单：选中全部文字后右键
            { "gallery_textbox_menu", { 900, 780 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PageBasic);
                  auto* box = FindNode<TextBox>(g, [](TextBox* t) { return t->GetPlaceholder() == "单行输入框"; });
                  box->SetText("LINE 0,0 100,100");
                  box->Focus();
                  box->SelectAll();
                  ui.Update();
                  const Vec2 p = box->GetScreenBounds().Center();
                  ui.PointerMove(p, 0);
                  ui.PointerDown(p, MouseButton::Right, 0, 1);
                  ui.PointerUp(p, MouseButton::Right, 0);
                  ui.Update();
              } },
            { "gallery_combo_open", { 900, 560 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PageBasic);
                  auto* combo = FindNode<ComboBox>(g, [](ComboBox* c) { return c->GetItems().size() > 10; });
                  combo->Open();
                  ui.Update();
                  ui.KeyDown(Key::Down, 0);
                  ui.KeyDown(Key::Down, 0);
                  ui.Update();
              } },
            // 线型下拉框：每项显示线型图案 + 名称 + 说明
            { "gallery_linetype_open", { 900, 700 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PageBasic);
                  auto* combo = FindNode<ComboBox>(g, [](ComboBox* c) { return c->GetItems().size() == 9; });
                  combo->Open();
                  ui.Update();
                  ui.KeyDown(Key::Down, 0);
                  ui.Update();
              } },
            // 图层表（多列、图标列、线型/线宽预览、当前层 ✓）+ 右侧特性面板
            { "gallery_layers", { 1280, 640 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PageLayers);
                  g->GetLayerTable()->Focus();
                  g->GetLayerTable()->SetCurrent(1);
                  ui.Update();
              } },
            { "gallery_layers_150", { 1280, 640 }, 1.5f, true,
              [](UIContext& ui, IRenderBackend&, auto&) { BuildGalleryScene(ui, GalleryRoot::PageLayers); } },
            // 图层表在位编辑名称单元格
            { "gallery_layer_edit", { 900, 400 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PageLayers);
                  ListView* table = g->GetLayerTable();
                  table->Focus();
                  table->SetCurrent(2);
                  ui.Update();
                  table->BeginEdit(2, 1);
                  ui.Update();
                  ui.TextInput("门窗-外");
                  ui.Update();
              } },
            // 多文档：一个标签已修改（●），在第二个标签上打开右键菜单
            { "gallery_documents", { 900, 400 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PageDocs);
                  TabView* docs = g->GetDocuments();
                  docs->SetTabModified(0, true);
                  g->NewDocument();
                  ui.Update();
                  const Rect r = docs->GetScreenBounds();
                  const Vec2 p{ r.min.x + 200.0f, r.min.y + TabView::kStripHeight * 0.5f };
                  ui.PointerMove(p, 0);
                  ui.PointerDown(p, MouseButton::Right, 0, 1);
                  ui.PointerUp(p, MouseButton::Right, 0);
                  ui.Update();
              } },
            // 很多标签时：标签条滚动，右侧出现 ⌄ 列表按钮
            { "gallery_documents_overflow", { 700, 300 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PageDocs);
                  for (int i = 0; i < 6; ++i)
                      g->NewDocument();
                  ui.Update();
              } },
            { "gallery_autocomplete", { 900, 560 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  BuildGalleryScene(ui, GalleryRoot::PageCommand);
                  for (int i = 0; i < 20 && !ui.WantsTextInput(); ++i)
                      ui.KeyDown(Key::Tab, 0);
                  ui.TextInput("L");
                  ui.Update();
                  ui.KeyDown(Key::Down, 0);
                  ui.Update();
              } },
            { "gallery_tooltip", { 900, 400 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  static uint32_t now = 0;
                  now = 1000;
                  ui.SetClock([] { return now; });
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PageBasic);
                  auto* b = FindNode<Button>(g, [](Button* x) { return x->GetTooltip().rfind("直线", 0) == 0; });
                  ui.PointerMove(b->GetScreenBounds().Center(), 0);
                  now += 600;
                  ui.Tick();
                  ui.Update();
              } },

            // 事件 → 状态 → 绘制：侧栏第 3 项处于按下状态
            { "demo_ui_pressed", { 900, 560 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend& b, auto& t)
              {
                  BuildDemo(ui, b, t);
                  const Vec2 item3{ 110.0f, 41.0f + 12.0f + 2 * 44.0f + 16.0f };
                  ui.PointerMove(item3, 0);
                  ui.PointerDown(item3, MouseButton::Left, 0);
              } },

            // 事件 → 布局：点"−"两次移除两项，再点右上角按钮隐藏侧栏
            { "demo_ui_sidebar_hidden", { 900, 560 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend& b, auto& t)
              {
                  BuildDemo(ui, b, t);
                  Click(ui, { 58.0f, 20.0f });
                  Click(ui, { 58.0f, 20.0f });
                  Click(ui, { 900.0f - 22.0f, 20.0f });
                  ui.PointerLeave();
              } },

            // ── M10.2：无边框主窗口的标题栏 + 多文档标签条 ─────────────
            { "main_titlebar", { 760, 120 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&) { BuildMainWindowUi(ui, false, true); } },
            { "main_titlebar_150", { 760, 120 }, 1.5f, true,
              [](UIContext& ui, IRenderBackend&, auto&) { BuildMainWindowUi(ui, true, false); } },
            { "main_titlebar_light", { 760, 120 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&) { ui.SetTheme(ThemeColors::Light()); BuildMainWindowUi(ui, true, true); } },

            // ── M10.3：命令行 ───────────────────────────────────────
            { "command_console", { 640, 300 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&) { BuildConsoleScene(ui); } },
            { "command_console_light", { 640, 300 }, 1.5f, true,
              [](UIContext& ui, IRenderBackend&, auto&) { ui.SetTheme(ThemeColors::Light()); BuildConsoleScene(ui); } },

            // ── M10.1：停靠区 ───────────────────────────────────────
            { "dock_layout", { 900, 500 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&) { BuildDockScene(ui, false); } },
            { "dock_layout_150", { 900, 500 }, 1.5f, true,
              [](UIContext& ui, IRenderBackend&, auto&) { BuildDockScene(ui, false); } },
            { "dock_drag_preview", { 900, 500 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&) { BuildDockScene(ui, true); } },
            { "dock_drag_preview_light", { 900, 500 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&) { ui.SetTheme(ThemeColors::Light()); BuildDockScene(ui, true); } },
            // ── M9：界面描述文件 ─────────────────────────────────────
            // 横向工具栏宽度不够：撤销、重做（禁用）、正交（选中）、带文字的按钮，后几个折叠到 »；竖向工具栏，当前工具选中
            { "ui_json_toolbars", { 480, 320 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&) { BuildJsonUi(ui, false); } },
            { "ui_json_toolbars_150", { 480, 320 }, 1.5f, true,
              [](UIContext& ui, IRenderBackend&, auto&) { BuildJsonUi(ui, false); } },
            // 点击 » 列出折叠的命令
            { "ui_json_overflow", { 480, 320 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&) { BuildJsonUi(ui, true); } },
            // ── M8：浅色主题 ───────────────────────────────────────
            { "gallery_basic_light", { 1280, 780 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  ui.SetTheme(ThemeColors::Light());
                  BuildGalleryScene(ui, GalleryRoot::PageBasic);
              } },
            // 先按深色构建并操作，最后才切换主题：画面必须与 gallery_basic_light 完全相同
            { "gallery_basic_switched", { 1280, 780 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  BuildGalleryScene(ui, GalleryRoot::PageBasic);
                  ui.SetTheme(ThemeColors::Light());
              } },
            { "gallery_list_light", { 1100, 600 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  ui.SetTheme(ThemeColors::Light());
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PageList);
                  if (auto* list = FindNode<ListView>(g, [](ListView* l) { return l->GetItemCount() == 10000; }))
                  {
                      list->Focus();
                      list->SetCurrent(3);
                      ui.KeyDown(Key::Down, Mods(ModifierKey::Shift));
                  }
                  if (auto* tree = FindNode<TreeView>(g, [](TreeView*) { return true; }))
                      tree->GetRootItem()->GetChildren()[1]->SetExpanded(true);
              } },
            { "gallery_layers_light", { 1280, 640 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  ui.SetTheme(ThemeColors::Light());
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PageLayers);
                  g->GetLayerTable()->Focus();
                  g->GetLayerTable()->SetCurrent(1);
                  ui.Update();
              } },
            { "gallery_menu_light", { 900, 500 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  ui.SetTheme(ThemeColors::Light());
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PageBasic);
                  g->GetMenuBar()->OpenMenu(2, true);      // 视图
                  ui.Update();
                  ui.KeyDown(Key::Down, 0);
                  ui.KeyDown(Key::Down, 0);                // "主题"（有子菜单，打开时第一项已高亮）
                  ui.KeyDown(Key::Right, 0);
                  ui.Update();
              } },
            { "gallery_dialog_light", { 900, 560 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  ui.SetTheme(ThemeColors::Light());
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PagePopup);
                  g->OpenLayerDialog();
                  ui.Update();
              } },
            { "gallery_documents_light", { 900, 400 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  ui.SetTheme(ThemeColors::Light());
                  GalleryRoot* g = BuildGalleryScene(ui, GalleryRoot::PageDocs);
                  g->GetDocuments()->SetTabModified(0, true);
                  g->NewDocument();
                  ui.Update();
              } },
            { "gallery_command_light", { 900, 560 }, 1.0f, true,
              [](UIContext& ui, IRenderBackend&, auto&)
              {
                  ui.SetTheme(ThemeColors::Light());
                  BuildGalleryScene(ui, GalleryRoot::PageCommand);
                  for (int i = 0; i < 20 && !ui.WantsTextInput(); ++i)
                      ui.KeyDown(Key::Tab, 0);
                  ui.TextInput("L");
                  ui.Update();
                  ui.KeyDown(Key::Down, 0);
                  ui.Update();
              } },
        };
    }

    // ── 渲染 ─────────────────────────────────────────────────────
    // 在给定后端上构建场景并渲染一帧；begin 负责准备渲染目标，end 负责取回图像
    SoftwareImage RenderScene(const Scene& scene, IRenderBackend& backend,
                              const std::function<void(int, int)>& begin,
                              const std::function<SoftwareImage()>& end)
    {
        const int w = static_cast<int>(std::lround(scene.size.x * scene.scale));
        const int h = static_cast<int>(std::lround(scene.size.y * scene.scale));

        std::vector<TextureId> textures;
        SoftwareImage image;
        {
            UIContext ui(&backend);
            ui.SetDisplaySize(scene.size, scene.scale);
            if (g_fonts.ui)
                ui.GetTextSystem().AddFont(g_fonts.ui);
            if (g_fonts.symbol)
                ui.GetTextSystem().AddFallbackFont(g_fonts.symbol);
            scene.build(ui, backend, textures);
            ui.Update();

            begin(w, h);
            ui.Render();
            image = end();
        }
        for (TextureId id : textures)
            backend.DestroyTexture(id);
        return image;
    }

    // ── 比较 ─────────────────────────────────────────────────────
    struct CompareResult
    {
        bool   sizeMatch = false;
        int    maxDiff   = 0;       // 所有像素、所有通道中的最大差值
        size_t badPixels = 0;       // 任一通道差值超过阈值的像素数
        size_t total     = 0;
    };

    CompareResult Compare(const SoftwareImage& actual, const SoftwareImage& expected, int threshold, SoftwareImage* diff)
    {
        CompareResult r;
        r.sizeMatch = actual.width == expected.width && actual.height == expected.height;
        if (!r.sizeMatch)
            return r;

        r.total = actual.pixels.size();
        if (diff)
        {
            diff->width  = actual.width;
            diff->height = actual.height;
            diff->pixels.resize(r.total);
        }

        for (size_t i = 0; i < r.total; ++i)
        {
            const uint32_t a = actual.pixels[i];
            const uint32_t e = expected.pixels[i];
            int d = 0;
            for (int ch = 0; ch < 32; ch += 8)
                d = std::max(d, std::abs(static_cast<int>((a >> ch) & 0xFF) - static_cast<int>((e >> ch) & 0xFF)));
            r.maxDiff = std::max(r.maxDiff, d);
            const bool bad = d > threshold;
            if (bad)
                ++r.badPixels;

            if (diff)
            {
                // 差异图：超阈值为红色，其余为变暗的灰度期望图
                const uint32_t lum = (((e & 0xFF) + ((e >> 8) & 0xFF) + ((e >> 16) & 0xFF)) / 3) / 3;
                diff->pixels[i] = bad ? RGBA(255, 40, 40) : RGBA(static_cast<uint8_t>(lum), static_cast<uint8_t>(lum), static_cast<uint8_t>(lum));
            }
        }
        return r;
    }

    // ── 容差 ─────────────────────────────────────────────────────
    // 软件光栅 vs 基准：确定性结果，只允许 1 级的舍入差异
    constexpr int    kBaselineThreshold = 1;
    constexpr double kBaselineMaxBad    = 0.0;

    // D3D11 vs 软件光栅：GPU 的插值、采样权重精度与软件实现有细微差别。
    // 实测（GTX 1060 与 WARP）所有像素差值都不超过 1；阈值留一点余量给其他厂商的显卡，
    // 只允许极少数像素超过阈值
    constexpr int    kCrossThreshold = 3;
    constexpr double kCrossMaxBad    = 0.0005;  // 0.05%

    bool Passed(const CompareResult& r, double maxBadRatio)
    {
        return r.sizeMatch && static_cast<double>(r.badPixels) <= maxBadRatio * static_cast<double>(r.total);
    }

    std::string Describe(const CompareResult& r)
    {
        if (!r.sizeMatch)
            return "尺寸不一致";
        char buf[128];
        std::snprintf(buf, sizeof(buf), "最大差值 %3d，超阈值像素 %zu（%.4f%%）",
                      r.maxDiff, r.badPixels, 100.0 * static_cast<double>(r.badPixels) / static_cast<double>(r.total));
        return buf;
    }
}

int main(int argc, char** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetDemoLogEnabled(false);

    // 调试版的断言失败默认弹出模态对话框，会卡住自动测试：改为输出到控制台
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    bool update = false, useD3D11 = true, saveAll = false, forceWarp = false;
    std::string filter;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--update") == 0)        update = true;
        else if (std::strcmp(argv[i], "--no-d3d11") == 0) useD3D11 = false;
        else if (std::strcmp(argv[i], "--save-all") == 0) saveAll = true;
        else if (std::strcmp(argv[i], "--warp") == 0)     forceWarp = true;
        else                                              filter = argv[i];
    }

    const std::filesystem::path baselineDir = MINIGUI_GOLDEN_DIR;
    const std::filesystem::path outDir      = MINIGUI_GOLDEN_OUT;
    std::filesystem::create_directories(baselineDir);
    std::filesystem::create_directories(outDir);

    // ── 字体与指纹 ──────────────────────────────────────────────
    g_fonts.ui     = LoadSystemUIFont();
    g_fonts.symbol = LoadSystemSymbolFont();
    bool fontsOk = g_fonts.Available();
    const std::filesystem::path fingerprintPath = baselineDir / "fonts.txt";
    if (!fontsOk)
    {
        std::printf("没有找到系统界面字体，跳过文字场景\n");
    }
    else if (update)
    {
        std::ofstream(fingerprintPath) << g_fonts.Fingerprint() << "\n";
    }
    else
    {
        std::string recorded;
        std::ifstream(fingerprintPath) >> recorded;
        if (recorded != g_fonts.Fingerprint())
        {
            std::printf("系统字体与生成基准时不同（指纹 %s，基准 %s），跳过文字场景\n",
                        g_fonts.Fingerprint().c_str(), recorded.empty() ? "缺失" : recorded.c_str());
            fontsOk = false;
        }
    }

    SoftwareBackend software;
    D3D11Capture    d3d11;
    if (useD3D11 && !d3d11.Initialize(forceWarp))
    {
        std::printf("D3D11 设备创建失败，跳过 D3D11 比较\n");
        useD3D11 = false;
    }
    if (useD3D11)
        std::printf("D3D11 适配器：%s\n", d3d11.GetAdapterName().c_str());
    std::printf("基准目录：%s\n\n", baselineDir.string().c_str());

    int failures = 0;
    int ran      = 0;
    int skipped  = 0;
    for (const Scene& scene : MakeScenes())
    {
        if (!filter.empty() && std::string(scene.name).find(filter) == std::string::npos)
            continue;
        if (scene.needsFont && !fontsOk)
        {
            std::printf("%-24s 跳过（字体不可用或与基准不一致）\n", scene.name);
            ++skipped;
            continue;
        }
        ++ran;

        const std::string baselinePath = (baselineDir / (std::string(scene.name) + ".png")).string();
        const std::string outPrefix    = (outDir / scene.name).string();

        const SoftwareImage sw = RenderScene(scene, software,
            [&](int w, int h) { software.BeginFrame(w, h, kClearColor); },
            [&] { return software.GetImage(); });

        std::printf("%-24s %4d×%-4d\n", scene.name, sw.width, sw.height);
        if (saveAll)
            WritePng(outPrefix + ".software.png", sw);

        // ── 软件光栅 vs 基准 ────────────────────────────────────
        if (update)
        {
            const bool ok = WritePng(baselinePath, sw);
            std::printf("  基准     %s\n", ok ? "已更新" : "写入失败");
            if (!ok) ++failures;
        }
        else
        {
            SoftwareImage baseline;
            if (!ReadPng(baselinePath, baseline))
            {
                std::printf("  基准     缺少基准图片，请先运行 MiniGUIGolden --update 并检查结果\n");
                WritePng(outPrefix + ".software.png", sw);
                ++failures;
            }
            else
            {
                SoftwareImage diff;
                const CompareResult r = Compare(sw, baseline, kBaselineThreshold, &diff);
                const bool ok = Passed(r, kBaselineMaxBad);
                std::printf("  基准     %s  %s\n", ok ? "通过" : "失败", Describe(r).c_str());
                if (!ok)
                {
                    ++failures;
                    WritePng(outPrefix + ".software.png", sw);
                    if (r.sizeMatch)
                        WritePng(outPrefix + ".baseline_diff.png", diff);
                }
            }
        }

        // ── D3D11 vs 软件光栅 ───────────────────────────────────
        if (useD3D11)
        {
            const SoftwareImage gpu = RenderScene(scene, d3d11.GetBackend(),
                [&](int w, int h) { d3d11.BeginFrame(w, h, kClearColor); },
                [&] { return d3d11.Readback(); });

            SoftwareImage diff;
            const CompareResult r = Compare(gpu, sw, kCrossThreshold, &diff);
            const bool ok = Passed(r, kCrossMaxBad);
            std::printf("  D3D11    %s  %s\n", ok ? "通过" : "失败", Describe(r).c_str());
            if (!ok)
                ++failures;
            if (!ok || saveAll)
            {
                WritePng(outPrefix + ".d3d11.png", gpu);
                WritePng(outPrefix + ".d3d11_diff.png", diff);
            }
        }
    }

    // 一个场景都没跑（例如参数拼错被当成了过滤条件）不能算通过
    if (ran == 0 && skipped == 0)
    {
        std::printf("没有匹配“%s”的场景\n", filter.c_str());
        return 1;
    }

    std::printf("\n%s（运行 %d 个场景，跳过 %d 个，失败 %d 项）\n",
                failures == 0 ? "全部通过" : "存在失败", ran, skipped, failures);
    if (failures)
        std::printf("实际图片与差异图：%s\n", outDir.string().c_str());
    return failures == 0 ? 0 : 1;
}
