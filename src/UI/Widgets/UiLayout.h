#pragma once
#include "Core/Node.h"
#include "Data/Json.h"
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniGUI
{
    class CommandRegistry;
    class DockSpace;
    class MenuBar;
    class TitleBar;
    class ToolBar;

    // 界面描述文件（JSON）→ 界面。描述菜单、工具栏和面板布局，具体内容（视口、特性面板、状态栏…）
    // 由宿主以"命名面板"的形式提供。格式：
    //
    //   {
    //     "commands": { "file.save": { "label": "保存(&S)", "shortcut": "Ctrl+S", "icon": "Save.png", "tooltip": "…" } },
    //     "menus":    [ { "title": "文件(&F)", "items": [ "file.new", "-", { "title": "最近打开", "items": [ … ] } ] } ],
    //     "toolbars": { "draw": { "items": [ "draw.line", "|", { "command": "draw.text", "text": true } ], "iconSize": 20 } },
    //     "layout":   { "type": "column", "children": [
    //                     { "type": "menubar" },
    //                     { "type": "toolbar", "id": "draw" },
    //                     { "type": "row", "grow": 1, "children": [
    //                         { "type": "panel", "name": "viewport", "grow": 1 },
    //                         { "type": "splitter" },
    //                         { "type": "panel", "name": "properties", "width": 260 } ] },
    //                     { "type": "panel", "name": "statusbar" } ] }
    //   }
    //
    // - commands：覆盖命令的名称、快捷键、图标、提示（只覆盖写出的字段；从文件里删掉后恢复为代码里的默认值）
    // - 菜单 / 工具栏的项："命令 ID"，"-" 或 "|" 为分隔线；菜单项可以是 { "title", "items" } 子菜单；
    //   工具栏项可以是 { "command", "text": true }（图标旁显示名称）
    // - 布局节点 type：row、column、menubar、toolbar（"id" 引用 toolbars，或直接写 "items"；"vertical"）、
    //   panel（"name"）、splitter（调整前一个兄弟的尺寸；"target": "next" 调整后一个）、separator、spacer、label（"text"）、
    //   titlebar（无边框窗口的标题栏：最小化 / 最大化 / 关闭按钮执行 window.minimize / window.maximize / window.close；
    //   "children" 放在图标右侧，通常是 menubar；"icon" 为图标文件，"title" 为初始标题，宿主可用 GetTitleBar 修改）、
    //   dock（停靠区："root" 为 DockSpace 的布局格式，"panels" 列出布局里没有、但可以由宿主随时显示的面板；"id" 用于 GetDock）
    // - 通用布局属性：width、height、minWidth、maxWidth、minHeight、maxHeight、grow、shrink、gap、
    //   padding（数字、[水平, 竖直] 或 [左, 上, 右, 下]）、align（start/center/end/stretch）、
    //   justify（start/center/end/space-between）、background（主题颜色名如 "Panel"，或 "#RRGGBB"）、visible
    // - 错误不中断：未知的命令 ID、面板名、类型、属性都记入警告，界面照常生成；JSON 语法错误时保留原界面
    class UiLayout
    {
    public:
        UiLayout(CommandRegistry& commands, std::string iconDir);
        ~UiLayout();

        // 宿主提供的面板：界面重建时会被回收、再放回新的位置（不会销毁），布局里没有引用的面板不显示
        // title：放进停靠区时标签上显示的名称（为空时用 name）
        void  RegisterPanel(const std::string& name, std::unique_ptr<Node> panel, std::string title = {});
        Node* GetPanel(const std::string& name) const;

        // 解析并应用：清空 parent 原有内容（推迟销毁，在事件回调里调用也安全）后按描述重建。
        // JSON 语法错误时返回 false，界面保持不变
        bool ApplyText(Node* parent, std::string_view jsonText);
        bool ApplyFile(Node* parent, const std::string& utf8Path);
        bool Apply(Node* parent, const JsonValue& doc);

        const std::string&              GetError()    const { return m_error; }     // 最近一次失败的原因
        const std::vector<std::string>& GetWarnings() const { return m_warnings; }

        MenuBar*   GetMenuBar()  const { return m_menuBar; }
        TitleBar*  GetTitleBar() const { return m_titleBar; }
        ToolBar*   GetToolBar(const std::string& id) const;
        DockSpace* GetDock(const std::string& id) const;

    private:
        struct BuildState;
        void ReclaimPanels();
        void RestoreCommandDefaults();
        std::unique_ptr<Node> BuildNode(const JsonValue& desc, const JsonValue& doc, bool parentIsRow, BuildState& st);
        void ApplyCommonStyle(Node* node, const JsonValue& desc, bool isPanel);

        struct CommandDefaults { std::string label, tooltip, icon, shortcut; };

        CommandRegistry& m_commands;
        std::string      m_iconDir;

        struct PanelSlot
        {
            Node*                 node = nullptr;   // 面板本身（无论是否在树上）
            std::unique_ptr<Node> owned;            // 不在树上时由这里持有
            LayoutStyle           originalStyle;    // 注册时的布局，重建前恢复（描述里删掉的属性不残留）
            bool                  originalVisible = true;
            std::string           title;
        };
        std::map<std::string, PanelSlot> m_panels;

        std::unordered_map<std::string, CommandDefaults> m_commandDefaults;

        MenuBar*                        m_menuBar  = nullptr;
        TitleBar*                       m_titleBar = nullptr;
        std::map<std::string, ToolBar*> m_toolBars;
        std::map<std::string, DockSpace*> m_docks;          // id 为空的停靠区用自动生成的键
        std::string                     m_error;
        std::vector<std::string>        m_warnings;
    };
}
