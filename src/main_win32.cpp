// SPH Fluid: native Win32 / OpenGL viewer, no dependencies beyond the Windows SDK.
//
// Controls:
//   left-drag   orbit the camera          mouse wheel  zoom
//   Space       pause / resume            Up / Down    simulation speed (substeps per frame)
//   R           restart the dam break     M            surface / particles
//   P           particle overlay          F            foam / spray sprites
//   O           sphere obstacle (restart) H            HUD
//   Esc         quit

#include "win32.hpp"

#include "gl.hpp"
#include "renderer.hpp"
#include "scene.hpp"
#include "sph.hpp"
#include "surface.hpp"
#include "thread_pool.hpp"

#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

constexpr wchar_t kAppName[] = L"SPH Fluid";
constexpr wchar_t kWindowClass[] = L"SphFluidViewer";
constexpr int kInitialWidth = 1280;  // client area at 96 DPI
constexpr int kInitialHeight = 800;
constexpr int kMaxSubsteps = 16;
constexpr GLsizei kHudGlyphs = 96;  // ASCII 32..127

std::wstring widen(std::string_view text) {
    if (text.empty()) return {};
    const int size = static_cast<int>(text.size());
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), size, wide.data(), length);
    return wide;
}

void showError(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), kAppName, MB_ICONERROR | MB_OK);
}

class Viewer {
public:
    int run(HINSTANCE instance, int showCommand);

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    std::expected<void, std::string> initGraphics();
    void destroyGraphics();
    void createHudFont();
    void resetSimulation();
    void onKey(WPARAM key, bool repeated);
    void frame();
    void countFrame();
    float dpiScale() const { return static_cast<float>(dpi_) / USER_DEFAULT_SCREEN_DPI; }

    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    HGLRC glrc_ = nullptr;
    UINT dpi_ = USER_DEFAULT_SCREEN_DPI;

    ThreadPool pool_;
    std::unique_ptr<Sph> sim_;
    SurfaceExtractor surface_;
    std::optional<Renderer> renderer_;
    HudFont hudFont_;

    OrbitCamera camera_{.target = {17.0f, 12.0f, 12.0f}, .distance = 80.0f};
    ViewOptions view_;
    bool obstacle_ = false;
    bool paused_ = false;
    int substeps_ = 4;
    bool dragging_ = false;
    POINT lastMouse_{};

    // The surface is rebuilt only when the particles moved since the last build.
    std::uint64_t simVersion_ = 0;
    std::uint64_t surfaceVersion_ = UINT64_MAX;

    using Clock = std::chrono::steady_clock;
    Clock::time_point fpsStart_ = Clock::now();
    int fpsFrames_ = 0;
    double fps_ = 0.0;
};

int Viewer::run(HINSTANCE instance, int showCommand) {
    // Per-monitor DPI awareness: sharp output, and sizes follow the monitor's scale.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const WNDCLASSEXW windowClass{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_OWNDC,  // OpenGL wants a private device context
        .lpfnWndProc = windowProc,
        .hInstance = instance,
        .hIcon = LoadIconW(nullptr, IDI_APPLICATION),
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .lpszClassName = kWindowClass,
    };
    if (!RegisterClassExW(&windowClass)) {
        showError(L"Could not register the window class.");
        return 1;
    }

    hwnd_ = CreateWindowExW(0, kWindowClass, kAppName, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, instance, this);
    if (!hwnd_) {
        showError(L"Could not create the window.");
        return 1;
    }

    // Size the client area for the scale of the monitor the window opened on.
    dpi_ = GetDpiForWindow(hwnd_);
    const int dpi = static_cast<int>(dpi_);
    RECT frameRect{0, 0, MulDiv(kInitialWidth, dpi, USER_DEFAULT_SCREEN_DPI),
                   MulDiv(kInitialHeight, dpi, USER_DEFAULT_SCREEN_DPI)};
    AdjustWindowRectExForDpi(&frameRect, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi_);
    SetWindowPos(hwnd_, nullptr, 0, 0, frameRect.right - frameRect.left, frameRect.bottom - frameRect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (const auto ready = initGraphics(); !ready) {
        showError(widen(ready.error()));
        DestroyWindow(hwnd_);
        return 1;
    }
    resetSimulation();
    ShowWindow(hwnd_, showCommand);

    MSG message{};
    for (;;) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) return static_cast<int>(message.wParam);
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (IsIconic(hwnd_)) {
            WaitMessage();  // nothing to show while minimized: sleep instead of spinning
            continue;
        }
        frame();
    }
}

