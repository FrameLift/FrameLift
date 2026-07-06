#include "GlVideoRenderer.h"

#include "IGraphicsBackend.h"
#include "YuvToRgbMatrix.h"

#include <framelift/Log.h>

#include "FFmpegLetterbox.h"

#include <cmath>
#include <cstddef>

// Minimal, self-contained GL 3.3-core loader. We deliberately avoid system GL
// headers (which are awkward under the MinGW cross-compile) and instead declare
// the handful of types, constants and entry points we use, loading them through
// the host's getProcAddr. 64-bit only, so the Windows __stdcall thunk can be
// omitted from the function pointers.
namespace
{
using GLenum = unsigned int;
using GLbitfield = unsigned int;
using GLboolean = unsigned char;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLfloat = float;
using GLchar = char;

constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_TEXTURE1 = 0x84C1;
constexpr GLenum GL_TEXTURE2 = 0x84C2;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLint GL_RGBA8 = 0x8058;
constexpr GLenum GL_RED = 0x1903;
constexpr GLenum GL_RG = 0x8227;
constexpr GLint GL_R8 = 0x8229;
constexpr GLint GL_RG8 = 0x822B;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLbitfield GL_COLOR_BUFFER_BIT = 0x00004000;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLint GL_LINEAR = 0x2601;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLint GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;
constexpr GLenum GL_BLEND = 0x0BE2;
constexpr GLenum GL_SRC_ALPHA = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum GL_FRAMEBUFFER_SRGB = 0x8DB9;

using PFNViewport = void (*)(GLint, GLint, GLsizei, GLsizei);
using PFNEnable = void (*)(GLenum);
using PFNDisable = void (*)(GLenum);
using PFNIsEnabled = GLboolean (*)(GLenum);
using PFNBlendFunc = void (*)(GLenum, GLenum);
using PFNClearColor = void (*)(GLfloat, GLfloat, GLfloat, GLfloat);
using PFNClear = void (*)(GLbitfield);
using PFNGenTextures = void (*)(GLsizei, GLuint*);
using PFNBindTexture = void (*)(GLenum, GLuint);
using PFNTexParameteri = void (*)(GLenum, GLenum, GLint);
using PFNTexImage2D = void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
using PFNTexSubImage2D = void (*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
using PFNPixelStorei = void (*)(GLenum, GLint);
using PFNDeleteTextures = void (*)(GLsizei, const GLuint*);
using PFNActiveTexture = void (*)(GLenum);
using PFNGenVertexArrays = void (*)(GLsizei, GLuint*);
using PFNBindVertexArray = void (*)(GLuint);
using PFNDeleteVertexArrays = void (*)(GLsizei, const GLuint*);
using PFNCreateShader = GLuint (*)(GLenum);
using PFNShaderSource = void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using PFNCompileShader = void (*)(GLuint);
using PFNGetShaderiv = void (*)(GLuint, GLenum, GLint*);
using PFNGetShaderInfoLog = void (*)(GLuint, GLsizei, GLsizei*, GLchar*);
using PFNDeleteShader = void (*)(GLuint);
using PFNCreateProgram = GLuint (*)();
using PFNAttachShader = void (*)(GLuint, GLuint);
using PFNLinkProgram = void (*)(GLuint);
using PFNGetProgramiv = void (*)(GLuint, GLenum, GLint*);
using PFNGetProgramInfoLog = void (*)(GLuint, GLsizei, GLsizei*, GLchar*);
using PFNUseProgram = void (*)(GLuint);
using PFNDeleteProgram = void (*)(GLuint);
using PFNGetUniformLocation = GLint (*)(GLuint, const GLchar*);
using PFNUniform1i = void (*)(GLint, GLint);
using PFNUniform3fv = void (*)(GLint, GLsizei, const GLfloat*);
using PFNUniformMatrix3fv = void (*)(GLint, GLsizei, GLboolean, const GLfloat*);
using PFNDrawArrays = void (*)(GLenum, GLint, GLsizei);

// Fullscreen-triangle blit. Vertex positions/UVs are generated from gl_VertexID,
// so no VBO or vertex attributes are needed (just a bound VAO). The fragment
// shader flips V because decoded frames are top-row-first while GL samples from
// the bottom.
constexpr const char* kVertexSrc = R"(#version 330 core
out vec2 vUV;
void main() {
    vec2 uv = vec2((gl_VertexID == 1) ? 2.0 : 0.0, (gl_VertexID == 2) ? 2.0 : 0.0);
    vUV = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr const char* kFragmentSrc = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;
void main() {
    FragColor = texture(uTex, vec2(vUV.x, 1.0 - vUV.y));
}
)";

// Planar YUV sampling: Y from an R8 texture, chroma either interleaved in an RG8
// texture (NV12) or split across two R8 textures (I420). The YUV→RGB matrix/bias
// come from YuvToRgbMatrix.h with range expansion and chroma offset folded in, so
// the conversion is a single multiply-add.
constexpr const char* kFragmentYuvSrc = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTexY;
uniform sampler2D uTexU;
uniform sampler2D uTexV;
uniform mat3 uYuvMat;
uniform vec3 uYuvBias;
uniform int uNv12;
void main() {
    vec2 uv = vec2(vUV.x, 1.0 - vUV.y);
    float y = texture(uTexY, uv).r;
    vec2 c = (uNv12 == 1) ? texture(uTexU, uv).rg
                          : vec2(texture(uTexU, uv).r, texture(uTexV, uv).r);
    FragColor = vec4(clamp(uYuvMat * vec3(y, c) + uYuvBias, 0.0, 1.0), 1.0);
}
)";

template <typename T>
bool Resolve(T& fn, void* (*getProc)(const char*, void*), void* ud, const char* name)
{
    fn = reinterpret_cast<T>(getProc(name, ud));
    if (!fn)
    {
        Log::Error("GlVideoRenderer: missing GL entry point {}", name);
        return false;
    }
    return true;
}
} // namespace

