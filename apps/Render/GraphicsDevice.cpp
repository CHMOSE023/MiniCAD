// 按本平台编译进来的图形接口创建图形设备（宏由 cmake/Render.cmake 定义）
#include "IGraphicsDevice.h"
#ifdef MINICAD_RENDER_D3D11
#include "D3D11/D3D11GraphicsDevice.h"
#endif
#ifdef MINICAD_RENDER_GLES3
#include "GLES3/GLES3GraphicsDevice.h"
#endif
#ifdef MINICAD_RENDER_WEBGPU
#include "WebGPU/WebGPUGraphicsDevice.h"
#endif

namespace MiniCAD
{
    std::unique_ptr<IGraphicsDevice> CreateGraphicsDevice(const NativeWindow& window, int width, int height, const std::string& preference)
    {
        (void)preference;
        std::unique_ptr<IGraphicsDevice> device;
#if defined(MINICAD_RENDER_D3D11)
        device = std::make_unique<D3D11GraphicsDevice>();
#elif defined(MINICAD_RENDER_GLES3)
        // 网页：同步创建的只有 WebGL2（使用 WebHost 创建的当前上下文）；WebGPU 见 CreateWebGPUGraphicsDeviceAsync
        device = std::make_unique<GLES3GraphicsDevice>();
#endif
        if (device && !device->Initialize(window, width, height))
            device.reset();
        return device;
    }

#ifdef __EMSCRIPTEN__
    void CreateWebGPUGraphicsDeviceAsync(const NativeWindow& window, int width, int height,
                                         std::function<void(std::unique_ptr<IGraphicsDevice>)> done)
    {
#ifdef MINICAD_RENDER_WEBGPU
        WebGPUGraphicsDevice::CreateAsync(window, width, height, std::move(done));
#else
        (void)window; (void)width; (void)height;
        done(nullptr);
#endif
    }
#endif
}
