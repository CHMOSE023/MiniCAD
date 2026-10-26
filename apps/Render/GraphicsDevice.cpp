// 按本平台编译进来的图形接口创建图形设备（宏由 cmake/Render.cmake 定义）
#include "IGraphicsDevice.h"
#include "Core/Log.h"
#ifdef MINICAD_RENDER_D3D11
#include "D3D11/D3D11GraphicsDevice.h"
#endif
#ifdef MINICAD_RENDER_GLES3
#include "GLES3/GLES3GraphicsDevice.h"
#endif

namespace MiniCAD
{
    std::unique_ptr<IGraphicsDevice> CreateGraphicsDevice(const NativeWindow& window, int width, int height,
                                                          const std::string& preference)
    {
        std::unique_ptr<IGraphicsDevice> device;
#if defined(MINICAD_RENDER_D3D11)
        (void)preference;
        device = std::make_unique<D3D11GraphicsDevice>();
#elif defined(MINICAD_RENDER_GLES3)
        // Linux 目前只有 OpenGL（ES 3）一个后端（Vulkan 是阶段 3）；网页上是 WebGL2
        if (!preference.empty() && preference != "gl")
            LOG_WARN("图形后端 %s 不可用，改用 OpenGL", preference.c_str());
        device = std::make_unique<GLES3GraphicsDevice>();
#endif
        if (device && !device->Initialize(window, width, height))
            device.reset();
        return device;
    }
}
