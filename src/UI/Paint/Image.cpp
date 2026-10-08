#include "Paint/Image.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#pragma warning(push, 0)
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC            // 符号限定在本文件内，宿主程序（例如 MiniCAD）自带 stb_image 也不会冲突
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include "stb/stb_image.h"
#pragma warning(pop)

namespace MiniGUI
{
    bool LoadImageFromMemory(const void* data, size_t size, Image& out)
    {
        int w = 0, h = 0, comp = 0;
        stbi_uc* pixels = stbi_load_from_memory(static_cast<const stbi_uc*>(data), static_cast<int>(size), &w, &h, &comp, 4);
        if (!pixels)
            return false;

        out.width  = w;
        out.height = h;
        out.pixels.resize(static_cast<size_t>(w) * h);
        std::memcpy(out.pixels.data(), pixels, out.pixels.size() * 4);
        stbi_image_free(pixels);
        return true;
    }

    Image ResizeImage(const Image& src, int width, int height)
    {
        Image out;
        if (src.Empty() || width <= 0 || height <= 0)
            return out;
        out.width  = width;
        out.height = height;
        out.pixels.resize(static_cast<size_t>(width) * height);

        const float sx = static_cast<float>(src.width) / static_cast<float>(width);
        const float sy = static_cast<float>(src.height) / static_cast<float>(height);

        auto texel = [&](int x, int y)
        {
            x = std::clamp(x, 0, src.width - 1);
            y = std::clamp(y, 0, src.height - 1);
            return src.pixels[static_cast<size_t>(y) * src.width + x];
        };

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                // 目标像素覆盖的源区域 [x0,x1)×[y0,y1)，按覆盖面积加权（预乘 alpha）
                const float x0 = x * sx, x1 = (x + 1) * sx;
                const float y0 = y * sy, y1 = (y + 1) * sy;
                double r = 0, g = 0, b = 0, a = 0, wsum = 0;

                if (sx <= 1.0f && sy <= 1.0f)
                {
                    // 放大：双线性
                    const float fx = (x + 0.5f) * sx - 0.5f, fy = (y + 0.5f) * sy - 0.5f;
                    const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
                    const float tx = fx - ix, ty = fy - iy;
                    const float wts[4] = { (1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty };
                    const Color32 cs[4] = { texel(ix, iy), texel(ix + 1, iy), texel(ix, iy + 1), texel(ix + 1, iy + 1) };
                    for (int k = 0; k < 4; ++k)
                    {
                        const double ca = ((cs[k] >> 24) & 0xFF) / 255.0 * wts[k];
                        r += (cs[k] & 0xFF) * ca; g += ((cs[k] >> 8) & 0xFF) * ca; b += ((cs[k] >> 16) & 0xFF) * ca;
                        a += ca; wsum += wts[k];
                    }
                }
                else
                {
                    for (int py = static_cast<int>(std::floor(y0)); py < static_cast<int>(std::ceil(y1)); ++py)
                    {
                        const float wy = std::min(y1, py + 1.0f) - std::max(y0, static_cast<float>(py));
                        for (int px = static_cast<int>(std::floor(x0)); px < static_cast<int>(std::ceil(x1)); ++px)
                        {
                            const float wx = std::min(x1, px + 1.0f) - std::max(x0, static_cast<float>(px));
                            const double w = static_cast<double>(wx) * wy;
                            const Color32 c = texel(px, py);
                            const double ca = ((c >> 24) & 0xFF) / 255.0 * w;
                            r += (c & 0xFF) * ca; g += ((c >> 8) & 0xFF) * ca; b += ((c >> 16) & 0xFF) * ca;
                            a += ca; wsum += w;
                        }
                    }
                }

                Color32 result = 0;
                if (a > 1e-9)
                {
                    auto u8 = [](double v) { return static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L)); };
                    result = RGBA(u8(r / a), u8(g / a), u8(b / a), u8(a / wsum * 255.0));
                }
                out.pixels[static_cast<size_t>(y) * width + x] = result;
            }
        }
        return out;
    }

    bool LoadImageFromFile(const std::string& utf8Path, Image& out)
    {
        const std::filesystem::path path(std::u8string(utf8Path.begin(), utf8Path.end()));
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return false;
        const std::streamsize size = file.tellg();
        if (size <= 0)
            return false;
        std::vector<char> bytes(static_cast<size_t>(size));
        file.seekg(0);
        if (!file.read(bytes.data(), size))
            return false;
        return LoadImageFromMemory(bytes.data(), bytes.size(), out);
    }
}
