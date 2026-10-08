#include "Widgets/UiLayout.h"
#include "Core/UIContext.h"
#include "Data/CommandRegistry.h"
#include "Style/Theme.hpp"
#include "Style/ThemeColors.h"
#include "Widgets/CommandUI.h"
#include "Widgets/Controls.h"
#include "Widgets/DockSpace.h"
#include "Widgets/Label.h"
#include "Widgets/Menu.h"
#include "Widgets/Panel.h"
#include "Widgets/Splitter.h"
#include "Widgets/TitleBar.h"
#include <algorithm>
#include <functional>
#include <set>

namespace MiniGUI
{
    namespace
    {
        bool IsSeparatorItem(const JsonValue& v)
        {
            return v.IsString() && (v.AsString() == "-" || v.AsString() == "|");
        }

        const std::set<std::string>& CommonKeys()
        {
            static const std::set<std::string> keys =
            {
                "type", "width", "height", "minWidth", "maxWidth", "minHeight", "maxHeight",
                "grow", "shrink", "gap", "padding", "align", "justify", "background", "visible", "comment",
            };
            return keys;
        }

        const std::set<std::string>& TypeKeys(const std::string& type)
        {
            static const std::map<std::string, std::set<std::string>> keys =
            {
                { "row",       { "children" } },
                { "column",    { "children" } },
                { "menubar",   {} },
                { "titlebar",  { "children", "icon", "title" } },
                { "toolbar",   { "id", "items", "vertical", "iconSize" } },
                { "panel",     { "name" } },
                { "splitter",  { "target", "min", "max" } },
                { "separator", {} },
                { "spacer",    {} },
                { "label",     { "text", "size", "color" } },
                { "dock",      { "id", "root", "panels" } },
            };
            static const std::set<std::string> none;
            const auto it = keys.find(type);
            return it == keys.end() ? none : it->second;
        }

        // 颜色："#RRGGBB" / "#RRGGBBAA"，或主题颜色名（"Panel"、"Background"…）
        bool ParseColor(const std::string& text, ColorRef& out)
        {
            if (!text.empty() && text[0] == '#' && (text.size() == 7 || text.size() == 9))
            {
                uint32_t v = 0;
                for (size_t i = 1; i < text.size(); ++i)
                {
                    const char c = text[i];
                    v <<= 4;
                    if (c >= '0' && c <= '9')      v |= static_cast<uint32_t>(c - '0');
                    else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
                    else return false;
                }
                out = text.size() == 7 ? ColorFromHex(v) : ColorFromHex(v >> 8, static_cast<uint8_t>(v & 0xFF));
                return true;
            }
            ThemeColor slot;
            if (ThemeColors::FindSlot(text, slot))
            {
                out = ColorRef(slot);
                return true;
            }
            return false;
        }
    }

    struct UiLayout::BuildState
    {
        std::set<std::string> usedPanels;
        std::set<const Node*> splittersTargetNext;     // "target": "next" 的分隔条
        std::string           path;     // 当前节点在描述里的位置，用于警告信息
        UIContext*            ctx = nullptr;
    };

    UiLayout::UiLayout(CommandRegistry& commands, std::string iconDir)
        : m_commands(commands)
        , m_iconDir(std::move(iconDir))
    {
    }

    UiLayout::~UiLayout() = default;

    void UiLayout::RegisterPanel(const std::string& name, std::unique_ptr<Node> panel, std::string title)
    {
        PanelSlot& slot      = m_panels[name];
        slot.title           = title.empty() ? name : std::move(title);
        slot.node            = panel.get();
        slot.originalStyle   = panel->GetLayoutStyle();
        slot.originalVisible = panel->IsVisible();
        slot.owned           = std::move(panel);
    }

    Node* UiLayout::GetPanel(const std::string& name) const
    {
        const auto it = m_panels.find(name);
        return it == m_panels.end() ? nullptr : it->second.node;
    }

    ToolBar* UiLayout::GetToolBar(const std::string& id) const
    {
        const auto it = m_toolBars.find(id);
        return it == m_toolBars.end() ? nullptr : it->second;
    }

    DockSpace* UiLayout::GetDock(const std::string& id) const
    {
        const auto it = m_docks.find(id);
        return it == m_docks.end() ? nullptr : it->second;
    }

