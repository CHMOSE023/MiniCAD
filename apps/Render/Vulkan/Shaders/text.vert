#version 450
// 纹理文字（Vertex_P3_C4_UV，网页版的字体图集），同 HLSL 的 kText
layout(push_constant) uniform Constants
{
    mat4 vp;
    vec4 options;
} pc;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUV;

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
    vUV    = aUV;
    gl_Position = pc.vp * vec4(aPos, 1.0);
}
