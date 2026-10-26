#pragma once
#include "D3D12/D3D12Context.h"
#include "Render/IRenderTarget.h"
#include <memory>

namespace MiniCAD
{
    // D3D12 离屏渲染目标：RGBA8 颜色缓冲（带 SRV，界面把它当纹理显示）+ D32 深度缓冲。
    // GetNativeHandle / GetNativeShaderResource 都返回颜色缓冲的 MiniGUI::D3D12Texture*；
    // 尺寸变化时换成新对象，指针随之改变（MainFrame 据此重新登记界面纹理）
    class D3D12RenderTarget : public IRenderTarget
    {
    public:
        explicit D3D12RenderTarget(MiniGUI::D3D12Context& ctx) : m_ctx(ctx) {}
        ~D3D12RenderTarget() override { Release(); }

        void Create(int width, int height) override;
        void Resize(int width, int height) override;
        void Release() override;

        int GetWidth () const override { return m_width; }
        int GetHeight() const override { return m_height; }

        void* GetNativeHandle()         const override { return m_color.get(); }
        void* GetNativeShaderResource() const override { return m_color.get(); }

        MiniGUI::D3D12Texture*      GetColor() const { return m_color.get(); }
        D3D12_CPU_DESCRIPTOR_HANDLE GetRtv()   const { return m_rtv; }
        D3D12_CPU_DESCRIPTOR_HANDLE GetDsv()   const { return m_dsv; }

    private:
        MiniGUI::D3D12Context&                 m_ctx;
        std::unique_ptr<MiniGUI::D3D12Texture> m_color;
        std::unique_ptr<MiniGUI::D3D12Texture> m_depth;
        D3D12_CPU_DESCRIPTOR_HANDLE            m_rtv = {};
        D3D12_CPU_DESCRIPTOR_HANDLE            m_dsv = {};
        int m_width  = 0;
        int m_height = 0;
    };
}
