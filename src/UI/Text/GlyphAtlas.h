#pragma once
#include "Core/Types/Rect.hpp"
#include "Render/DrawData.hpp"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace MiniGUI
{
    class Font;
    class IRenderBackend;

    // 动态字形图集：按需光栅化字形并打包进一张 RGBA8 纹理（白色 RGB + 覆盖率 alpha），
    // 同时预留一块白色像素供纯色图元采样，因此文字和图形可以合并到同一批次绘制。
    // - 行式（shelf）打包；装不下时尺寸翻倍（最大 maxSize），到上限后清空重建
    // - 尺寸变化或清空后，之前生成的 UV 失效：Changed() 返回 true，调用方需要重新生成本帧绘制指令
    // - CPU 端保留一份像素，脏区域在 Upload() 时一次性提交给后端
    class GlyphAtlas
    {
    public:
        struct Entry
        {
            RectI rect;         // 在图集中的像素区域（不含间隔）；宽高为 0 表示空白字形
            int   offsetX = 0;  // 相对笔位置
            int   offsetY = 0;  // 相对基线，向下为正
        };

        GlyphAtlas(IRenderBackend* backend, int initialSize = 512, int maxSize = 2048);
        ~GlyphAtlas();

        GlyphAtlas(const GlyphAtlas&) = delete;
        GlyphAtlas& operator=(const GlyphAtlas&) = delete;

        // 查找字形，没有就光栅化并加入图集。pixelSize 用于区分缓存，scale 为字体单位到像素的缩放
        const Entry* GetGlyph(const Font* font, int glyph, float pixelSize, float scale);

        TextureId GetTexture()       const { return m_texture; }
        Vec2      GetWhitePixelUV()  const;
        Vec2      ToUV(int x, int y) const { return { static_cast<float>(x) / m_width, static_cast<float>(y) / m_height }; }
        int       GetWidth()         const { return m_width; }
        int       GetHeight()        const { return m_height; }
        size_t    GetGlyphCount()    const { return m_glyphs.size(); }

        void BeginFrame() { m_changed = false; }
        bool Changed() const { return m_changed; }
        void Upload();          // 把脏区域提交给后端
        void Clear();           // 清空所有字形（保留白色像素）

    private:
        struct GlyphKey
        {
            const Font* font;
            int         glyph;
            int         sizeQ;  // 像素字号 × 4 取整

            bool operator==(const GlyphKey& o) const { return font == o.font && glyph == o.glyph && sizeQ == o.sizeQ; }
        };

        struct GlyphKeyHash
        {
            size_t operator()(const GlyphKey& k) const
            {
                size_t h = std::hash<const void*>()(k.font);
                h ^= static_cast<size_t>(k.glyph) * 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
                h ^= static_cast<size_t>(k.sizeQ) * 0xC2B2AE3D27D4EB4Full + (h << 6) + (h >> 2);
                return h;
            }
        };

        struct Shelf
        {
            int y;
            int height;
            int x;      // 下一个可用位置
        };

        bool Allocate(int width, int height, RectI& out);
        bool Grow();
        void ResetPacking();
        void RecreateTexture();
        void MarkDirty(const RectI& r);

    private:
        IRenderBackend* m_backend = nullptr;
        TextureId       m_texture = InvalidTextureId;
        int             m_width   = 0;
        int             m_height  = 0;
        int             m_initialSize = 0;
        int             m_maxSize     = 0;

        std::vector<uint32_t> m_pixels;
        std::vector<Shelf>    m_shelves;
        int                   m_nextShelfY = 0;
        RectI                 m_white;

        std::unordered_map<GlyphKey, Entry, GlyphKeyHash> m_glyphs;

        bool  m_changed = false;
        bool  m_dirty   = false;
        RectI m_dirtyRect;
    };
}
