#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MiniGUI
{
    class CheckBox;
    class ComboBox;
    class Label;
    class Node;
    class NumberBox;
    class TextBox;

    // 读取模型的值；返回 std::nullopt 表示"多种"（多个对象取值不同）
    template<typename T>
    using Getter = std::function<std::optional<T>()>;
    // 把用户修改的值写回模型；为空表示只读（控件禁用 / 输入框只读）
    template<typename T>
    using Setter = std::function<void(const T&)>;

    // 数据绑定：把编辑控件与数据模型的 getter / setter 连接起来（特性面板的常见用法）。
    // - Refresh：按 getter 更新所有控件。正在编辑的输入框（有焦点）跳过，不打断用户输入
    // - 用户修改控件：调用 setter 写回模型，然后调用 SetOnChanged 设置的回调（例如通知命令状态、重绘视口），
    //   再 Refresh 一次（写回后其他字段可能也变了）
    // - "多种"：复选框显示不确定状态；数值框、输入框清空并显示 "*多种*"；下拉框不选中任何项并显示 "*多种*"
    // 生命周期：绑定集合持有控件的裸指针，应与控件同时销毁（通常作为面板的成员）；
    // 控件先于绑定集合销毁时先调用 Clear。绑定集合先销毁时，控件的回调自动失效
    class BindingSet
    {
    public:
        BindingSet();
        ~BindingSet();
        BindingSet(const BindingSet&) = delete;
        BindingSet& operator=(const BindingSet&) = delete;

        void BindCheck (CheckBox* box, Getter<bool> get, Setter<bool> set = {});
        void BindNumber(NumberBox* box, Getter<double> get, Setter<double> set = {});
        void BindText  (TextBox* box, Getter<std::string> get, Setter<std::string> set = {});   // Enter 或失去焦点时写回
        // items 不为空时每次 Refresh 更新下拉项（例如图层列表会变化）
        void BindChoice(ComboBox* box, Getter<int> get, Setter<int> set = {},
                        std::function<std::vector<std::string>()> items = {});
        void BindLabel (Label* label, std::function<std::string()> get);

        void SetOnChanged(std::function<void()> cb) { m_onChanged = std::move(cb); }

        void   Refresh();
        void   Clear();
        size_t Size() const { return m_entries.size(); }

        struct Entry;   // 实现细节

    private:
        void Add(std::shared_ptr<Entry> entry);
        void Changed();

        std::vector<std::shared_ptr<Entry>> m_entries;
        std::function<void()>               m_onChanged;
        std::shared_ptr<BindingSet*>        m_self;     // 控件回调通过它找到绑定集合；集合销毁时置空
    };
}
