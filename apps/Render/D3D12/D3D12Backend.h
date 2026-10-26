#pragma once
#include "Render/IRenderBackend.h"
#include "D3D12/D3D12Context.h"
#include <memory>
#include <unordered_map>

namespace MiniGUI
{
    // D3D12 界面后端，功能同 D3D11Backend。
    // 命令录进上下文的命令列表（D3D12Context::GetList），画到 D3D12Context::GetUiTarget 指定的渲染目标；
    // 提交与呈现由宿主负责。顶点 / 索引每帧从上下文的上传缓冲分配，纹理数据经上传缓冲复制到默认堆
    class D3D12Backend : public IRenderBackend
    {
    public:
        explicit D3D12Backend(D3D12Context& context);
        ~D3D12Backend() override;

        D3D12Backend(const D3D12Backend&) = delete;
        D3D12Backend& operator=(const D3D12Backend&) = delete;

        TextureId CreateTexture (int width, int height, TextureFormat format, const void* pixels = nullptr) override;
        void      UpdateTexture (TextureId id, const RectI& region, const void* pixels, int pitch) override;
        void      DestroyTexture(TextureId id) override;
        void      Render        (const DrawData& data) override;

        // 把宿主的纹理（例如 CAD 视口的渲染结果）登记为界面纹理；绘制前转到 PIXEL_SHADER_RESOURCE。用 DestroyTexture 注销
        TextureId RegisterExternalTexture(D3D12Texture* texture);

    private:
        ID3D12PipelineState* GetPipeline(DXGI_FORMAT format);

        struct TextureEntry
        {
            std::unique_ptr<D3D12Texture> owned;            // 外部纹理为空
            D3D12Texture*                 texture = nullptr;
        };

    private:
        D3D12Context& m_ctx;

        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
        Microsoft::WRL::ComPtr<ID3DBlob>            m_vs;
        Microsoft::WRL::ComPtr<ID3DBlob>            m_ps;
        std::unordered_map<int, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_pipelines;   // 按渲染目标格式

        std::unordered_map<TextureId, TextureEntry> m_textures;
        TextureId m_nextTextureId = 1;
    };
}
