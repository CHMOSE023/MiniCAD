#pragma once
#include <DirectXMath.h>

using namespace DirectX;

namespace MiniCAD 
{

    class Camera
    {
    public:
        Camera(float width, float height);
 
        void Pan    (float dx, float dy);                          // 屏幕像素平移
        void Zoom   (float delta, int mouseX = 0, int mouseY = 0); // 缩放，可选鼠标中心
        void Resize (float width, float height);

        // void Update (float dx, float dy, float scroll, bool isPanning, int mouseX = 1, int mouseY = 1);

        XMMATRIX GetView()      const;
        XMMATRIX GetProj()      const;
        XMMATRIX GetViewProj()  const { return m_viewProj; };

        XMFLOAT3 GetCameraPos() const;
        float    GetWidth()     const { return m_screenWidth; }
        float    GetHeight()    const { return m_screenHeight; }

        XMFLOAT3 ScreenToWorld(int px, int py)                    const;        
        XMFLOAT2 WorldToScreen(const DirectX::XMFLOAT3& worldPos) const;

    private:
        void UpdateViewProj();

    private:       
        XMFLOAT3 m_target       = { 0, 0, 0 };  // 观察中心    
        float    m_height       = 10.0f;        // 相机高度（仅用于 view）
        float    m_zoom         = 10.0f;        // 正交缩放（核心）
		float    m_aspect       = 1.0f;         // 宽高比（仅用于 proj）
        float    m_screenWidth  = 1.0f;
        float    m_screenHeight = 1.0f;

        XMMATRIX m_viewProj    = XMMatrixIdentity();
        XMMATRIX m_invViewProj = XMMatrixIdentity();
    };

}