#pragma once
#include "Render/IRenderBackend.h"
#include <unordered_map>
#include <wrl/client.h>
#include <d3d11.h>

namespace MiniGUI
{
    // D3D11 渲染后端。
    // 设备和上下文由宿主创建并传入（MiniCAD 可以把自己的设备交给它共用），后端只增加引用计数。
    // Render 画到调用时已绑定的渲染目标上，并会修改管线状态（IA/VS/PS/RS/OM），
    // 宿主在之后绘制自己的内容前需要重新设置所需状态。
    class D3D11Backend : public IRenderBackend
    {
    public:
        D3D11Backend(ID3D11Device* device, ID3D11DeviceContext* context);
        ~D3D11Backend() override;

        D3D11Backend(const D3D11Backend&) = delete;
        D3D11Backend& operator=(const D3D11Backend&) = delete;

        virtual TextureId CreateTexture (int width, int height, TextureFormat format, const void* pixels = nullptr) override;
        virtual void      UpdateTexture (TextureId id, const RectI& region, const void* pixels, int pitch) override;
        virtual void      DestroyTexture(TextureId id) override;
        virtual void      Render        (const DrawData& data) override;

        // ── D3D11 专属 ──────────────────────────────────────────
        // 把宿主的 SRV（例如 CAD 视口的渲染结果）登记为纹理，控件可以直接显示；用 DestroyTexture 注销
        TextureId RegisterExternalTexture(ID3D11ShaderResourceView* srv);

        ID3D11Device*        GetDevice()  const { return m_device.Get(); }
        ID3D11DeviceContext* GetContext() const { return m_context.Get(); }

    private:
        void CreatePipeline();
        bool EnsureBuffers(size_t vertexCount, size_t indexCount);
        void SetupRenderState(const DrawData& data, float fbWidth, float fbHeight);

        struct TextureEntry
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D>          texture;   // 外部纹理为空
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
            int  width    = 0;
            int  height   = 0;
            bool external = false;
        };

    private:
        Microsoft::WRL::ComPtr<ID3D11Device>             m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext>      m_context;

        Microsoft::WRL::ComPtr<ID3D11VertexShader>       m_vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>        m_ps;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>        m_layout;
        Microsoft::WRL::ComPtr<ID3D11Buffer>             m_cb;
        Microsoft::WRL::ComPtr<ID3D11BlendState>         m_blend;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>    m_raster;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState>  m_depth;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>       m_sampler;

        Microsoft::WRL::ComPtr<ID3D11Buffer>             m_vb;
        Microsoft::WRL::ComPtr<ID3D11Buffer>             m_ib;
        size_t m_vbCapacity = 0;   // 顶点个数
        size_t m_ibCapacity = 0;   // 索引个数

        std::unordered_map<TextureId, TextureEntry> m_textures;
        TextureId m_nextTextureId = 1;
    };
}
