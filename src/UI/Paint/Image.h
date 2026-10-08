#pragma once
#include "Core/Types/Color.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace MiniGUI
{
    // RGBA8 图像（像素内存顺序 R、G、B、A，与 Color32 一致）
    struct Image
    {
        int                  width  = 0;
        int                  height = 0;
        std::vector<Color32> pixels;

        bool Empty() const { return width <= 0 || height <= 0; }
    };

    // 读取 PNG / JPG / BMP / TGA（stb_image），统一转为 RGBA8；路径按 UTF-8 解释
    bool LoadImageFromFile  (const std::string& utf8Path, Image& out);
    bool LoadImageFromMemory(const void* data, size_t size, Image& out);

    // 缩放到指定像素尺寸：缩小时按面积平均（预乘 alpha，透明边缘不发黑），放大时双线性。
    // 用来把 64×64 的图标缩到显示尺寸，避免 GPU 直接缩小产生的锯齿
    Image ResizeImage(const Image& src, int width, int height);
}