struct GlVideoRenderer::Impl
{
    // GL entry points.
    PFNViewport Viewport = nullptr;
    PFNEnable Enable = nullptr;
    PFNDisable Disable = nullptr;
    PFNIsEnabled IsEnabled = nullptr;
    PFNBlendFunc BlendFunc = nullptr;
    PFNClearColor ClearColor = nullptr;
    PFNClear Clear = nullptr;
    PFNGenTextures GenTextures = nullptr;
    PFNBindTexture BindTexture = nullptr;
    PFNTexParameteri TexParameteri = nullptr;
    PFNTexImage2D TexImage2D = nullptr;
    PFNTexSubImage2D TexSubImage2D = nullptr;
    PFNPixelStorei PixelStorei = nullptr;
    PFNDeleteTextures DeleteTextures = nullptr;
    PFNActiveTexture ActiveTexture = nullptr;
    PFNGenVertexArrays GenVertexArrays = nullptr;
    PFNBindVertexArray BindVertexArray = nullptr;
    PFNDeleteVertexArrays DeleteVertexArrays = nullptr;
    PFNCreateShader CreateShader = nullptr;
    PFNShaderSource ShaderSource = nullptr;
    PFNCompileShader CompileShader = nullptr;
    PFNGetShaderiv GetShaderiv = nullptr;
    PFNGetShaderInfoLog GetShaderInfoLog = nullptr;
    PFNDeleteShader DeleteShader = nullptr;
    PFNCreateProgram CreateProgram = nullptr;
    PFNAttachShader AttachShader = nullptr;
    PFNLinkProgram LinkProgram = nullptr;
    PFNGetProgramiv GetProgramiv = nullptr;
    PFNGetProgramInfoLog GetProgramInfoLog = nullptr;
    PFNUseProgram UseProgram = nullptr;
    PFNDeleteProgram DeleteProgram = nullptr;
    PFNGetUniformLocation GetUniformLocation = nullptr;
    PFNUniform1i Uniform1i = nullptr;
    PFNUniform3fv Uniform3fv = nullptr;
    PFNUniformMatrix3fv UniformMatrix3fv = nullptr;
    PFNDrawArrays DrawArrays = nullptr;

    // GL objects.
    GLuint program = 0; // RGBA blit (video RGBA path + subtitle overlay)
    GLuint programYuv = 0;
    GLuint vao = 0;
    GLuint texture = 0;             // RGBA video frame
    GLuint planeTex[3] = {0, 0, 0}; // Y / U-or-UV / V planes
    GLint uTexLoc = -1;
    GLint uTexYLoc = -1;
    GLint uTexULoc = -1;
    GLint uTexVLoc = -1;
    GLint uYuvMatLoc = -1;
    GLint uYuvBiasLoc = -1;
    GLint uNv12Loc = -1;
    int texW = 0;
    int texH = 0;
    VideoPixelFormat texFormat = VideoPixelFormat::RGBA;
    int matColorspace = -1; // last colorspace/range/height baked into the YUV uniforms
    int matFullRange = -1;
    int matHeight = -1;
    bool hasFrame = false;

