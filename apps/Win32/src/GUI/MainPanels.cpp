// ── 宿主面板（文档区、特性、状态栏）与界面描述文件、用户布局的加载 ─────────
#include "GUI/MainFrame.h"
#include "GUI/StatusBarView.h"
#include "GUI/Lineweights.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Log.h"
#include "Core/UIContext.h"
#include "Data/Json.h"
#include "Document/Document.h"
#include "Style/Theme.hpp"
#include "Widgets/Button.h"
#include "Widgets/ComboBox.h"
#include "Widgets/Controls.h"
#include "Widgets/DockSpace.h"
#include "Widgets/Label.h"
#include "Widgets/Menu.h"
#include "Widgets/NumberBox.h"
#include "Widgets/Panel.h"
#include "Widgets/PropertyGrid.h"
#include "Widgets/TextBox.h"
#include "Widgets/TabView.h"
#include "Widgets/TitleBar.h"
#include "Widgets/UiLayout.h"
#include "Widgets/ViewportHost.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>

namespace MiniCAD
{
    namespace
    {
        namespace Theme = MiniGUI::Theme;

        std::string ToUtf8(const std::wstring& w)
        {
            const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
            if (n > 1)
                WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
            return s;
        }

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

        // 用户布局文件：%LOCALAPPDATA%\MiniCAD\layout.json
        std::filesystem::path UserLayoutPath()
        {
            wchar_t buf[MAX_PATH] = L"";
            const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
            if (n == 0 || n >= MAX_PATH)
                return {};
            return std::filesystem::path(buf) / L"MiniCAD" / L"layout.json";
        }

