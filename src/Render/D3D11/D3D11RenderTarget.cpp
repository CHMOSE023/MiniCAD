#include "D3D11RenderTarget.h"
#include <d3d11.h>
#include <dxgiformat.h>
namespace MiniCAD
{
    void D3D11RenderTarget::Create(int width, int height)
    {  
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width                = width;
        desc.Height               = height;
        desc.MipLevels            = 1;
        desc.ArraySize            = 1;
        desc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count     = 1;
        desc.Usage                = D3D11_USAGE_DEFAULT;
        desc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        m_device->CreateTexture2D(&desc, nullptr, &m_texture); 
        m_device->CreateRenderTargetView  (m_texture.Get(), nullptr, &m_rtv);
        m_device->CreateShaderResourceView(m_texture.Get(), nullptr, &m_srv);
    }

    void D3D11RenderTarget::Resize(int width, int height)
    {
        if (width == m_width && height == m_height)
            return;

        m_width  = width;
        m_height = height;

        Release();
        Create(width, height);
    }

    void D3D11RenderTarget::Release()
    {
        m_srv.Reset();
        m_rtv.Reset();
        m_texture.Reset();

        m_width = 0;
        m_height = 0;
    }
}