// SPH Fluid - native Win32/OpenGL viewer for the Visual Studio solution.
//
// This file intentionally avoids GLFW/GLAD so the .sln builds in Visual Studio
// without external dependencies. The CMake build still uses viewer_glfw.cpp.
//
// Controls:
//   left-drag   orbit camera
//   mouse wheel zoom
//   Space       pause / resume
//   Up/Down     simulation speed
//   R           reset
//   M           toggle surface / particles
//   P           overlay particles while surface mode is active
//   F           toggle foam/spray overlay in surface mode
//   O           toggle solid sphere obstacle
//   H           toggle HUD overlay
//   Esc         quit

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <gl/GL.h>

#include "mat4.hpp"
#include "sph.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif
#ifndef GL_POINT_SPRITE
#define GL_POINT_SPRITE 0x8861
#endif

using GLchar = char;
using GLsizeiptr = ptrdiff_t;

using PFNGLCREATESHADERPROC = GLuint(APIENTRY*)(GLenum type);
using PFNGLSHADERSOURCEPROC = void(APIENTRY*)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
using PFNGLCOMPILESHADERPROC = void(APIENTRY*)(GLuint shader);
using PFNGLGETSHADERIVPROC = void(APIENTRY*)(GLuint shader, GLenum pname, GLint* params);
using PFNGLGETSHADERINFOLOGPROC = void(APIENTRY*)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
using PFNGLCREATEPROGRAMPROC = GLuint(APIENTRY*)();
using PFNGLATTACHSHADERPROC = void(APIENTRY*)(GLuint program, GLuint shader);
using PFNGLLINKPROGRAMPROC = void(APIENTRY*)(GLuint program);
using PFNGLGETPROGRAMIVPROC = void(APIENTRY*)(GLuint program, GLenum pname, GLint* params);
using PFNGLGETPROGRAMINFOLOGPROC = void(APIENTRY*)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
using PFNGLDELETESHADERPROC = void(APIENTRY*)(GLuint shader);
using PFNGLGENVERTEXARRAYSPROC = void(APIENTRY*)(GLsizei n, GLuint* arrays);
using PFNGLBINDVERTEXARRAYPROC = void(APIENTRY*)(GLuint array);
using PFNGLGENBUFFERSPROC = void(APIENTRY*)(GLsizei n, GLuint* buffers);
using PFNGLBINDBUFFERPROC = void(APIENTRY*)(GLenum target, GLuint buffer);
using PFNGLBUFFERDATAPROC = void(APIENTRY*)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
using PFNGLENABLEVERTEXATTRIBARRAYPROC = void(APIENTRY*)(GLuint index);
using PFNGLVERTEXATTRIBPOINTERPROC = void(APIENTRY*)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
using PFNGLUSEPROGRAMPROC = void(APIENTRY*)(GLuint program);
using PFNGLGETUNIFORMLOCATIONPROC = GLint(APIENTRY*)(GLuint program, const GLchar* name);
using PFNGLUNIFORMMATRIX4FVPROC = void(APIENTRY*)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
using PFNGLUNIFORM3FPROC = void(APIENTRY*)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
using PFNGLUNIFORM1FPROC = void(APIENTRY*)(GLint location, GLfloat v0);
using PFNWGLSWAPINTERVALEXTPROC = BOOL(WINAPI*)(int interval);

static PFNGLCREATESHADERPROC glCreateShader_ = nullptr;
static PFNGLSHADERSOURCEPROC glShaderSource_ = nullptr;
static PFNGLCOMPILESHADERPROC glCompileShader_ = nullptr;
static PFNGLGETSHADERIVPROC glGetShaderiv_ = nullptr;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog_ = nullptr;
static PFNGLCREATEPROGRAMPROC glCreateProgram_ = nullptr;
static PFNGLATTACHSHADERPROC glAttachShader_ = nullptr;
static PFNGLLINKPROGRAMPROC glLinkProgram_ = nullptr;
static PFNGLGETPROGRAMIVPROC glGetProgramiv_ = nullptr;
static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog_ = nullptr;
static PFNGLDELETESHADERPROC glDeleteShader_ = nullptr;
static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays_ = nullptr;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray_ = nullptr;
static PFNGLGENBUFFERSPROC glGenBuffers_ = nullptr;
static PFNGLBINDBUFFERPROC glBindBuffer_ = nullptr;
static PFNGLBUFFERDATAPROC glBufferData_ = nullptr;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray_ = nullptr;
static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer_ = nullptr;
static PFNGLUSEPROGRAMPROC glUseProgram_ = nullptr;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_ = nullptr;
static PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv_ = nullptr;
static PFNGLUNIFORM3FPROC glUniform3f_ = nullptr;
static PFNGLUNIFORM1FPROC glUniform1f_ = nullptr;
static PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT_ = nullptr;

