#include "renderer.hpp"
#include "shaders.hpp"
#include "sph.hpp"
#include "surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <initializer_list>
#include <string_view>

namespace {

using Sources = std::initializer_list<std::string_view>;

template <class GetParam, class GetLog>
std::string infoLog(GLuint object, GetParam getParam, GetLog getLog) {
    GLint length = 0;
    getParam(object, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    getLog(object, static_cast<GLsizei>(log.size()), nullptr, log.data());
    log.resize(std::strlen(log.c_str()));
    return log;
}

std::expected<gl::Shader, std::string> compileShader(GLenum type, Sources sources) {
    std::vector<const GLchar*> strings;
    std::vector<GLint> lengths;
    for (const std::string_view source : sources) {
        strings.push_back(source.data());
        lengths.push_back(static_cast<GLint>(source.size()));
    }
    gl::Shader shader{gl::CreateShader(type)};
    gl::ShaderSource(shader.get(), static_cast<GLsizei>(strings.size()), strings.data(), lengths.data());
    gl::CompileShader(shader.get());

    GLint ok = GL_FALSE;
    gl::GetShaderiv(shader.get(), GL_COMPILE_STATUS, &ok);
    if (!ok) return std::unexpected(infoLog(shader.get(), gl::GetShaderiv, gl::GetShaderInfoLog));
    return shader;
}

std::expected<gl::Program, std::string> linkProgram(std::string_view name, Sources vertex, Sources fragment) {
    auto vs = compileShader(GL_VERTEX_SHADER, vertex);
    if (!vs) return std::unexpected(std::format("{} vertex shader:\n{}", name, vs.error()));
    auto fs = compileShader(GL_FRAGMENT_SHADER, fragment);
    if (!fs) return std::unexpected(std::format("{} fragment shader:\n{}", name, fs.error()));

    gl::Program program{gl::CreateProgram()};
    gl::AttachShader(program.get(), vs->get());
    gl::AttachShader(program.get(), fs->get());
    gl::LinkProgram(program.get());

    GLint ok = GL_FALSE;
    gl::GetProgramiv(program.get(), GL_LINK_STATUS, &ok);
    if (!ok)
        return std::unexpected(std::format("{} program:\n{}", name,
                                           infoLog(program.get(), gl::GetProgramiv, gl::GetProgramInfoLog)));
    return program;  // the shaders are released now; the linked program keeps the code
}

GLint uniform(const gl::Program& program, const char* name) {
    return gl::GetUniformLocation(program.get(), name);
}

struct Attribute {
    GLuint location;
    GLint components;
    std::size_t offset;
};

void setUpMesh(gl::VertexArray& vao, gl::Buffer& vbo, GLsizei stride, std::initializer_list<Attribute> attributes) {
    vao = gl::createVertexArray();
    vbo = gl::createBuffer();
    gl::BindVertexArray(vao.get());
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo.get());
    for (const Attribute& a : attributes) {
        gl::EnableVertexAttribArray(a.location);
        gl::VertexAttribPointer(a.location, a.components, GL_FLOAT, GL_FALSE, stride,
                                reinterpret_cast<const void*>(a.offset));
    }
    gl::BindVertexArray(0);
}

}  // namespace

std::string describeMode(const ViewOptions& view, bool obstacle) {
    std::string mode = view.surface ? "surface" : "particles";
    if (view.surface && view.particles) mode += " + particles";
    if (view.surface && view.foam) mode += " + foam";
    if (obstacle) mode += " + obstacle";
    return mode;
}

