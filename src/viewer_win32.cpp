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

static std::unique_ptr<Sph> makeSim() {
    SphParams p;
    p.h = 1.0f;
    p.boxMin = {0, 0, 0};
    p.boxMax = {34, 34, 24};
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
    GLuint particleVao = 0;
    GLuint particleVbo = 0;
    GLuint lineVao = 0;
    GLuint lineVbo = 0;

    std::unique_ptr<Sph> sim;
    std::vector<float> boxLines;
    std::vector<float> interleaved;

    int frames = 0;
    std::chrono::steady_clock::time_point lastTitleUpdate = std::chrono::steady_clock::now();
};

static ViewerState* g_state = nullptr;

static void resetSimulation(ViewerState& s) {
    s.sim = makeSim();
    buildBoxLines(s.sim->params().boxMin, s.sim->params().boxMax, s.boxLines);
    glBindVertexArray_(s.lineVao);
    glBindBuffer_(GL_ARRAY_BUFFER, s.lineVbo);
    glBufferData_(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(s.boxLines.size() * sizeof(float)), s.boxLines.data(), GL_STATIC_DRAW);
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

static bool initRenderer(ViewerState& s) {
    s.camera.target = {17, 12, 12};
    s.camera.distance = 80.0f;
    s.sim = makeSim();

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_POINT_SPRITE); // compat-profile: required for gl_PointCoord
    glEnable(GL_DEPTH_TEST);

    s.particleProgram = linkProgram(PARTICLE_VS, PARTICLE_FS);
    s.lineProgram = linkProgram(LINE_VS, LINE_FS);

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
    buildBoxLines(s.sim->params().boxMin, s.sim->params().boxMax, s.boxLines);
    glBindVertexArray_(s.lineVao);
    glBindBuffer_(GL_ARRAY_BUFFER, s.lineVbo);
    glBufferData_(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(s.boxLines.size() * sizeof(float)), s.boxLines.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray_(0);
    glVertexAttribPointer_(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));

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
    for (size_t i = 0; i < n; ++i) {
        s.interleaved[i * 4 + 0] = pos[i].x;
        s.interleaved[i * 4 + 1] = pos[i].y;
        s.interleaved[i * 4 + 2] = pos[i].z;
        s.interleaved[i * 4 + 3] = vel[i].length();
    }

    glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    Mat4 vp = s.camera.viewProj();

    glUseProgram_(s.lineProgram);
    glUniformMatrix4fv_(glGetUniformLocation_(s.lineProgram, "uViewProj"), 1, GL_FALSE, vp.data());
    glUniform3f_(glGetUniformLocation_(s.lineProgram, "uColor"), 0.25f, 0.3f, 0.4f);
    glBindVertexArray_(s.lineVao);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(s.boxLines.size() / 3));

    glBindBuffer_(GL_ARRAY_BUFFER, s.particleVbo);
    glBufferData_(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(s.interleaved.size() * sizeof(float)), s.interleaved.data(), GL_STREAM_DRAW);
    glUseProgram_(s.particleProgram);
    glUniformMatrix4fv_(glGetUniformLocation_(s.particleProgram, "uViewProj"), 1, GL_FALSE, vp.data());
    glUniform1f_(glGetUniformLocation_(s.particleProgram, "uPointScale"), 760.0f);
    glUniform1f_(glGetUniformLocation_(s.particleProgram, "uSpeedScale"), 1.0f / 28.0f);
    glBindVertexArray_(s.particleVao);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(n));

    SwapBuffers(s.hdc);

    ++s.frames;
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - s.lastTitleUpdate).count();
    if (dt >= 0.5) {
        const double fps = static_cast<double>(s.frames) / dt;
        s.frames = 0;
        s.lastTitleUpdate = now;

        char title[192];
        std::snprintf(title, sizeof(title),
            "SPH Fluid  |  %zu particles  |  %.0f fps  |  x%d%s",
            n, fps, s.substeps, s.paused ? "  [PAUSED]" : "");
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

    if (state.glrc) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(state.glrc);
    }
    if (state.hwnd && state.hdc) ReleaseDC(state.hwnd, state.hdc);
    return 0;
}
