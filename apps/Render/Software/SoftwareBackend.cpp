#include "Software/SoftwareBackend.h"
#include "Paint/DrawList.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

namespace MiniGUI
{
    namespace
    {
        constexpr int64_t kSubPixel = 256;     // 8 位亚像素精度
        constexpr int64_t kHalf     = kSubPixel / 2;

        struct Float4
        {
            float r, g, b, a;
        };

        Float4 Unpack(uint32_t c)
        {
            constexpr float k = 1.0f / 255.0f;
            return { static_cast<float>(c & 0xFF) * k,
                     static_cast<float>((c >> 8) & 0xFF) * k,
                     static_cast<float>((c >> 16) & 0xFF) * k,
                     static_cast<float>(c >> 24) * k };
        }

        uint32_t ToUnorm8(float v)
        {
            v = std::clamp(v, 0.0f, 1.0f);
            return static_cast<uint32_t>(v * 255.0f + 0.5f);
        }

        uint32_t Pack(const Float4& c)
        {
            return ToUnorm8(c.r) | (ToUnorm8(c.g) << 8) | (ToUnorm8(c.b) << 16) | (ToUnorm8(c.a) << 24);
        }

        // 双线性采样，边缘截断（与 D3D11_TEXTURE_ADDRESS_CLAMP + 线性过滤一致）
        Float4 SampleBilinear(const SoftwareImage& tex, float u, float v)
        {
            const float tx = u * static_cast<float>(tex.width)  - 0.5f;
            const float ty = v * static_cast<float>(tex.height) - 0.5f;
            const float fx0 = std::floor(tx);
            const float fy0 = std::floor(ty);
            const float fx  = tx - fx0;
            const float fy  = ty - fy0;

            auto clampX = [&](int x) { return std::clamp(x, 0, tex.width - 1); };
            auto clampY = [&](int y) { return std::clamp(y, 0, tex.height - 1); };
            const int x0 = clampX(static_cast<int>(fx0)), x1 = clampX(static_cast<int>(fx0) + 1);
            const int y0 = clampY(static_cast<int>(fy0)), y1 = clampY(static_cast<int>(fy0) + 1);

            const Float4 c00 = Unpack(tex.At(x0, y0)), c10 = Unpack(tex.At(x1, y0));
            const Float4 c01 = Unpack(tex.At(x0, y1)), c11 = Unpack(tex.At(x1, y1));

            auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
            auto mix  = [&](float a00, float a10, float a01, float a11)
            {
                return lerp(lerp(a00, a10, fx), lerp(a01, a11, fx), fy);
            };
            return { mix(c00.r, c10.r, c01.r, c11.r), mix(c00.g, c10.g, c01.g, c11.g),
                     mix(c00.b, c10.b, c01.b, c11.b), mix(c00.a, c10.a, c01.a, c11.a) };
        }

        // 边函数 w(p) = A·px + B·py + C，表示点 p 在有向边 a→b 的哪一侧
        struct Edge
        {
            int64_t A, B, C;
            int64_t bias;   // 左上边 0，其余 -1：w + bias >= 0 才算覆盖

            Edge(int64_t xa, int64_t ya, int64_t xb, int64_t yb)
            {
                A = ya - yb;
                B = xb - xa;
                C = (yb - ya) * xa - (xb - xa) * ya;

                // 三角形已统一为屏幕上顺时针（y 向下时面积为正）：
                // 上边是水平且向右的边，左边是向上走的边
                const int64_t dy = yb - ya;
                const int64_t dx = xb - xa;
                const bool topLeft = (dy == 0 && dx > 0) || dy < 0;
                bias = topLeft ? 0 : -1;
            }

            int64_t Eval(int64_t px, int64_t py) const { return A * px + B * py + C; }
        };
    }

    void SoftwareBackend::BeginFrame(int width, int height, Color32 clearColor)
    {
        m_target.width  = std::max(width, 0);
        m_target.height = std::max(height, 0);
        m_target.pixels.assign(static_cast<size_t>(m_target.width) * m_target.height, clearColor);
    }