        // 第一次加载就失败时使用的最小布局：至少能看到绘图区和状态栏里的错误信息（标题栏保证窗口能拖动、关闭）
        constexpr const char* kFallbackLayout = R"json({
            "layout": { "type": "column", "children": [
                { "type": "titlebar" },
                { "type": "panel", "name": "documents", "grow": 1 },
                { "type": "panel", "name": "statusbar" } ] }
        })json";

        // 选中实体的公共属性；没有选中返回 -1（下拉框显示占位文字），取值不同返回 nullopt（显示"*多种*"）
        template<typename F>
        std::optional<int> CommonIndex(Editor& editor, F indexOf)
        {
            std::optional<int> result;
            bool any = false;
            for (Object* obj : editor.GetSelectedObjects())
            {
                const auto* e = dynamic_cast<const Entity*>(obj);
                if (!e)
                    continue;
                const int idx = indexOf(e->GetAttr());
                if (!any)
                    result = idx;
                else if (result && *result != idx)
                    return std::nullopt;
                any = true;
            }
            return any ? result : std::optional<int>(-1);
        }

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

    // =========================================================
    // 文档区：文档标签条 + 视口（所有文档共用一个视口，切换标签时换相机状态）
    // =========================================================
    std::unique_ptr<MiniGUI::Node> MainFrame::CreateDocumentArea()
    {
        using namespace MiniGUI;
        auto area = std::make_unique<Panel>(Theme::Background);
        LayoutStyle as;
        as.direction = FlexDirection::Column;
        area->SetLayoutStyle(as);

        // ── 标签条 ───────────────────────────────────────────────
        m_docTabs = area->AddChild<TabView>();
        m_docTabs->EditLayoutStyle().height = TabView::kStripHeight;
        m_docTabs->EditLayoutStyle().shrink = 0.0f;
        m_docTabs->SetFocusable(false);             // 点击标签后键盘仍在绘图区
        m_docTabs->SetShowNewTabButton(true);
        m_docTabs->SetOnSelectionChanged([this](int i)
        {
            if (m_syncing || i < 0)
                return;
            ActivateDocument(reinterpret_cast<Document*>(m_docTabs->GetTabData(i)));
            m_viewport->Focus();
        });
        m_docTabs->SetOnCloseRequested([this](int i) { CloseDocument(reinterpret_cast<Document*>(m_docTabs->GetTabData(i))); });
        m_docTabs->SetOnNewTabRequested([this] { m_commands.Execute("file.new"); });
        m_docTabs->SetTabContextMenuExtra([this](int i)
        {
            Document* doc = reinterpret_cast<Document*>(m_docTabs->GetTabData(i));
            return std::vector<MenuItem>{
                MenuItem("保存(&S)",    [this, doc] { ActivateDocument(doc); m_commands.Execute("file.save"); }),
                MenuItem("另存为(&A)…", [this, doc] { ActivateDocument(doc); m_commands.Execute("file.saveAs"); }),
            };
        });

        // ── 视口 ─────────────────────────────────────────────────
        auto* vp = area->AddChild<ViewportHost>();
        vp->EditLayoutStyle().grow = 1.0f;
        vp->SetCursor(CursorShape::Hidden);       // 视口自己绘制十字光标
        vp->SetOnRender ([this](int w, int h) { RenderViewport(w, h); });
        vp->SetOnPointer([this](const ViewportPointerEvent& e) { OnViewportPointer(e); });
        vp->SetOnKey    ([this](const KeyEvent& e) { return OnViewportKey(e); });
        m_viewport = vp;

        // ── 没有文档时的提示 ─────────────────────────────────────
        auto* hint = area->AddChild<Node>();
        LayoutStyle hs;
        hs.direction  = FlexDirection::Column;
        hs.alignItems = Align::Center;
        hs.justify    = Justify::Center;
        hs.gap        = 12.0f;
        hs.grow       = 1.0f;
        hint->SetLayoutStyle(hs);
        hint->AddChild<Label>("没有打开的文档", 18.0f, Theme::TextDim);
        Node* buttons = hint->AddChild<Node>();
        LayoutStyle bs;
        bs.direction = FlexDirection::Row;
        bs.gap       = 10.0f;
        buttons->SetLayoutStyle(bs);
        buttons->AddChild<Button>("新建 (Ctrl+N)", [this] { m_commands.Execute("file.new"); })->SetStyle(ButtonStyle::Primary());
        buttons->AddChild<Button>("打开… (Ctrl+O)", [this] { m_commands.Execute("file.open"); });
        hint->SetVisible(false);
        m_noDocHint = hint;
        return area;
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
                const auto ids = SortedLayerIds(*d);
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

        // ── 绘图辅助（与工具栏、状态栏、F3 / F8 是同一份数据，任何一处修改都同步）────
        Expander* aux = grid->AddGroup("绘图辅助");
        Editor& editor = dm.GetEditor();
        auto bindToggle = [this](CheckBox* box, std::function<bool()> get, std::function<void()> toggle)
        {
            m_bindings.BindCheck(box, [get] { return std::optional<bool>(get()); },
                                      [get, toggle](const bool& v) { if (v != get()) toggle(); });
        };
        bindToggle(grid->AddProperty<CheckBox>(aux, "对象捕捉"), [&editor] { return editor.IsSnapEnabled(); },  [&editor] { editor.ToggleSnap(); });
        bindToggle(grid->AddProperty<CheckBox>(aux, "正交"),     [&editor] { return editor.IsOrthoEnabled(); }, [&editor] { editor.ToggleOrtho(); });
        bindToggle(grid->AddProperty<CheckBox>(aux, "悬停高亮"), [&editor] { return editor.IsHoverEnabled(); }, [&editor] { editor.ToggleHover(); });
        auto* radius = grid->AddProperty<NumberBox>(aux, "捕捉孔径", 10.0, 1.0, 50.0, 1.0, 0);
        m_bindings.BindNumber(radius,
            [&editor] { return std::optional<double>(editor.GetSnapEngine().GetSnapRadiusPx()); },
            [&editor](const double& v) { editor.GetSnapEngine().SetSnapRadiusPx(v); });

        // ── 选择集（可编辑：多个对象取值不同时显示"*多种*"；修改走 ChangeAttrCommand，可撤销）────────────
        Expander* sel = grid->AddGroup("选择集");
        auto* count = grid->AddProperty<Label>(sel, "数量", std::string(), 13.0f);
        m_bindings.BindLabel(count, [&editor, doc]
        {
            const size_t n = doc() ? editor.GetSelection().size() : 0;
            return n == 0 ? std::string("未选择") : std::to_string(n) + " 个对象";
        });
        auto* selLayer = grid->AddProperty<ComboBox>(sel, "图层");
        selLayer->SetPlaceholder("—");
        m_bindings.BindChoice(selLayer,
            [&editor, doc]() -> std::optional<int>
            {
                Document* d = doc();
                if (!d) return -1;
                const auto ids = SortedLayerIds(*d);
                return CommonIndex(editor, [&ids](const EntityAttr& a)
                {
                    return static_cast<int>(std::find(ids.begin(), ids.end(), a.LayerId) - ids.begin());
                });
            },
            [&editor, doc](const int& i)
            {
                Document* d = doc();
                if (!d) return;
                const auto ids = SortedLayerIds(*d);
                if (i >= 0 && static_cast<size_t>(i) < ids.size())
                    editor.ChangeSelectionAttr(AttrChange{ .Layer = ids[static_cast<size_t>(i)] });
            },
            layerNames);

        auto* selColor = grid->AddProperty<ComboBox>(sel, "颜色", ColorLabels());
        selColor->SetPlaceholder("—");
        m_bindings.BindChoice(selColor,
            [&editor, doc]() -> std::optional<int>
            {
                if (!doc()) return -1;
                return CommonIndex(editor, [](const EntityAttr& a) { return ColorIndex(a.Color); });
            },
            [&editor](const int& i)
            {
                const auto& items = ColorItems();
                if (i >= 0 && static_cast<size_t>(i) < items.size())      // "其他"不可设置
                    editor.ChangeSelectionAttr(AttrChange{ .Color = items[static_cast<size_t>(i)].color });
            });

        auto* selLinetype = grid->AddProperty<ComboBox>(sel, "线型");
        selLinetype->SetPlaceholder("—");
        m_bindings.BindChoice(selLinetype,
            [&editor, doc]() -> std::optional<int>
            {
                if (!doc()) return -1;
                return CommonIndex(editor, [](const EntityAttr& a) { return static_cast<int>(a.LineType); });
            },
            [&editor](const int& i)
            {
                if (i >= 0)
                    editor.ChangeSelectionAttr(AttrChange{ .LineType = static_cast<LineTypeID>(i) });
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
            [&editor, doc]() -> std::optional<double>
            {
                if (!doc()) return std::nullopt;
                std::optional<double> result;
                bool any = false;
                for (Object* obj : editor.GetSelectedObjects())
                {
                    const auto* e = dynamic_cast<const Entity*>(obj);
                    if (!e) continue;
                    const double s = e->GetAttr().LinetypeScale;
                    if (!any) result = s;
                    else if (result && *result != s) return std::nullopt;
                    any = true;
                }
                return any ? result : std::optional<double>(1.0);
            },
            [&editor](const double& v) { editor.ChangeSelectionAttr(AttrChange{ .LinetypeScale = v }); });

        auto* selWeight = grid->AddProperty<ComboBox>(sel, "线宽", LineweightLabels());
        selWeight->SetPlaceholder("—");
        m_bindings.BindChoice(selWeight,
            [&editor, doc]() -> std::optional<int>
            {
                if (!doc()) return -1;
                return CommonIndex(editor, [](const EntityAttr& a) { return LineweightIndex(a.Lineweight); });
            },
            [&editor](const int& i)
            {
                if (i >= 0 && static_cast<size_t>(i) < std::size(kLineweightItems))
                    editor.ChangeSelectionAttr(AttrChange{ .Lineweight = kLineweightItems[i].lw });
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
            [slots, &editor](const std::string& v)
            {
                const Slot& s = (*slots)[0];
                if (!s.name.empty() && s.editable)
                    editor.SetSelectionProperty(s.name, PropValue(v));
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
                [slots, i, &editor](const double& v)
                {
                    const Slot& s = (*slots)[i];
                    if (!s.name.empty() && s.editable)
                        editor.SetSelectionProperty(s.name, PropValue(v));
                });
        }

        // 选择集变化后：重新分配槽（在 BindingSet::Refresh 之前调用）
        m_syncGeometryRows = [slots, geom, textSlot, numberBoxes, &editor, doc]
        {
            for (Slot& s : *slots) s = Slot{};
            if (doc())
            {
                size_t nextNumber = 1;
                for (const CommonProperty& p : editor.GetSelectionProperties())
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
            m_viewport->RequestRender();
        });
        return panel;
    }

    // =========================================================
    // 状态栏：MiniCAD 的状态栏（工具 | 坐标 | 捕捉 正交 悬停 | 文档）+ 界面描述文件的加载结果
    // =========================================================
    std::unique_ptr<MiniGUI::Node> MainFrame::CreateStatusBar()
    {
        using namespace MiniGUI;
        constexpr float kHeight = 26.0f;
        auto status = std::make_unique<Panel>(Theme::Panel);
        LayoutStyle s;
        s.direction  = FlexDirection::Row;
        s.alignItems = Align::Center;
        s.height     = kHeight;
        s.shrink     = 0.0f;
        s.padding    = Edges::Make(0.0f, 0.0f, 10.0f, 0.0f);
        status->SetLayoutStyle(s);

        m_statusBar = status->AddChild<StatusBarView>(m_docManager, kHeight);
        m_statusBar->EditLayoutStyle().grow     = 1.0f;
        m_statusBar->EditLayoutStyle().minWidth = 0.0f;
        m_statusBar->SetOnToggled([this] { StateChanged(); });

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
        const std::string fileName = ToUtf8(ToPath(m_uiPath).filename().wstring());

        m_uiWriteTime = WriteTime(m_uiPath);
        const bool ok = m_layout->ApplyFile(m_uiHost, m_uiPath);
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
            const size_t n = m_layout->GetWarnings().size();
            std::string text = "界面：" + fileName;
            if (n > 0)
                text += "，" + std::to_string(n) + " 条警告：" + m_layout->GetWarnings().front();
            m_uiStatus->SetText(text);
            m_uiStatus->SetColor(n > 0 ? Theme::Danger : Theme::TextDim);
            m_uiStatus->SetTooltip(m_uiPath);
            LOG_INFO("界面描述已加载：%s（%zu 条警告）", m_uiPath.c_str(), n);
        }

        // 标题栏是重新生成的：恢复标题和激活状态
        if (TitleBar* bar = m_layout->GetTitleBar())
        {
            bar->SetTitle(m_title);
            bar->SetWindowActive(GetActiveWindow() == m_hwnd);
        }

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

    void MainFrame::WatchUiFile()
    {
        const std::wstring dir = ToPath(m_uiPath).parent_path().wstring();
        m_uiWatch = FindFirstChangeNotificationW(dir.c_str(), FALSE,
                                                 FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE);
        if (m_uiWatch == INVALID_HANDLE_VALUE)
        {
            m_uiWatch = nullptr;
            LOG_WARN("无法监视界面描述文件所在目录，修改后请按 F5 重新加载");
        }
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
        const std::filesystem::path path = UserLayoutPath();
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

    void MainFrame::SaveUserLayout()
    {
        using namespace MiniGUI;
        DockSpace* dock = m_layout->GetDock("main");
        const std::filesystem::path path = UserLayoutPath();
        if (!dock || !m_useUserLayout || path.empty())
            return;

        JsonValue root = JsonValue::MakeObject();
        root.Set("comment", JsonValue("MiniCAD 面板布局，由程序自动保存；删除本文件或使用“视图 > 重置面板布局”恢复默认"));
        root.Set("dock", dock->SaveLayout());
        JsonValue panels = JsonValue::MakeArray();      // 保存时已有的面板：之后新增的面板据此识别
        for (const std::string& id : dock->GetPanelIds())
            panels.Push(id);
        root.Set("panels", std::move(panels));

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << root.Dump(2);
        if (!out)
            LOG_WARN("无法保存用户布局：%s", ToUtf8(path.wstring()).c_str());
    }

    void MainFrame::ResetLayout()
    {
        std::error_code ec;
        const std::filesystem::path path = UserLayoutPath();
        if (!path.empty())
            std::filesystem::remove(path, ec);
        ReloadUi();         // 按界面描述文件里的默认布局重建（不再读取用户布局）
    }
}
