#include "Gallery.h"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include "Widgets/AutoComplete.h"
#include "Widgets/Button.h"
#include "Widgets/CadPreview.h"
#include "Widgets/ColorPicker.h"
#include "Widgets/ComboBox.h"
#include "Widgets/Dialog.h"
#include "Widgets/Label.h"
#include "Widgets/ListView.h"
#include "Widgets/Menu.h"
#include "Widgets/NumberBox.h"
#include "Widgets/Panel.h"
#include "Widgets/PropertyGrid.h"
#include "Widgets/ScrollView.h"
#include "Widgets/Splitter.h"
#include "Widgets/TabView.h"
#include "Widgets/TextBox.h"
#include "Style/Theme.hpp"
#include <algorithm>
#include <cstdio>

namespace MiniGUI
{
    namespace
    {
        const uint8_t kCtrl = Mods(ModifierKey::Ctrl);

        LayoutStyle RowStyle(float gap = 12.0f)
        {
            LayoutStyle s;
            s.direction  = FlexDirection::Row;
            s.alignItems = Align::Center;
            s.gap        = gap;
            return s;
        }

        LayoutStyle ColumnStyle(float padding, float gap)
        {
            LayoutStyle s;
            s.padding = Edges::All(padding);
            s.gap     = gap;
            return s;
        }

        Node* AddRow(Node* parent, float gap = 12.0f)
        {
            Node* row = parent->AddChild<Node>();
            row->SetLayoutStyle(RowStyle(gap));
            return row;
        }

        void AddSection(Node* parent, const char* title)
        {
            parent->AddChild<Label>(title, 13.0f, Theme::TextDim)->EditLayoutStyle().margin = Edges::Make(0, 6, 0, 0);
        }

        Label* AddFieldLabel(Node* parent, const char* text, float width = 0.0f)
        {
            Label* l = parent->AddChild<Label>(text);
            if (width > 0.0f)
                l->EditLayoutStyle().width = width;
            return l;
        }

        TextParams CellText(ColorRef color = Theme::Text)
        {
            TextParams p;
            p.size     = Theme::FontSize;
            p.color    = color;
            p.vAlign   = TextAlign::Center;
            p.ellipsis = true;
            return p;
        }

        std::string ColorName(Color32 c)
        {
            struct Named { uint32_t rgb; const char* name; };
            static const Named names[] =
            {
                { 0xFF0000, "红" }, { 0xFFFF00, "黄" }, { 0x00FF00, "绿" }, { 0x00FFFF, "青" },
                { 0x0000FF, "蓝" }, { 0xFF00FF, "洋红" }, { 0xFFFFFF, "白" }, { 0x808080, "8" }, { 0xC0C0C0, "9" },
            };
            const uint32_t rgb = ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
            for (const auto& n : names)
                if (n.rgb == rgb)
                    return n.name;
            char buf[16];
            std::snprintf(buf, sizeof(buf), "#%06X", rgb);
            return buf;
        }

        std::string LineweightName(float mm)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f 毫米", mm);
            return buf;
        }

        // 右键菜单演示区域：用通用的 AttachContextMenu 挂菜单
        class ContextArea : public Panel
        {
        public:
            ContextArea() : Panel(Theme::PanelAlt)
            {
                SetBorder(Theme::BorderSubtle, 1.0f);
                SetRounding(6.0f);
                LayoutStyle s;
                s.height     = 150.0f;
                s.justify    = Justify::Center;
                s.alignItems = Align::Center;
                SetLayoutStyle(s);
                AddChild<Label>("在此区域单击右键（或获得焦点后按菜单键 / Shift+F10）打开菜单", 14.0f, Theme::TextDim);
                SetFocusable(true);
            }
        };

        // AutoCAD 常用命令（英文名 + 中文名），供命令行补全
        struct CommandInfo { const char* name; const char* title; };
        const CommandInfo kCommands[] =
        {
            { "LINE", "直线" }, { "LAYER", "图层特性" }, { "LENGTHEN", "拉长" }, { "LIST", "列表" },
            { "CIRCLE", "圆" }, { "COPY", "复制" }, { "CHAMFER", "倒角" }, { "ARC", "圆弧" },
            { "ARRAY", "阵列" }, { "MOVE", "移动" }, { "MIRROR", "镜像" }, { "MTEXT", "多行文字" },
            { "OFFSET", "偏移" }, { "PLINE", "多段线" }, { "RECTANG", "矩形" }, { "ROTATE", "旋转" },
            { "SCALE", "缩放" }, { "TRIM", "修剪" }, { "EXTEND", "延伸" }, { "ERASE", "删除" },
            { "ZOOM", "视图缩放" }, { "DIMLINEAR", "线性标注" }, { "HATCH", "图案填充" }, { "BLOCK", "创建块" },
        };