static void* getGlProcAddress(const char* name) {
    void* p = reinterpret_cast<void*>(wglGetProcAddress(name));
    const auto ip = reinterpret_cast<std::uintptr_t>(p);
    if (p == nullptr || ip == 1 || ip == 2 || ip == 3 || p == reinterpret_cast<void*>(-1)) {
        HMODULE module = LoadLibraryA("opengl32.dll");
        p = module ? reinterpret_cast<void*>(GetProcAddress(module, name)) : nullptr;
    }
    return p;
}

#define LOAD_GL(name) \
    do { \
        name##_ = reinterpret_cast<decltype(name##_)>(getGlProcAddress(#name)); \
        if (!name##_) { \
            MessageBoxA(nullptr, "Required OpenGL function is missing: " #name, "SPH Fluid", MB_ICONERROR | MB_OK); \
            return false; \
        } \
    } while (false)

static bool loadOpenGL() {
    LOAD_GL(glCreateShader);
    LOAD_GL(glShaderSource);
    LOAD_GL(glCompileShader);
    LOAD_GL(glGetShaderiv);
    LOAD_GL(glGetShaderInfoLog);
    LOAD_GL(glCreateProgram);
    LOAD_GL(glAttachShader);
    LOAD_GL(glLinkProgram);
    LOAD_GL(glGetProgramiv);
    LOAD_GL(glGetProgramInfoLog);
    LOAD_GL(glDeleteShader);
    LOAD_GL(glGenVertexArrays);
    LOAD_GL(glBindVertexArray);
    LOAD_GL(glGenBuffers);
    LOAD_GL(glBindBuffer);
    LOAD_GL(glBufferData);
    LOAD_GL(glEnableVertexAttribArray);
    LOAD_GL(glVertexAttribPointer);
    LOAD_GL(glUseProgram);
    LOAD_GL(glGetUniformLocation);
    LOAD_GL(glUniformMatrix4fv);
    LOAD_GL(glUniform3f);
    LOAD_GL(glUniform1f);

    wglSwapIntervalEXT_ = reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>(getGlProcAddress("wglSwapIntervalEXT"));
    if (wglSwapIntervalEXT_) wglSwapIntervalEXT_(1);
    return true;
}

static const char* PARTICLE_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aSpeed;
uniform mat4 uViewProj;
uniform float uPointScale;
uniform float uSpeedScale;
out float vSpeedT;
void main(){
    vec4 clip = uViewProj * vec4(aPos, 1.0);
    gl_Position = clip;
    gl_PointSize = clamp(uPointScale / max(clip.w, 0.001), 2.0, 64.0);
    vSpeedT = clamp(aSpeed * uSpeedScale, 0.0, 1.0);
}
)";

static const char* PARTICLE_FS = R"(#version 330 core
in float vSpeedT;
out vec4 frag;
void main(){
    vec2 c = gl_PointCoord * 2.0 - 1.0;
    float d2 = dot(c, c);
    if (d2 > 1.0) discard;
    vec3 n = vec3(c.x, -c.y, sqrt(1.0 - d2));
    vec3 L = normalize(vec3(0.3, 0.85, 0.5));
    float diff = max(dot(n, L), 0.0);
    vec3 H = normalize(L + vec3(0.0, 0.0, 1.0));
    float spec = pow(max(dot(n, H), 0.0), 48.0);
    vec3 deep = vec3(0.05, 0.25, 0.55);
    vec3 foam = vec3(0.80, 0.92, 1.0);
    vec3 base = mix(deep, foam, vSpeedT);
    vec3 col = base * (0.3 + 0.7 * diff) + vec3(spec) * 0.6;
    frag = vec4(col, 1.0);
}
)";

static const char* LINE_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uViewProj;
void main(){ gl_Position = uViewProj * vec4(aPos, 1.0); }
)";

static const char* LINE_FS = R"(#version 330 core
out vec4 frag;
uniform vec3 uColor;
void main(){ frag = vec4(uColor, 1.0); }
)";

static const char* SURFACE_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 uViewProj;
out vec3 vNormal;
out vec3 vWorld;
void main(){
    vWorld = aPos;
    vNormal = normalize(aNormal);
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

static const char* SURFACE_FS = R"(#version 330 core
in vec3 vNormal;
in vec3 vWorld;
uniform vec3 uCameraPos;
uniform vec3 uBoxMax;
out vec4 frag;
void main(){
    vec3 n = normalize(vNormal);
    vec3 L = normalize(vec3(-0.35, 0.9, 0.45));
    vec3 V = normalize(uCameraPos - vWorld);

    float ndotl = max(dot(n, L), 0.0);
    vec3 H = normalize(L + V);
    float specTight = pow(max(dot(n, H), 0.0), 120.0);
    float specWide = pow(max(dot(n, H), 0.0), 24.0);
    float fresnel = pow(1.0 - clamp(dot(n, V), 0.0, 1.0), 4.0);

    float heightT = clamp(vWorld.y / max(uBoxMax.y, 0.001), 0.0, 1.0);
    vec3 deep = vec3(0.015, 0.10, 0.24);
    vec3 mid = vec3(0.025, 0.32, 0.62);
    vec3 shallow = vec3(0.36, 0.78, 0.96);
    vec3 sky = vec3(0.50, 0.72, 1.00);

    vec3 water = mix(deep, mid, smoothstep(0.0, 0.7, heightT));
    water = mix(water, shallow, smoothstep(0.72, 1.0, heightT) * 0.55);

    float facing = 0.18 + 0.82 * ndotl;
    vec3 col = water * facing;
    col += sky * fresnel * 0.40;
    col += vec3(1.0) * specTight * 1.15;
    col += vec3(0.65, 0.86, 1.0) * specWide * 0.20;

    // Slight contact-darkening near the bottom makes the volume read less like plastic.
    col *= mix(0.72, 1.0, smoothstep(0.0, 0.15, heightT));
    frag = vec4(col, 1.0);
}
)";

