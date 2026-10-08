#pragma once
#include "Render/DrawData.hpp"

namespace MiniGUI
{
    enum class TextureFormat
    {
        RGBA8,      // 每像素 4 字节，R 在最低地址
    };

    // 渲染后端接口：D3D11 / Vulkan / 软件光栅 各实现一份。
    // 约定：
    //   - 接口里不出现任何图形 API 的类型；设备、交换链归宿主所有，由后端自己的构造参数传入
    //   - Render 只负责把 DrawData 画到当前渲染目标上，不负责 Present
    //   - 不提供逐像素操作或读回像素的接口
    class IRenderBackend
    {
    public:
        virtual ~IRenderBackend() = default;

        // pixels 可以为 nullptr（内容未定义，之后用 UpdateTexture 填充）
        virtual TextureId CreateTexture (int width, int height, TextureFormat format, const void* pixels = nullptr) = 0;
        virtual void      UpdateTexture (TextureId id, const RectI& region, const void* pixels, int pitch) = 0;
        virtual void      DestroyTexture(TextureId id) = 0;

        virtual void      Render(const DrawData& data) = 0;
    };
}
