#version 450
// 文字：字形图集只用 alpha
layout(set = 0, binding = 0) uniform sampler2D uTex;
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(vColor.rgb, vColor.a * texture(uTex, vUV).a);
}
