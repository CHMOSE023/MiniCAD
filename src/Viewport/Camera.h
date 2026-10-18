#pragma once
#include "Core/Math/Point3.hpp"
#include "Core/Math/Point2.hpp"
#include "Core/Math/Vec3.hpp"
#include "Core/Math/Mat4.hpp"
#include "Core/GeomKernel/AABB.hpp"

namespace MiniCAD
{
    // 视图方向用“从目标看向眼睛”的方位角与仰角表示（度）：
    //   眼睛方向 e = (cos 仰角 · cos 方位角, cos 仰角 · sin 方位角, sin 仰角)
    //   俯视为方位角 −90°、仰角 90°（屏幕右 = +X、屏幕上 = +Y），前视为 (−90°, 0°)，西南等轴测为 (−135°, 35.26°)
    struct CameraState
    {
        Math::Point3 Target    = {};
        double       Zoom      = 100.0;
        double       Azimuth   = -90.0;
        double       Elevation = 90.0;
    };

    enum class StandardView { Top, Bottom, Front, Back, Left, Right, SWIso, SEIso, NEIso, NWIso };

    // 正交相机。俯视（平面视图）时与二维版本完全相同：只有平移与缩放，屏幕点落在 z = 0 平面上。
    // 三维视图（M6，见 docs/Solid/PLAN.md）：视图绕目标点旋转；屏幕点反算到 z = 0 平面（构造平面），
    // 视线与 XY 平面接近平行（如前视）时改为落在过目标点、垂直于视线的平面上
    class Camera
    {
    public:
        Camera(double width, double height);

        void Pan    (double dx, double dy);                          // 屏幕像素平移
        void Zoom   (double delta, double mouseX = 0, double mouseY = 0);  // 缩放（delta 为滚轮格数，正为放大），可选鼠标中心
        void Resize (double width, double height);
        void ZoomToBounds(double minX, double minY, double maxX, double maxY, double margin = 1.1);   // 居中并缩放到刚好容纳该范围（留边）
        void ZoomToBounds(const AABB& box, double margin = 1.1);    // 三维范围：按当前视图方向投影后容纳

        // ── 视图方向（M6）─────────────────────────────────────────
        void   SetView(double azimuthDeg, double elevationDeg);     // 仰角限制在 [−90°, 90°]
        void   SetStandardView(StandardView v);
        void   Orbit(double dxPixels, double dyPixels);             // 绕目标点环绕：左右拖动改方位角，上下拖动改仰角
        double GetAzimuth()   const { return m_azimuth; }
        double GetElevation() const { return m_elevation; }
        bool   IsPlanView()   const;                                // 正俯视（方位角 −90°、仰角 90°）：二维模式
        Math::Vec3 GetViewDir() const { return m_forward; }        // 视线方向（眼睛 → 目标），俯视为 (0, 0, −1)
        Math::Vec3 GetRight()   const { return m_right; }          // 屏幕向右在世界中的方向
        Math::Vec3 GetUp()      const { return m_up; }             // 屏幕向上在世界中的方向

        Math::Mat4   GetView()      const;
        Math::Mat4   GetProj()      const;
        Math::Mat4   GetViewProj()  const { return m_viewProj; };

        Math::Point3 GetCameraPos() const;
        Math::Point3 GetTarget()    const { return m_target; }
        double       GetWidth()     const { return m_screenWidth; }
        double       GetHeight()    const { return m_screenHeight; }
        // 1 屏幕像素对应的世界长度：只取决于缩放与视口高度，平移不改变它
        // （不要用 WorldToScreen 两点之差来求——远离原点时有舍入误差，平移也会让结果抖动）
        double       GetWorldPerPixel() const { return m_screenHeight > 0.0 ? m_zoom / m_screenHeight : 0.0; }

        CameraState  GetState() const;
        void         SetState(const CameraState& s);

        Math::Point3 ScreenToWorld(int px, int py)               const;
        Math::Point3 ScreenToWorld(double px, double py)         const;
        Math::Point2 WorldToScreen(const Math::Point3& worldPos) const;

    private:
        void UpdateBasis();
        void UpdateViewProj();

    private:
        double        m_screenWidth  = 1.0;
        double        m_screenHeight = 1.0;
        double        m_height       = 10.0;         // 相机高度（仅用于 view）
        double        m_zoom         = 10.0;         // 正交缩放（核心）
        double        m_aspect       = 1.0;          // 宽高比（仅用于 proj）
        Math::Point3  m_target       = { };          // 观察中心
        double        m_azimuth      = -90.0;
        double        m_elevation    = 90.0;
        Math::Vec3    m_right        = { 1, 0, 0 };
        Math::Vec3    m_up           = { 0, 1, 0 };
        Math::Vec3    m_forward      = { 0, 0, -1 };
        Math::Mat4    m_viewProj     = Math::Mat4::Identity();
        Math::Mat4    m_invViewProj  = Math::Mat4::Identity();
    };

}
