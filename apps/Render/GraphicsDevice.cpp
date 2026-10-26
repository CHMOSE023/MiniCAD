// 按本平台编译进来的图形接口创建图形设备（宏由 cmake/Render.cmake 定义）
#include "IGraphicsDevice.h"
#ifdef MINICAD_RENDER_D3D11
#include "D3D11/D3D11GraphicsDevice.h"
#endif
#ifdef MINICAD_RENDER_GLES3
#include "GLES3/GLES3GraphicsDevice.h"
#endif

namespace MiniCAD
{
    std::unique_ptr<IGraphicsDevice> CreateGraphicsDevice(const NativeWindow& window, int width, int height)
    {
        std::unique_ptr<IGraphicsDevice> device;
#if defined(MINICAD_RENDER_D3D11)
        device = std::make_unique<D3D11GraphicsDevice>();
#elif defined(MINICAD_RENDER_GLES3)
        device = std::make_unique<GLES3GraphicsDevice>();
#endif
        if (device && !device->Initialize(window, width, height))
            device.reset();
        return device;
    }
}
