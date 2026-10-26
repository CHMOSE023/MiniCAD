#version 450
// 输出 = 纹理采样 × 顶点色；纯色图元采样白色像素
layout(set = 0, binding = 0) uniform sampler2D uTex;
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vColor * texture(uTex, vUV);
}