static void debugLog(const char* text) {
    OutputDebugStringA(text);
    OutputDebugStringA("\n");
}

static GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader_(type);
    glShaderSource_(shader, 1, &src, nullptr);
    glCompileShader_(shader);

    GLint ok = 0;
    glGetShaderiv_(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048]{};
        glGetShaderInfoLog_(shader, sizeof(log), nullptr, log);
        debugLog(log);
        MessageBoxA(nullptr, log, "Shader compile error", MB_ICONERROR | MB_OK);
    }
    return shader;
}

static GLuint linkProgram(const char* vs, const char* fs) {
    GLuint program = glCreateProgram_();
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    glAttachShader_(program, v);
    glAttachShader_(program, f);
    glLinkProgram_(program);
    glDeleteShader_(v);
    glDeleteShader_(f);

    GLint ok = 0;
    glGetProgramiv_(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048]{};
        glGetProgramInfoLog_(program, sizeof(log), nullptr, log);
        debugLog(log);
        MessageBoxA(nullptr, log, "Shader link error", MB_ICONERROR | MB_OK);
    }
    return program;
}

static std::unique_ptr<Sph> makeSim(bool obstacle = false) {
    SphParams p;
    p.h = 1.0f;
    p.boxMin = {0, 0, 0};
    p.boxMax = {34, 34, 24};
    p.sphereObstacle = obstacle;
    p.sphereCenter = {20.0f, 8.0f, 12.0f};
    p.sphereRadius = 4.8f;
    auto sim = std::make_unique<Sph>(p);
    sim->emitBlock({1.5f, 1.5f, 1.5f}, {13, 32, 22.5f}, 0.6f);
    sim->setThreads(static_cast<int>(std::thread::hardware_concurrency()));
    return sim;
}

static void buildBoxLines(const Vec3& a, const Vec3& b, std::vector<float>& out) {
    Vec3 c[8] = {{a.x,a.y,a.z},{b.x,a.y,a.z},{b.x,b.y,a.z},{a.x,b.y,a.z},
                 {a.x,a.y,b.z},{b.x,a.y,b.z},{b.x,b.y,b.z},{a.x,b.y,b.z}};
    int e[24] = {0,1,1,2,2,3,3,0, 4,5,5,6,6,7,7,4, 0,4,1,5,2,6,3,7};
    out.clear();
    for (int i : e) {
        out.push_back(c[i].x);
        out.push_back(c[i].y);
        out.push_back(c[i].z);
    }
}

static void appendSphereWire(const Vec3& center, float radius, std::vector<float>& out) {
    constexpr int segments = 72;
    auto push = [&](const Vec3& p) {
        out.push_back(p.x);
        out.push_back(p.y);
        out.push_back(p.z);
    };
    for (int ring = 0; ring < 3; ++ring) {
        for (int i = 0; i < segments; ++i) {
            const float a0 = 6.28318530718f * static_cast<float>(i) / static_cast<float>(segments);
            const float a1 = 6.28318530718f * static_cast<float>(i + 1) / static_cast<float>(segments);
            Vec3 p0{}, p1{};
            if (ring == 0) { // XZ plane
                p0 = {center.x + std::cos(a0) * radius, center.y, center.z + std::sin(a0) * radius};
                p1 = {center.x + std::cos(a1) * radius, center.y, center.z + std::sin(a1) * radius};
            } else if (ring == 1) { // XY plane
                p0 = {center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius, center.z};
                p1 = {center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius, center.z};
            } else { // YZ plane
                p0 = {center.x, center.y + std::cos(a0) * radius, center.z + std::sin(a0) * radius};
                p1 = {center.x, center.y + std::cos(a1) * radius, center.z + std::sin(a1) * radius};
            }
            push(p0);
            push(p1);
        }
    }
}

static void buildSceneLines(const SphParams& params, std::vector<float>& out) {
    buildBoxLines(params.boxMin, params.boxMax, out);
    if (params.sphereObstacle) appendSphereWire(params.sphereCenter, params.sphereRadius, out);
}

struct SurfaceVertex {
    Vec3 p;
    Vec3 n;
};

