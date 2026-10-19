#pragma once
#include "Core/Node.h"
#include "Core/Event.hpp"
#include "Viewport/Camera.h"
#include <functional>

namespace MiniCAD
{
    // 视图方块（M6，同 AutoCAD 的 ViewCube）：叠在绘图区右上角，按当前视线方向画出立方体与指南针。
    //   - 立方体按 3×3×3 分块：6 个面中央、12 条棱中间、8 个角各是一块，点哪一块就从那个方向看
    //     （面 → 正视，棱 → 两面之间 45°，角 → 等轴测，共 26 个方向），切换后缩放到全图、可撤销
    //   - 悬停：整块高亮（角块同时亮三个面，棱块亮两个面）
    //   - 面上的字（上 / 下 / 前 / 后 / 左 / 右）贴在面上，跟着面一起转；指南针圆环上：东（+X）/ 南（−Y）/ 西（−X）/ 北（+Y），
    //     点字母转到从那个方向看（仰角不变）
    //   - 拖动：环绕（同 Shift + 中键）
    class ViewCube : public MiniGUI::Node
    {
    public:
        struct Callbacks
        {
            std::function<const Camera*()>              camera;    // 当前文档的相机；没有文档时返回空
            std::function<void(double az, double el)>   setView;   // 切换视图方向（度）
            std::function<void(double dx, double dy)>   orbit;     // 拖动环绕（像素）
        };

        explicit ViewCube(Callbacks callbacks);

        // 局部坐标处点击后的视图方向（方位角 / 仰角，度），不在方块或指南针字母上返回 false
        bool PickView(MiniGUI::Vec2 local, double& azimuthDeg, double& elevationDeg) const;
        // 视图方向（从目标看向眼睛）→ 方位角 / 仰角（度）；正上、正下方时方位角取 −90°（屏幕上方为 +Y，与俯视一致）
        static void DirectionToView(const Math::Vec3& eye, double& azimuthDeg, double& elevationDeg);
        // 方块大小与位置（局部坐标），测试按它算点击位置
        float           Scale() const;
        MiniGUI::Vec2   Center() const;
        MiniGUI::Vec2   Project(const Math::Vec3& w) const;   // 世界方向 → 局部坐标

    protected:
        void OnPaint(MiniGUI::DrawList& dl, const MiniGUI::Rect& screenRect) override;
        void OnPointerEvent(MiniGUI::PointerEvent& e) override;
        MiniGUI::CursorShape GetCursor(MiniGUI::Vec2 local) const override { (void)local; return MiniGUI::CursorShape::Arrow; }

    private:
        // 命中的部分：立方体的一块（X / Y / Z 各为 −1、0、1，不全为 0），或指南针的一个字母
        struct Hit
        {
            int  X = 0, Y = 0, Z = 0;
            int  Compass = -1;   // 0 东、1 南、2 西、3 北
            bool Block() const { return X != 0 || Y != 0 || Z != 0; }
            bool operator==(const Hit&) const = default;
        };

        Hit HitTest(MiniGUI::Vec2 local) const;

        Callbacks       m_cb;
        Hit             m_hover;
        bool            m_pressed  = false;
        bool            m_dragging = false;
        MiniGUI::Vec2   m_pressPos;
        MiniGUI::Vec2   m_lastPos;
    };
}
