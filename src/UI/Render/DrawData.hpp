#pragma once
#include "Core/Types/Vec2.hpp"
#include "Core/Types/Rect.hpp"
#include "Core/Types/Color.hpp"
#include <cstdint>
#include <vector>

namespace MiniGUI
{
    class DrawList;

    // 纹理句柄：由渲染后端分配，0 表示无效
    using TextureId = uint64_t;
    constexpr TextureId InvalidTextureId = 0;

    using DrawIndex = uint32_t;

    // 顶点格式：整个界面只有这一种顶点，所有后端共用
    struct DrawVert
    {
        Vec2    pos;    // 逻辑像素坐标
        Vec2    uv;     // 纹理坐标
        Color32 color;  // RGBA8
    };
    static_assert(sizeof(DrawVert) == 20, "DrawVert 布局必须与着色器输入一致");

    // 绘制命令：纹理或裁剪矩形变化时才切分出新命令
    struct DrawCmd
    {
        Rect      clipRect;             // 逻辑像素
        TextureId texture     = InvalidTextureId;
        uint32_t  indexOffset = 0;      // 在所属 DrawList 索引数组中的起始位置
        uint32_t  indexCount  = 0;
    };

    // 一帧的全部绘制数据；后端按 lists 的顺序依次绘制（后面的盖在前面的上面）
    struct DrawData
    {
        std::vector<const DrawList*> lists;
        Vec2  displaySize;              // 逻辑像素尺寸
        float framebufferScale = 1.0f;  // 物理像素 / 逻辑像素（DPI 缩放系数）
    };
}