struct SurfaceExtractor {
    float cell = 0.75f;
    float radius = 1.45f;
    float iso = 0.48f;
    Vec3 origin{};
    int nx = 0, ny = 0, nz = 0;
    std::vector<float> field;
    std::vector<SurfaceVertex> vertices;

    int index(int x, int y, int z) const { return x + nx * (y + ny * z); }

    Vec3 posAt(int x, int y, int z) const {
        return {origin.x + cell * static_cast<float>(x),
                origin.y + cell * static_cast<float>(y),
                origin.z + cell * static_cast<float>(z)};
    }

    float sample(int x, int y, int z) const {
        x = std::clamp(x, 0, nx - 1);
        y = std::clamp(y, 0, ny - 1);
        z = std::clamp(z, 0, nz - 1);
        return field[index(x, y, z)];
    }

    Vec3 gradient(int x, int y, int z) const {
        Vec3 g{sample(x + 1, y, z) - sample(x - 1, y, z),
               sample(x, y + 1, z) - sample(x, y - 1, z),
               sample(x, y, z + 1) - sample(x, y, z - 1)};
        // Field increases towards the fluid interior.  Negating gives outward normals.
        return (g * -1.0f).normalized();
    }

    void depositField(const std::vector<Vec3>& particles, const SphParams& params) {
        origin = params.boxMin;
        nx = static_cast<int>(std::ceil((params.boxMax.x - params.boxMin.x) / cell)) + 1;
        ny = static_cast<int>(std::ceil((params.boxMax.y - params.boxMin.y) / cell)) + 1;
        nz = static_cast<int>(std::ceil((params.boxMax.z - params.boxMin.z) / cell)) + 1;
        field.assign(static_cast<size_t>(nx) * ny * nz, 0.0f);

        const float r2max = radius * radius;
        const int cr = static_cast<int>(std::ceil(radius / cell));
        for (const Vec3& p : particles) {
            const int cx = static_cast<int>(std::floor((p.x - origin.x) / cell));
            const int cy = static_cast<int>(std::floor((p.y - origin.y) / cell));
            const int cz = static_cast<int>(std::floor((p.z - origin.z) / cell));
            const int x0 = std::max(0, cx - cr), x1 = std::min(nx - 1, cx + cr);
            const int y0 = std::max(0, cy - cr), y1 = std::min(ny - 1, cy + cr);
            const int z0 = std::max(0, cz - cr), z1 = std::min(nz - 1, cz + cr);
            for (int z = z0; z <= z1; ++z) for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
                Vec3 q = posAt(x, y, z);
                Vec3 d = q - p;
                const float r2 = d.lengthSquared();
                if (r2 < r2max) {
                    const float t = 1.0f - r2 / r2max;
                    field[index(x, y, z)] += t * t * t;
                }
            }
        }
    }

    SurfaceVertex vertexOnEdge(const SurfaceVertex& a, float va, const SurfaceVertex& b, float vb) const {
        float t = (iso - va) / ((vb - va) == 0.0f ? 1.0f : (vb - va));
        t = std::clamp(t, 0.0f, 1.0f);
        SurfaceVertex r;
        r.p = a.p + (b.p - a.p) * t;
        r.n = (a.n + (b.n - a.n) * t).normalized();
        return r;
    }

    void emitTri(const SurfaceVertex& a, const SurfaceVertex& b, const SurfaceVertex& c) {
        Vec3 fn = (b.p - a.p).cross(c.p - a.p).normalized();
        if (fn.lengthSquared() == 0.0f) return;
        vertices.push_back(a);
        vertices.push_back(b);
        vertices.push_back(c);
    }

    void polygonizeTet(const SurfaceVertex v[4], const float val[4]) {
        int inside[4], outside[4], ni = 0, no = 0;
        for (int i = 0; i < 4; ++i) {
            if (val[i] >= iso) inside[ni++] = i;
            else outside[no++] = i;
        }
        if (ni == 0 || ni == 4) return;
        if (ni == 1) {
            SurfaceVertex a = vertexOnEdge(v[inside[0]], val[inside[0]], v[outside[0]], val[outside[0]]);
            SurfaceVertex b = vertexOnEdge(v[inside[0]], val[inside[0]], v[outside[1]], val[outside[1]]);
            SurfaceVertex c = vertexOnEdge(v[inside[0]], val[inside[0]], v[outside[2]], val[outside[2]]);
            emitTri(a, b, c);
        } else if (ni == 3) {
            SurfaceVertex a = vertexOnEdge(v[outside[0]], val[outside[0]], v[inside[0]], val[inside[0]]);
            SurfaceVertex b = vertexOnEdge(v[outside[0]], val[outside[0]], v[inside[1]], val[inside[1]]);
            SurfaceVertex c = vertexOnEdge(v[outside[0]], val[outside[0]], v[inside[2]], val[inside[2]]);
            emitTri(a, c, b);
        } else { // ni == 2
            SurfaceVertex a = vertexOnEdge(v[inside[0]], val[inside[0]], v[outside[0]], val[outside[0]]);
            SurfaceVertex b = vertexOnEdge(v[inside[0]], val[inside[0]], v[outside[1]], val[outside[1]]);
            SurfaceVertex c = vertexOnEdge(v[inside[1]], val[inside[1]], v[outside[0]], val[outside[0]]);
            SurfaceVertex d = vertexOnEdge(v[inside[1]], val[inside[1]], v[outside[1]], val[outside[1]]);
            emitTri(a, b, c);
            emitTri(b, d, c);
        }
    }

    void build(const std::vector<Vec3>& particles, const SphParams& params) {
        depositField(particles, params);
        vertices.clear();
        vertices.reserve(particles.size() * 3);

        static constexpr int cornerOffset[8][3] = {
            {0,0,0},{1,0,0},{1,1,0},{0,1,0},
            {0,0,1},{1,0,1},{1,1,1},{0,1,1}
        };
        static constexpr int tetra[6][4] = {
            {0, 5, 1, 6}, {0, 1, 2, 6}, {0, 2, 3, 6},
            {0, 3, 7, 6}, {0, 7, 4, 6}, {0, 4, 5, 6}
        };

        for (int z = 0; z < nz - 1; ++z) for (int y = 0; y < ny - 1; ++y) for (int x = 0; x < nx - 1; ++x) {
            float cv[8];
            SurfaceVertex corner[8];
            float minv = 1e9f, maxv = -1e9f;
            for (int c = 0; c < 8; ++c) {
                const int gx = x + cornerOffset[c][0];
                const int gy = y + cornerOffset[c][1];
                const int gz = z + cornerOffset[c][2];
                cv[c] = sample(gx, gy, gz);
                minv = std::min(minv, cv[c]);
                maxv = std::max(maxv, cv[c]);
                corner[c].p = posAt(gx, gy, gz);
                corner[c].n = gradient(gx, gy, gz);
            }
            if (minv > iso || maxv < iso) continue;
            for (const auto& t : tetra) {
                SurfaceVertex tv[4] = {corner[t[0]], corner[t[1]], corner[t[2]], corner[t[3]]};
                float vv[4] = {cv[t[0]], cv[t[1]], cv[t[2]], cv[t[3]]};
                polygonizeTet(tv, vv);
            }
        }
    }
};

