#pragma once
#include "D3D12/D3D12Backend.h"
#include "D3D12/D3D12Context.h"
#include "Software/SoftwareBackend.h"
#include <memory>
#include <string>

namespace MiniGUI::Test
{
    // 离屏 D3D12 渲染 + 读回像素，用来和软件光栅的结果比较（同 D3D11Capture）。
    // 优先使用硬件设备，没有显卡（例如 CI 虚拟机）时退回 WARP
    class D3D12Capture
    {
    public:
        ~D3D12Capture();

        bool Initialize(bool forceWarp = false);

        D3D12Backend&      GetBackend()     { return *m_backend; }
        const std::string& GetAdapterName() const { return m_ctx->GetAdapterName(); }

        void          BeginFrame(int width, int height, Color32 clearColor);   // 创建渲染目标并设为界面目标，清屏
        SoftwareImage Readback();                                              // 提交、等 GPU 执行完、读回

    private:
        std::unique_ptr<D3D12Context>  m_ctx;
        std::unique_ptr<D3D12Backend>  m_backend;
        std::unique_ptr<D3D12Texture>  m_target;
        D3D12_CPU_DESCRIPTOR_HANDLE    m_rtv = {};
        int m_width  = 0;
        int m_height = 0;
    };
}