    // =========================================================
    // 应用
    // =========================================================
    bool UiLayout::ApplyFile(Node* parent, const std::string& utf8Path)
    {
        std::string text;
        if (!ReadTextFile(utf8Path, text))
        {
            m_error = "无法读取界面描述文件：" + utf8Path;
            return false;
        }
        return ApplyText(parent, text);
    }

    bool UiLayout::ApplyText(Node* parent, std::string_view jsonText)
    {
        JsonValue doc;
        JsonError err;
        if (!ParseJson(jsonText, doc, err))
        {
            m_error = "界面描述文件格式错误，" + err.ToString();
            return false;
        }
        return Apply(parent, doc);
    }

    void UiLayout::ReclaimPanels()
    {
        // 停靠区里的面板（包括没有显示的）由停靠区持有，先全部取回
        for (auto& [id, dock] : m_docks)
        {
            for (auto& [name, node] : dock->TakeAllPanels())
            {
                auto it = m_panels.find(name);
                if (it != m_panels.end() && it->second.node == node.get())
                    it->second.owned = std::move(node);
            }
        }
        m_docks.clear();

        // 把宿主的面板从旧界面上摘下来（不销毁），重建时放回去
        for (auto& [name, slot] : m_panels)
        {
            if (!slot.owned && slot.node && slot.node->GetParent())
                slot.owned = slot.node->GetParent()->RemoveChild(slot.node);
            if (slot.node)
            {
                slot.node->SetLayoutStyle(slot.originalStyle);
                slot.node->SetVisible(slot.originalVisible);
            }
        }
    }

    void UiLayout::RestoreCommandDefaults()
    {
        // 第一次见到的命令记下代码里的默认值；之后每次应用前先恢复，保证从文件里删掉的覆盖不会残留
        for (const std::string& id : m_commands.GetIds())
        {
            Command* c = m_commands.Find(id);
            auto it = m_commandDefaults.find(id);
            if (it == m_commandDefaults.end())
            {
                m_commandDefaults.emplace(id, CommandDefaults{ c->label, c->tooltip, c->icon, c->shortcut });
                continue;
            }
            c->label   = it->second.label;
            c->tooltip = it->second.tooltip;
            c->icon    = it->second.icon;
            if (c->shortcut != it->second.shortcut)
                m_commands.SetShortcut(id, it->second.shortcut);
        }
    }

    bool UiLayout::Apply(Node* parent, const JsonValue& doc)
    {
        m_error.clear();
        m_warnings.clear();
        if (!doc.IsObject())
        {
            m_error = "界面描述文件的顶层必须是对象 { … }";
            return false;
        }
        for (const auto& [key, value] : doc.GetObject())
        {
            (void)value;
            if (key != "commands" && key != "menus" && key != "toolbars" && key != "layout" && key != "comment")
                m_warnings.push_back("顶层：未知的字段 \"" + key + "\"");
        }

        // ── 命令覆盖 ─────────────────────────────────────────────
        RestoreCommandDefaults();
        m_commands.ApplyOverrides(doc["commands"], m_warnings);
        for (const std::string& c : m_commands.FindShortcutConflicts())
            m_warnings.push_back("快捷键冲突 " + c);

        // ── 清空旧界面（推迟销毁）并重建 ──────────────────────────
        ReclaimPanels();
        m_menuBar  = nullptr;
        m_titleBar = nullptr;
        m_toolBars.clear();
        UIContext* ctx = parent->GetContext();
        while (!parent->GetChildren().empty())
        {
            std::unique_ptr<Node> old = parent->RemoveChild(parent->GetChildren().back().get());
            if (ctx)
                ctx->DeferDelete(std::move(old));
        }

        BuildState st;
        st.path = "layout";
        st.ctx  = ctx;
        const JsonValue& layout = doc["layout"];
        if (!layout.IsObject())
        {
            m_warnings.push_back("缺少 layout，界面为空");
            return true;
        }
        if (std::unique_ptr<Node> root = BuildNode(layout, doc, false, st))
        {
            if (!layout.Find("grow"))
                root->EditLayoutStyle().grow = 1.0f;     // 默认填满宿主的容器
            parent->AddChild(std::move(root));
        }
        parent->InvalidateLayout();
        return true;
    }