struct ViewerState {
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC glrc = nullptr;

    OrbitCamera camera;
    bool dragging = false;
    bool paused = false;
    double lastX = 0.0;
    double lastY = 0.0;
    int substeps = 4;

    GLuint particleProgram = 0;
    GLuint lineProgram = 0;
    GLuint surfaceProgram = 0;
    GLuint particleVao = 0;
    GLuint particleVbo = 0;
    GLuint lineVao = 0;
    GLuint lineVbo = 0;
    GLuint surfaceVao = 0;
    GLuint surfaceVbo = 0;
    GLuint fontBase = 0;
    HFONT hudFont = nullptr;

    bool surfaceMode = true;
    bool showParticles = false;
    bool showFoam = true;
    bool obstacleEnabled = false;
    bool showHud = true;
    int surfaceBuildStride = 2;
    int surfaceFrame = 0;
    SurfaceExtractor surface;

    std::unique_ptr<Sph> sim;
    std::vector<float> boxLines;
    std::vector<float> interleaved;
    std::vector<float> foamInterleaved;

    int frames = 0;
    double fpsDisplay = 0.0;
    std::chrono::steady_clock::time_point lastTitleUpdate = std::chrono::steady_clock::now();
};

static ViewerState* g_state = nullptr;

static void resetSimulation(ViewerState& s) {
    s.sim = makeSim(s.obstacleEnabled);
    buildSceneLines(s.sim->params(), s.boxLines);
    glBindVertexArray_(s.lineVao);
    glBindBuffer_(GL_ARRAY_BUFFER, s.lineVbo);
    glBufferData_(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(s.boxLines.size() * sizeof(float)), s.boxLines.data(), GL_STATIC_DRAW);
    s.surface.vertices.clear();
    s.surfaceFrame = 0;
}

static bool createOpenGLContext(ViewerState& s) {
    s.hdc = GetDC(s.hwnd);
    if (!s.hdc) return false;

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int format = ChoosePixelFormat(s.hdc, &pfd);
    if (format == 0 || !SetPixelFormat(s.hdc, format, &pfd)) return false;

    s.glrc = wglCreateContext(s.hdc);
    if (!s.glrc || !wglMakeCurrent(s.hdc, s.glrc)) return false;

    return loadOpenGL();
}


static void createHudFont(ViewerState& s) {
    s.fontBase = glGenLists(96);
    s.hudFont = CreateFontA(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, FF_DONTCARE | DEFAULT_PITCH,
        "Consolas");
    if (!s.hudFont) {
        s.hudFont = CreateFontA(
            -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, FF_DONTCARE | DEFAULT_PITCH,
            "Courier New");
    }
    HGDIOBJ old = SelectObject(s.hdc, s.hudFont);
    wglUseFontBitmapsA(s.hdc, 32, 96, s.fontBase);
    SelectObject(s.hdc, old);
}

