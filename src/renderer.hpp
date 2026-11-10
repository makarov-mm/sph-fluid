#pragma once
#include "gl.hpp"
#include "mat4.hpp"

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

class Sph;
class SurfaceExtractor;

struct ViewOptions {
    bool surface = true;     // reconstructed surface; otherwise plain particles
    bool particles = false;  // particle overlay in surface mode
    bool foam = true;        // spray sprites in surface mode
    bool hud = true;
};

// "surface + foam + obstacle", for the HUD and the window title.
std::string describeMode(const ViewOptions& view, bool obstacle);

// Bitmap font as display lists for ASCII 32..127, created by the platform layer
// (wglUseFontBitmaps on Windows).
struct HudFont {
    GLuint listBase = 0;  // display list of the space character; 0 = no font
    int charWidth = 0;    // advance in pixels; the font is monospaced
};

struct HudStats {
    double fps = 0.0;
    std::size_t particles = 0;
    int substeps = 0;
    bool paused = false;
    bool obstacle = false;
    std::size_t surfaceVertices = 0;
};

// Draws the scene: sky, tank wireframe, reconstructed water surface, foam
// sprites, particle impostors and the HUD. Needs a current OpenGL 3.3
// compatibility context (the HUD uses fixed-function bitmap text).
class Renderer {
public:
    static std::expected<Renderer, std::string> create();

    void resize(int width, int height);
    void setSceneLines(std::span<const float> lines);
    void uploadSurface(const SurfaceExtractor& surface);
    void draw(const OrbitCamera& camera, const Sph& sim, const ViewOptions& view);
    // scale: 1.0 at 96 DPI.
    void drawHud(const HudFont& font, float scale, const HudStats& stats, const ViewOptions& view) const;

private:
    struct ParticleProgram {
        gl::Program program;
        GLint viewProj = -1, pointScale = -1, maxPointSize = -1, speedScale = -1;
    };
    struct LineProgram {
        gl::Program program;
        GLint viewProj = -1, color = -1;
    };
    struct SurfaceProgram {
        gl::Program program;
        GLint viewProj = -1, cameraPos = -1, boxMax = -1;
    };
    struct SkyProgram {
        gl::Program program;
        GLint forward = -1, right = -1, up = -1, tanHalfFov = -1, aspect = -1;
    };
    struct Mesh {
        gl::VertexArray vao;
        gl::Buffer vbo;
        GLsizei vertexCount = 0;
    };

    Renderer() = default;
    void createMeshes();
    void drawSky(const OrbitCamera& camera) const;
    void drawPoints(Mesh& mesh, const std::vector<float>& data, const Mat4& viewProj, float sizeAt800px,
                    float speedForWhite);

    ParticleProgram particleProgram_;
    LineProgram lineProgram_;
    SurfaceProgram surfaceProgram_;
    SkyProgram skyProgram_;
    gl::VertexArray emptyVao_;  // the sky triangle has no attributes
    Mesh lines_, surface_, particles_, foam_;
    std::vector<float> particleData_, foamData_;  // x, y, z, speed per point
    int width_ = 1, height_ = 1;
};