std::expected<void, std::string> Viewer::initGraphics() {
    hdc_ = GetDC(hwnd_);
    if (!hdc_) return std::unexpected("Could not get the window's device context.");

    const PIXELFORMATDESCRIPTOR format{
        .nSize = sizeof(PIXELFORMATDESCRIPTOR),
        .nVersion = 1,
        .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        .iPixelType = PFD_TYPE_RGBA,
        .cColorBits = 32,
        .cDepthBits = 24,
        .cStencilBits = 8,
        .iLayerType = PFD_MAIN_PLANE,
    };
    const int pixelFormat = ChoosePixelFormat(hdc_, &format);
    if (pixelFormat == 0 || !SetPixelFormat(hdc_, pixelFormat, &format))
        return std::unexpected("No suitable OpenGL pixel format.");

    // A legacy context is the driver's highest compatibility-profile version,
    // which the HUD's fixed-function bitmap text relies on.
    glrc_ = wglCreateContext(hdc_);
    if (!glrc_ || !wglMakeCurrent(hdc_, glrc_)) return std::unexpected("Could not create an OpenGL context.");

    if (auto version = gl::requireVersion(3, 3); !version) return version;
    if (auto loaded = gl::loadFunctions(); !loaded)
        return std::unexpected("The OpenGL driver does not provide " + loaded.error() + ".");

    using SwapIntervalProc = BOOL(WINAPI*)(int);
    if (const PROC swapInterval = wglGetProcAddress("wglSwapIntervalEXT"))
        reinterpret_cast<SwapIntervalProc>(reinterpret_cast<void (*)()>(swapInterval))(1);  // vsync

    auto renderer = Renderer::create();
    if (!renderer) return std::unexpected(renderer.error());
    renderer_.emplace(std::move(*renderer));

    RECT client{};
    GetClientRect(hwnd_, &client);
    renderer_->resize(client.right, client.bottom);
    createHudFont();
    return {};
}

void Viewer::destroyGraphics() {
    if (glrc_) {
        renderer_.reset();  // GL objects go while the context is still current
        if (hudFont_.listBase != 0) glDeleteLists(hudFont_.listBase, kHudGlyphs);
        hudFont_ = {};
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(glrc_);
        glrc_ = nullptr;
    }
    if (hdc_) {
        ReleaseDC(hwnd_, hdc_);
        hdc_ = nullptr;
    }
}