        // 图层表的列
        enum LayerColumn { ColCurrent, ColName, ColOn, ColFreeze, ColLock, ColColor, ColLinetype, ColLineweight };
    }

    // =========================================================
    // 构建
    // =========================================================
    GalleryRoot* BuildGallery(UIContext& ui, const std::string& assetsDir)
    {
        GalleryRoot* root = ui.GetRoot()->AddChild<GalleryRoot>(assetsDir);
        root->Populate();
        root->RegisterShortcuts(ui.GetShortcuts());
        return root;
    }

    GalleryRoot::GalleryRoot(std::string assetsDir)
        : m_assetsDir(std::move(assetsDir))
    {
        LayoutStyle s;
        s.direction = FlexDirection::Column;
        SetLayoutStyle(s);

        m_layers =
        {
            { "0",    ColorFromHex(0xFFFFFF), 0, 0.25f },
            { "墙体", ColorFromHex(0xFF0000), 0, 0.50f },
            { "门窗", ColorFromHex(0xFFFF00), 0, 0.25f },
            { "标注", ColorFromHex(0x00FF00), 0, 0.18f },
            { "轴线", ColorFromHex(0x00FFFF), 3, 0.13f },
            { "隐藏", ColorFromHex(0x808080), 2, 0.18f },
            { "填充", ColorFromHex(0x8080FF), 0, 0.09f },
        };
        m_layers[5].on     = false;
        m_layers[6].frozen = true;
        m_layers[3].locked = true;
    }

    std::string GalleryRoot::IconPath(const char* name) const
    {
        return m_assetsDir + "/icons/" + name + ".png";
    }

    void GalleryRoot::SetStatus(std::string text)
    {
        if (m_status)
            m_status->SetText(std::move(text));
    }

    void GalleryRoot::Populate()
    {
        BuildMenuBar();
        AddChild<Separator>();

        m_tabs = AddChild<TabView>();
        m_tabs->EditLayoutStyle().grow = 1.0f;
        m_tabs->AddTab("基础控件",     std::unique_ptr<Node>(BuildBasicPage()));
        m_tabs->AddTab("列表与树",     std::unique_ptr<Node>(BuildListPage()));
        m_tabs->AddTab("图层与特性",   std::unique_ptr<Node>(BuildLayerPage()));
        m_tabs->AddTab("多文档",       std::unique_ptr<Node>(BuildDocumentPage()));
        m_tabs->AddTab("弹层与对话框", std::unique_ptr<Node>(BuildPopupPage()));
        m_tabs->AddTab("命令行",       std::unique_ptr<Node>(BuildCommandPage()));

        AddChild<Separator>();
        Panel* statusBar = AddChild<Panel>(Theme::Panel);
        {
            LayoutStyle s = RowStyle();
            s.height  = 26.0f;
            s.padding = Edges::Symmetric(12.0f, 0.0f);
            s.shrink  = 0.0f;
            statusBar->SetLayoutStyle(s);
        }
        m_status = statusBar->AddChild<Label>("就绪", 12.0f, Theme::TextDim);
        m_status->EditLayoutStyle().grow = 1.0f;
        m_status->SetEllipsis(true);
        statusBar->AddChild<Label>("Alt / F10 菜单栏  ·  Ctrl+1～6 切换页面  ·  Ctrl+T 深色/浅色  ·  F1 关于", 12.0f, Theme::TextDim);
    }

    void GalleryRoot::RegisterShortcuts(ShortcutTable& shortcuts)
    {
        for (int i = 0; i < PageCount; ++i)
            shortcuts.Register(static_cast<Key>(static_cast<int>(Key::Num1) + i), kCtrl, [this, i] { m_tabs->SetSelected(i); });
        shortcuts.Register(Key::N, kCtrl, [this] { m_tabs->SetSelected(PageDocs); NewDocument(); });
        shortcuts.Register(Key::O, kCtrl, [this] { SetStatus("快捷键：打开 (Ctrl+O)"); });
        shortcuts.Register(Key::T, kCtrl, [this] { SetTheme(!IsLightTheme()); });
        ShortcutTable::Options fn;
        fn.allowInTextInput = true;
        shortcuts.Register(Key::F1, 0, [this]
        {
            ShowMessageBox(*GetContext(), "关于 MiniGUI", "MiniGUI 是为 MiniCAD 编写的保留模式界面库，使用 D3D11 渲染，用来替换 Dear ImGui。");
        }, fn);
    }

    void GalleryRoot::OnPaint(DrawList& dl, const Rect& screenRect)
    {
        dl.AddRectFilled(screenRect, Theme::Background);
    }

    bool GalleryRoot::IsLightTheme() const
    {
        const UIContext* ctx = GetContext();
        return ctx && !ctx->GetTheme().IsDark();
    }

    void GalleryRoot::SetTheme(bool light)
    {
        UIContext* ctx = GetContext();
        if (!ctx)
            return;
        ctx->SetTheme(light ? ThemeColors::Light() : ThemeColors::Dark());
        SetStatus(light ? "主题：浅色" : "主题：深色");
    }

    void GalleryRoot::BuildMenuBar()
    {
        m_menuBar = AddChild<MenuBar>();
        m_menuBar->AddMenu("文件(&F)", {
            MenuItem("新建(&N)", [this] { m_tabs->SetSelected(PageDocs); NewDocument(); }, "Ctrl+N"),
            MenuItem("打开(&O)…", [this] { SetStatus("菜单：打开"); }, "Ctrl+O"),
            MenuItem::Sub("最近打开(&R)", {
                MenuItem("&1 D:\\Drawings\\建筑平面图.mcad", [this] { SetStatus("打开：建筑平面图.mcad"); }),
                MenuItem("&2 D:\\Drawings\\结构详图.mcad",   [this] { SetStatus("打开：结构详图.mcad"); }),
            }),
            MenuItem::Separator(),
            MenuItem("保存(&S)", [this] { OpenSaveMessageBox(); }, "Ctrl+S"),
            MenuItem("退出(&X)", [this] { SetStatus("菜单：退出（示例中不执行）"); }, "Alt+F4"),
        });
        m_menuBar->AddMenu("编辑(&E)", {
            MenuItem("撤销(&U)", [this] { SetStatus("菜单：撤销"); }, "Ctrl+Z"),
            MenuItem("重做(&R)", [this] { SetStatus("菜单：重做"); }, "Ctrl+Y"),
            MenuItem::Separator(),
            MenuItem("复制(&C)", [this] { SetStatus("菜单：复制"); }, "Ctrl+C"),
            MenuItem("粘贴(&P)", {}, "Ctrl+V").Enabled(false),
        });
        m_menuBar->AddMenu("视图(&V)", {
            MenuItem("显示栅格(&G)", [this] { m_showGrid = !m_showGrid; m_gridCheck->SetChecked(m_showGrid); })
                .CheckedIf([this] { return m_showGrid; }),
            MenuItem("显示线宽(&L)", [this] { m_showLineweight = !m_showLineweight; m_lineweightCheck->SetChecked(m_showLineweight); })
                .CheckedIf([this] { return m_showLineweight; }),
            MenuItem::Separator(),
            MenuItem::Sub("主题(&T)", {
                MenuItem("深色(&D)", [this] { SetTheme(false); }).CheckedIf([this] { return !IsLightTheme(); }),
                MenuItem("浅色(&L)", [this] { SetTheme(true); }).CheckedIf([this] { return IsLightTheme(); }),
                MenuItem::Separator(),
                MenuItem("切换深色/浅色", [this] { SetTheme(!IsLightTheme()); }, "Ctrl+T"),
            }),
            MenuItem::Sub("页面(&P)", {
                MenuItem("基础控件(&1)",     [this] { m_tabs->SetSelected(PageBasic); },   "Ctrl+1"),
                MenuItem("列表与树(&2)",     [this] { m_tabs->SetSelected(PageList); },    "Ctrl+2"),
                MenuItem("图层与特性(&3)",   [this] { m_tabs->SetSelected(PageLayers); },  "Ctrl+3"),
                MenuItem("多文档(&4)",       [this] { m_tabs->SetSelected(PageDocs); },    "Ctrl+4"),
                MenuItem("弹层与对话框(&5)", [this] { m_tabs->SetSelected(PagePopup); },   "Ctrl+5"),
                MenuItem("命令行(&6)",       [this] { m_tabs->SetSelected(PageCommand); }, "Ctrl+6"),
            }),
        });
        m_menuBar->AddMenu("帮助(&H)", {
            MenuItem("关于 MiniGUI(&A)…", [this]
            {
                ShowMessageBox(*GetContext(), "关于 MiniGUI", "MiniGUI 是为 MiniCAD 编写的保留模式界面库，使用 D3D11 渲染，用来替换 Dear ImGui。");
            }, "F1"),
        });
    }

    // =========================================================
    // CAD 风格的下拉框：线型、线宽、图层
    // =========================================================
    ComboBox* GalleryRoot::MakeLinetypeCombo(Node* parent, int selected)
    {
        std::vector<std::string> names;
        for (const auto& lt : StandardLinetypes())
            names.push_back(lt.name);
        ComboBox* combo = parent->AddChild<ComboBox>(std::move(names), selected);
        combo->SetDropDownWidth(380.0f);
        combo->SetItemPainter([combo](DrawList& dl, const Rect& box, int index, bool inList)
        {
            const LinetypeDef& lt = StandardLinetypes()[static_cast<size_t>(index)];
            const float sampleW = inList ? 110.0f : std::min(70.0f, box.Width() * 0.45f);
            DrawLinetypeSample(dl, Rect{ box.min.x, box.min.y, box.min.x + sampleW, box.max.y }, lt.pattern, Theme::Text);
            if (UIContext* ctx = combo->GetContext())
            {
                const Rect textBox{ box.min.x + sampleW + 10.0f, box.min.y, box.max.x, box.max.y };
                ctx->GetTextSystem().Draw(dl, textBox, inList ? lt.name + "  " + lt.description : lt.name, CellText());
            }
        });
        return combo;
    }

    ComboBox* GalleryRoot::MakeLineweightCombo(Node* parent, float selected)
    {
        const auto& weights = StandardLineweights();
        std::vector<std::string> names;
        int sel = 0;
        for (size_t i = 0; i < weights.size(); ++i)
        {
            names.push_back(LineweightName(weights[i]));
            if (std::abs(weights[i] - selected) < 0.001f)
                sel = static_cast<int>(i);
        }
        ComboBox* combo = parent->AddChild<ComboBox>(std::move(names), sel);
        combo->SetItemPainter([combo](DrawList& dl, const Rect& box, int index, bool)
        {
            const float mm = StandardLineweights()[static_cast<size_t>(index)];
            DrawLineweightSample(dl, Rect{ box.min.x, box.min.y, box.min.x + 60.0f, box.max.y }, mm, Theme::Text);
            if (UIContext* ctx = combo->GetContext())
                ctx->GetTextSystem().Draw(dl, Rect{ box.min.x + 70.0f, box.min.y, box.max.x, box.max.y }, LineweightName(mm), CellText());
        });
        return combo;
    }

    ComboBox* GalleryRoot::MakeLayerCombo(Node* parent)
    {
        ComboBox* combo = parent->AddChild<ComboBox>();
        combo->SetDropDownWidth(220.0f);
        combo->SetItemPainter([this, combo](DrawList& dl, const Rect& box, int index, bool)
        {
            if (index < 0 || index >= static_cast<int>(m_layers.size()))
                return;
            const Layer& l = m_layers[static_cast<size_t>(index)];
            float x = box.min.x;
            DrawLayerOnIcon(dl, Rect{ x, box.min.y, x + 16.0f, box.max.y }, l.on);           x += 20.0f;
            DrawLayerFreezeIcon(dl, Rect{ x, box.min.y, x + 16.0f, box.max.y }, l.frozen);   x += 20.0f;
            const float cy = box.Center().y;
            DrawColorSwatch(dl, Rect{ x, cy - 6.0f, x + 12.0f, cy + 6.0f }, l.color);       x += 20.0f;
            if (UIContext* ctx = combo->GetContext())
                ctx->GetTextSystem().Draw(dl, Rect{ x, box.min.y, box.max.x, box.max.y }, l.name, CellText());
        });
        combo->SetOnChanged([this](int i)
        {
            m_currentLayer = i;
            RefreshLayerViews();
            SetStatus("当前图层：" + m_layers[static_cast<size_t>(i)].name);
        });
        m_layerCombos.push_back(combo);
        return combo;
    }

    // =========================================================
    // 页面 1：基础控件
    // =========================================================
    Node* GalleryRoot::BuildBasicPage()
    {
        auto* scroll = new ScrollView();
        Node* col = scroll->SetContent<Node>();
        col->SetLayoutStyle(ColumnStyle(20.0f, 12.0f));

        // 按钮
        AddSection(col, "按钮与图标按钮（悬停可看提示；图标按显示尺寸重采样，任何缩放下都清晰）");
        Node* row = AddRow(col);
        row->AddChild<Button>("普通按钮", [this] { SetStatus("点击：普通按钮"); });
        Button* primary = row->AddChild<Button>("默认按钮", [this] { SetStatus("点击：默认按钮"); });
        primary->SetStyle(ButtonStyle::Primary());
        row->AddChild<Button>("禁用按钮", [] {})->SetEnabled(false);
        row->AddChild<Separator>(true)->EditLayoutStyle().height = 24.0f;

        const struct { const char* icon; const char* tip; } tools[] =
        {
            { "Line", "直线 (L)\n指定两点绘制直线段" }, { "Circle", "圆 (C)" }, { "Arc", "圆弧 (A)" },
            { "Rect", "矩形 (REC)" }, { "Pline", "多段线 (PL)" }, { "Move", "移动 (M)" }, { "Rotate", "旋转 (RO)" },
        };
        for (const auto& t : tools)
        {
            Button* b = row->AddChild<Button>([this, t] { SetStatus(std::string("工具：") + t.icon); });
            b->SetStyle(ButtonStyle::Flat());
            b->SetLayoutStyle([] { LayoutStyle s = RowStyle(0); s.justify = Justify::Center; s.width = 32; s.height = 32; return s; }());
            b->SetIcon(IconPath(t.icon), { 22.0f, 22.0f });
            b->SetTooltip(t.tip);
        }
        Button* undo = row->AddChild<Button>("撤销", [this] { SetStatus("点击：撤销"); });
        undo->SetIcon(IconPath("Undo"), { 16.0f, 16.0f });

        // 复选框与单选按钮
        AddSection(col, "复选框与单选按钮");
        row = AddRow(col, 20.0f);
        m_gridCheck = row->AddChild<CheckBox>("显示栅格", m_showGrid);
        m_gridCheck->SetOnChanged([this](bool on) { m_showGrid = on; SetStatus(on ? "显示栅格：开" : "显示栅格：关"); });
        m_lineweightCheck = row->AddChild<CheckBox>("显示线宽", m_showLineweight);
        m_lineweightCheck->SetOnChanged([this](bool on) { m_showLineweight = on; });
        CheckBox* partial = row->AddChild<CheckBox>("部分图层可见");
        partial->SetState(CheckBox::State::Indeterminate);
        row->AddChild<CheckBox>("禁用选项", true)->SetEnabled(false);
        row->AddChild<Separator>(true)->EditLayoutStyle().height = 20.0f;
        row->AddChild<Label>("单位：");
        row->AddChild<RadioButton>(&m_units, 0, "毫米");
        row->AddChild<RadioButton>(&m_units, 1, "厘米");
        row->AddChild<RadioButton>(&m_units, 2, "米");
        m_units.SetValue(0);
        m_units.SetOnChanged([this](int v)
        {
            const char* names[] = { "毫米", "厘米", "米" };
            SetStatus(std::string("单位：") + names[v]);
        });

        // 滑块与进度条
        AddSection(col, "滑块与进度条（滑块获得焦点后可用方向键和滚轮）");
        row = AddRow(col, 16.0f);
        Slider* slider = row->AddChild<Slider>(0.0f, 100.0f, 40.0f);
        slider->SetStep(1.0f);
        slider->EditLayoutStyle().width = 260.0f;
        Label* value = row->AddChild<Label>("40 %");
        value->EditLayoutStyle().width = 50.0f;
        ProgressBar* progress = row->AddChild<ProgressBar>(0.4f);
        progress->EditLayoutStyle().width = 220.0f;
        slider->SetOnChanged([value, progress](float v)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.0f %%", v);
            value->SetText(buf);
            progress->SetValue(v / 100.0f);
        });
        ProgressBar* busy = row->AddChild<ProgressBar>();
        busy->SetIndeterminate(true);
        busy->EditLayoutStyle().width = 120.0f;

        // 数值与颜色
        AddSection(col, "数值输入（支持表达式，例如 1200/3+50）与颜色");
        row = AddRow(col);
        AddFieldLabel(row, "长度");
        NumberBox* length = row->AddChild<NumberBox>(1200.0, 0.0, 1.0e6, 10.0, 2);
        length->EditLayoutStyle().width = 140.0f;
        length->SetOnChanged([this](double v) { char b[64]; std::snprintf(b, sizeof(b), "长度 = %.2f", v); SetStatus(b); });
        AddFieldLabel(row, "角度");
        NumberBox* angle = row->AddChild<NumberBox>(45.0, -360.0, 360.0, 15.0, 1);
        angle->EditLayoutStyle().width = 110.0f;
        AddFieldLabel(row, "颜色");
        ColorButton* color = row->AddChild<ColorButton>(ColorFromHex(0xFF0000));
        color->SetOnChanged([this](Color32 c) { SetStatus("颜色：" + ColorName(c)); });

        // CAD 下拉框
        AddSection(col, "CAD 下拉框：线型（带图案预览）、线宽（按粗细预览）、图层（开关、冻结、颜色）");
        row = AddRow(col);
        AddFieldLabel(row, "线型");
        ComboBox* linetype = MakeLinetypeCombo(row, 1);
        linetype->EditLayoutStyle().width = 200.0f;
        linetype->SetOnChanged([this](int i) { SetStatus("线型：" + StandardLinetypes()[static_cast<size_t>(i)].name); });
        AddFieldLabel(row, "线宽");
        MakeLineweightCombo(row, 0.30f)->EditLayoutStyle().width = 170.0f;
        AddFieldLabel(row, "图层");
        MakeLayerCombo(row)->EditLayoutStyle().width = 180.0f;

        // 文本
        AddSection(col, "文本输入（右键有编辑菜单；多行输入框可用滚轮滚动）");
        row = AddRow(col);
        row->SetLayoutStyle([] { LayoutStyle s = RowStyle(); s.alignItems = Align::Start; return s; }());
        TextBox* single = row->AddChild<TextBox>();
        single->SetPlaceholder("单行输入框");
        single->EditLayoutStyle().width = 240.0f;
        TextBox* multi = row->AddChild<TextBox>(
            "多行输入框会自动换行，支持撤销、双击选词、输入法。\n第二行：内容超过可见行数时可以用滚轮滚动，右侧显示位置指示。\n"
            "第三行\n第四行\n第五行");
        multi->SetMultiline(true);
        multi->SetRows(3);
        multi->EditLayoutStyle().width = 360.0f;

        RefreshLayerViews();
        return scroll;
    }

    // =========================================================
    // 页面 2：列表与树（在位编辑、右键菜单、按字母查找）
    // =========================================================
    Node* GalleryRoot::BuildListPage()
    {
        auto* page = new Node();
        page->SetLayoutStyle([] { LayoutStyle s; s.direction = FlexDirection::Row; return s; }());

        TreeView* tree = page->AddChild<TreeView>();
        m_layerTree = tree;
        tree->EditLayoutStyle().width = 240.0f;
        tree->EditLayoutStyle().shrink = 0.0f;
        tree->SetEditable(true);
        tree->SetOnItemRenamed([this](TreeItem*, const std::string& text)
        {
            if (text.empty())
            {
                SetStatus("名称不能为空");
                return false;
            }
            SetStatus("重命名为：" + text);
            return true;
        });
        tree->SetItemContextMenu([this, tree](TreeItem* item) -> std::vector<MenuItem>
        {
            if (!item)
                return { MenuItem("新建分组", [tree] { tree->BeginEdit(tree->AddItem(nullptr, "新分组")); }) };
            return {
                MenuItem("重命名(&M)", [tree, item] { tree->BeginEdit(item); }, "F2").Enabled(item->IsEditable()),
                MenuItem("新建子项(&N)", [tree, item] { item->SetExpanded(true); tree->BeginEdit(item->AddChild("新建项")); }),
                MenuItem::Separator(),
                MenuItem("删除(&D)", [this, tree, item] { SetStatus("已删除：" + item->GetText()); tree->RemoveItem(item); }, "Del")
                    .Enabled(item->GetParent() != tree->GetRootItem()),
            };
        });

        TreeItem* blocks = tree->AddItem(nullptr, "块定义");
        blocks->SetEditable(false);
        TreeItem* doors = blocks->AddChild("门");
        doors->AddChild("单开门 900");
        doors->AddChild("双开门 1500");
        blocks->AddChild("窗");
        blocks->AddChild("标高符号");
        blocks->SetExpanded(true);
        TreeItem* lts = tree->AddItem(nullptr, "线型");
        lts->SetEditable(false);
        for (const auto& lt : StandardLinetypes())
            lts->AddChild(lt.name);
        tree->SetOnSelectionChanged([this](TreeItem* item) { if (item) SetStatus("树：" + item->GetText()); });

        page->AddChild<Splitter>(tree)->SetLimits(140.0f, 480.0f);

        Node* right = page->AddChild<Node>();
        right->SetLayoutStyle([] { LayoutStyle s = ColumnStyle(12.0f, 8.0f); s.grow = 1.0f; return s; }());
        right->AddChild<Label>("10 000 个实体：Ctrl/Shift 多选，F2 或慢速单击重命名，右键菜单，直接输入字母按开头跳转（只绘制可见行）",
                               13.0f, Theme::TextDim);

        ListView* list = right->AddChild<ListView>();
        list->EditLayoutStyle().grow = 1.0f;
        list->SetSelectionMode(SelectionMode::Multiple);
        list->SetEditable(true);
        const char*    types[]  = { "直线", "圆", "圆弧", "多段线", "文字", "标注" };
        const uint32_t colors[] = { 0xFF0000, 0xFFFF00, 0x00FF00, 0x00FFFF, 0x8080FF, 0xFFFFFF };
        std::vector<ListItem> items(10000);
        for (size_t i = 0; i < items.size(); ++i)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s #%zu", types[i % 6], i + 1);
            items[i].text   = buf;
            items[i].detail = m_layers[i % m_layers.size()].name;
            items[i].swatch = ColorFromHex(colors[i % 6]);
        }
        list->SetItems(std::move(items));
        list->SetOnSelectionChanged([this, list]
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "已选择 %zu 个实体", list->GetSelection().size());
            SetStatus(buf);
        });
        list->SetOnItemActivated([this, list](int i) { SetStatus("激活：" + list->GetItems()[static_cast<size_t>(i)].text); });
        list->SetOnItemEdited([this](int, int, const std::string& text)
        {
            if (text.empty())
            {
                SetStatus("名称不能为空");
                return false;
            }
            SetStatus("已重命名为：" + text);
            return true;
        });
        list->SetRowContextMenu([this, list](int row) -> std::vector<MenuItem>
        {
            if (row < 0)
                return { MenuItem("全选(&A)", [list] { list->SelectAll(); }, "Ctrl+A") };
            const size_t n = list->GetSelection().size();
            char title[64];
            std::snprintf(title, sizeof(title), "删除 %zu 个实体(&D)", n);
            return {
                MenuItem("重命名(&M)", [list, row] { list->BeginEdit(row); }, "F2").Enabled(n == 1),
                MenuItem("复制(&C)", [this, n] { char b[64]; std::snprintf(b, sizeof(b), "已复制 %zu 个实体", n); SetStatus(b); }, "Ctrl+C"),
                MenuItem::Separator(),
                MenuItem(title, [this] { SetStatus("示例中不执行删除"); }, "Del"),
                MenuItem::Separator(),
                MenuItem("全选(&A)", [list] { list->SelectAll(); }, "Ctrl+A"),
            };
        });
        return page;
    }

    // =========================================================
    // 页面 3：图层与特性（多列表格 + 特性面板）
    // =========================================================
    Node* GalleryRoot::BuildLayerPage()
    {
        auto* page = new Node();
        page->SetLayoutStyle([] { LayoutStyle s; s.direction = FlexDirection::Row; return s; }());

        Node* left = page->AddChild<Node>();
        left->SetLayoutStyle([] { LayoutStyle s = ColumnStyle(12.0f, 8.0f); s.grow = 1.0f; s.shrink = 1.0f; s.minWidth = 200.0f; return s; }());

        Node* tools = AddRow(left, 8.0f);
        tools->AddChild<Button>("新建图层", [this] { AddLayer(); });
        tools->AddChild<Button>("删除图层", [this] { DeleteLayer(m_layerTable->GetCurrent()); });
        tools->AddChild<Button>("置为当前", [this]
        {
            const int i = m_layerTable->GetCurrent();
            if (i >= 0) { m_currentLayer = i; RefreshLayerViews(); SetStatus("当前图层：" + m_layers[static_cast<size_t>(i)].name); }
        });
        for (const auto& c : tools->GetChildren())
            c->EditLayoutStyle().shrink = 0.0f;     // 窗口窄时压缩说明文字，不压缩按钮
        Label* hint = tools->AddChild<Label>("点击 开/冻结/锁定 图标切换；双击行置为当前；点击名称或按 F2 重命名；点击表头按名称排序", 12.0f, Theme::TextDim);
        hint->SetEllipsis(true);
        hint->EditLayoutStyle().grow     = 1.0f;
        hint->EditLayoutStyle().minWidth = 0.0f;

        m_layerTable = left->AddChild<ListView>();
        ListView* table = m_layerTable;
        table->EditLayoutStyle().grow = 1.0f;
        table->SetColumns({
            { "",     30.0f,  TextAlign::Center },
            { "名称", 140.0f, TextAlign::Start, true },
            { "开",   44.0f,  TextAlign::Center },
            { "冻结", 44.0f,  TextAlign::Center },
            { "锁定", 44.0f,  TextAlign::Center },
            { "颜色", 90.0f },
            { "线型", 170.0f },
            { "线宽", 130.0f },
        });
        table->SetSortIndicator(ColName, true);
        table->SetCellPainter([this, table](DrawList& dl, const Rect& cell, int row, int col, bool) -> bool
        {
            const Layer& l = m_layers[static_cast<size_t>(row)];
            UIContext* ctx = table->GetContext();
            switch (col)
            {
            case ColCurrent:
                if (row == m_currentLayer)
                {
                    const Vec2 c = cell.Center();
                    const Vec2 pts[3] = { { c.x - 5.0f, c.y }, { c.x - 1.5f, c.y + 3.5f }, { c.x + 5.0f, c.y - 3.5f } };
                    dl.AddPolyline(pts, ColorFromHex(0x5FB865), false, 1.8f);
                }
                return true;
            case ColOn:     DrawLayerOnIcon(dl, cell, l.on);         return true;
            case ColFreeze: DrawLayerFreezeIcon(dl, cell, l.frozen); return true;
            case ColLock:   DrawLayerLockIcon(dl, cell, l.locked);   return true;
            case ColColor:
            {
                const float cy = cell.Center().y;
                DrawColorSwatch(dl, Rect{ cell.min.x + 8.0f, cy - 6.0f, cell.min.x + 20.0f, cy + 6.0f }, l.color);
                if (ctx)
                    ctx->GetTextSystem().Draw(dl, Rect{ cell.min.x + 28.0f, cell.min.y, cell.max.x - 4.0f, cell.max.y }, ColorName(l.color), CellText());
                return true;
            }
            case ColLinetype:
            {
                const LinetypeDef& lt = StandardLinetypes()[static_cast<size_t>(l.linetype)];
                DrawLinetypeSample(dl, Rect{ cell.min.x + 8.0f, cell.min.y, cell.min.x + 62.0f, cell.max.y }, lt.pattern, Theme::Text);
                if (ctx)
                    ctx->GetTextSystem().Draw(dl, Rect{ cell.min.x + 70.0f, cell.min.y, cell.max.x - 4.0f, cell.max.y }, lt.name, CellText());
                return true;
            }
            case ColLineweight:
                DrawLineweightSample(dl, Rect{ cell.min.x + 8.0f, cell.min.y, cell.min.x + 44.0f, cell.max.y }, l.lineweight, Theme::Text);
                if (ctx)
                    ctx->GetTextSystem().Draw(dl, Rect{ cell.min.x + 52.0f, cell.min.y, cell.max.x - 4.0f, cell.max.y }, LineweightName(l.lineweight), CellText());
                return true;
            default:
                return false;   // 名称列用默认文字
            }
        });
        table->SetOnCellClicked([this](int row, int col)
        {
            Layer& l = m_layers[static_cast<size_t>(row)];
            switch (col)
            {
            case ColOn:     l.on = !l.on;         SetStatus(l.name + (l.on ? "：打开" : "：关闭")); break;
            case ColFreeze: l.frozen = !l.frozen; SetStatus(l.name + (l.frozen ? "：冻结" : "：解冻")); break;
            case ColLock:   l.locked = !l.locked; SetStatus(l.name + (l.locked ? "：锁定" : "：解锁")); break;
            default: return;
            }
            RefreshLayerViews();
        });
        table->SetOnItemActivated([this](int i)
        {
            m_currentLayer = i;
            RefreshLayerViews();
            SetStatus("当前图层：" + m_layers[static_cast<size_t>(i)].name);
        });
        table->SetOnItemEdited([this](int row, int, const std::string& text) { return RenameLayer(row, text); });
        table->SetOnHeaderClicked([this](int col)
        {
            if (col != ColName)
                return;
            SortLayers(!m_sortAscending);
        });
        table->SetRowContextMenu([this, table](int row) -> std::vector<MenuItem>
        {
            std::vector<MenuItem> items{ MenuItem("新建图层(&N)", [this] { AddLayer(); }) };
            if (row >= 0)
            {
                items.push_back(MenuItem("置为当前(&C)", [this, row] { m_currentLayer = row; RefreshLayerViews(); }).Enabled(row != m_currentLayer));
                items.push_back(MenuItem("重命名(&M)", [table, row] { table->BeginEdit(row, ColName); }, "F2").Enabled(m_layers[static_cast<size_t>(row)].name != "0"));
                items.push_back(MenuItem::Separator());
                items.push_back(MenuItem("删除图层(&D)", [this, row] { DeleteLayer(row); }, "Del"));
            }
            return items;
        });

        // 特性面板（分隔条在它左边，向左拖动变宽）
        Splitter* split = page->AddChild<Splitter>(nullptr, true, false);
        PropertyGrid* props = page->AddChild<PropertyGrid>();
        split->SetTarget(props);
        split->SetLimits(220.0f, 520.0f);
        props->EditLayoutStyle().width  = 320.0f;
        props->EditLayoutStyle().shrink = 0.0f;

        // CAD 下拉框先建在临时容器里，再移到特性行（与其他特性控件同样对齐）
        Node holder;
        auto moveOut = [&holder]() { return holder.RemoveChild(holder.GetChildren().back().get()); };

        Expander* general = props->AddGroup("常规");
        props->AddProperty<ColorButton>(general, "颜色", ColorFromHex(0xFF0000));
        MakeLayerCombo(&holder);
        props->AddProperty(general, "图层", moveOut());
        MakeLinetypeCombo(&holder, 0);
        props->AddProperty(general, "线型", moveOut());
        props->AddProperty<NumberBox>(general, "线型比例", 1.0, 0.001, 1000.0, 0.1, 3);
        MakeLineweightCombo(&holder, 0.25f);
        props->AddProperty(general, "线宽", moveOut());
        props->AddProperty<NumberBox>(general, "透明度", 0.0, 0.0, 90.0, 5.0, 0);

        Expander* geometry = props->AddGroup("几何图形");
        props->AddProperty<NumberBox>(geometry, "起点 X", 1250.5, -1.0e9, 1.0e9, 10.0, 4);
        props->AddProperty<NumberBox>(geometry, "起点 Y", 830.0, -1.0e9, 1.0e9, 10.0, 4);
        props->AddProperty<NumberBox>(geometry, "端点 X", 3420.0, -1.0e9, 1.0e9, 10.0, 4);
        props->AddProperty<NumberBox>(geometry, "端点 Y", 830.0, -1.0e9, 1.0e9, 10.0, 4);
        TextBox* len = props->AddProperty<TextBox>(geometry, "长度", "2169.5000");
        len->SetReadOnly(true);

        Expander* other = props->AddGroup("其他（多个实体取值不同）");
        TextBox* multi = props->AddProperty<TextBox>(other, "注释");
        multi->SetPlaceholder("*多种*");
        props->AddProperty<CheckBox>(other, "可打印", "", true);

        RefreshLayerViews();
        return page;
    }

    void GalleryRoot::RefreshLayerViews()
    {
        if (m_layerTable)
        {
            const int current = m_layerTable->GetCurrent();
            std::vector<ListItem> items;
            for (const Layer& l : m_layers)
            {
                ListItem item;
                item.text  = l.name;         // 第 0 列（状态列自绘）；按字母查找也用它
                item.cells = { l.name };     // 第 1 列：名称
                items.push_back(std::move(item));
            }
            m_layerTable->SetItems(std::move(items));
            if (current >= 0 && current < static_cast<int>(m_layers.size()))
                m_layerTable->SetCurrent(current);
        }
        for (ComboBox* combo : m_layerCombos)
        {
            std::vector<std::string> names;
            for (const Layer& l : m_layers)
                names.push_back(l.name);
            combo->SetItems(std::move(names));
            combo->SetSelectedIndex(m_currentLayer);
        }
    }

    void GalleryRoot::SortLayers(bool ascending)
    {
        m_sortAscending = ascending;
        const std::string current = m_layers[static_cast<size_t>(m_currentLayer)].name;
        std::stable_sort(m_layers.begin(), m_layers.end(), [ascending](const Layer& a, const Layer& b)
        {
            return ascending ? a.name < b.name : a.name > b.name;
        });
        for (int i = 0; i < static_cast<int>(m_layers.size()); ++i)
            if (m_layers[static_cast<size_t>(i)].name == current)
                m_currentLayer = i;
        m_layerTable->SetSortIndicator(ColName, ascending);
        RefreshLayerViews();
        SetStatus(ascending ? "按名称升序" : "按名称降序");
    }

    bool GalleryRoot::RenameLayer(int index, const std::string& name)
    {
        // 与 AutoCAD 一致：0 层不能改名，名称不能为空或重复
        if (m_layers[static_cast<size_t>(index)].name == "0")
        {
            SetStatus("图层“0”不能重命名");
            return false;
        }
        if (name.empty())
        {
            SetStatus("图层名不能为空");
            return false;
        }
        for (int i = 0; i < static_cast<int>(m_layers.size()); ++i)
        {
            if (i != index && m_layers[static_cast<size_t>(i)].name == name)
            {
                SetStatus("图层“" + name + "”已存在，请换一个名称");
                return false;
            }
        }
        m_layers[static_cast<size_t>(index)].name = name;
        SetStatus("图层已重命名为：" + name);
        RefreshLayerViews();
        return true;
    }

    void GalleryRoot::AddLayer()
    {
        int n = 1;
        std::string name;
        for (;; ++n)
        {
            name = "图层" + std::to_string(n);
            const bool exists = std::any_of(m_layers.begin(), m_layers.end(), [&](const Layer& l) { return l.name == name; });
            if (!exists)
                break;
        }
        Layer layer;
        layer.name = name;
        m_layers.push_back(layer);
        RefreshLayerViews();
        const int row = static_cast<int>(m_layers.size()) - 1;
        m_layerTable->SetCurrent(row);
        m_layerTable->Focus();
        m_layerTable->BeginEdit(row, ColName);    // 与 AutoCAD 一致：新建后立即进入重命名
    }

    void GalleryRoot::DeleteLayer(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_layers.size()))
            return;
        const std::string name = m_layers[static_cast<size_t>(index)].name;
        if (name == "0" || index == m_currentLayer)
        {
            ShowMessageBox(*GetContext(), "删除图层", "不能删除图层“0”和当前图层。");
            return;
        }
        m_layers.erase(m_layers.begin() + index);
        if (m_currentLayer > index)
            --m_currentLayer;
        RefreshLayerViews();
        SetStatus("已删除图层：" + name);
    }

    // =========================================================
    // 页面 4：多文档标签页
    // =========================================================
    Node* GalleryRoot::BuildDocumentPage()
    {
        auto* page = new Node();
        page->SetLayoutStyle(ColumnStyle(12.0f, 8.0f));
        page->AddChild<Label>("\"+\" 或 Ctrl+N 新建；× / 中键 / Ctrl+W 关闭（有修改时询问）；右键标签：关闭其他、关闭右侧；拖动标签排序；标签太多时右侧 ⌄ 列出全部",
                              13.0f, Theme::TextDim)->SetEllipsis(true);

        m_docs = page->AddChild<TabView>();
        m_docs->EditLayoutStyle().grow = 1.0f;
        m_docs->SetShowNewTabButton(true);
        m_docs->SetOnNewTabRequested([this] { NewDocument(); });
        m_docs->SetOnTabMoved([this](int from, int to)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "标签从第 %d 位移到第 %d 位", from + 1, to + 1);
            SetStatus(buf);
        });
        m_docs->SetTabContextMenuExtra([this](int tab) -> std::vector<MenuItem>
        {
            const std::string title = m_docs->GetTabTitle(tab);
            return { MenuItem("复制完整路径(&P)", [this, title] { SetStatus("已复制：D:\\Drawings\\" + title); }) };
        });
        m_docs->SetOnCloseRequested([this](int index)
        {
            if (!m_docs->IsTabModified(index))
            {
                m_docs->RemoveTab(index);
                return;
            }
            // 有未保存的修改：询问（关闭时按内容节点定位，期间标签顺序可能变化）
            Node* content = m_docs->GetTabContent(index);
            ShowMessageBox(*GetContext(), "MiniCAD", "“" + m_docs->GetTabTitle(index) + "”已修改，是否保存更改？",
                           { "保存", "不保存", "取消" },
                           [this, content](int choice)
                           {
                               const int i = m_docs->IndexOf(content);
                               if (i < 0 || choice == 2)
                                   return;
                               SetStatus(choice == 0 ? "已保存并关闭" : "已关闭（未保存）");
                               m_docs->RemoveTab(i);
                           });
        });

        for (const char* name : { "建筑平面图.mcad", "结构详图.mcad", "给排水系统图.mcad" })
        {
            m_untitled = 0;
            NewDocument();
            m_docs->SetTabTitle(m_docs->GetTabCount() - 1, name);
        }
        m_untitled = 1;
        m_docs->SetSelected(0);
        return page;
    }

    void GalleryRoot::NewDocument()
    {
        if (!m_docs)
            return;
        auto content = std::make_unique<Node>();
        content->SetLayoutStyle(ColumnStyle(12.0f, 8.0f));
        content->AddChild<Label>("文档内容（示例用一个多行输入框代替 CAD 视口；输入文字后标签显示 ● 表示未保存）", 12.0f, Theme::TextDim);
        TextBox* body = content->AddChild<TextBox>();
        body->SetMultiline(true);
        body->EditLayoutStyle().grow = 1.0f;
        Node* raw = content.get();
        body->SetOnChanged([this, raw](const std::string&)
        {
            m_docs->SetTabModified(m_docs->IndexOf(raw), true);
        });

        const std::string title = m_untitled > 0 ? "未命名" + std::to_string(m_untitled++) + ".mcad" : "文档";
        const int index = m_docs->AddTab(title, std::move(content), true);
        m_docs->SetSelected(index);
        if (m_untitled > 0)
            SetStatus("新建：" + title);
    }

    // =========================================================
    // 页面 5：弹层与对话框
    // =========================================================
    Node* GalleryRoot::BuildPopupPage()
    {
        auto* page = new Node();
        page->SetLayoutStyle(ColumnStyle(20.0f, 14.0f));

        AddSection(page, "对话框（模态对话框挡住下方界面；浮动面板可拖动标题栏移动）");
        Node* row = AddRow(page);
        row->AddChild<Button>("图层特性…", [this] { OpenLayerDialog(); });
        row->AddChild<Button>("浮动面板", [this] { OpenFloatingPanel(); });
        row->AddChild<Button>("消息框…", [this] { OpenSaveMessageBox(); });

        AddSection(page, "右键菜单（含子菜单、快捷键提示、勾选、禁用项、助记符）");
        ContextArea* area = page->AddChild<ContextArea>();
        AttachContextMenu(area, [this]
        {
            return std::vector<MenuItem>{
                MenuItem("重复上一个命令(&R)", [this] { SetStatus("右键菜单：重复上一个命令"); }, "Enter"),
                MenuItem::Separator(),
                MenuItem("剪切(&T)", [this] { SetStatus("右键菜单：剪切"); }, "Ctrl+X"),
                MenuItem("复制(&C)", [this] { SetStatus("右键菜单：复制"); }, "Ctrl+C"),
                MenuItem("粘贴(&P)", {}, "Ctrl+V").Enabled(false),
                MenuItem::Separator(),
                MenuItem::Sub("绘图(&D)", {
                    MenuItem("直线(&L)", [this] { SetStatus("右键菜单：直线"); }, "L"),
                    MenuItem("圆(&C)",   [this] { SetStatus("右键菜单：圆"); },   "C"),
                    MenuItem::Sub("圆弧(&A)", {
                        MenuItem("三点(&3)", [this] { SetStatus("右键菜单：三点圆弧"); }),
                        MenuItem("起点、圆心、端点(&S)", [this] { SetStatus("右键菜单：起点圆心端点"); }),
                    }),
                }),
                MenuItem("正交模式(&O)", [this] { m_showGrid = !m_showGrid; SetStatus("右键菜单：切换正交"); }, "F8")
                    .CheckedIf([this] { return m_showGrid; }),
            };
        });

        AddSection(page, "悬浮提示：鼠标停留 0.5 秒显示，在相邻控件间移动时立即切换");
        row = AddRow(page);
        row->AddChild<Button>("短提示", [] {})->SetTooltip("这是一条提示");
        row->AddChild<Button>("长提示", [] {})->SetTooltip(
            "较长的提示会自动换行：MiniGUI 的悬浮提示最宽 360 像素，超出部分换到下一行，中文可以在任意两个字之间换行。");
        Button* disabled = row->AddChild<Button>("禁用按钮上的提示", [] {});
        disabled->SetTooltip("禁用的控件也可以显示提示（例如说明为什么不可用）");
        disabled->SetEnabled(false);
        return page;
    }

    void GalleryRoot::OpenLayerDialog()
    {
        UIContext* ctx = GetContext();
        Dialog* d = ctx->OpenPopup<Dialog>("图层特性");
        Node* body = d->GetBody();

        auto field = [body](const char* label)
        {
            Node* row = AddRow(body, 10.0f);
            AddFieldLabel(row, label, 56.0f);
            return row;
        };
        TextBox* name = field("名称")->AddChild<TextBox>("墙体");
        name->EditLayoutStyle().width = 220.0f;
        field("颜色")->AddChild<ColorButton>(ColorFromHex(0xFF0000));
        MakeLinetypeCombo(field("线型"), 0)->EditLayoutStyle().width = 220.0f;
        MakeLineweightCombo(field("线宽"), 0.25f)->EditLayoutStyle().width = 220.0f;
        Node* flags = AddRow(body, 20.0f);
        flags->EditLayoutStyle().margin = Edges::Make(66, 4, 0, 0);
        flags->AddChild<CheckBox>("冻结");
        flags->AddChild<CheckBox>("锁定");
        flags->AddChild<CheckBox>("打印", true);

        d->AddButton("确定", [this, d, name] { SetStatus("已保存图层：" + name->GetText()); d->Close(); }, DialogButtonRole::Default);
        d->AddButton("取消", [this, d] { SetStatus("已取消"); d->Close(); }, DialogButtonRole::Cancel);
    }

    void GalleryRoot::OpenFloatingPanel()
    {
        UIContext* ctx = GetContext();
        Dialog* d = ctx->OpenPopup<Dialog>("特性（浮动面板，可拖动）", false);
        d->MoveTo({ GetSize().x - 380.0f, 90.0f });
        Node* body = d->GetBody();
        const char* axes[] = { "X", "Y", "Z" };
        const double values[] = { 1250.5, 830.0, 0.0 };
        for (int i = 0; i < 3; ++i)
        {
            Node* row = AddRow(body, 10.0f);
            AddFieldLabel(row, axes[i], 24.0f);
            row->AddChild<NumberBox>(values[i], -1.0e9, 1.0e9, 10.0, 2)->EditLayoutStyle().width = 200.0f;
        }
        d->AddButton("关闭", [d] { d->Close(); }, DialogButtonRole::Cancel);
    }

    void GalleryRoot::OpenSaveMessageBox()
    {
        ShowMessageBox(*GetContext(), "MiniCAD", "“建筑平面图.mcad”已修改，是否保存更改？",
                       { "保存", "不保存", "取消" },
                       [this](int i)
                       {
                           const char* names[] = { "保存", "不保存", "取消" };
                           SetStatus(std::string("消息框结果：") + names[i]);
                       });
    }

    // =========================================================
    // 页面 6：命令行
    // =========================================================
    Node* GalleryRoot::BuildCommandPage()
    {
        auto* page = new Node();
        page->SetLayoutStyle(ColumnStyle(16.0f, 8.0f));
        page->AddChild<Label>("命令行：输入 L、C、M 等字母出现补全列表，↑↓ 选择，Enter / Tab 接受，再按 Enter 执行", 13.0f, Theme::TextDim);

        m_history = page->AddChild<ListView>();
        m_history->EditLayoutStyle().grow = 1.0f;
        m_history->SetFocusable(false);

        Node* row = AddRow(page, 8.0f);
        row->AddChild<Label>("命令:");
        TextBox* cmd = row->AddChild<TextBox>();
        cmd->EditLayoutStyle().grow = 1.0f;
        cmd->SetPlaceholder("输入命令");
        cmd->EnableAutoComplete([](const std::string& text)
        {
            // 英文名前缀匹配（不区分大小写），或中文名包含输入
            std::string upper = text;
            for (char& c : upper)
                if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            std::vector<ListItem> out;
            for (const auto& c : kCommands)
                if (std::string(c.name).rfind(upper, 0) == 0 || std::string(c.title).find(text) != std::string::npos)
                {
                    ListItem item;
                    item.text   = c.name;
                    item.detail = c.title;
                    out.push_back(std::move(item));
                }
            return out;
        });
        cmd->SetOnSubmit([this, cmd](const std::string& text)
        {
            if (text.empty())
                return;
            RunCommand(text);
            cmd->SetText({});
        });
        return page;
    }

    void GalleryRoot::RunCommand(const std::string& text)
    {
        std::string title = "未知命令";
        for (const auto& c : kCommands)
            if (text == c.name)
                title = c.title;
        m_historyItems.push_back("命令: " + text + "    （" + title + "）");
        std::vector<ListItem> items;
        for (const auto& h : m_historyItems)
        {
            ListItem item;
            item.text = h;
            items.push_back(std::move(item));
        }
        m_history->SetItems(std::move(items));
        m_history->EnsureVisible(static_cast<int>(m_historyItems.size()) - 1);
        SetStatus("执行：" + text);
    }
}
