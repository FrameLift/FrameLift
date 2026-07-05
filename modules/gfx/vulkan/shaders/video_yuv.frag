#version 450

// Planar YUV video blit: Y from an R8 image, chroma either interleaved in an RG8
// image (NV12 — U/V bindings then both hold the UV image) or split across two R8
// images (I420). The YUV→RGB matrix/bias come from YuvToRgbMatrix.h with the range
// expansion and chroma offset folded in, so the conversion is one multiply-add.
//
// No V flip (matches video.frag): frames are uploaded top-row-first into top-origin
// images and the framebuffer is Y-down.
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uTexY;
layout(binding = 1) uniform sampler2D uTexU;
layout(binding = 2) uniform sampler2D uTexV;

layout(push_constant) uniform YuvPc
{
    vec4 matCol0; // YUV→RGB matrix columns (xyz used)
    vec4 matCol1;
    vec4 matCol2;
    vec4 bias; // xyz = bias; w = 1.0 for NV12 (sample UV.rg), 0.0 for I420
} pc;

void main()
{
    float y = texture(uTexY, vUV).r;
    vec2 c = (pc.bias.w > 0.5) ? texture(uTexU, vUV).rg
                               : vec2(texture(uTexU, vUV).r, texture(uTexV, vUV).r);
    mat3 m = mat3(pc.matCol0.xyz, pc.matCol1.xyz, pc.matCol2.xyz);
    FragColor = vec4(clamp(m * vec3(y, c) + pc.bias.xyz, 0.0, 1.0), 1.0);
}
