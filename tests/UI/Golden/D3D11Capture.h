#pragma once
#include "D3D11/D3D11Backend.h"
#include "Software/SoftwareBackend.h"
#include <memory>
#include <string>
#include <wrl/client.h>
#include <d3d11.h>

namespace MiniGUI::Test
{
    // 离屏 D3D11 渲染 + 读回像素，用来和软件光栅的结果比较。
    // 优先使用硬件设备，没有显卡（例如 CI 虚拟机）时退回 WARP
    class D3D11Capture
    {
    public:
        bool Initialize(bool forceWarp = false);

        D3D11Backend&      GetBackend()     { return *m_backend; }
        const std::string& GetAdapterName() const { return m_adapterName; }

        void          BeginFrame(int width, int height, Color32 clearColor);   // 创建并绑定渲染目标，清屏
        SoftwareImage Readback();

    private:
        Microsoft::WRL::ComPtr<ID3D11Device>           m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext>    m_context;
        Microsoft::WRL::ComPtr<ID3D11Texture2D>        m_target;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
        std::unique_ptr<D3D11Backend>                  m_backend;
        std::string                                    m_adapterName;
        int m_width  = 0;
        int m_height = 0;
    };
}