    // Subtitle overlay texture (sized to the on-screen video rectangle).
    GLuint overlayTexture = 0;
    int overlayW = 0;
    int overlayH = 0;
    bool hasOverlay = false;

    bool LoadFunctions(void* (*getProc)(const char*, void*), void* ud);
    GLuint CompileStage(GLenum type, const char* src);
    GLuint LinkStages(const char* fragSrc);
    bool BuildPrograms();
};

bool GlVideoRenderer::Impl::LoadFunctions(void* (*getProc)(const char*, void*), void* ud)
{
    bool ok = true;
    ok &= Resolve(Viewport, getProc, ud, "glViewport");
    ok &= Resolve(Enable, getProc, ud, "glEnable");
    ok &= Resolve(Disable, getProc, ud, "glDisable");
    ok &= Resolve(IsEnabled, getProc, ud, "glIsEnabled");
    ok &= Resolve(BlendFunc, getProc, ud, "glBlendFunc");
    ok &= Resolve(ClearColor, getProc, ud, "glClearColor");
    ok &= Resolve(Clear, getProc, ud, "glClear");
    ok &= Resolve(GenTextures, getProc, ud, "glGenTextures");
    ok &= Resolve(BindTexture, getProc, ud, "glBindTexture");
    ok &= Resolve(TexParameteri, getProc, ud, "glTexParameteri");
    ok &= Resolve(TexImage2D, getProc, ud, "glTexImage2D");
    ok &= Resolve(TexSubImage2D, getProc, ud, "glTexSubImage2D");
    ok &= Resolve(PixelStorei, getProc, ud, "glPixelStorei");
    ok &= Resolve(DeleteTextures, getProc, ud, "glDeleteTextures");
    ok &= Resolve(ActiveTexture, getProc, ud, "glActiveTexture");
    ok &= Resolve(GenVertexArrays, getProc, ud, "glGenVertexArrays");
    ok &= Resolve(BindVertexArray, getProc, ud, "glBindVertexArray");
    ok &= Resolve(DeleteVertexArrays, getProc, ud, "glDeleteVertexArrays");
    ok &= Resolve(CreateShader, getProc, ud, "glCreateShader");
    ok &= Resolve(ShaderSource, getProc, ud, "glShaderSource");
    ok &= Resolve(CompileShader, getProc, ud, "glCompileShader");
    ok &= Resolve(GetShaderiv, getProc, ud, "glGetShaderiv");
    ok &= Resolve(GetShaderInfoLog, getProc, ud, "glGetShaderInfoLog");
    ok &= Resolve(DeleteShader, getProc, ud, "glDeleteShader");
    ok &= Resolve(CreateProgram, getProc, ud, "glCreateProgram");
    ok &= Resolve(AttachShader, getProc, ud, "glAttachShader");
    ok &= Resolve(LinkProgram, getProc, ud, "glLinkProgram");
    ok &= Resolve(GetProgramiv, getProc, ud, "glGetProgramiv");
    ok &= Resolve(GetProgramInfoLog, getProc, ud, "glGetProgramInfoLog");
    ok &= Resolve(UseProgram, getProc, ud, "glUseProgram");
    ok &= Resolve(DeleteProgram, getProc, ud, "glDeleteProgram");
    ok &= Resolve(GetUniformLocation, getProc, ud, "glGetUniformLocation");
    ok &= Resolve(Uniform1i, getProc, ud, "glUniform1i");
    ok &= Resolve(Uniform3fv, getProc, ud, "glUniform3fv");
    ok &= Resolve(UniformMatrix3fv, getProc, ud, "glUniformMatrix3fv");
    ok &= Resolve(DrawArrays, getProc, ud, "glDrawArrays");
    return ok;
}

