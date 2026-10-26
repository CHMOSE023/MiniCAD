#version 450
// 光栅图像：纹理 RGBA × 顶点颜色（白色 = 原样）
layout(set = 0, binding = 0) uniform sampler2D uTex;
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(uTex, vUV) * vColor;
}
