#version 450
// CAD 视口的线 / 三角形 / 点（Vertex_P3_C4），同 Hlsl/ViewportShaders.h 的 kLine。
// Mat4 为行向量约定，按行存放的 16 个数作为列主序矩阵读入，得到转置，pc.vp * v 与 HLSL 的 mul(v, M) 相同
layout(push_constant) uniform Constants
{
    mat4 vp;
    vec4 options;       // x：1 浅色背景，0 深色背景
} pc;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 0) out vec4 vColor;

// 7 号色语义：浅色背景时纯白 → 纯黑，深色背景时纯黑 → 纯白
vec4 MapColor(vec4 c)
{
    vec3 d = abs(c.rgb - vec3(pc.options.x > 0.5 ? 1.0 : 0.0));
    if (max(d.x, max(d.y, d.z)) < 0.004)
        c.rgb = 1.0 - c.rgb;
    return c;
}

void main()
{
    vColor = MapColor(aColor);
    gl_Position = pc.vp * vec4(aPos, 1.0);
    gl_PointSize = 1.0;     // 点图元恒定 1 像素（与 D3D 的 POINTLIST 一致）；线、三角形忽略此值
}
