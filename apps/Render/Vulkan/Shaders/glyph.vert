#version 450
// 字形实例（GlyphInstancing.hpp）：绑定 0 为字形网格的局部顶点，绑定 1 为逐实例的仿射变换与颜色。同 HLSL 的 kGlyph
layout(push_constant) uniform Constants
{
    mat4 vp;
    vec4 options;
} pc;

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aM;        // m00 m01 m10 m11
layout(location = 2) in vec3 aT;        // tx ty z
layout(location = 3) in vec4 aColor;
layout(location = 0) out vec4 vColor;

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
    vec2 w = vec2(aPos.x * aM.x + aPos.y * aM.z + aT.x, aPos.x * aM.y + aPos.y * aM.w + aT.y);
    gl_Position = pc.vp * vec4(w, aT.z, 1.0);
}