static void drawHudText(const ViewerState& s, int x, int y, const char* text) {
    glRasterPos2i(x, y);
    glListBase(s.fontBase - 32);
    glCallLists(static_cast<GLsizei>(std::strlen(text)), GL_UNSIGNED_BYTE, text);
}

static void renderHud(const ViewerState& s) {
    if (!s.showHud || s.fontBase == 0 || !s.sim) return;

    // The panel and bitmap text use the fixed-function pipeline (glOrtho,
    // glRasterPos, glBegin). A modern shader program is still bound from the
    // particle/surface passes; while it is active the fixed-function matrices
    // are ignored and the HUD is transformed by the 3D camera (off-screen).
    // Return to fixed-function before drawing the overlay.
    glUseProgram_(0);
    glBindVertexArray_(0);

    RECT rc{};
    GetClientRect(s.hwnd, &rc);
    const int w = std::max(1L, rc.right - rc.left);
    const int h = std::max(1L, rc.bottom - rc.top);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(w), static_cast<double>(h), 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const float panelX = 14.0f;
    const float panelY = 14.0f;
    const float panelW = 390.0f;
    const float panelH = 218.0f;
    glColor4f(0.02f, 0.025f, 0.035f, 0.72f);
    glBegin(GL_QUADS);
    glVertex2f(panelX, panelY);
    glVertex2f(panelX + panelW, panelY);
    glVertex2f(panelX + panelW, panelY + panelH);
    glVertex2f(panelX, panelY + panelH);
    glEnd();

    glColor4f(0.55f, 0.68f, 0.82f, 0.45f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(panelX, panelY);
    glVertex2f(panelX + panelW, panelY);
    glVertex2f(panelX + panelW, panelY + panelH);
    glVertex2f(panelX, panelY + panelH);
    glEnd();

    const auto& pos = s.sim->positions();
    char line[256]{};
    int y = 38;

    glColor3f(0.86f, 0.93f, 1.0f);
    drawHudText(s, 28, y, "SPH Fluid / Surface Reconstruction");
    y += 26;

    glColor3f(0.72f, 0.86f, 1.0f);
    std::snprintf(line, sizeof(line), "FPS: %.0f    Particles: %zu    Substeps: x%d", s.fpsDisplay, pos.size(), s.substeps);
    drawHudText(s, 28, y, line);
    y += 20;
    std::snprintf(line, sizeof(line), "Mode: %s%s%s%s",
        s.surfaceMode ? "surface" : "particles",
        s.showParticles ? " + particles" : "",
        s.showFoam ? " + foam" : "",
        s.obstacleEnabled ? " + obstacle" : "");
    drawHudText(s, 28, y, line);
    y += 20;
    std::snprintf(line, sizeof(line), "State: %s    Surface verts: %zu", s.paused ? "paused" : "running", s.surface.vertices.size());
    drawHudText(s, 28, y, line);
    y += 30;

    glColor3f(0.86f, 0.88f, 0.90f);
    drawHudText(s, 28, y, "Controls:"); y += 20;
    drawHudText(s, 42, y, "Mouse drag / wheel  orbit / zoom"); y += 18;
    drawHudText(s, 42, y, "Space  pause    R  reset"); y += 18;
    drawHudText(s, 42, y, "Up/Down  simulation speed"); y += 18;
    drawHudText(s, 42, y, "M  surface/particles    P  particle overlay"); y += 18;
    drawHudText(s, 42, y, "F  foam    O  obstacle    H  hide HUD");

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

static bool initRenderer(ViewerState& s) {
    s.camera.target = {17, 12, 12};
    s.camera.distance = 80.0f;
    s.sim = makeSim(s.obstacleEnabled);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_POINT_SPRITE); // compat-profile: required for gl_PointCoord
    glEnable(GL_DEPTH_TEST);

    s.particleProgram = linkProgram(PARTICLE_VS, PARTICLE_FS);
    s.lineProgram = linkProgram(LINE_VS, LINE_FS);
    s.surfaceProgram = linkProgram(SURFACE_VS, SURFACE_FS);
    createHudFont(s);

    glGenVertexArrays_(1, &s.particleVao);
    glGenBuffers_(1, &s.particleVbo);
    glBindVertexArray_(s.particleVao);
    glBindBuffer_(GL_ARRAY_BUFFER, s.particleVbo);
    glEnableVertexAttribArray_(0);
    glVertexAttribPointer_(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray_(1);
    glVertexAttribPointer_(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

    glGenVertexArrays_(1, &s.lineVao);
    glGenBuffers_(1, &s.lineVbo);
    buildSceneLines(s.sim->params(), s.boxLines);
    glBindVertexArray_(s.lineVao);
    glBindBuffer_(GL_ARRAY_BUFFER, s.lineVbo);
    glBufferData_(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(s.boxLines.size() * sizeof(float)), s.boxLines.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray_(0);
    glVertexAttribPointer_(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));

    glGenVertexArrays_(1, &s.surfaceVao);
    glGenBuffers_(1, &s.surfaceVbo);
    glBindVertexArray_(s.surfaceVao);
    glBindBuffer_(GL_ARRAY_BUFFER, s.surfaceVbo);
    glEnableVertexAttribArray_(0);
    glVertexAttribPointer_(0, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray_(1);
    glVertexAttribPointer_(1, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex), reinterpret_cast<void*>(sizeof(Vec3)));

    RECT rc{};
    GetClientRect(s.hwnd, &rc);
    const int width = std::max(1L, rc.right - rc.left);
    const int height = std::max(1L, rc.bottom - rc.top);
    s.camera.aspect = static_cast<float>(width) / static_cast<float>(height);
    glViewport(0, 0, width, height);
    return true;
}

static void renderFrame(ViewerState& s) {
    if (!s.paused) {
        for (int i = 0; i < s.substeps; ++i) s.sim->step();
    }

    const auto& pos = s.sim->positions();
    const auto& vel = s.sim->velocities();
    const size_t n = pos.size();
    s.interleaved.resize(n * 4);
    s.foamInterleaved.clear();
    s.foamInterleaved.reserve(n / 8);
    const Vec3 boxMax = s.sim->params().boxMax;
    for (size_t i = 0; i < n; ++i) {
        const float speed = vel[i].length();
        s.interleaved[i * 4 + 0] = pos[i].x;
        s.interleaved[i * 4 + 1] = pos[i].y;
        s.interleaved[i * 4 + 2] = pos[i].z;
        s.interleaved[i * 4 + 3] = speed;

        // Cheap visual foam/spray: fast particles and particles close to the free surface
        // are rendered as small bright point sprites over the reconstructed mesh.
        const bool fast = speed > 5.0f;
        const bool high = pos[i].y > boxMax.y * 0.45f;
        if (fast && high) {
            s.foamInterleaved.push_back(pos[i].x);
            s.foamInterleaved.push_back(pos[i].y);
            s.foamInterleaved.push_back(pos[i].z);
            s.foamInterleaved.push_back(speed);
        }
    }

    glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    Mat4 vp = s.camera.viewProj();

    glUseProgram_(s.lineProgram);
    glUniformMatrix4fv_(glGetUniformLocation_(s.lineProgram, "uViewProj"), 1, GL_FALSE, vp.data());
    glUniform3f_(glGetUniformLocation_(s.lineProgram, "uColor"), 0.25f, 0.3f, 0.4f);
    glBindVertexArray_(s.lineVao);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(s.boxLines.size() / 3));

    if (s.surfaceMode) {
        if ((s.surfaceFrame++ % s.surfaceBuildStride) == 0 || s.surface.vertices.empty()) {
            s.surface.build(pos, s.sim->params());
            glBindBuffer_(GL_ARRAY_BUFFER, s.surfaceVbo);
            glBufferData_(GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(s.surface.vertices.size() * sizeof(SurfaceVertex)),
                s.surface.vertices.data(), GL_STREAM_DRAW);
        }
        glUseProgram_(s.surfaceProgram);
        glUniformMatrix4fv_(glGetUniformLocation_(s.surfaceProgram, "uViewProj"), 1, GL_FALSE, vp.data());
        const Vec3 eye = s.camera.eye();
        const Vec3 box = s.sim->params().boxMax;
        glUniform3f_(glGetUniformLocation_(s.surfaceProgram, "uCameraPos"), eye.x, eye.y, eye.z);
        glUniform3f_(glGetUniformLocation_(s.surfaceProgram, "uBoxMax"), box.x, box.y, box.z);
        glBindVertexArray_(s.surfaceVao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(s.surface.vertices.size()));

        if (s.showFoam && !s.foamInterleaved.empty()) {
            glBindBuffer_(GL_ARRAY_BUFFER, s.particleVbo);
            glBufferData_(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(s.foamInterleaved.size() * sizeof(float)), s.foamInterleaved.data(), GL_STREAM_DRAW);
            glUseProgram_(s.particleProgram);
            glUniformMatrix4fv_(glGetUniformLocation_(s.particleProgram, "uViewProj"), 1, GL_FALSE, vp.data());
            glUniform1f_(glGetUniformLocation_(s.particleProgram, "uPointScale"), 360.0f);
            glUniform1f_(glGetUniformLocation_(s.particleProgram, "uSpeedScale"), 1.0f / 10.0f);
            glBindVertexArray_(s.particleVao);
            glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(s.foamInterleaved.size() / 4));
        }
    }

    if (!s.surfaceMode || s.showParticles) {
        glBindBuffer_(GL_ARRAY_BUFFER, s.particleVbo);
        glBufferData_(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(s.interleaved.size() * sizeof(float)), s.interleaved.data(), GL_STREAM_DRAW);
        glUseProgram_(s.particleProgram);
        glUniformMatrix4fv_(glGetUniformLocation_(s.particleProgram, "uViewProj"), 1, GL_FALSE, vp.data());
        glUniform1f_(glGetUniformLocation_(s.particleProgram, "uPointScale"), 760.0f);
        glUniform1f_(glGetUniformLocation_(s.particleProgram, "uSpeedScale"), 1.0f / 28.0f);
        glBindVertexArray_(s.particleVao);
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(n));
    }

    renderHud(s);

    SwapBuffers(s.hdc);

    ++s.frames;
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - s.lastTitleUpdate).count();
    if (dt >= 0.5) {
        const double fps = static_cast<double>(s.frames) / dt;
        s.fpsDisplay = fps;
        s.frames = 0;
        s.lastTitleUpdate = now;

        char title[192];
        std::snprintf(title, sizeof(title),
            "SPH Fluid  |  %zu particles  |  %.0f fps  |  x%d  |  %s%s%s%s",
            n, fps, s.substeps, s.surfaceMode ? "surface" : "particles", s.showFoam ? " + foam" : "", s.obstacleEnabled ? " + obstacle" : "", s.paused ? "  [PAUSED]" : "");
        SetWindowTextA(s.hwnd, title);
    }
}