    // =========================================================
    // 节点
    // =========================================================
    void UiLayout::ApplyCommonStyle(Node* node, const JsonValue& desc, bool isPanel)
    {
        LayoutStyle& s = node->EditLayoutStyle();
        auto num = [&](const char* key, float& field)
        {
            if (const JsonValue* v = desc.Find(key))
            {
                if (v->IsNumber())
                    field = static_cast<float>(v->AsNumber());
                else if (v->IsString() && v->AsString() == "auto")
                    field = kAuto;
                else
                    m_warnings.push_back(std::string(key) + " 应为数字");
            }
        };
        num("width", s.width);
        num("height", s.height);
        num("minWidth", s.minWidth);
        num("maxWidth", s.maxWidth);
        num("minHeight", s.minHeight);
        num("maxHeight", s.maxHeight);
        num("grow", s.grow);
        num("shrink", s.shrink);
        num("gap", s.gap);

        if (const JsonValue* p = desc.Find("padding"))
        {
            const auto& a = p->GetArray();
            if (p->IsNumber())
                s.padding = Edges::All(static_cast<float>(p->AsNumber()));
            else if (a.size() == 2)
                s.padding = Edges::Symmetric(static_cast<float>(a[0].AsNumber()), static_cast<float>(a[1].AsNumber()));
            else if (a.size() == 4)
                s.padding = Edges::Make(static_cast<float>(a[0].AsNumber()), static_cast<float>(a[1].AsNumber()),
                                        static_cast<float>(a[2].AsNumber()), static_cast<float>(a[3].AsNumber()));
            else
                m_warnings.push_back("padding 应为数字、[水平, 竖直] 或 [左, 上, 右, 下]");
        }

        if (const JsonValue* v = desc.Find("align"))
        {
            const std::string& a = v->AsString();
            if (a == "start")        s.alignItems = Align::Start;
            else if (a == "center")  s.alignItems = Align::Center;
            else if (a == "end")     s.alignItems = Align::End;
            else if (a == "stretch") s.alignItems = Align::Stretch;
            else m_warnings.push_back("align 应为 start / center / end / stretch");
        }
        if (const JsonValue* v = desc.Find("justify"))
        {
            const std::string& j = v->AsString();
            if (j == "start")              s.justify = Justify::Start;
            else if (j == "center")        s.justify = Justify::Center;
            else if (j == "end")           s.justify = Justify::End;
            else if (j == "space-between") s.justify = Justify::SpaceBetween;
            else m_warnings.push_back("justify 应为 start / center / end / space-between");
        }

        if (const JsonValue* v = desc.Find("background"))
        {
            ColorRef color;
            if (!ParseColor(v->AsString(), color))
                m_warnings.push_back("无法识别的颜色 \"" + v->AsString() + "\"（用 #RRGGBB 或主题颜色名）");
            else if (auto* panel = dynamic_cast<Panel*>(node))
                panel->SetBackground(color);
            else if (auto* toolbar = dynamic_cast<ToolBar*>(node))
                toolbar->SetBackground(color);
            else if (!isPanel)
                m_warnings.push_back("background 只能用于 row / column / toolbar");
        }

        if (const JsonValue* v = desc.Find("visible"))
            node->SetVisible(v->AsBool(true));
    }

