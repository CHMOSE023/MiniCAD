#pragma once
#include "Render/IRenderBackend.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace MiniGUI
{
    // RGBA8 图像，像素内存顺序 R、G、B、A（与 Color32 一致），行紧密排列
    struct SoftwareImage
    {
        int                   width  = 0;
        int                   height = 0;
        std::vector<uint32_t> pixels;

        uint32_t  At(int x, int y) const { return pixels[static_cast<size_t>(y) * width + x]; }
        uint32_t& At(int x, int y)       { return pixels[static_cast<size_t>(y) * width + x]; }
    };

    // 软件光栅后端：把同一份 DrawData 画到内存图像里，用于截图对比测试。
    // 光栅化规则与 D3D11 对齐，保证两个后端画面一致：
    //   - 像素中心在 (x+0.5, y+0.5)，顶点坐标吸附到 1/256 像素（D3D11 要求的 8 位亚像素精度）
    //   - 左上填充规则：相邻三角形共享的边不会重复绘制（羽化带的 alpha 混合依赖这一点）
    //   - 双线性采样、边缘截断；输出 = 纹理 × 顶点色；标准 alpha 混合；写回时四舍五入
    // 结果与平台、显卡无关，是确定性的
    class SoftwareBackend : public IRenderBackend
    {
    public:
        // 设置目标图像尺寸（物理像素）并用 clearColor 填充；Render 之前调用
        void BeginFrame(int width, int height, Color32 clearColor);
        const SoftwareImage& GetImage() const { return m_target; }

        virtual TextureId CreateTexture (int width, int height, TextureFormat format, const void* pixels = nullptr) override;
        virtual void      UpdateTexture (TextureId id, const RectI& region, const void* pixels, int pitch) override;
        virtual void      DestroyTexture(TextureId id) override;
        virtual void      Render        (const DrawData& data) override;

    private:
        struct ScissorRect
        {
            int left, top, right, bottom;   // right / bottom 不包含
        };

        void DrawTriangle(const DrawVert& v0, const DrawVert& v1, const DrawVert& v2,
                          const SoftwareImage& texture, const ScissorRect& scissor, float scale);

    private:
        SoftwareImage                                m_target;
        std::unordered_map<TextureId, SoftwareImage> m_textures;
        TextureId                                    m_nextTextureId = 1;
    };
}
