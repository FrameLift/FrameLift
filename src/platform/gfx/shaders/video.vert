#version 450

// Fullscreen-triangle blit (Vulkan port of the GL renderer's shader). Vertex
// positions/UVs are generated from gl_VertexIndex, so no vertex buffer or attributes
// are needed — the renderer issues a 3-vertex draw with an empty vertex input state.
layout(location = 0) out vec2 vUV;

void main()
{
    vec2 uv = vec2((gl_VertexIndex == 1) ? 2.0 : 0.0,
                   (gl_VertexIndex == 2) ? 2.0 : 0.0);
    vUV = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
