#include "GlVideoRenderer.h"

#include "../ffmpeg/FFmpegLetterbox.h"
#include "IGraphicsBackend.h"

#include <framelift/Log.h>

#include <cmath>
#include <cstddef>

// Minimal, self-contained GL 3.3-core loader. We deliberately avoid system GL
// headers (which are awkward under the MinGW cross-compile) and instead declare
// the handful of types, constants and entry points we use, loading them through
// the host's getProcAddr. 64-bit only, so the Windows __stdcall thunk that GL
// historically needs is a no-op and can be omitted from the function pointers.
namespace
{
using GLenum = unsigned int;
using GLbitfield = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLfloat = float;
using GLchar = char;

constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLint GL_RGBA8 = 0x8058;
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

using PFNViewport = void (*)(GLint, GLint, GLsizei, GLsizei);
using PFNEnable = void (*)(GLenum);
using PFNDisable = void (*)(GLenum);
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
    PFNDrawArrays DrawArrays = nullptr;

    // GL objects.
    GLuint program = 0;
    GLuint vao = 0;
    GLuint texture = 0;
    GLint uTexLoc = -1;
    int texW = 0;
    int texH = 0;
    bool hasFrame = false;

    // Subtitle overlay texture (sized to the on-screen video rectangle).
    GLuint overlayTexture = 0;
    int overlayW = 0;
    int overlayH = 0;
    bool hasOverlay = false;

    bool LoadFunctions(void* (*getProc)(const char*, void*), void* ud);
    GLuint CompileStage(GLenum type, const char* src);
    bool BuildProgram();
};

bool GlVideoRenderer::Impl::LoadFunctions(void* (*getProc)(const char*, void*), void* ud)
{
    bool ok = true;
    ok &= Resolve(Viewport, getProc, ud, "glViewport");
    ok &= Resolve(Enable, getProc, ud, "glEnable");
    ok &= Resolve(Disable, getProc, ud, "glDisable");
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

bool GlVideoRenderer::Impl::BuildProgram()
{
    const GLuint vs = CompileStage(GL_VERTEX_SHADER, kVertexSrc);
    const GLuint fs = CompileStage(GL_FRAGMENT_SHADER, kFragmentSrc);
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
        return false;
    }

    program = CreateProgram();
    AttachShader(program, vs);
    AttachShader(program, fs);
    LinkProgram(program);
    DeleteShader(vs);
    DeleteShader(fs);

    GLint status = 0;
    GetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status)
    {
        GLchar log[1024] = {};
        GetProgramInfoLog(program, sizeof(log), nullptr, log);
        Log::Error("GlVideoRenderer: program link failed: {}", log);
        DeleteProgram(program);
        program = 0;
        return false;
    }

    uTexLoc = GetUniformLocation(program, "uTex");
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
}

bool GlVideoRenderer::Init(IGraphicsBackend* backend)
{
    if (!backend)
    {
        return false;
    }

    // Adapter so the internal loader keeps its (name, ud) shape: resolve every GL
    // entry point through the backend's proc loader.
    const auto getProcAddr = [](const char* name, void* ud) -> void* {
        return static_cast<IGraphicsBackend*>(ud)->GetProcAddr(name);
    };

    auto impl = std::make_unique<Impl>();
    if (!impl->LoadFunctions(getProcAddr, backend))
    {
        return false;
    }
    if (!impl->BuildProgram())
    {
        return false;
    }

    impl->GenVertexArrays(1, &impl->vao);

    const auto allocTexture = [&](GLuint tex) {
        impl->BindTexture(GL_TEXTURE_2D, tex);
        impl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        impl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        impl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        impl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    impl->GenTextures(1, &impl->texture);
    allocTexture(impl->texture);
    impl->GenTextures(1, &impl->overlayTexture);
    allocTexture(impl->overlayTexture);
    impl->BindTexture(GL_TEXTURE_2D, 0);

    impl_ = std::move(impl);
    return true;
}

void GlVideoRenderer::Upload(const uint8_t* rgba, int w, int h)
{
    if (!impl_ || !rgba || w <= 0 || h <= 0)
    {
        return;
    }

    impl_->BindTexture(GL_TEXTURE_2D, impl_->texture);
    impl_->PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (w != impl_->texW || h != impl_->texH)
    {
        // (Re)allocate the texture storage when the frame size changes.
        impl_->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        impl_->texW = w;
        impl_->texH = h;
    }
    else
    {
        impl_->TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    impl_->BindTexture(GL_TEXTURE_2D, 0);
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

void GlVideoRenderer::Draw(int fbW, int fbH, bool drawOverlay)
{
    if (!impl_ || fbW <= 0 || fbH <= 0)
    {
        return;
    }

    // Clear the whole framebuffer to black first (covers the letterbox bars).
    impl_->Viewport(0, 0, fbW, fbH);
    impl_->ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    impl_->Clear(GL_COLOR_BUFFER_BIT);

    if (!impl_->hasFrame || impl_->texW <= 0 || impl_->texH <= 0)
    {
        return;
    }

    // Aspect-ratio-preserving fit (letterbox / pillarbox), centered. The subtitle
    // overlay is rendered at this same on-screen size, so it maps 1:1 over the video.
    const LetterboxRect vp = ComputeLetterbox(fbW, fbH, impl_->texW, impl_->texH);

    impl_->Viewport(vp.x, vp.y, vp.w, vp.h);
    impl_->UseProgram(impl_->program);
    impl_->ActiveTexture(GL_TEXTURE0);
    impl_->BindTexture(GL_TEXTURE_2D, impl_->texture);
    impl_->Uniform1i(impl_->uTexLoc, 0);
    impl_->BindVertexArray(impl_->vao);
    impl_->DrawArrays(GL_TRIANGLES, 0, 3);

    // Composite the subtitle overlay over the video within the same rectangle,
    // using straight-alpha blending.
    if (drawOverlay && impl_->hasOverlay)
    {
        impl_->Enable(GL_BLEND);
        impl_->BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        impl_->BindTexture(GL_TEXTURE_2D, impl_->overlayTexture);
        impl_->DrawArrays(GL_TRIANGLES, 0, 3);
        impl_->Disable(GL_BLEND);
    }

    // Restore neutral state so the host's ImGui pass starts clean.
    impl_->BindVertexArray(0);
    impl_->BindTexture(GL_TEXTURE_2D, 0);
    impl_->UseProgram(0);
    impl_->Viewport(0, 0, fbW, fbH);
}
