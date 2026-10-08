#pragma once
#include "Core/Math/Point3.hpp"
#include "Core/Math/Point2.hpp"
#include "Core/Math/Mat4.hpp"

namespace MiniCAD
{
    struct CameraState
    {
        Math::Point3 Target = {};
        double       Zoom   = 100.0;
    };

    class Camera
    {
    public:
        Camera(double width, double height);
 
        void Pan    (double dx, double dy);                          // 屏幕像素平移
        void Zoom   (double delta, int mouseX = 0, int mouseY = 0);  // 缩放，可选鼠标中心
        void Resize (double width, double height);
        void ZoomToBounds(double minX, double minY, double maxX, double maxY, double margin = 1.1);   // 居中并缩放到刚好容纳该范围（留边）

        Math::Mat4   GetView()      const;
        Math::Mat4   GetProj()      const;
        Math::Mat4   GetViewProj()  const { return m_viewProj; };

        Math::Point3 GetCameraPos() const;
        double       GetWidth()     const { return m_screenWidth; }
        double       GetHeight()    const { return m_screenHeight; }

        CameraState  GetState() const;
        void         SetState(const CameraState& s);

        Math::Point3 ScreenToWorld(int px, int py)               const;
        Math::Point2 WorldToScreen(const Math::Point3& worldPos) const;

    private:
        void UpdateViewProj();

    private:       
        double        m_screenWidth  = 1.0;
        double        m_screenHeight = 1.0; 
        double        m_height       = 10.0;         // 相机高度（仅用于 view）
        double        m_zoom         = 10.0;         // 正交缩放（核心）
        double        m_aspect       = 1.0;          // 宽高比（仅用于 proj）
        Math::Point3  m_target       = { };          // 观察中心    
        Math::Mat4    m_viewProj     = Math::Mat4::Identity();
        Math::Mat4    m_invViewProj  = Math::Mat4::Identity();
    };

}