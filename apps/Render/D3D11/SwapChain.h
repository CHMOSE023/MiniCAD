#pragma once
#include "Device.h"  
#include <dxgi1_2.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <dxgiformat.h>
namespace MiniCAD
{
    class SwapChain
    {
    public:
        struct Options
        {
            DXGI_FORMAT backBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
            DXGI_FORMAT depthFormat      = DXGI_FORMAT_D32_FLOAT;
            UINT        bufferCount      = 2;
            bool        enableVSync      = true;
            bool        allowTearing     = false;
        };

        SwapChain() = default;


        void Initialize(Device* device, HWND hwnd, UINT width, UINT height, const Options& opt = Options{});

        void Resize(UINT width, UINT height);
        void Present();

        ID3D11RenderTargetView* GetRTV()       const { return m_rtv.Get(); }
        ID3D11DepthStencilView* GetDSV()       const { return m_dsv.Get(); }
        D3D11_VIEWPORT          GetViewport()  const { return m_viewport; }
        IDXGISwapChain1*        GetSwapChain() const { return m_swapChain.Get(); }
         
    private:
        void CreateSwapChain();
        void CreateSizeDependent();

    private:
        HWND      m_hwnd   = nullptr;
        UINT      m_width  = 1;
        UINT      m_height = 1;
        Options   m_opt    = {};
        Device*   m_device = nullptr;

        D3D11_VIEWPORT  m_viewport{};

        Microsoft::WRL::ComPtr<IDXGISwapChain1>        m_swapChain;

        Microsoft::WRL::ComPtr<ID3D11Texture2D>        m_backBuffer;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;

        Microsoft::WRL::ComPtr<ID3D11Texture2D>        m_depth;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_dsv;

    };
}