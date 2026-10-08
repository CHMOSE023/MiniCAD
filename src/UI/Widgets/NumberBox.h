#pragma once
#include "Core/Node.h"
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace MiniGUI
{
    class TextBox;

    // 计算简单算术表达式：+ - * / 括号、一元负号、小数和科学计数法。
    // 非法输入返回空（NumberBox 用它让用户直接输入 "1200/3+50" 这类尺寸）
    std::optional<double> EvaluateExpression(std::string_view text);

    // 数值输入框：输入框 + 右侧上下微调按钮。
    // - Enter 或失去焦点时提交；非法输入恢复为原值；结果限制在范围内并按小数位数取整
    // - ↑↓ 按步长调整（Shift ×10）；获得焦点时滚轮也可调整
    class NumberBox : public Node
    {
    public:
        explicit NumberBox(double value = 0.0,
                           double minValue = -std::numeric_limits<double>::infinity(),
                           double maxValue = std::numeric_limits<double>::infinity(),
                           double step = 1.0, int decimals = 2);

        void   SetValue(double value);                 // 不触发 OnChanged；同时结束"多种"状态
        // 多个对象取值不同：输入框清空并显示占位文字"*多种*"；直接回车或失去焦点保持原样，输入数字后对所有对象生效
        void   SetMixed();
        bool   IsMixed() const { return m_mixed; }
        double GetValue() const { return m_value; }
        void   SetRange(double minValue, double maxValue);
        void   SetStep(double step) { m_step = step; }
        void   SetDecimals(int decimals);
        void   SetOnChanged(std::function<void(double)> cb) { m_onChanged = std::move(cb); }

        TextBox* GetTextBox() const { return m_box; }

    protected:
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void OnPointerEvent(PointerEvent& e) override;

    private:
        friend class SpinButtons;

        void   Commit();                               // 解析输入框文字
        void   Apply(double value, bool notify);
        bool   m_mixed = false;
        void   StepBy(double steps);
        double Normalize(double value) const;
        std::string Format(double value) const;

        TextBox* m_box = nullptr;
        double m_value;
        double m_min, m_max, m_step;
        int    m_decimals;
        std::function<void(double)> m_onChanged;
    };
}
