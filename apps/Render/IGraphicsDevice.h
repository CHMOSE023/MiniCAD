#pragma once
#include "Render/IRenderer.h"
#include "Render/IRenderTarget.h"
#include "Render/IRenderBackend.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace MiniCAD
{
    // 宿主要画的窗口：Win32 为 HWND；网页版不用（WebGL 上下文由 WebHost 创建并设为当前），
    // Linux 不用（EGL / GLES3 上下文由宿主经 SDL3 创建并设为当前）
    struct NativeWindow
    {
        void* handle = nullptr;
    };

    // 宿主侧的图形设备：一种图形接口一份实现（D3D11GraphicsDevice、GLES3GraphicsDevice ……），见 docs/Render/PLAN.md。
    // 持有设备、窗口的交换链 / 帧缓冲、CAD 视口渲染器与界面后端；宿主只通过本接口使用图形接口，
    // 核心库、MiniGUI、apps/Common 看不到它（它们只认 IRenderer / IRenderTarget / IRenderBackend）。
    class IGraphicsDevice
    {
    public:
        virtual ~IGraphicsDevice() = default;

        virtual bool Initialize(const NativeWindow& window, int width, int height) = 0;
        virtual void Resize(int width, int height) = 0;    // 窗口绘图缓冲的像素尺寸
        virtual void BeginUiPass() = 0;                     // 绑定窗口的帧缓冲并清屏
        virtual void Present() = 0;
        virtual void WaitIdle() {}                          // 等 GPU 执行完已提交的命令（自测计时用）
        virtual void ReleaseFreeMemory() {}                 // 让驱动真正销毁已释放的缓冲（关闭文档后）

        virtual IRenderer&               GetRenderer()  = 0;
        virtual MiniGUI::IRenderBackend& GetUiBackend() = 0;
        virtual std::unique_ptr<IRenderTarget> CreateRenderTarget() = 0;
        // 把渲染目标的着色器资源（IRenderTarget::GetNativeShaderResource）登记为界面纹理
        virtual MiniGUI::TextureId RegisterViewportTexture(void* nativeShaderResource) = 0;

        // 读回渲染目标的像素（RGBA8，第一行在顶部；自测与 MINICAD_BENCH_SHOT 截图用）。不支持时返回 false
        virtual bool ReadPixels(IRenderTarget& target, int& width, int& height, std::vector<uint8_t>& rgba)
        {
            (void)target; (void)width; (void)height; (void)rgba;
            return false;
        }

        virtual const char* GetName() const = 0;            // 关于对话框显示，例如 "D3D11"
    };

    // 按本平台编译进来的图形接口创建设备并初始化；失败返回 nullptr。
    // preference：指定后端（小写，如 "gl"；来自 --gpu / MINICAD_GPU），空串 = 按本平台的默认顺序
    std::unique_ptr<IGraphicsDevice> CreateGraphicsDevice(const NativeWindow& window, int width, int height,
                                                          const std::string& preference = {});
}
