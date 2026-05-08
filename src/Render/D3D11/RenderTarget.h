#pragma once 
#include <wrl/client.h>
#include <d3d11.h>
 
namespace MiniCAD
{
    class RenderTarget
    {
    public:
        void Create(ID3D11Device* device, int width, int height);
        void Resize(ID3D11Device* device, int width, int height);
        void Release();

        ID3D11ShaderResourceView* GetSRV() const { return m_srv.Get(); }
        ID3D11RenderTargetView*   GetRTV() const { return m_rtv.Get(); } 

        int GetWidth()  const { return m_width; }
        int GetHeight() const { return m_height; }

    private:
        Microsoft::WRL::ComPtr<ID3D11Texture2D>          m_texture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   m_rtv;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;   

        int m_width = 0;
        int m_height = 0;
    };
}
