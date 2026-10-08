#include "Data/CommandRegistry.h"
#include "Data/Json.h"
#include <algorithm>
#include <cctype>
#include <map>

namespace MiniGUI
{
    // =========================================================
    // 快捷键文本
    // =========================================================
    namespace
    {
        struct KeyName { const char* name; Key key; };

        // 第一个名称是规范写法（FormatKeyChord 使用）
        constexpr KeyName kKeyNames[] =
        {
            { "Esc", Key::Escape }, { "Escape", Key::Escape },
            { "Enter", Key::Enter }, { "Return", Key::Enter },
            { "Tab", Key::Tab }, { "Backspace", Key::Backspace },
            { "Del", Key::Delete }, { "Delete", Key::Delete },
            { "Ins", Key::Insert }, { "Insert", Key::Insert },
            { "Space", Key::Space },
            { "Home", Key::Home }, { "End", Key::End },
            { "PgUp", Key::PageUp }, { "PageUp", Key::PageUp },
            { "PgDn", Key::PageDown }, { "PageDown", Key::PageDown },
            { "Left", Key::Left }, { "Right", Key::Right }, { "Up", Key::Up }, { "Down", Key::Down },
        };

        bool IEquals(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            }
            return true;
        }

        std::string_view Trim(std::string_view s)
        {
            while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
            while (!s.empty() && s.back() == ' ')  s.remove_suffix(1);
            return s;
        }

        bool ParseKeyName(std::string_view name, Key& out)
        {
            if (name.size() == 1)
            {
                const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
                if (c >= 'A' && c <= 'Z')
                {
                    out = KeyFromLetter(c);
                    return true;
                }
                if (c >= '0' && c <= '9')
                {
                    out = static_cast<Key>(static_cast<int>(Key::Num0) + (c - '0'));
                    return true;
                }
            }
            if ((name[0] == 'F' || name[0] == 'f') && name.size() <= 3)
            {
                int n = 0;
                for (size_t i = 1; i < name.size(); ++i)
                {
                    if (!std::isdigit(static_cast<unsigned char>(name[i])))
                        return false;
                    n = n * 10 + (name[i] - '0');
                }
                if (n >= 1 && n <= 12)
                {
                    out = static_cast<Key>(static_cast<int>(Key::F1) + (n - 1));
                    return true;
                }
                return false;
            }
            for (const KeyName& k : kKeyNames)
            {
                if (IEquals(name, k.name))
                {
                    out = k.key;
                    return true;
                }
            }
            return false;
        }

