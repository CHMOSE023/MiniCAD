#pragma once
#include "Core/Node.h"
#include "Style/Theme.hpp"
#include <functional>
#include <string>
#include <vector>

// 常用小控件：分隔线、复选框、单选按钮、滑块、进度条
namespace MiniGUI
{
    // 分隔线：水平（默认）或竖直，1 像素
    class Separator : public Node
    {
    public:
        explicit Separator(bool vertical = false, ColorRef color = Theme::BorderSubtle);

    protected:
        void OnPaint(DrawList& dl, const Rect& screenRect) override;

    private:
        ColorRef m_color;
    };

    // 带文字的开关类控件公共部分：勾选框/圆点 + 文字，点击或空格切换
    class ToggleBase : public Node
    {
    public:
        void SetText(std::string text);
        const std::string& GetText() const { return m_text; }

    protected:
        explicit ToggleBase(std::string text);

        Vec2 MeasureContent(Vec2 available) override;
        void OnPointerEvent(PointerEvent& e) override;
        void OnKeyEvent(KeyEvent& e) override;

        virtual void Activate() = 0;                                        // 被点击或按空格
        void DrawLabelAndFocus(DrawList& dl, const Rect& r, float boxSize);  // 文字与焦点框
        Rect BoxRect(const Rect& r, float boxSize) const;

        std::string m_text;
        bool        m_pressed = false;
    };

    // 复选框：勾选 / 未勾选 / 不确定（三态，例如"部分图层可见"）
    class CheckBox : public ToggleBase
    {
    public:
        enum class State { Unchecked, Checked, Indeterminate };

        explicit CheckBox(std::string text = {}, bool checked = false);

        void  SetChecked(bool checked) { SetState(checked ? State::Checked : State::Unchecked); }
        bool  IsChecked() const        { return m_state == State::Checked; }
        void  SetState(State state);
        State GetState() const         { return m_state; }

        void SetOnChanged(std::function<void(bool)> cb) { m_onChanged = std::move(cb); }

    protected:
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void Activate() override;

    private:
        State                     m_state = State::Unchecked;
        std::function<void(bool)> m_onChanged;
    };

    class RadioButton;

    // 单选组：同一组内只有一个按钮被选中。组对象由调用方持有（通常是所在面板的成员）；
    // 组和按钮谁先销毁都可以：组销毁时会解除与按钮的关联
    class RadioGroup
    {
    public:
        RadioGroup() = default;
        ~RadioGroup();
        RadioGroup(const RadioGroup&) = delete;
        RadioGroup& operator=(const RadioGroup&) = delete;

        int  GetValue() const { return m_value; }
        void SetValue(int value);
        void SetOnChanged(std::function<void(int)> cb) { m_onChanged = std::move(cb); }

    private:
        friend class RadioButton;
        void Add(RadioButton* b)    { m_buttons.push_back(b); }
        void Remove(RadioButton* b);

        std::vector<RadioButton*> m_buttons;
        int                       m_value = -1;
        std::function<void(int)>  m_onChanged;
    };

    class RadioButton : public ToggleBase
    {
    public:
        RadioButton(RadioGroup* group, int value, std::string text);
        ~RadioButton() override;

        bool IsSelected() const;
        int  GetValue() const { return m_value; }

    protected:
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void OnKeyEvent(KeyEvent& e) override;      // 上下左右在组内移动
        void Activate() override;

    private:
        friend class RadioGroup;
        RadioGroup* m_group;
        int         m_value;
    };

    // 滑块：拖动或点击轨道设置数值；方向键 / 滚轮按步长调整
    class Slider : public Node
    {
    public:
        Slider(float minValue = 0.0f, float maxValue = 1.0f, float value = 0.0f);

        void  SetRange(float minValue, float maxValue);
        void  SetStep(float step) { m_step = step; }        // 0 表示连续
        void  SetValue(float value);
        float GetValue() const { return m_value; }
        void  SetOnChanged(std::function<void(float)> cb) { m_onChanged = std::move(cb); }

    protected:
        Vec2 MeasureContent(Vec2 available) override;
        void OnPaint(DrawList& dl, const Rect& screenRect) override;
        void OnPointerEvent(PointerEvent& e) override;
        void OnKeyEvent(KeyEvent& e) override;

    private:
        void  SetValueFromX(float localX);
        float Snap(float v) const;
        float Fraction() const;

        float m_min, m_max, m_value;
        float m_step     = 0.0f;
        bool  m_dragging = false;
        std::function<void(float)> m_onChanged;
    };

    // 进度条：0～1；SetIndeterminate 显示为不确定进度（静止的条纹，不做动画以保持空闲零开销）
    class ProgressBar : public Node
    {
    public:
        explicit ProgressBar(float value = 0.0f);

        void  SetValue(float value);
        float GetValue() const { return m_value; }
        void  SetIndeterminate(bool indeterminate) { m_indeterminate = indeterminate; Invalidate(); }

    protected:
        Vec2 MeasureContent(Vec2 available) override;
        void OnPaint(DrawList& dl, const Rect& screenRect) override;

    private:
        float m_value;
        bool  m_indeterminate = false;
    };
}