std::expected<Renderer, std::string> Renderer::create() {
    using namespace shaders;
    auto particle = linkProgram("particle", {kVersion, kParticleVs}, {kVersion, kParticleFs});
    if (!particle) return std::unexpected(particle.error());
    auto line = linkProgram("line", {kVersion, kLineVs}, {kVersion, kLineFs});
    if (!line) return std::unexpected(line.error());
    auto surface = linkProgram("surface", {kVersion, kSurfaceVs}, {kVersion, kSkyCommon, kSurfaceFs});
    if (!surface) return std::unexpected(surface.error());
    auto sky = linkProgram("sky", {kVersion, kSkyVs}, {kVersion, kSkyCommon, kSkyFs});
    if (!sky) return std::unexpected(sky.error());

    Renderer r;
    r.particleProgram_.program = std::move(*particle);
    r.particleProgram_.viewProj = uniform(r.particleProgram_.program, "uViewProj");
    r.particleProgram_.pointScale = uniform(r.particleProgram_.program, "uPointScale");
    r.particleProgram_.maxPointSize = uniform(r.particleProgram_.program, "uMaxPointSize");
    r.particleProgram_.speedScale = uniform(r.particleProgram_.program, "uSpeedScale");

    r.lineProgram_.program = std::move(*line);
    r.lineProgram_.viewProj = uniform(r.lineProgram_.program, "uViewProj");
    r.lineProgram_.color = uniform(r.lineProgram_.program, "uColor");

    r.surfaceProgram_.program = std::move(*surface);
    r.surfaceProgram_.viewProj = uniform(r.surfaceProgram_.program, "uViewProj");
    r.surfaceProgram_.cameraPos = uniform(r.surfaceProgram_.program, "uCameraPos");
    r.surfaceProgram_.boxMax = uniform(r.surfaceProgram_.program, "uBoxMax");

    r.skyProgram_.program = std::move(*sky);
    r.skyProgram_.forward = uniform(r.skyProgram_.program, "uForward");
    r.skyProgram_.right = uniform(r.skyProgram_.program, "uRight");
    r.skyProgram_.up = uniform(r.skyProgram_.program, "uUp");
    r.skyProgram_.tanHalfFov = uniform(r.skyProgram_.program, "uTanHalfFov");
    r.skyProgram_.aspect = uniform(r.skyProgram_.program, "uAspect");

    r.createMeshes();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_POINT_SPRITE);  // compatibility profile: required for gl_PointCoord
    return r;
}

void Renderer::createMeshes() {
    emptyVao_ = gl::createVertexArray();
    constexpr auto vec3Stride = static_cast<GLsizei>(3 * sizeof(float));
    constexpr auto pointStride = static_cast<GLsizei>(4 * sizeof(float));  // x, y, z, speed
    constexpr auto surfaceStride = static_cast<GLsizei>(sizeof(SurfaceVertex));
    setUpMesh(lines_.vao, lines_.vbo, vec3Stride, {{0, 3, 0}});
    setUpMesh(surface_.vao, surface_.vbo, surfaceStride,
              {{0, 3, offsetof(SurfaceVertex, position)}, {1, 3, offsetof(SurfaceVertex, normal)}});
    setUpMesh(particles_.vao, particles_.vbo, pointStride, {{0, 3, 0}, {1, 1, 3 * sizeof(float)}});
    setUpMesh(foam_.vao, foam_.vbo, pointStride, {{0, 3, 0}, {1, 1, 3 * sizeof(float)}});
}

void Renderer::resize(int width, int height) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    glViewport(0, 0, width_, height_);
}

void Renderer::setSceneLines(std::span<const float> lines) {
    gl::BindBuffer(GL_ARRAY_BUFFER, lines_.vbo.get());
    gl::BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size_bytes()), lines.data(), GL_STATIC_DRAW);
    lines_.vertexCount = static_cast<GLsizei>(lines.size() / 3);
}

void Renderer::uploadSurface(const SurfaceExtractor& surface) {
    // Orphan the buffer, then copy the extractor's per-task chunks back to back.
    gl::BindBuffer(GL_ARRAY_BUFFER, surface_.vbo.get());
    gl::BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(surface.vertexCount() * sizeof(SurfaceVertex)), nullptr,
                   GL_STREAM_DRAW);
    GLintptr offset = 0;
    for (const std::vector<SurfaceVertex>& chunk : surface.chunks()) {
        const auto bytes = static_cast<GLsizeiptr>(chunk.size() * sizeof(SurfaceVertex));
        if (bytes == 0) continue;
        gl::BufferSubData(GL_ARRAY_BUFFER, offset, bytes, chunk.data());
        offset += bytes;
    }
    surface_.vertexCount = static_cast<GLsizei>(surface.vertexCount());
}

