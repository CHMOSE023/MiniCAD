#include "Text/GlyphAtlas.h"
#include "Text/Font.h"
#include "Render/IRenderBackend.h"
#include <algorithm>
#include <cmath>

namespace MiniGUI
{
    namespace
    {
        constexpr int      kPadding    = 1;             // 字形之间留 1 像素空隙，防止采样串色
        constexpr int      kWhiteSize  = 4;             // 白色区域 4×4，线性过滤采样中心时只会取到白色
        constexpr uint32_t kEmptyPixel = 0x00FFFFFFu;   // 透明白：边缘被采样到时颜色不会发黑
    }

    GlyphAtlas::GlyphAtlas(IRenderBackend* backend, int initialSize, int maxSize)
        : m_backend(backend)
        , m_width(initialSize)
        , m_height(initialSize)
        , m_initialSize(initialSize)
        , m_maxSize(std::max(maxSize, initialSize))
    {
        m_pixels.assign(static_cast<size_t>(m_width) * m_height, kEmptyPixel);
        ResetPacking();
        RecreateTexture();
    }

    GlyphAtlas::~GlyphAtlas()
    {
        if (m_texture != InvalidTextureId)
            m_backend->DestroyTexture(m_texture);
    }

    Vec2 GlyphAtlas::GetWhitePixelUV() const
    {
        return ToUV(m_white.x + kWhiteSize / 2, m_white.y + kWhiteSize / 2);
    }

    // =========================================================
    // 打包
    // =========================================================
    void GlyphAtlas::ResetPacking()
    {
        m_shelves.clear();
        m_nextShelfY = 0;

        // 白色区域总是第一个分配，位于左上角
        Allocate(kWhiteSize, kWhiteSize, m_white);
        for (int y = 0; y < kWhiteSize; ++y)
            for (int x = 0; x < kWhiteSize; ++x)
                m_pixels[static_cast<size_t>(m_white.y + y) * m_width + m_white.x + x] = 0xFFFFFFFFu;
        MarkDirty(m_white);
    }

    bool GlyphAtlas::Allocate(int width, int height, RectI& out)
    {
        const int pw = width + kPadding;
        const int ph = height + kPadding;

        // 选高度最接近的现有行，行高浪费不超过一半
        Shelf* best = nullptr;
        for (Shelf& s : m_shelves)
        {
            if (s.height < ph || s.height > ph + ph / 2 + 2 || s.x + pw > m_width)
                continue;
            if (!best || s.height < best->height)
                best = &s;
        }

        if (!best)
        {
            if (m_nextShelfY + ph > m_height || pw > m_width)
                return false;
            m_shelves.push_back(Shelf{ m_nextShelfY, ph, 0 });
            m_nextShelfY += ph;
            best = &m_shelves.back();
        }

        out = RectI{ best->x, best->y, width, height };
        best->x += pw;
        return true;
    }

    bool GlyphAtlas::Grow()
    {
        if (m_width >= m_maxSize)
            return false;

        // 宽高都翻倍，已有像素原位保留，已有的行可以继续向右扩展
        const int newW = std::min(m_width * 2, m_maxSize);
        const int newH = std::min(m_height * 2, m_maxSize);
        std::vector<uint32_t> pixels(static_cast<size_t>(newW) * newH, kEmptyPixel);
        for (int y = 0; y < m_height; ++y)
            std::copy_n(&m_pixels[static_cast<size_t>(y) * m_width], m_width, &pixels[static_cast<size_t>(y) * newW]);

        m_pixels = std::move(pixels);
        m_width  = newW;
        m_height = newH;
        RecreateTexture();
        m_changed = true;
        return true;
    }

    void GlyphAtlas::Clear()
    {
        m_glyphs.clear();
        std::fill(m_pixels.begin(), m_pixels.end(), kEmptyPixel);
        ResetPacking();
        MarkDirty(RectI{ 0, 0, m_width, m_height });
        m_changed = true;
    }

    // =========================================================
    // 字形
    // =========================================================
    const GlyphAtlas::Entry* GlyphAtlas::GetGlyph(const Font* font, int glyph, float pixelSize, float scale)
    {
        const GlyphKey key{ font, glyph, static_cast<int>(std::lround(pixelSize * 4.0f)) };
        auto it = m_glyphs.find(key);
        if (it != m_glyphs.end())
            return &it->second;

        GlyphBitmap bmp;
        font->RasterizeGlyph(glyph, scale, bmp);

        Entry e;
        e.offsetX = bmp.offsetX;
        e.offsetY = bmp.offsetY;

        if (bmp.width > 0 && bmp.height > 0)
        {
            RectI r;
            while (!Allocate(bmp.width, bmp.height, r))
            {
                if (Grow())
                    continue;
                // 已到最大尺寸：清空重建（本帧之前生成的字形指令需要重做，Changed() 会通知调用方）
                Clear();
                if (!Allocate(bmp.width, bmp.height, r))
                    return nullptr;   // 单个字形比整个图集还大
                break;
            }

            for (int y = 0; y < bmp.height; ++y)
            {
                uint32_t*      dst = &m_pixels[static_cast<size_t>(r.y + y) * m_width + r.x];
                const uint8_t* src = &bmp.coverage[static_cast<size_t>(y) * bmp.width];
                for (int x = 0; x < bmp.width; ++x)
                    dst[x] = (static_cast<uint32_t>(src[x]) << 24) | 0x00FFFFFFu;
            }
            e.rect = r;
            MarkDirty(r);
        }

        return &m_glyphs.emplace(key, e).first->second;
    }

    // =========================================================
    // 纹理
    // =========================================================
    void GlyphAtlas::RecreateTexture()
    {
        if (m_texture != InvalidTextureId)
            m_backend->DestroyTexture(m_texture);
        m_texture = m_backend->CreateTexture(m_width, m_height, TextureFormat::RGBA8, m_pixels.data());
        m_dirty   = false;
    }

    void GlyphAtlas::MarkDirty(const RectI& r)
    {
        if (!m_dirty)
        {
            m_dirtyRect = r;
            m_dirty     = true;
            return;
        }
        const int x0 = std::min(m_dirtyRect.x, r.x);
        const int y0 = std::min(m_dirtyRect.y, r.y);
        const int x1 = std::max(m_dirtyRect.x + m_dirtyRect.width,  r.x + r.width);
        const int y1 = std::max(m_dirtyRect.y + m_dirtyRect.height, r.y + r.height);
        m_dirtyRect = RectI{ x0, y0, x1 - x0, y1 - y0 };
    }

    void GlyphAtlas::Upload()
    {
        if (!m_dirty)
            return;
        const uint32_t* src = &m_pixels[static_cast<size_t>(m_dirtyRect.y) * m_width + m_dirtyRect.x];
        m_backend->UpdateTexture(m_texture, m_dirtyRect, src, m_width * 4);
        m_dirty = false;
    }
}