    std::unique_ptr<Node> UiLayout::BuildNode(const JsonValue& desc, const JsonValue& doc, bool parentIsRow, BuildState& st)
    {
        if (!desc.IsObject())
        {
            m_warnings.push_back(st.path + "：布局节点应为对象 { \"type\": … }");
            return nullptr;
        }
        const std::string& type = desc["type"].AsString();
        const std::string  where = st.path + "（" + (type.empty() ? std::string("?") : type) + "）";

        for (const auto& [key, value] : desc.GetObject())
        {
            (void)value;
            if (!CommonKeys().count(key) && !TypeKeys(type).count(key))
                m_warnings.push_back(where + "：未知的属性 \"" + key + "\"");
        }

        std::unique_ptr<Node> node;
        bool isPanel = false;

        if (type == "row" || type == "column")
        {
            auto panel = std::make_unique<Panel>();
            panel->EditLayoutStyle().direction = type == "row" ? FlexDirection::Row : FlexDirection::Column;
            const std::string base = st.path;
            const auto& children = desc["children"].GetArray();
            for (size_t i = 0; i < children.size(); ++i)
            {
                st.path = base + "." + std::to_string(i);
                if (std::unique_ptr<Node> child = BuildNode(children[i], doc, type == "row", st))
                    panel->AddChild(std::move(child));
            }
            st.path = base;

            // 分隔条：默认调整前一个兄弟，"target": "next" 调整后一个
            const auto& kids = panel->GetChildren();
            for (size_t i = 0; i < kids.size(); ++i)
            {
                auto* sp = dynamic_cast<Splitter*>(kids[i].get());
                if (!sp)
                    continue;
                const bool next = st.splittersTargetNext.count(sp) > 0;
                Node* target = next ? (i + 1 < kids.size() ? kids[i + 1].get() : nullptr)
                                    : (i > 0 ? kids[i - 1].get() : nullptr);
                if (!target)
                    m_warnings.push_back(where + "：splitter 旁边没有可调整的节点");
                sp->SetTarget(target);
            }
            node = std::move(panel);
        }
        else if (type == "menubar")
        {
            auto bar = std::make_unique<MenuBar>();
            std::function<std::vector<MenuItem>(const JsonValue&, const std::string&)> buildItems;
            buildItems = [&](const JsonValue& items, const std::string& menuPath)
            {
                std::vector<MenuItem> result;
                for (const JsonValue& it : items.GetArray())
                {
                    if (IsSeparatorItem(it))
                        result.push_back(MenuItem::Separator());
                    else if (it.IsString())
                    {
                        if (!m_commands.Find(it.AsString()))
                            m_warnings.push_back("菜单 " + menuPath + "：没有名为 \"" + it.AsString() + "\" 的命令");
                        result.push_back(CommandMenuItem(m_commands, it.AsString()));
                    }
                    else if (it.IsObject() && it.Find("items"))
                    {
                        const std::string title = it["title"].AsString("?");
                        result.push_back(MenuItem::Sub(title, buildItems(it["items"], menuPath + " > " + title)));
                    }
                    else
                        m_warnings.push_back("菜单 " + menuPath + "：无法识别的菜单项 " + it.Dump());
                }
                return result;
            };
            for (const JsonValue& menu : doc["menus"].GetArray())
            {
                const std::string title = menu["title"].AsString("?");
                bar->AddMenu(title, buildItems(menu["items"], title));
            }
            if (doc["menus"].Size() == 0)
                m_warnings.push_back(where + "：menus 为空");
            if (m_menuBar)
                m_warnings.push_back(where + "：出现了多个菜单栏，键盘导航只对最后一个有效");
            m_menuBar = bar.get();
            node = std::move(bar);
        }
        else if (type == "titlebar")
        {
            auto bar = std::make_unique<TitleBar>(m_commands);
            bar->SetTitle(desc["title"].AsString());
            if (const JsonValue* icon = desc.Find("icon"))
            {
                const TextureId tex = st.ctx ? st.ctx->LoadTexture(m_iconDir + "/" + icon->AsString(), { 16.0f, 16.0f }) : InvalidTextureId;
                if (tex == InvalidTextureId)
                    m_warnings.push_back(where + "：无法加载图标 \"" + icon->AsString() + "\"");
                bar->SetIcon(tex);
            }
            const std::string base = st.path;
            const auto& children = desc["children"].GetArray();
            for (size_t i = 0; i < children.size(); ++i)
            {
                st.path = base + "." + std::to_string(i);
                if (std::unique_ptr<Node> child = BuildNode(children[i], doc, true, st))
                    bar->GetContent()->AddChild(std::move(child));
            }
            st.path = base;
            if (m_titleBar)
                m_warnings.push_back(where + "：出现了多个标题栏，GetTitleBar 只返回最后一个");
            m_titleBar = bar.get();
            node = std::move(bar);
        }
        else if (type == "toolbar")
        {
            const std::string id = desc["id"].AsString();
            const JsonValue* def = nullptr;
            if (!id.empty())
            {
                def = doc["toolbars"].Find(id);
                if (!def)
                    m_warnings.push_back(where + "：toolbars 里没有 \"" + id + "\"");
            }
            // 工具栏定义：{ "items": [...], "iconSize": 20 } 或直接是数组；布局节点上的 items / iconSize 优先
            const JsonValue& items = desc.Find("items") ? desc["items"]
                                   : (def && def->IsArray() ? *def : (def ? (*def)["items"] : desc["items"]));
            const JsonValue* iconSize = desc.Find("iconSize") ? desc.Find("iconSize") : (def ? def->Find("iconSize") : nullptr);

            auto bar = std::make_unique<ToolBar>(m_commands, desc["vertical"].AsBool(false));
            bar->SetIconDir(m_iconDir);
            if (iconSize)
                bar->SetIconSize(static_cast<float>(iconSize->AsNumber(20.0)));
            for (const JsonValue& it : items.GetArray())
            {
                if (IsSeparatorItem(it))
                    bar->AddSeparator();
                else if (it.IsString() || (it.IsObject() && it.Find("command")))
                {
                    const std::string cmd = it.IsString() ? it.AsString() : it["command"].AsString();
                    if (!m_commands.Find(cmd))
                        m_warnings.push_back("工具栏 " + (id.empty() ? where : id) + "：没有名为 \"" + cmd + "\" 的命令");
                    bar->AddCommand(cmd, it.IsObject() && it["text"].AsBool(false));
                }
                else
                    m_warnings.push_back("工具栏 " + id + "：无法识别的项 " + it.Dump());
            }
            if (!id.empty())
                m_toolBars[id] = bar.get();
            node = std::move(bar);
        }
        else if (type == "panel")
        {
            const std::string name = desc["name"].AsString();
            auto it = m_panels.find(name);
            if (it == m_panels.end())
            {
                m_warnings.push_back(where + "：宿主没有提供名为 \"" + name + "\" 的面板");
                return nullptr;
            }
            if (!it->second.owned)
            {
                m_warnings.push_back(where + "：面板 \"" + name + "\" 被引用了多次，只放在第一处");
                return nullptr;
            }
            st.usedPanels.insert(name);
            node    = std::move(it->second.owned);
            isPanel = true;
        }
        else if (type == "dock")
        {
            auto dock = std::make_unique<DockSpace>();
            // 收集布局里引用的面板和 "panels" 里列出的面板，交给停靠区
            std::vector<std::string> names;
            std::function<void(const JsonValue&)> collect = [&](const JsonValue& v)
            {
                for (const JsonValue& t : v["tabs"].GetArray())
                    names.push_back(t.IsString() ? t.AsString() : t["panel"].AsString());
                for (const JsonValue& c : v["children"].GetArray())
                    collect(c);
            };
            collect(desc["root"]);
            for (const JsonValue& p : desc["panels"].GetArray())
                names.push_back(p.AsString());

            for (const std::string& name : names)
            {
                auto it = m_panels.find(name);
                if (it == m_panels.end() || dock->HasPanel(name))
                    continue;       // 未知面板由 LoadLayout 报告；同一停靠区里重复引用也由它报告
                if (!it->second.owned)
                {
                    m_warnings.push_back(where + "：面板 \"" + name + "\" 已在别处使用");
                    continue;
                }
                st.usedPanels.insert(name);
                dock->AddPanel(name, it->second.title, std::move(it->second.owned));
            }
            if (desc.Find("root"))
                dock->LoadLayout(desc["root"], &m_warnings);

            const std::string id = desc["id"].AsString();
            m_docks[id.empty() ? "#" + std::to_string(m_docks.size()) : id] = dock.get();
            node = std::move(dock);
        }
        else if (type == "splitter")
        {
            auto sp = std::make_unique<Splitter>(nullptr, parentIsRow, desc["target"].AsString("previous") != "next");
            sp->SetLimits(static_cast<float>(desc["min"].AsNumber(60.0)), static_cast<float>(desc["max"].AsNumber(2000.0)));
            if (desc["target"].AsString() == "next")
                st.splittersTargetNext.insert(sp.get());       // 目标由所在容器在建完兄弟后设置
            else if (desc.Find("target") && desc["target"].AsString() != "previous")
                m_warnings.push_back(where + "：target 应为 previous 或 next");
            if (parentIsRow) sp->EditLayoutStyle().width = 5.0f;
            else             sp->EditLayoutStyle().height = 5.0f;
            node = std::move(sp);
        }
        else if (type == "separator")
        {
            auto sep = std::make_unique<Separator>(parentIsRow);
            node = std::move(sep);
        }
        else if (type == "spacer")
        {
            node = std::make_unique<Node>();
            node->EditLayoutStyle().grow = 1.0f;
            node->SetHitTestVisible(false);
        }
        else if (type == "label")
        {
            ColorRef color = Theme::Text;
            if (const JsonValue* c = desc.Find("color"); c && !ParseColor(c->AsString(), color))
                m_warnings.push_back(where + "：无法识别的颜色 \"" + c->AsString() + "\"");
            node = std::make_unique<Label>(desc["text"].AsString(), static_cast<float>(desc["size"].AsNumber(Theme::FontSize)), color);
        }
        else
        {
            m_warnings.push_back(where + "：未知的类型 \"" + type + "\"");
            return nullptr;
        }

        ApplyCommonStyle(node.get(), desc, isPanel);
        return node;
    }
}
