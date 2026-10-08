#pragma once
#include "Core/Event.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace MiniGUI
{
    // 全局快捷键表：按键在分发给焦点节点之前统一匹配，匹配成功即消费，不再分发。
    // 规则（沿用 MiniCAD HandleShortcuts 的经验）：
    //   - 只响应按下沿，忽略自动重复：弹出文件对话框会抢走焦点、抬起消息丢失，
    //     若允许重复会无限触发
    //   - 文本输入控件持有焦点时默认让位，Ctrl+C/V/Z 等交给输入框；
    //     功能键（F3 捕捉、F8 正交…）这类不参与文字编辑的可以设置 allowInTextInput
    //   - 修饰键必须完全一致：注册 Ctrl+S 不会响应 Ctrl+Shift+S
    class ShortcutTable
    {
    public:
        using Id = uint32_t;

        struct Options
        {
            bool allowInTextInput = false;
            bool allowRepeat      = false;
        };

        Id   Register(Key key, uint8_t modifiers, std::function<void()> action);
        Id   Register(Key key, uint8_t modifiers, std::function<void()> action, const Options& options);
        void Unregister(Id id);
        void Clear() { m_entries.clear(); }

        // 返回是否匹配并执行了某个快捷键
        bool Process(Key key, uint8_t modifiers, bool repeat, bool textInputActive);

        // 生命周期令牌：注册快捷键的一方（命令注册表）可能比快捷键表活得久，注销前用它判断表是否还在
        std::weak_ptr<void> GetLifetimeToken() const { return m_alive; }

    private:
        struct Entry
        {
            Id                    id;
            Key                   key;
            uint8_t               modifiers;
            Options               options;
            std::function<void()> action;
        };

        std::vector<Entry> m_entries;
        Id                 m_nextId = 1;
        std::shared_ptr<void> m_alive = std::make_shared<int>(0);
    };

    // 修饰键组合的便捷写法：Mods(ModifierKey::Ctrl, ModifierKey::Shift)
    template<typename... T>
    constexpr uint8_t Mods(T... keys) { return static_cast<uint8_t>((0 | ... | static_cast<uint8_t>(keys))); }
}