GLuint GlVideoRenderer::Impl::CompileStage(GLenum type, const char* src)
{
    const GLuint shader = CreateShader(type);
    ShaderSource(shader, 1, &src, nullptr);
    CompileShader(shader);

    GLint status = 0;
    GetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status)
    {
        GLchar log[1024] = {};
        GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        Log::Error("GlVideoRenderer: shader compile failed: {}", log);
        DeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint GlVideoRenderer::Impl::LinkStages(const char* fragSrc)
{
    const GLuint vs = CompileStage(GL_VERTEX_SHADER, kVertexSrc);
    const GLuint fs = CompileStage(GL_FRAGMENT_SHADER, fragSrc);
    if (!vs || !fs)
    {
        if (vs)
        {
            DeleteShader(vs);
        }
        if (fs)
        {
            DeleteShader(fs);
        }
        return 0;
    }

    const GLuint prog = CreateProgram();
    AttachShader(prog, vs);
    AttachShader(prog, fs);
    LinkProgram(prog);
    DeleteShader(vs);
    DeleteShader(fs);

    GLint status = 0;
    GetProgramiv(prog, GL_LINK_STATUS, &status);
    if (!status)
    {
        GLchar log[1024] = {};
        GetProgramInfoLog(prog, sizeof(log), nullptr, log);
        Log::Error("GlVideoRenderer: program link failed: {}", log);
        DeleteProgram(prog);
        return 0;
    }
    return prog;
}

bool GlVideoRenderer::Impl::BuildPrograms()
{
    program = LinkStages(kFragmentSrc);
    programYuv = LinkStages(kFragmentYuvSrc);
    if (!program || !programYuv)
    {
        return false;
    }

    uTexLoc = GetUniformLocation(program, "uTex");
    uTexYLoc = GetUniformLocation(programYuv, "uTexY");
    uTexULoc = GetUniformLocation(programYuv, "uTexU");
    uTexVLoc = GetUniformLocation(programYuv, "uTexV");
    uYuvMatLoc = GetUniformLocation(programYuv, "uYuvMat");
    uYuvBiasLoc = GetUniformLocation(programYuv, "uYuvBias");
    uNv12Loc = GetUniformLocation(programYuv, "uNv12");
    return true;
}

// ── Public surface ────────────────────────────────────────────────────────────

GlVideoRenderer::GlVideoRenderer() = default;

GlVideoRenderer::~GlVideoRenderer()
{
    if (!impl_)
    {
        return;
    }
    // Valid only while the GL context is still current — the host destroys the
    // player (and thus this renderer) before the window/context (see App member
    // order), so the deletes below run against a live context.
    if (impl_->texture && impl_->DeleteTextures)
    {
        impl_->DeleteTextures(1, &impl_->texture);
    }
    for (GLuint tex : impl_->planeTex)
    {
        if (tex && impl_->DeleteTextures)
        {
            impl_->DeleteTextures(1, &tex);
        }
    }
    if (impl_->overlayTexture && impl_->DeleteTextures)
    {
        impl_->DeleteTextures(1, &impl_->overlayTexture);
    }
    if (impl_->vao && impl_->DeleteVertexArrays)
    {
        impl_->DeleteVertexArrays(1, &impl_->vao);
    }
    if (impl_->program && impl_->DeleteProgram)
    {
        impl_->DeleteProgram(impl_->program);
    }
    if (impl_->programYuv && impl_->DeleteProgram)
    {
        impl_->DeleteProgram(impl_->programYuv);
    }
}

bool GlVideoRenderer::Init(IGraphicsBackend* backend)
{
    if (!backend)
    {
        return false;
    }

    // Adapter so the internal loader keeps its (name, ud) shape: resolve every GL
    // entry point through the backend's proc loader.
    const auto getProcAddr = [](const char* name, void* ud) -> void*
    {
        return static_cast<IGraphicsBackend*>(ud)->GetProcAddr(name);
    };

    auto impl = std::make_unique<Impl>();
    if (!impl->LoadFunctions(getProcAddr, backend))
    {
        return false;
    }
    if (!impl->BuildPrograms())
    {
        return false;
    }

    impl->GenVertexArrays(1, &impl->vao);

    const auto allocTexture = [&](GLuint tex)
    {
        impl->BindTexture(GL_TEXTURE_2D, tex);
        impl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        impl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        impl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        impl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    impl->GenTextures(1, &impl->texture);
    allocTexture(impl->texture);
    for (GLuint& tex : impl->planeTex)
    {
        impl->GenTextures(1, &tex);
        allocTexture(tex);
    }
    impl->GenTextures(1, &impl->overlayTexture);
    allocTexture(impl->overlayTexture);
    impl->BindTexture(GL_TEXTURE_2D, 0);

    impl_ = std::move(impl);
    return true;
}

void GlVideoRenderer::UploadFrame(const uint8_t* data, const VideoFrameDesc& desc)
{
    if (!impl_ || !data || desc.w <= 0 || desc.h <= 0)
    {
        return;
    }

    impl_->PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const bool reconfig = desc.w != impl_->texW || desc.h != impl_->texH || desc.format != impl_->texFormat;

    const auto uploadPlane = [&](GLuint tex, GLint internalFmt, GLenum fmt, int w, int h, const uint8_t* pixels)
    {
        impl_->BindTexture(GL_TEXTURE_2D, tex);
        if (reconfig)
        {
            impl_->TexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, pixels);
        }
        else
        {
            impl_->TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, GL_UNSIGNED_BYTE, pixels);
        }
    };

    const int cw = (desc.w + 1) / 2; // chroma texels per row
    const int ch = (desc.h + 1) / 2;
    switch (desc.format)
    {
    case VideoPixelFormat::RGBA:
        uploadPlane(impl_->texture, GL_RGBA8, GL_RGBA, desc.w, desc.h, data + desc.planeOffset[0]);
        break;
    case VideoPixelFormat::NV12:
        uploadPlane(impl_->planeTex[0], GL_R8, GL_RED, desc.w, desc.h, data + desc.planeOffset[0]);
        uploadPlane(impl_->planeTex[1], GL_RG8, GL_RG, cw, ch, data + desc.planeOffset[1]);
        break;
    case VideoPixelFormat::YUV420P:
        uploadPlane(impl_->planeTex[0], GL_R8, GL_RED, desc.w, desc.h, data + desc.planeOffset[0]);
        uploadPlane(impl_->planeTex[1], GL_R8, GL_RED, cw, ch, data + desc.planeOffset[1]);
        uploadPlane(impl_->planeTex[2], GL_R8, GL_RED, cw, ch, data + desc.planeOffset[2]);
        break;
    }
    impl_->BindTexture(GL_TEXTURE_2D, 0);

    // Bake the conversion uniforms only when the colourimetry (or format) changes;
    // they are stable across a stream.
    if (desc.format != VideoPixelFormat::RGBA && (reconfig || desc.colorspace != impl_->matColorspace ||
                                                  desc.fullRange != impl_->matFullRange || desc.h != impl_->matHeight))
    {
        float mat[9];
        float bias[3];
        YuvToRgb::BuildYuvToRgbMatrix(desc.colorspace, desc.fullRange, desc.h, mat, bias);
        impl_->UseProgram(impl_->programYuv);
        impl_->UniformMatrix3fv(impl_->uYuvMatLoc, 1, 0 /*GL_FALSE: column-major*/, mat);
        impl_->Uniform3fv(impl_->uYuvBiasLoc, 1, bias);
        impl_->Uniform1i(impl_->uNv12Loc, desc.format == VideoPixelFormat::NV12 ? 1 : 0);
        impl_->UseProgram(0);
        impl_->matColorspace = desc.colorspace;
        impl_->matFullRange = desc.fullRange;
        impl_->matHeight = desc.h;
    }

    impl_->texW = desc.w;
    impl_->texH = desc.h;
    impl_->texFormat = desc.format;
    impl_->hasFrame = true;
}