void Renderer::draw(const OrbitCamera& camera, const Sph& sim, const ViewOptions& view) {
    glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawSky(camera);

    const Mat4 viewProj = camera.viewProj();

    gl::UseProgram(lineProgram_.program.get());
    gl::UniformMatrix4fv(lineProgram_.viewProj, 1, GL_FALSE, viewProj.data());
    gl::Uniform3f(lineProgram_.color, 0.25f, 0.3f, 0.4f);
    gl::BindVertexArray(lines_.vao.get());
    glDrawArrays(GL_LINES, 0, lines_.vertexCount);

    const auto positions = sim.positions();
    const auto velocities = sim.velocities();

    if (view.surface) {
        const Vec3 eye = camera.eye();
        const Vec3& boxMax = sim.params().boxMax;
        gl::UseProgram(surfaceProgram_.program.get());
        gl::UniformMatrix4fv(surfaceProgram_.viewProj, 1, GL_FALSE, viewProj.data());
        gl::Uniform3f(surfaceProgram_.cameraPos, eye.x, eye.y, eye.z);
        gl::Uniform3f(surfaceProgram_.boxMax, boxMax.x, boxMax.y, boxMax.z);
        gl::BindVertexArray(surface_.vao.get());
        // The water's faces lie on the walls; nudging them back in depth keeps the
        // tank's edges visible on top of them.
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 1.0f);
        glDrawArrays(GL_TRIANGLES, 0, surface_.vertexCount);
        glDisable(GL_POLYGON_OFFSET_FILL);

        if (view.foam) {
            // Spray is made of isolated droplets, which read as low local density.
            // Selecting on density as well as speed keeps coherent sheets of water
            // part of the surface instead of turning them into sprites.
            const auto densities = sim.densities();
            const float foamDensity = 0.55f * sim.params().restDensity;
            foamData_.clear();
            for (std::size_t i = 0; i < positions.size(); ++i) {
                if (densities[i] >= foamDensity) continue;
                const float speed = velocities[i].length();
                if (speed > 3.0f) foamData_.insert(foamData_.end(), {positions[i].x, positions[i].y, positions[i].z, speed});
            }
            drawPoints(foam_, foamData_, viewProj, 360.0f, 10.0f);
        }
    }

    if (!view.surface || view.particles) {
        particleData_.clear();
        for (std::size_t i = 0; i < positions.size(); ++i)
            particleData_.insert(particleData_.end(),
                                 {positions[i].x, positions[i].y, positions[i].z, velocities[i].length()});
        drawPoints(particles_, particleData_, viewProj, 760.0f, 28.0f);
    }
}

void Renderer::drawSky(const OrbitCamera& camera) const {
    const Vec3 forward = (camera.target - camera.eye()).normalized();
    const Vec3 right = forward.cross({0.0f, 1.0f, 0.0f}).normalized();
    const Vec3 up = right.cross(forward);

    // Drawn first and without depth, so everything else lands on top of it.
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    gl::UseProgram(skyProgram_.program.get());
    gl::Uniform3f(skyProgram_.forward, forward.x, forward.y, forward.z);
    gl::Uniform3f(skyProgram_.right, right.x, right.y, right.z);
    gl::Uniform3f(skyProgram_.up, up.x, up.y, up.z);
    gl::Uniform1f(skyProgram_.tanHalfFov, std::tan(camera.fovY * 0.5f));
    gl::Uniform1f(skyProgram_.aspect, camera.aspect);
    gl::BindVertexArray(emptyVao_.get());
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawPoints(Mesh& mesh, const std::vector<float>& data, const Mat4& viewProj, float sizeAt800px,
                          float speedForWhite) {
    if (data.empty()) return;
    // Sprite sizes are tuned for an 800-pixel-high view and scale with the viewport,
    // so particles keep their size relative to the scene at any resolution or DPI.
    const float scale = static_cast<float>(height_) / 800.0f;

    gl::BindBuffer(GL_ARRAY_BUFFER, mesh.vbo.get());
    gl::BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), GL_STREAM_DRAW);
    gl::UseProgram(particleProgram_.program.get());
    gl::UniformMatrix4fv(particleProgram_.viewProj, 1, GL_FALSE, viewProj.data());
    gl::Uniform1f(particleProgram_.pointScale, sizeAt800px * scale);
    gl::Uniform1f(particleProgram_.maxPointSize, 64.0f * scale);
    gl::Uniform1f(particleProgram_.speedScale, 1.0f / speedForWhite);
    gl::BindVertexArray(mesh.vao.get());
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(data.size() / 4));
}