void Viewer::createHudFont() {
    if (hudFont_.listBase != 0) glDeleteLists(hudFont_.listBase, kHudGlyphs);
    hudFont_ = {};

    const int height = MulDiv(16, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
    const HFONT font = CreateFontW(-height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    if (!font) return;

    const HGDIOBJ previous = SelectObject(hdc_, font);
    SIZE glyph{};
    GetTextExtentPoint32W(hdc_, L"0", 1, &glyph);
    const GLuint base = glGenLists(kHudGlyphs);
    // Some drivers fail the first call after context creation; one retry is the usual remedy.
    const bool built = base != 0 && (wglUseFontBitmapsW(hdc_, 32, kHudGlyphs, base) ||
                                     wglUseFontBitmapsW(hdc_, 32, kHudGlyphs, base));
    SelectObject(hdc_, previous);
    DeleteObject(font);  // the display lists keep their own copy of the glyphs

    if (built)
        hudFont_ = {.listBase = base, .charWidth = static_cast<int>(glyph.cx)};
    else if (base != 0)
        glDeleteLists(base, kHudGlyphs);
}

void Viewer::resetSimulation() {
    sim_ = createDamBreak(obstacle_, pool_);
    renderer_->setSceneLines(buildSceneWireframe(sim_->params()));
    ++simVersion_;
}

void Viewer::onKey(WPARAM key, bool repeated) {
    switch (key) {
    case VK_UP: substeps_ = std::min(substeps_ + 1, kMaxSubsteps); return;
    case VK_DOWN: substeps_ = std::max(substeps_ - 1, 1); return;
    case VK_ESCAPE: DestroyWindow(hwnd_); return;
    default: break;
    }
    if (repeated) return;  // a held key must not keep flipping the toggles below
    switch (key) {
    case VK_SPACE: paused_ = !paused_; break;
    case 'R': resetSimulation(); break;
    case 'M': view_.surface = !view_.surface; break;
    case 'P': view_.particles = !view_.particles; break;
    case 'F': view_.foam = !view_.foam; break;
    case 'O':
        obstacle_ = !obstacle_;
        resetSimulation();
        break;
    case 'H': view_.hud = !view_.hud; break;
    default: break;
    }
}

void Viewer::frame() {
    if (!paused_) {
        for (int i = 0; i < substeps_; ++i) sim_->step();
        ++simVersion_;
    }
    if (view_.surface && surfaceVersion_ != simVersion_) {
        const SphParams& params = sim_->params();
        surface_.build(sim_->positions(), params.boxMin, params.boxMax, pool_);
        renderer_->uploadSurface(surface_);
        surfaceVersion_ = simVersion_;
    }

    renderer_->draw(camera_, *sim_, view_);
    if (view_.hud) {
        const HudStats stats{
            .fps = fps_,
            .particles = sim_->size(),
            .substeps = substeps_,
            .paused = paused_,
            .obstacle = obstacle_,
            .surfaceVertices = view_.surface ? surface_.vertexCount() : 0,
        };
        renderer_->drawHud(hudFont_, dpiScale(), stats, view_);
    }
    SwapBuffers(hdc_);
    countFrame();
}

void Viewer::countFrame() {
    ++fpsFrames_;
    const auto now = Clock::now();
    const double elapsed = std::chrono::duration<double>(now - fpsStart_).count();
    if (elapsed < 0.5) return;

    fps_ = fpsFrames_ / elapsed;
    fpsFrames_ = 0;
    fpsStart_ = now;
    const std::wstring title = std::format(L"{}  |  {} particles  |  {:.0f} fps  |  x{}  |  {}{}", kAppName, sim_->size(),
                                           fps_, substeps_, widen(describeMode(view_, obstacle_)),
                                           paused_ ? L"  [paused]" : L"");
    SetWindowTextW(hwnd_, title.c_str());
}

LRESULT CALLBACK Viewer::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* viewer = static_cast<Viewer*>(reinterpret_cast<const CREATESTRUCTW*>(lParam)->lpCreateParams);
        viewer->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(viewer));
    }
    if (auto* viewer = reinterpret_cast<Viewer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
        return viewer->handleMessage(message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT Viewer::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            const int width = std::max(1, static_cast<int>(LOWORD(lParam)));
            const int height = std::max(1, static_cast<int>(HIWORD(lParam)));
            camera_.aspect = static_cast<float>(width) / static_cast<float>(height);
            if (renderer_) renderer_->resize(width, height);
        }
        return 0;

    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam);
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        if (glrc_) createHudFont();
        return 0;
    }

    case WM_LBUTTONDOWN:
        dragging_ = true;
        lastMouse_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        SetCapture(hwnd_);
        return 0;

    case WM_LBUTTONUP:
        ReleaseCapture();  // WM_CAPTURECHANGED ends the drag
        return 0;

    case WM_CAPTURECHANGED:  // also when another window takes the mouse mid-drag
        dragging_ = false;
        return 0;

    case WM_MOUSEMOVE: {
        const POINT mouse{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (dragging_) {
            const float toLogical = 1.0f / dpiScale();
            camera_.rotate(static_cast<float>(mouse.x - lastMouse_.x) * toLogical,
                           static_cast<float>(mouse.y - lastMouse_.y) * toLogical);
        }
        lastMouse_ = mouse;
        return 0;
    }

    case WM_MOUSEWHEEL:
        camera_.zoom(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA);
        return 0;

    case WM_KEYDOWN:
        onKey(wParam, (lParam & (1 << 30)) != 0);  // bit 30: the key was already down (auto-repeat)
        return 0;

    case WM_DESTROY:
        destroyGraphics();
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    Viewer viewer;
    return viewer.run(instance, showCommand);
}