static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ViewerState* s = g_state;
    switch (msg) {
    case WM_SIZE:
        if (s) {
            int width = std::max(1, static_cast<int>(LOWORD(lParam)));
            int height = std::max(1, static_cast<int>(HIWORD(lParam)));
            s->camera.aspect = static_cast<float>(width) / static_cast<float>(height);
            if (s->glrc) glViewport(0, 0, width, height);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (s) {
            s->dragging = true;
            s->lastX = static_cast<double>(GET_X_LPARAM(lParam));
            s->lastY = static_cast<double>(GET_Y_LPARAM(lParam));
            SetCapture(hwnd);
        }
        return 0;

    case WM_LBUTTONUP:
        if (s) {
            s->dragging = false;
            ReleaseCapture();
        }
        return 0;

    case WM_MOUSEMOVE:
        if (s) {
            const double x = static_cast<double>(GET_X_LPARAM(lParam));
            const double y = static_cast<double>(GET_Y_LPARAM(lParam));
            if (s->dragging) s->camera.rotate(static_cast<float>(x - s->lastX), static_cast<float>(y - s->lastY));
            s->lastX = x;
            s->lastY = y;
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (s) {
            const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
            s->camera.zoom(delta);
        }
        return 0;

    case WM_KEYDOWN:
        if (s) {
            if (wParam == VK_SPACE) s->paused = !s->paused;
            else if (wParam == VK_UP) s->substeps = std::min(s->substeps + 1, 16);
            else if (wParam == VK_DOWN) s->substeps = std::max(s->substeps - 1, 1);
            else if (wParam == 'R') resetSimulation(*s);
            else if (wParam == 'M') s->surfaceMode = !s->surfaceMode;
            else if (wParam == 'P') s->showParticles = !s->showParticles;
            else if (wParam == 'F') s->showFoam = !s->showFoam;
            else if (wParam == 'O') { s->obstacleEnabled = !s->obstacleEnabled; resetSimulation(*s); }
            else if (wParam == 'H') s->showHud = !s->showHud;
            else if (wParam == VK_ESCAPE) DestroyWindow(hwnd);
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    SetProcessDPIAware();

    ViewerState state;
    g_state = &state;

    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "SphFluidWindow";

    if (!RegisterClassExA(&wc)) {
        MessageBoxA(nullptr, "RegisterClassEx failed", "SPH Fluid", MB_ICONERROR | MB_OK);
        return 1;
    }

    RECT rect{0, 0, 1280, 800};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    state.hwnd = CreateWindowExA(
        0,
        wc.lpszClassName,
        "SPH Fluid",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!state.hwnd) {
        MessageBoxA(nullptr, "CreateWindowEx failed", "SPH Fluid", MB_ICONERROR | MB_OK);
        return 1;
    }

    if (!createOpenGLContext(state)) {
        MessageBoxA(nullptr, "Could not create an OpenGL context", "SPH Fluid", MB_ICONERROR | MB_OK);
        return 1;
    }

    if (!initRenderer(state)) {
        MessageBoxA(nullptr, "Renderer initialization failed", "SPH Fluid", MB_ICONERROR | MB_OK);
        return 1;
    }

    ShowWindow(state.hwnd, nCmdShow);
    UpdateWindow(state.hwnd);

    MSG msg{};
    bool running = true;
    while (running) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (running) renderFrame(state);
    }

    if (state.fontBase) glDeleteLists(state.fontBase, 96);
    if (state.hudFont) DeleteObject(state.hudFont);
    if (state.glrc) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(state.glrc);
    }
    if (state.hwnd && state.hdc) ReleaseDC(state.hwnd, state.hdc);
    return 0;
}
