// SPH fluid — OpenGL viewer (GLFW + GLAD). Renders particles as lit sphere
// imposters inside a wireframe box.
//
// Controls:
//   left-drag   orbit        wheel       zoom
//   Space       pause        R           reset
//   Up/Down     sim speed    Esc         quit

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "mat4.hpp"
#include "sph.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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
    vec3 n = vec3(c.x, -c.y, sqrt(1.0 - d2));     // sphere normal
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

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "shader error: %s\n", log);
    }
    return s;
}
static GLuint link(const char* vs, const char* fs) {
    GLuint p = glCreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

static std::unique_ptr<Sph> makeSim() {
    SphParams p;
    p.h = 1.0f;
    p.boxMin = {0, 0, 0};
    p.boxMax = {34, 34, 24};
    auto s = std::make_unique<Sph>(p);
    s->emitBlock({1.5f, 1.5f, 1.5f}, {13, 32, 22.5f}, 0.6f); // dam break on one side
    s->setThreads((int)std::thread::hardware_concurrency());
    return s;
}

struct App {
    OrbitCamera cam;
    bool dragging = false, paused = false;
    double lastX = 0, lastY = 0;
    int substeps = 4;
};

static void buildBoxLines(const Vec3& a, const Vec3& b, std::vector<float>& out) {
    Vec3 c[8] = {{a.x,a.y,a.z},{b.x,a.y,a.z},{b.x,b.y,a.z},{a.x,b.y,a.z},
                 {a.x,a.y,b.z},{b.x,a.y,b.z},{b.x,b.y,b.z},{a.x,b.y,b.z}};
    int e[24] = {0,1,1,2,2,3,3,0, 4,5,5,6,6,7,7,4, 0,4,1,5,2,6,3,7};
    out.clear();
    for (int i : e) { out.push_back(c[i].x); out.push_back(c[i].y); out.push_back(c[i].z); }
}

int main() {
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* win = glfwCreateWindow(1280, 800, "SPH Fluid", nullptr, nullptr);
    if (!win) { std::fprintf(stderr, "window creation failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress)) { std::fprintf(stderr, "glad load failed\n"); return 1; }
    glfwSwapInterval(1);

    App app;
    app.cam.target = {17, 12, 12};
    app.cam.distance = 80;
    glfwSetWindowUserPointer(win, &app);

    glfwSetMouseButtonCallback(win, [](GLFWwindow* w, int b, int action, int) {
        auto* a = static_cast<App*>(glfwGetWindowUserPointer(w));
        if (b == GLFW_MOUSE_BUTTON_LEFT) a->dragging = (action == GLFW_PRESS);
    });
    glfwSetCursorPosCallback(win, [](GLFWwindow* w, double x, double y) {
        auto* a = static_cast<App*>(glfwGetWindowUserPointer(w));
        if (a->dragging) a->cam.rotate(float(x - a->lastX), float(y - a->lastY));
        a->lastX = x; a->lastY = y;
    });
    glfwSetScrollCallback(win, [](GLFWwindow* w, double, double dy) {
        static_cast<App*>(glfwGetWindowUserPointer(w))->cam.zoom(float(dy));
    });
    glfwSetFramebufferSizeCallback(win, [](GLFWwindow* w, int fw, int fh) {
        glViewport(0, 0, fw, fh);
        static_cast<App*>(glfwGetWindowUserPointer(w))->cam.aspect =
            float(fw) / float(fh > 0 ? fh : 1);
    });

    auto sim = makeSim();

    glfwSetKeyCallback(win, [](GLFWwindow* w, int key, int, int action, int) {
        if (action != GLFW_PRESS) return;
        auto* a = static_cast<App*>(glfwGetWindowUserPointer(w));
        if (key == GLFW_KEY_SPACE) a->paused = !a->paused;
        else if (key == GLFW_KEY_UP) a->substeps = std::min(a->substeps + 1, 16);
        else if (key == GLFW_KEY_DOWN) a->substeps = std::max(a->substeps - 1, 1);
        else if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, 1);
    });

    int fw, fh; glfwGetFramebufferSize(win, &fw, &fh);
    glViewport(0, 0, fw, fh);
    app.cam.aspect = float(fw) / float(fh > 0 ? fh : 1);

    GLuint particleProg = link(PARTICLE_VS, PARTICLE_FS);
    GLuint lineProg = link(LINE_VS, LINE_FS);

    GLuint pVao, pVbo, lVao, lVbo;
    glGenVertexArrays(1, &pVao); glGenBuffers(1, &pVbo);
    glBindVertexArray(pVao);
    glBindBuffer(GL_ARRAY_BUFFER, pVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 16, (void*)12);

    std::vector<float> boxLines;
    buildBoxLines(sim->params().boxMin, sim->params().boxMax, boxLines);
    glGenVertexArrays(1, &lVao); glGenBuffers(1, &lVbo);
    glBindVertexArray(lVao);
    glBindBuffer(GL_ARRAY_BUFFER, lVbo);
    glBufferData(GL_ARRAY_BUFFER, boxLines.size() * 4, boxLines.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_DEPTH_TEST);

    std::vector<float> interleaved;
    double t0 = glfwGetTime();
    int frames = 0;

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        if (glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS) sim = makeSim();

        if (!app.paused)
            for (int s = 0; s < app.substeps; ++s) sim->step();

        const auto& pos = sim->positions();
        const auto& vel = sim->velocities();
        size_t n = pos.size();
        interleaved.resize(n * 4);
        for (size_t i = 0; i < n; ++i) {
            interleaved[i*4+0] = pos[i].x;
            interleaved[i*4+1] = pos[i].y;
            interleaved[i*4+2] = pos[i].z;
            interleaved[i*4+3] = vel[i].length();
        }

        glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        Mat4 vp = app.cam.viewProj();

        glUseProgram(lineProg);
        glUniformMatrix4fv(glGetUniformLocation(lineProg, "uViewProj"), 1, GL_FALSE, vp.data());
        glUniform3f(glGetUniformLocation(lineProg, "uColor"), 0.25f, 0.3f, 0.4f);
        glBindVertexArray(lVao);
        glDrawArrays(GL_LINES, 0, (GLsizei)(boxLines.size() / 3));

        glBindBuffer(GL_ARRAY_BUFFER, pVbo);
        glBufferData(GL_ARRAY_BUFFER, interleaved.size() * 4, interleaved.data(), GL_STREAM_DRAW);
        glUseProgram(particleProg);
        glUniformMatrix4fv(glGetUniformLocation(particleProg, "uViewProj"), 1, GL_FALSE, vp.data());
        glUniform1f(glGetUniformLocation(particleProg, "uPointScale"), 760.0f);
        glUniform1f(glGetUniformLocation(particleProg, "uSpeedScale"), 1.0f / 28.0f);
        glBindVertexArray(pVao);
        glDrawArrays(GL_POINTS, 0, (GLsizei)n);

        glfwSwapBuffers(win);

        if (++frames >= 30) {
            double t = glfwGetTime();
            double fps = frames / (t - t0);
            t0 = t; frames = 0;
            char title[160];
            std::snprintf(title, sizeof(title),
                "SPH Fluid  |  %zu particles  |  %.0f fps  |  x%d%s",
                n, fps, app.substeps, app.paused ? "  [PAUSED]" : "");
            glfwSetWindowTitle(win, title);
        }
    }

    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
