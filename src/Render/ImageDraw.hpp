#pragma once
#include "Core/Image/ImageData.hpp"
#include "Render/VertexTypes.hpp"
#include <memory>

namespace MiniCAD
{
    // 一张待绘制的光栅图像：位图 + 两个三角形（6 个顶点，带 UV）。
    struct ImageDraw
    {
        std::shared_ptr<const ImageData> Image;
        Vertex_P3_C4_UV                  Verts[6];
    };
}
