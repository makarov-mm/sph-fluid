#pragma once
// GLSL 3.30 sources. A program stage is compiled from several pieces, which lets
// the sky and the water surface share one sky model and tone curve.
#include <string_view>

namespace shaders {

inline constexpr std::string_view kVersion = "#version 330 core\n";

// Procedural sky (gradient, sun disk and glow) plus Reinhard tone mapping and gamma.
inline constexpr std::string_view kSkyCommon = R"(
const vec3 SUN_DIR = normalize(vec3(-0.35, 0.85, 0.45));

vec3 skyColor(vec3 d) {
    float t = clamp(d.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = vec3(0.55, 0.65, 0.78);
    vec3 zenith  = vec3(0.10, 0.22, 0.45);
    vec3 ground  = vec3(0.05, 0.06, 0.08);
    vec3 sky = mix(horizon, zenith, smoothstep(0.5, 1.0, t));
    sky = mix(ground, sky, smoothstep(0.42, 0.5, t));
    float sd = max(dot(d, SUN_DIR), 0.0);
    sky += vec3(1.0, 0.90, 0.75) * pow(sd, 350.0) * 3.0;   // sun disk
    sky += vec3(1.0, 0.85, 0.70) * pow(sd, 8.0) * 0.15;    // sun glow
    return sky;
}

vec3 toDisplay(vec3 c) {
    c = c / (c + vec3(1.0));          // Reinhard
    return pow(c, vec3(1.0 / 2.2));   // gamma
}
)";

// Particles as lit sphere impostors; colour runs from deep blue to foam white with speed.
inline constexpr std::string_view kParticleVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aSpeed;
uniform mat4 uViewProj;
uniform float uPointScale;    // sprite size in pixels at unit clip-space distance
uniform float uMaxPointSize;
uniform float uSpeedScale;
out float vSpeedT;

void main() {
    vec4 clip = uViewProj * vec4(aPos, 1.0);
    gl_Position = clip;
    gl_PointSize = clamp(uPointScale / max(clip.w, 0.001), 2.0, uMaxPointSize);
    vSpeedT = clamp(aSpeed * uSpeedScale, 0.0, 1.0);
}
)";

inline constexpr std::string_view kParticleFs = R"(
in float vSpeedT;
out vec4 fragColor;

void main() {
    vec2 c = gl_PointCoord * 2.0 - 1.0;
    float d2 = dot(c, c);
    if (d2 > 1.0) discard;
    vec3 n = vec3(c.x, -c.y, sqrt(1.0 - d2));
    vec3 L = normalize(vec3(0.3, 0.85, 0.5));
    float diffuse = max(dot(n, L), 0.0);
    vec3 H = normalize(L + vec3(0.0, 0.0, 1.0));
    float specular = pow(max(dot(n, H), 0.0), 48.0);
    vec3 base = mix(vec3(0.05, 0.25, 0.55), vec3(0.80, 0.92, 1.0), vSpeedT);
    fragColor = vec4(base * (0.3 + 0.7 * diffuse) + vec3(specular) * 0.6, 1.0);
}
)";

inline constexpr std::string_view kLineVs = R"(
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;

void main() { gl_Position = uViewProj * vec4(aPos, 1.0); }
)";

inline constexpr std::string_view kLineFs = R"(
uniform vec3 uColor;
out vec4 fragColor;

void main() { fragColor = vec4(uColor, 1.0); }
)";

inline constexpr std::string_view kSurfaceVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uViewProj;
out vec3 vNormal;
out vec3 vWorld;

void main() {
    vWorld = aPos;
    vNormal = aNormal;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

// Water: Fresnel blend of sky reflection over a depth-tinted body, sun glint.
inline constexpr std::string_view kSurfaceFs = R"(
in vec3 vNormal;
in vec3 vWorld;
uniform vec3 uCameraPos;
uniform vec3 uBoxMax;
out vec4 fragColor;

void main() {
    vec3 n = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorld);
    if (dot(n, V) < 0.0) n = -n;                         // two-sided: face the camera

    vec3 reflection = skyColor(reflect(-V, n)) * 0.7;    // tamed so the sky does not wash out
    float fresnel = 0.04 + 0.60 * pow(1.0 - clamp(dot(n, V), 0.0, 1.0), 5.0);  // capped: the body always shows

    float h = clamp(vWorld.y / max(uBoxMax.y, 0.001), 0.0, 1.0);
    vec3 body = mix(vec3(0.01, 0.10, 0.22), vec3(0.05, 0.34, 0.52), h);  // deeper is darker

    vec3 col = mix(body, reflection, fresnel);
    float glint = pow(max(dot(n, normalize(SUN_DIR + V)), 0.0), 200.0);
    col += vec3(1.0, 0.95, 0.85) * glint * 1.2;                         // sharp sun glint
    col += vec3(0.0, 0.25, 0.30) * max(dot(n, SUN_DIR), 0.0) * 0.12;    // subsurface tint
    fragColor = vec4(toDisplay(col), 1.0);
}
)";

// Full-screen triangle generated from gl_VertexID; needs only an empty VAO.
inline constexpr std::string_view kSkyVs = R"(
out vec2 vNdc;

void main() {
    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    vNdc = p;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

inline constexpr std::string_view kSkyFs = R"(
in vec2 vNdc;
uniform vec3 uForward;
uniform vec3 uRight;
uniform vec3 uUp;
uniform float uTanHalfFov;
uniform float uAspect;
out vec4 fragColor;

void main() {
    vec3 dir = normalize(uForward + vNdc.x * uTanHalfFov * uAspect * uRight + vNdc.y * uTanHalfFov * uUp);
    fragColor = vec4(toDisplay(skyColor(dir)), 1.0);
}
)";

}  // namespace shaders
