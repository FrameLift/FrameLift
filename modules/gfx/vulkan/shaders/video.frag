#version 450

// Samples the video (or subtitle-overlay) texture. Used by both draws — the renderer
// switches the bound texture + blend state, not the shader.
//
// No V flip here (unlike the GL renderer): frames are uploaded top-row-first into a
// top-origin Vulkan image and the framebuffer is Y-down, so row 0 already maps to the
// top of the screen.
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uTex;

void main()
{
    FragColor = texture(uTex, vUV);
}