void Renderer::drawHud(const HudFont& font, float scale, const HudStats& stats, const ViewOptions& view) const {
    if (font.listBase == 0) return;

    using Color = std::array<float, 3>;
    constexpr Color titleColor{0.86f, 0.93f, 1.0f};
    constexpr Color infoColor{0.72f, 0.86f, 1.0f};
    constexpr Color helpColor{0.86f, 0.88f, 0.90f};
    struct Line {
        int advance;  // baseline distance from the previous line (from the top for the first), 96-DPI pixels
        int x;
        Color color;
        std::string text;
    };
    const std::array lines{
        Line{38, 28, titleColor, "SPH Fluid / Surface Reconstruction"},
        Line{26, 28, infoColor,
             std::format("FPS: {:.0f}    Particles: {}    Substeps: x{}", stats.fps, stats.particles, stats.substeps)},
        Line{20, 28, infoColor, "Mode: " + describeMode(view, stats.obstacle)},
        Line{20, 28, infoColor,
             std::format("State: {}    Surface verts: {}", stats.paused ? "paused" : "running", stats.surfaceVertices)},
        Line{30, 28, helpColor, "Controls:"},
        Line{20, 42, helpColor, "Mouse drag / wheel  orbit / zoom"},
        Line{18, 42, helpColor, "Space  pause    R  reset"},
        Line{18, 42, helpColor, "Up/Down  simulation speed"},
        Line{18, 42, helpColor, "M  surface/particles    P  particle overlay"},
        Line{18, 42, helpColor, "F  foam    O  obstacle    H  hide HUD"},
    };

    const auto px = [scale](int value) { return static_cast<int>(std::lround(static_cast<float>(value) * scale)); };
    int bottom = 0, right = 0;
    for (const Line& line : lines) {
        bottom += px(line.advance);
        right = std::max(right, px(line.x) + static_cast<int>(line.text.size()) * font.charWidth);
    }
    const auto left = static_cast<float>(px(14));
    const auto top = static_cast<float>(px(14));
    const auto panelRight = static_cast<float>(right + px(14));
    const auto panelBottom = static_cast<float>(bottom + px(6));

    // Fixed-function overlay: no program, pixel coordinates with y pointing down.
    gl::UseProgram(0);
    gl::BindVertexArray(0);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, width_, height_, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const auto panel = [&](GLenum mode) {
        glBegin(mode);
        glVertex2f(left, top);
        glVertex2f(panelRight, top);
        glVertex2f(panelRight, panelBottom);
        glVertex2f(left, panelBottom);
        glEnd();
    };
    glColor4f(0.02f, 0.025f, 0.035f, 0.72f);
    panel(GL_QUADS);
    glColor4f(0.55f, 0.68f, 0.82f, 0.45f);
    panel(GL_LINE_LOOP);

    glListBase(font.listBase - 32);
    int baseline = 0;
    for (const Line& line : lines) {
        baseline += px(line.advance);
        glColor3f(line.color[0], line.color[1], line.color[2]);  // must precede glRasterPos
        glRasterPos2i(px(line.x), baseline);
        glCallLists(static_cast<GLsizei>(line.text.size()), GL_UNSIGNED_BYTE, line.text.data());
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}
