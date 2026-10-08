#pragma once
#include "Core/Event.hpp"
#include "Core/ShortcutTable.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MiniGUI
{
    class JsonValue;

    // 快捷键：按键 + 修饰键。文本形式 "Ctrl+Shift+S"、"F3"、"Del"、"Ctrl+1"（大小写不敏感）
    struct KeyChord
    {
        Key     key       = Key::None;
        uint8_t modifiers = 0;

        bool IsValid() const { return key != Key::None; }
        bool operator==(const KeyChord&) const = default;
    };

    bool        ParseKeyChord(std::string_view text, KeyChord& out);   // 空串或无法识别返回 false
    std::string FormatKeyChord(const KeyChord& chord);                  // 规范写法："Ctrl+Shift+S"

    // 命令：界面上一个"动作"的全部描述。菜单项、工具栏按钮、快捷键都引用命令 ID，
    // 名称、图标、快捷键、可用/选中状态只在这里定义一次（也可以由界面描述文件覆盖）
    struct Command
    {
        std::string id;                     // "file.save"
        std::string label;                  // "保存(&S)"：菜单显示，& 后为助记符
        std::string tooltip;                // 工具栏悬浮提示的补充说明（第二行）
        std::string icon;                   // 图标路径（相对界面资源目录），空表示没有图标
        std::string shortcut;               // "Ctrl+S"；空表示没有
        bool        allowInTextInput = false;   // 输入框有焦点时快捷键是否仍然生效（F3、F8 这类功能键）

        std::function<void()> execute;
        std::function<bool()> canExecute;   // 为空表示总是可用
        std::function<bool()> isChecked;    // 为空表示不是切换类命令
    };

    // 命令注册表。
    // - Execute 前检查 canExecute；执行后自动 NotifyStateChanged
    // - BindShortcuts 把所有命令的快捷键注册到 ShortcutTable；快捷键变化（ApplyOverrides、SetShortcut）后自动重新绑定
    // - 状态变化通知：宿主的数据变化时（选中集、当前工具…）调用 NotifyStateChanged，
    //   工具栏、数据绑定等订阅者据此刷新可用/选中状态
    class CommandRegistry
    {
    public:
        using ListenerId = uint32_t;

        CommandRegistry() = default;
        ~CommandRegistry();
        CommandRegistry(const CommandRegistry&) = delete;
        CommandRegistry& operator=(const CommandRegistry&) = delete;

        Command&       Register(Command command);           // 同 ID 重复注册时替换
        const Command* Find(std::string_view id) const;
        Command*       Find(std::string_view id);
        const std::vector<std::string>& GetIds() const { return m_order; }   // 注册顺序

        bool Execute(std::string_view id);                  // 不存在或不可用返回 false
        bool IsEnabled(std::string_view id) const;          // 不存在返回 false
        bool IsChecked(std::string_view id) const;
        bool IsToggle(std::string_view id) const;

        // 显示用：菜单项文字（含助记符）、去掉助记符的名称、工具栏提示（名称 (快捷键) + 说明）
        std::string GetLabel(std::string_view id) const;
        static std::string StripMnemonic(std::string_view label);    // "保存(&S)" → "保存"；"&File" → "File"
        std::string GetTooltip(std::string_view id) const;

        void SetShortcut(std::string_view id, std::string shortcut);

        // 界面描述文件的 "commands" 部分：{ "file.save": { "label": …, "shortcut": …, "icon": …, "tooltip": … } }。
        // 只覆盖给出的字段；未注册的 ID 写入 warnings
        void ApplyOverrides(const JsonValue& commands, std::vector<std::string>& warnings);

        // 快捷键
        void BindShortcuts(ShortcutTable& table);
        void UnbindShortcuts();
        // 两个命令用了同一个快捷键时返回描述（"Ctrl+S：file.save 与 file.saveAll"）
        std::vector<std::string> FindShortcutConflicts() const;

        // 状态变化通知
        ListenerId AddListener(std::function<void()> listener);
        void       RemoveListener(ListenerId id);
        void       NotifyStateChanged();

        // 生命周期令牌：订阅者（工具栏）可能比注册表活得久，析构时用它判断注册表是否还在
        std::weak_ptr<void> GetLifetimeToken() const { return m_alive; }

    private:
        void Rebind();

        std::unordered_map<std::string, Command> m_commands;
        std::vector<std::string>                 m_order;

        ShortcutTable*                  m_table = nullptr;
        std::weak_ptr<void>             m_tableAlive;      // 快捷键表（属于 UIContext）可能先销毁
        std::vector<ShortcutTable::Id>  m_bound;

        struct Listener { ListenerId id; std::function<void()> fn; };
        std::vector<Listener> m_listeners;
        ListenerId            m_nextListener = 1;
        bool                  m_notifying    = false;
        std::shared_ptr<void> m_alive = std::make_shared<int>(0);
    };
}
