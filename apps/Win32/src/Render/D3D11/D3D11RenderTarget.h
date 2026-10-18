#pragma once 
#include <wrl/client.h>
#include <d3d11.h>
#include "Render/IRenderTarget.h"
namespace MiniCAD
{
	class D3D11RenderTarget :public IRenderTarget
    {
    public:
        D3D11RenderTarget(ID3D11Device* device) : m_device(device) {}

        virtual void Create( int width, int height) override;
        virtual void Resize( int width, int height) override;
        virtual void Release() override;
         
        int GetWidth () const { return m_width; }
        int GetHeight() const { return m_height; }

        virtual void* GetNativeHandle()         const override { return m_rtv.Get(); }
        virtual void* GetNativeShaderResource() const override { return m_srv.Get(); }
        ID3D11Texture2D* GetTexture() const { return m_texture.Get(); }
        // 深度缓冲（M6：三维视图里着色面写深度、线按深度测试；二维显示不写深度，测试总能通过）
        ID3D11DepthStencilView* GetDepthView() const { return m_dsv.Get(); }

    private:
        Microsoft::WRL::ComPtr<ID3D11Texture2D>          m_texture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   m_rtv;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;    
        Microsoft::WRL::ComPtr<ID3D11Texture2D>          m_depth;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView>   m_dsv;

        ID3D11Device*                                    m_device = nullptr;
        int m_width = 0;
        int m_height = 0;
    };
}