    // =========================================================
    // 纹理
    // =========================================================
    TextureId SoftwareBackend::CreateTexture(int width, int height, TextureFormat format, const void* pixels)
    {
        assert(format == TextureFormat::RGBA8);
        (void)format;
        if (width <= 0 || height <= 0)
            return InvalidTextureId;

        SoftwareImage img;
        img.width  = width;
        img.height = height;
        img.pixels.resize(static_cast<size_t>(width) * height, 0);
        if (pixels)
            std::memcpy(img.pixels.data(), pixels, img.pixels.size() * sizeof(uint32_t));

        const TextureId id = m_nextTextureId++;
        m_textures.emplace(id, std::move(img));
        return id;
    }

    void SoftwareBackend::UpdateTexture(TextureId id, const RectI& region, const void* pixels, int pitch)
    {
        auto it = m_textures.find(id);
        if (it == m_textures.end() || pixels == nullptr)
            return;

        SoftwareImage& img = it->second;
        if (region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
            region.x + region.width > img.width || region.y + region.height > img.height)
        {
            assert(false && "UpdateTexture 区域越界");
            return;
        }

        const auto* src = static_cast<const uint8_t*>(pixels);
        for (int y = 0; y < region.height; ++y)
            std::memcpy(&img.At(region.x, region.y + y), src + static_cast<size_t>(y) * pitch, static_cast<size_t>(region.width) * 4);
    }

    void SoftwareBackend::DestroyTexture(TextureId id)
    {
        m_textures.erase(id);
    }

