#version 450
// 光栅图像（Vertex_P3_C4_UV）：颜色不换（不按背景深浅映射），同 HLSL 的 kImage
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

void main()
{
    vColor = aColor;
    vUV    = aUV;
    gl_Position = pc.vp * vec4(aPos, 1.0);
}
