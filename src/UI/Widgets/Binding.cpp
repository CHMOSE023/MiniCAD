#include "Widgets/Binding.h"
#include "Widgets/ComboBox.h"
#include "Widgets/Controls.h"
#include "Widgets/Label.h"
#include "Widgets/NumberBox.h"
#include "Widgets/TextBox.h"

namespace MiniGUI
{
    namespace
    {
        constexpr const char* kMixed = "*多种*";
    }

    struct BindingSet::Entry
    {
        std::weak_ptr<BindingSet*> owner;

        virtual ~Entry() = default;
        virtual void Refresh() = 0;

        // 写回模型之后通知绑定集合（集合可能已在 setter 里被销毁）
        void NotifyOwner()
        {
            if (auto o = owner.lock(); o && *o)
                (*o)->Changed();
        }
    };

    namespace
    {
        struct CheckEntry : BindingSet::Entry
        {
            CheckBox*    box;
            Getter<bool> get;
            void Refresh() override
            {
                const auto v = get();
                box->SetState(!v ? CheckBox::State::Indeterminate : (*v ? CheckBox::State::Checked : CheckBox::State::Unchecked));
            }
        };

        struct NumberEntry : BindingSet::Entry
        {
            NumberBox*     box;
            Getter<double> get;
            void Refresh() override
            {
                if (box->GetTextBox()->HasFocus())
                    return;     // 正在输入
                if (const auto v = get())
                    box->SetValue(*v);
                else
                    box->SetMixed();
            }
        };

        struct TextEntry : BindingSet::Entry
        {
            TextBox*            box;
            Getter<std::string> get;
            Setter<std::string> set;
            std::string         placeholder;    // 绑定前的占位文字
            std::string         shown;          // 最近一次显示的模型值（判断失去焦点时是否改过）
            bool                mixed = false;

            void Refresh() override
            {
                if (box->HasFocus())
                    return;
                const auto v = get();
                mixed = !v;
                shown = v ? *v : std::string();
                if (box->GetText() != shown)
                    box->SetText(shown);
                box->SetPlaceholder(mixed ? kMixed : placeholder);
            }

            void Commit(const std::string& text)
            {
                if (!set || text == shown)
                    return;     // 没改（"多种"状态下保持空白也算没改）
                shown = text;
                set(text);
                NotifyOwner();
            }
        };

        struct ChoiceEntry : BindingSet::Entry
        {
            ComboBox*   box;
            Getter<int> get;
            std::function<std::vector<std::string>()> items;
            std::string placeholder;

            void Refresh() override
            {
                if (items)
                {
                    std::vector<std::string> list = items();
                    if (list != box->GetItems())
                        box->SetItems(std::move(list));
                }
                const auto v = get();
                box->SetSelectedIndex(v ? *v : -1);
                box->SetPlaceholder(v ? placeholder : std::string(kMixed));
            }
        };

        struct LabelEntry : BindingSet::Entry
        {
            Label*                       label;
            std::function<std::string()> get;
            void Refresh() override { label->SetText(get()); }
        };
    }

    BindingSet::BindingSet()
        : m_self(std::make_shared<BindingSet*>(this))
    {
    }

    BindingSet::~BindingSet()
    {
        *m_self = nullptr;
    }

    void BindingSet::Add(std::shared_ptr<Entry> entry)
    {
        entry->owner = m_self;
        entry->Refresh();
        m_entries.push_back(std::move(entry));
    }

    void BindingSet::Changed()
    {
        if (m_onChanged)
        {
            auto cb = m_onChanged;
            cb();
        }
        Refresh();
    }

    void BindingSet::Refresh()
    {
        const auto entries = m_entries;     // getter 里可能修改绑定
        for (const auto& e : entries)
            e->Refresh();
    }

    void BindingSet::Clear()
    {
        m_entries.clear();
    }

    // =========================================================
    // 各类控件
    // =========================================================
    void BindingSet::BindCheck(CheckBox* box, Getter<bool> get, Setter<bool> set)
    {
        auto e = std::make_shared<CheckEntry>();
        e->box = box;
        e->get = std::move(get);
        box->SetEnabled(static_cast<bool>(set));
        std::weak_ptr<CheckEntry> weak = e;
        box->SetOnChanged([weak, set = std::move(set)](bool checked)
        {
            if (auto entry = weak.lock(); entry && set)
            {
                set(checked);
                entry->NotifyOwner();
            }
        });
        Add(std::move(e));
    }

    void BindingSet::BindNumber(NumberBox* box, Getter<double> get, Setter<double> set)
    {
        auto e = std::make_shared<NumberEntry>();
        e->box = box;
        e->get = std::move(get);
        box->SetEnabled(static_cast<bool>(set));
        std::weak_ptr<NumberEntry> weak = e;
        box->SetOnChanged([weak, set = std::move(set)](double value)
        {
            if (auto entry = weak.lock(); entry && set)
            {
                set(value);
                entry->NotifyOwner();
            }
        });
        Add(std::move(e));
    }

    void BindingSet::BindText(TextBox* box, Getter<std::string> get, Setter<std::string> set)
    {
        auto e = std::make_shared<TextEntry>();
        e->box         = box;
        e->get         = std::move(get);
        e->set         = std::move(set);
        e->placeholder = box->GetPlaceholder();
        box->SetReadOnly(!e->set);
        std::weak_ptr<TextEntry> weak = e;
        box->SetOnSubmit([weak](const std::string& text)
        {
            if (auto entry = weak.lock())
                entry->Commit(text);
        });
        box->SetOnFocusChanged([weak, box](bool focused)
        {
            if (focused)
                return;
            if (auto entry = weak.lock())
            {
                entry->Commit(box->GetText());
                entry->Refresh();       // 编辑期间模型可能变了
            }
        });
        Add(std::move(e));
    }

    void BindingSet::BindChoice(ComboBox* box, Getter<int> get, Setter<int> set,
                                std::function<std::vector<std::string>()> items)
    {
        auto e = std::make_shared<ChoiceEntry>();
        e->box         = box;
        e->get         = std::move(get);
        e->items       = std::move(items);
        e->placeholder = box->GetPlaceholder();
        box->SetEnabled(static_cast<bool>(set));
        std::weak_ptr<ChoiceEntry> weak = e;
        box->SetOnChanged([weak, set = std::move(set)](int index)
        {
            if (auto entry = weak.lock(); entry && set && index >= 0)
            {
                set(index);
                entry->NotifyOwner();
            }
        });
        Add(std::move(e));
    }

    void BindingSet::BindLabel(Label* label, std::function<std::string()> get)
    {
        auto e = std::make_shared<LabelEntry>();
        e->label = label;
        e->get   = std::move(get);
        Add(std::move(e));
    }
}