    // =========================================================
    // 渲染
    // =========================================================
    void SoftwareBackend::Render(const DrawData& data)
    {
        const float scale    = data.framebufferScale;
        const float fbWidth  = std::min(data.displaySize.x * scale, static_cast<float>(m_target.width));
        const float fbHeight = std::min(data.displaySize.y * scale, static_cast<float>(m_target.height));
        if (fbWidth <= 0.0f || fbHeight <= 0.0f)
            return;

        for (const DrawList* list : data.lists)
        {
            const auto& vertices = list->GetVertices();
            const auto& indices  = list->GetIndices();

            for (const DrawCmd& cmd : list->GetCommands())
            {
                if (cmd.indexCount == 0)
                    continue;

                // 裁剪矩形的取整方式与 D3D11Backend 完全相同
                ScissorRect sc;
                sc.left   = static_cast<int>(std::floor(std::max(cmd.clipRect.min.x * scale, 0.0f) + 0.5f));
                sc.top    = static_cast<int>(std::floor(std::max(cmd.clipRect.min.y * scale, 0.0f) + 0.5f));
                sc.right  = static_cast<int>(std::floor(std::min(cmd.clipRect.max.x * scale, fbWidth)  + 0.5f));
                sc.bottom = static_cast<int>(std::floor(std::min(cmd.clipRect.max.y * scale, fbHeight) + 0.5f));
                sc.right  = std::min(sc.right, m_target.width);
                sc.bottom = std::min(sc.bottom, m_target.height);
                if (sc.right <= sc.left || sc.bottom <= sc.top)
                    continue;

                auto it = m_textures.find(cmd.texture);
                if (it == m_textures.end())
                {
                    assert(false && "DrawCmd 引用了不存在的纹理");
                    continue;
                }

                const uint32_t end = cmd.indexOffset + cmd.indexCount;
                for (uint32_t i = cmd.indexOffset; i + 2 < end; i += 3)
                {
                    DrawTriangle(vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]],
                                 it->second, sc, scale);
                }
            }
        }
    }

    void SoftwareBackend::DrawTriangle(const DrawVert& v0, const DrawVert& v1, const DrawVert& v2,
                                       const SoftwareImage& texture, const ScissorRect& sc, float scale)
    {
        // 物理像素坐标吸附到 1/256 像素
        auto toFixed = [scale](float v) { return static_cast<int64_t>(std::llround(static_cast<double>(v) * scale * kSubPixel)); };

        const DrawVert* p[3] = { &v0, &v1, &v2 };
        int64_t X[3] = { toFixed(v0.pos.x), toFixed(v1.pos.x), toFixed(v2.pos.x) };
        int64_t Y[3] = { toFixed(v0.pos.y), toFixed(v1.pos.y), toFixed(v2.pos.y) };

        int64_t area = (X[1] - X[0]) * (Y[2] - Y[0]) - (Y[1] - Y[0]) * (X[2] - X[0]);
        if (area == 0)
            return;
        if (area < 0)
        {
            // 统一为顺时针，保证边函数在内部为正、左上规则判断一致
            std::swap(p[1], p[2]);
            std::swap(X[1], X[2]);
            std::swap(Y[1], Y[2]);
            area = -area;
        }

        // 包围盒（只考虑中心落在三角形范围内的像素），再与裁剪矩形相交
        const int64_t minX = std::min({ X[0], X[1], X[2] });
        const int64_t maxX = std::max({ X[0], X[1], X[2] });
        const int64_t minY = std::min({ Y[0], Y[1], Y[2] });
        const int64_t maxY = std::max({ Y[0], Y[1], Y[2] });

        const int x0 = std::max(sc.left,       static_cast<int>(std::floor(static_cast<double>(minX - kHalf) / kSubPixel)));
        const int x1 = std::min(sc.right - 1,  static_cast<int>(std::ceil (static_cast<double>(maxX - kHalf) / kSubPixel)));
        const int y0 = std::max(sc.top,        static_cast<int>(std::floor(static_cast<double>(minY - kHalf) / kSubPixel)));
        const int y1 = std::min(sc.bottom - 1, static_cast<int>(std::ceil (static_cast<double>(maxY - kHalf) / kSubPixel)));
        if (x0 > x1 || y0 > y1)
            return;

        // w0 对应顶点 0 的权重（对边 1→2），依此类推
        const Edge e0(X[1], Y[1], X[2], Y[2]);
        const Edge e1(X[2], Y[2], X[0], Y[0]);
        const Edge e2(X[0], Y[0], X[1], Y[1]);

        const Float4 c[3] = { Unpack(p[0]->color), Unpack(p[1]->color), Unpack(p[2]->color) };

        // 三个顶点 UV 相同（纯色图元都采样白色像素）时只采样一次
        const bool   constUV  = p[0]->uv == p[1]->uv && p[1]->uv == p[2]->uv;
        const Float4 constTex = constUV ? SampleBilinear(texture, p[0]->uv.x, p[0]->uv.y) : Float4{};

        const double invArea = 1.0 / static_cast<double>(area);
        const int64_t stepX0 = e0.A * kSubPixel, stepX1 = e1.A * kSubPixel, stepX2 = e2.A * kSubPixel;

        for (int y = y0; y <= y1; ++y)
        {
            const int64_t py = y * kSubPixel + kHalf;
            const int64_t px = x0 * kSubPixel + kHalf;
            int64_t w0 = e0.Eval(px, py);
            int64_t w1 = e1.Eval(px, py);
            int64_t w2 = e2.Eval(px, py);

            uint32_t* row = &m_target.At(0, y);
            for (int x = x0; x <= x1; ++x, w0 += stepX0, w1 += stepX1, w2 += stepX2)
            {
                if (w0 + e0.bias < 0 || w1 + e1.bias < 0 || w2 + e2.bias < 0)
                    continue;

                const float l0 = static_cast<float>(static_cast<double>(w0) * invArea);
                const float l1 = static_cast<float>(static_cast<double>(w1) * invArea);
                const float l2 = static_cast<float>(static_cast<double>(w2) * invArea);

                Float4 src{ c[0].r * l0 + c[1].r * l1 + c[2].r * l2,
                            c[0].g * l0 + c[1].g * l1 + c[2].g * l2,
                            c[0].b * l0 + c[1].b * l1 + c[2].b * l2,
                            c[0].a * l0 + c[1].a * l1 + c[2].a * l2 };

                Float4 tex = constTex;
                if (!constUV)
                {
                    const float u = p[0]->uv.x * l0 + p[1]->uv.x * l1 + p[2]->uv.x * l2;
                    const float v = p[0]->uv.y * l0 + p[1]->uv.y * l1 + p[2]->uv.y * l2;
                    tex = SampleBilinear(texture, u, v);
                }
                src.r *= tex.r; src.g *= tex.g; src.b *= tex.b; src.a *= tex.a;

                if (src.a <= 0.0f)
                    continue;

                // 混合：rgb = src·a + dst·(1-a)，alpha = a + dst.a·(1-a)
                const Float4 dst = Unpack(row[x]);
                const float  ia  = 1.0f - src.a;
                row[x] = Pack({ src.r * src.a + dst.r * ia,
                                src.g * src.a + dst.g * ia,
                                src.b * src.a + dst.b * ia,
                                src.a + dst.a * ia });
            }
        }
    }
}
