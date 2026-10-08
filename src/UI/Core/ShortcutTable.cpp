#include "Core/ShortcutTable.h"
#include <algorithm>

namespace MiniGUI
{
    ShortcutTable::Id ShortcutTable::Register(Key key, uint8_t modifiers, std::function<void()> action)
    {
        return Register(key, modifiers, std::move(action), Options{});
    }

    ShortcutTable::Id ShortcutTable::Register(Key key, uint8_t modifiers, std::function<void()> action, const Options& options)
    {
        const Id id = m_nextId++;
        m_entries.push_back(Entry{ id, key, modifiers, options, std::move(action) });
        return id;
    }

    void ShortcutTable::Unregister(Id id)
    {
        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [id](const Entry& e) { return e.id == id; }),
                        m_entries.end());
    }

    bool ShortcutTable::Process(Key key, uint8_t modifiers, bool repeat, bool textInputActive)
    {
        for (const Entry& e : m_entries)
        {
            if (e.key != key || e.modifiers != modifiers)
                continue;
            if (repeat && !e.options.allowRepeat)
                continue;
            if (textInputActive && !e.options.allowInTextInput)
                continue;

            // 复制一份再调用：回调里可能注销快捷键，导致 m_entries 重新分配
            auto action = e.action;
            if (action)
                action();
            return true;
        }
        return false;
    }
}
