#pragma once
#include "Core/Math/PackedTypes.hpp"
#include "Core/Math/Point3.hpp"

namespace MiniCAD
{
    // 世界坐标 → 顶点坐标：先减去渲染原点（双精度）再转 float。测绘坐标的图纸（如 (538845, 3686089)）直接转 float
    // 只剩 0.25 的分辨率，放大后线画偏、橡皮筋一格一格地跳；Viewport 把原点合进视图投影矩阵（双精度）再交给 GPU
    inline Math::Float3 ToRenderFloat3(const Math::Point3& p, const Math::Point3& origin)
    {
        return { static_cast<float>(p.x - origin.x), static_cast<float>(p.y - origin.y), static_cast<float>(p.z - origin.z) };
    }

    // 顶点颜色：RGBA 各 8 位，GPU 按 UNORM 读成 0～1 的 float4（D3D11 R8G8B8A8_UNORM / GL 归一化 UNSIGNED_BYTE）。
    // 实体颜色本来就是 8 位（ACI / 真彩），几百万顶点的图纸顶点从 28 字节降到 16 字节
    struct VertexColor
    {
        uint8_t r = 0, g = 0, b = 0, a = 0;

        constexpr VertexColor() = default;
        constexpr VertexColor(float r_, float g_, float b_, float a_)
            : r(ToByte(r_)), g(ToByte(g_)), b(ToByte(b_)), a(ToByte(a_)) {}
        constexpr VertexColor(const Math::Float4& c) : VertexColor(c.x, c.y, c.z, c.w) {}

        constexpr Math::Float4 ToFloat4() const
        {
            return { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
        }
        constexpr bool operator==(const VertexColor&) const = default;

        static constexpr uint8_t ToByte(float v)
        {
            return static_cast<uint8_t>((v <= 0.0f ? 0.0f : v >= 1.0f ? 1.0f : v) * 255.0f + 0.5f);
        }
    };

    struct Vertex_P3_C4
    {
        Math::Float3 pos;
        VertexColor  color;
    };
    static_assert(sizeof(Vertex_P3_C4) == 16);

    struct Vertex_P3
    {
        Math::Float3 pos;
    };

    struct Vertex_P3_C4_UV
    {
        Math::Float3 pos;
        Math::Float4 color;
        Math::Float2 uv;
    };
}