        std::string KeyToName(Key key)
        {
            const int k = static_cast<int>(key);
            if (k >= static_cast<int>(Key::A) && k <= static_cast<int>(Key::Z))
                return std::string(1, static_cast<char>('A' + (k - static_cast<int>(Key::A))));
            if (k >= static_cast<int>(Key::Num0) && k <= static_cast<int>(Key::Num9))
                return std::string(1, static_cast<char>('0' + (k - static_cast<int>(Key::Num0))));
            if (k >= static_cast<int>(Key::F1) && k <= static_cast<int>(Key::F12))
                return "F" + std::to_string(k - static_cast<int>(Key::F1) + 1);
            for (const KeyName& n : kKeyNames)
            {
                if (n.key == key)
                    return n.name;
            }
            return {};
        }
    }

    bool ParseKeyChord(std::string_view text, KeyChord& out)
    {
        text = Trim(text);
        if (text.empty())
            return false;

        KeyChord chord;
        while (true)
        {
            // 最后一段是按键；"Ctrl++" 这种以 + 为键的写法不支持
            const size_t plus = text.find('+');
            const std::string_view part = Trim(text.substr(0, plus));
            if (part.empty())
                return false;

            if (plus == std::string_view::npos)
            {
                if (!ParseKeyName(part, chord.key))
                    return false;
                break;
            }

            if (IEquals(part, "Ctrl") || IEquals(part, "Control"))
                chord.modifiers |= static_cast<uint8_t>(ModifierKey::Ctrl);
            else if (IEquals(part, "Shift"))
                chord.modifiers |= static_cast<uint8_t>(ModifierKey::Shift);
            else if (IEquals(part, "Alt"))
                chord.modifiers |= static_cast<uint8_t>(ModifierKey::Alt);
            else
                return false;
            text = text.substr(plus + 1);
        }
        out = chord;
        return true;
    }

    std::string FormatKeyChord(const KeyChord& chord)
    {
        if (!chord.IsValid())
            return {};
        std::string s;
        if (chord.modifiers & static_cast<uint8_t>(ModifierKey::Ctrl))  s += "Ctrl+";
        if (chord.modifiers & static_cast<uint8_t>(ModifierKey::Shift)) s += "Shift+";
        if (chord.modifiers & static_cast<uint8_t>(ModifierKey::Alt))   s += "Alt+";
        return s + KeyToName(chord.key);
    }

    // =========================================================
    // 注册与查询
    // =========================================================
    CommandRegistry::~CommandRegistry()
    {
        UnbindShortcuts();
    }

    Command& CommandRegistry::Register(Command command)
    {
        auto it = m_commands.find(command.id);
        if (it == m_commands.end())
        {
            m_order.push_back(command.id);
            it = m_commands.emplace(command.id, std::move(command)).first;
        }
        else
        {
            it->second = std::move(command);
        }
        Rebind();
        return it->second;
    }

    const Command* CommandRegistry::Find(std::string_view id) const
    {
        const auto it = m_commands.find(std::string(id));
        return it == m_commands.end() ? nullptr : &it->second;
    }

    Command* CommandRegistry::Find(std::string_view id)
    {
        const auto it = m_commands.find(std::string(id));
        return it == m_commands.end() ? nullptr : &it->second;
    }

    bool CommandRegistry::IsEnabled(std::string_view id) const
    {
        const Command* c = Find(id);
        return c && c->execute && (!c->canExecute || c->canExecute());
    }

    bool CommandRegistry::IsChecked(std::string_view id) const
    {
        const Command* c = Find(id);
        return c && c->isChecked && c->isChecked();
    }

    bool CommandRegistry::IsToggle(std::string_view id) const
    {
        const Command* c = Find(id);
        return c && c->isChecked;
    }

    bool CommandRegistry::Execute(std::string_view id)
    {
        if (!IsEnabled(id))
            return false;
        // 复制一份：命令里可能重新注册命令（例如重新加载界面描述）
        const auto execute = Find(id)->execute;
        execute();
        NotifyStateChanged();
        return true;
    }

    std::string CommandRegistry::StripMnemonic(std::string_view label)
    {
        std::string s;
        s.reserve(label.size());
        for (size_t i = 0; i < label.size(); ++i)
        {
            // "(&S)" 整段去掉（中文菜单的写法）；"&File" 只去掉 &；"&&" 表示字面的 &
            if (label[i] == '(' && i + 3 < label.size() && label[i + 1] == '&' && label[i + 3] == ')')
            {
                i += 3;
                continue;
            }
            if (label[i] == '&')
            {
                if (i + 1 < label.size() && label[i + 1] == '&')
                {
                    s += '&';
                    ++i;
                }
                continue;
            }
            s += label[i];
        }
        return s;
    }

    std::string CommandRegistry::GetLabel(std::string_view id) const
    {
        const Command* c = Find(id);
        return c ? (c->label.empty() ? c->id : c->label) : std::string(id);
    }

    std::string CommandRegistry::GetTooltip(std::string_view id) const
    {
        const Command* c = Find(id);
        if (!c)
            return std::string(id);
        std::string tip = StripMnemonic(GetLabel(id));
        if (!c->shortcut.empty())
            tip += " (" + c->shortcut + ")";
        if (!c->tooltip.empty())
            tip += "\n" + c->tooltip;
        return tip;
    }

    void CommandRegistry::SetShortcut(std::string_view id, std::string shortcut)
    {
        if (Command* c = Find(id))
        {
            c->shortcut = std::move(shortcut);
            Rebind();
        }
    }

    void CommandRegistry::ApplyOverrides(const JsonValue& commands, std::vector<std::string>& warnings)
    {
        for (const auto& [id, fields] : commands.GetObject())
        {
            Command* c = Find(id);
            if (!c)
            {
                warnings.push_back("commands：没有名为 \"" + id + "\" 的命令");
                continue;
            }
            if (const JsonValue* v = fields.Find("label"))    c->label   = v->AsString();
            if (const JsonValue* v = fields.Find("tooltip"))  c->tooltip = v->AsString();
            if (const JsonValue* v = fields.Find("icon"))     c->icon    = v->AsString();
            if (const JsonValue* v = fields.Find("shortcut"))
            {
                KeyChord chord;
                const std::string& text = v->AsString();
                if (!text.empty() && !ParseKeyChord(text, chord))
                    warnings.push_back("commands." + id + "：无法识别的快捷键 \"" + text + "\"");
                else
                    c->shortcut = text.empty() ? std::string() : FormatKeyChord(chord);
            }
        }
        Rebind();
        NotifyStateChanged();
    }

    // =========================================================
    // 快捷键绑定
    // =========================================================
    void CommandRegistry::BindShortcuts(ShortcutTable& table)
    {
        UnbindShortcuts();
        m_table      = &table;
        m_tableAlive = table.GetLifetimeToken();
        Rebind();
    }

    void CommandRegistry::UnbindShortcuts()
    {
        if (m_table && !m_tableAlive.expired())
        {
            for (ShortcutTable::Id id : m_bound)
                m_table->Unregister(id);
        }
        m_bound.clear();
        m_table = nullptr;
        m_tableAlive.reset();
    }

    void CommandRegistry::Rebind()
    {
        if (!m_table)
            return;
        if (m_tableAlive.expired())
        {
            m_table = nullptr;      // 快捷键表已随 UIContext 销毁
            m_bound.clear();
            return;
        }
        for (ShortcutTable::Id id : m_bound)
            m_table->Unregister(id);
        m_bound.clear();

        for (const std::string& id : m_order)
        {
            const Command& c = m_commands.at(id);
            KeyChord chord;
            if (!ParseKeyChord(c.shortcut, chord))
                continue;
            ShortcutTable::Options opt;
            opt.allowInTextInput = c.allowInTextInput;
            m_bound.push_back(m_table->Register(chord.key, chord.modifiers, [this, id] { Execute(id); }, opt));
        }
    }

    std::vector<std::string> CommandRegistry::FindShortcutConflicts() const
    {
        std::map<std::string, std::vector<std::string>> byChord;
        for (const std::string& id : m_order)
        {
            KeyChord chord;
            if (ParseKeyChord(m_commands.at(id).shortcut, chord))
                byChord[FormatKeyChord(chord)].push_back(id);
        }
        std::vector<std::string> result;
        for (const auto& [chord, ids] : byChord)
        {
            if (ids.size() < 2)
                continue;
            std::string s = chord + "：";
            for (size_t i = 0; i < ids.size(); ++i)
                s += (i ? " 与 " : "") + ids[i];
            result.push_back(std::move(s));
        }
        return result;
    }

    // =========================================================
    // 状态变化通知
    // =========================================================
    CommandRegistry::ListenerId CommandRegistry::AddListener(std::function<void()> listener)
    {
        const ListenerId id = m_nextListener++;
        m_listeners.push_back({ id, std::move(listener) });
        return id;
    }

    void CommandRegistry::RemoveListener(ListenerId id)
    {
        m_listeners.erase(std::remove_if(m_listeners.begin(), m_listeners.end(),
                                         [id](const Listener& l) { return l.id == id; }),
                          m_listeners.end());
    }

    void CommandRegistry::NotifyStateChanged()
    {
        if (m_notifying)
            return;     // 订阅者里再次触发通知时不递归
        m_notifying = true;
        const auto listeners = m_listeners;     // 订阅者里可能增删订阅
        for (const Listener& l : listeners)
        {
            const bool stillRegistered = std::any_of(m_listeners.begin(), m_listeners.end(),
                                                     [&](const Listener& x) { return x.id == l.id; });
            if (stillRegistered && l.fn)
                l.fn();
        }
        m_notifying = false;
    }
}
