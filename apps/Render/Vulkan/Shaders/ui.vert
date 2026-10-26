#version 450
// MiniGUI 界面（DrawVert），同 Hlsl/UiShader.h：逻辑像素 → 裁剪空间
layout(push_constant) uniform Constants
{
    vec2 scale;
    vec2 translate;
} pc;

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUV;

void main()
{
    vColor = aColor;
    vUV    = aUV;
    gl_Position = vec4(aPos * pc.scale + pc.translate, 0.0, 1.0);
}