void GlVideoRenderer::UploadOverlay(const uint8_t* rgba, int w, int h)
{
    if (!impl_ || !rgba || w <= 0 || h <= 0)
    {
        return;
    }

    impl_->BindTexture(GL_TEXTURE_2D, impl_->overlayTexture);
    impl_->PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (w != impl_->overlayW || h != impl_->overlayH)
    {
        impl_->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        impl_->overlayW = w;
        impl_->overlayH = h;
    }
    else
    {
        impl_->TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    impl_->BindTexture(GL_TEXTURE_2D, 0);
    impl_->hasOverlay = true;
}

void GlVideoRenderer::Draw(int fbX, int fbY, int fbW, int fbH, bool drawOverlay)
{
    // A nonzero origin never occurs under GL: the fallback title bar (the only source
    // of a video inset) exists only with the Vulkan backend on Wayland, and GL's
    // bottom-left viewport origin would need a flip this path does not implement.
    (void)fbX;
    (void)fbY;
    if (!impl_ || fbW <= 0 || fbH <= 0)
    {
        return;
    }

    // Qt's RHI GL backend can leave GL_FRAMEBUFFER_SRGB enabled after rendering
    // blended scene-graph content (e.g. once a translucent overlay panel is shown).
    // With the single-threaded "basic" render loop the context is reused, so that
    // enable leaks into this raw-GL blit and re-encodes our already-gamma-encoded
    // pixels through the sRGB OETF — the video visibly brightens. Our RGBA is written
    // verbatim to a non-sRGB target, so force the state we depend on and restore it
    // afterwards (changedStates() can't cover framebuffer-sRGB). Disable blend too so
    // the opaque video quad can't inherit a leaked blend-enable.
    const bool wasSrgb = impl_->IsEnabled(GL_FRAMEBUFFER_SRGB) != 0;
    impl_->Disable(GL_FRAMEBUFFER_SRGB);
    impl_->Disable(GL_BLEND);

    // Clear the whole framebuffer to black first (covers the letterbox bars).
    impl_->Viewport(0, 0, fbW, fbH);
    impl_->ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    impl_->Clear(GL_COLOR_BUFFER_BIT);

    if (!impl_->hasFrame || impl_->texW <= 0 || impl_->texH <= 0)
    {
        if (wasSrgb)
        {
            impl_->Enable(GL_FRAMEBUFFER_SRGB);
        }
        return;
    }

    // Aspect-ratio-preserving fit (letterbox / pillarbox), centered. The subtitle
    // overlay is rendered at this same on-screen size, so it maps 1:1 over the video.
    const LetterboxRect vp = ComputeLetterbox(fbW, fbH, impl_->texW, impl_->texH);

    impl_->Viewport(vp.x, vp.y, vp.w, vp.h);
    impl_->BindVertexArray(impl_->vao);

    if (impl_->texFormat == VideoPixelFormat::RGBA)
    {
        impl_->UseProgram(impl_->program);
        impl_->ActiveTexture(GL_TEXTURE0);
        impl_->BindTexture(GL_TEXTURE_2D, impl_->texture);
        impl_->Uniform1i(impl_->uTexLoc, 0);
        impl_->DrawArrays(GL_TRIANGLES, 0, 3);
    }
    else
    {
        // Planar YUV: shader-side conversion. For NV12 the V sampler is never taken
        // (uNv12 branch), but bind the UV texture there too so every sampler unit
        // holds a valid texture.
        const GLuint vTex = impl_->texFormat == VideoPixelFormat::NV12 ? impl_->planeTex[1] : impl_->planeTex[2];
        impl_->UseProgram(impl_->programYuv);
        impl_->ActiveTexture(GL_TEXTURE0);
        impl_->BindTexture(GL_TEXTURE_2D, impl_->planeTex[0]);
        impl_->ActiveTexture(GL_TEXTURE1);
        impl_->BindTexture(GL_TEXTURE_2D, impl_->planeTex[1]);
        impl_->ActiveTexture(GL_TEXTURE2);
        impl_->BindTexture(GL_TEXTURE_2D, vTex);
        impl_->Uniform1i(impl_->uTexYLoc, 0);
        impl_->Uniform1i(impl_->uTexULoc, 1);
        impl_->Uniform1i(impl_->uTexVLoc, 2);
        impl_->DrawArrays(GL_TRIANGLES, 0, 3);
        // Leave units 1/2 unbound again so Qt Quick sees the state it expects.
        impl_->BindTexture(GL_TEXTURE_2D, 0);
        impl_->ActiveTexture(GL_TEXTURE1);
        impl_->BindTexture(GL_TEXTURE_2D, 0);
        impl_->ActiveTexture(GL_TEXTURE0);
    }

    // Composite the subtitle overlay over the video within the same rectangle,
    // using straight-alpha blending (always through the RGBA program).
    if (drawOverlay && impl_->hasOverlay)
    {
        impl_->UseProgram(impl_->program);
        impl_->ActiveTexture(GL_TEXTURE0);
        impl_->BindTexture(GL_TEXTURE_2D, impl_->overlayTexture);
        impl_->Uniform1i(impl_->uTexLoc, 0);
        impl_->Enable(GL_BLEND);
        impl_->BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        impl_->DrawArrays(GL_TRIANGLES, 0, 3);
        impl_->Disable(GL_BLEND);
    }

    // Restore neutral state before returning control to Qt Quick.
    impl_->BindVertexArray(0);
    impl_->BindTexture(GL_TEXTURE_2D, 0);
    impl_->UseProgram(0);
    impl_->Viewport(0, 0, fbW, fbH);
    if (wasSrgb)
    {
        impl_->Enable(GL_FRAMEBUFFER_SRGB);
    }
}
