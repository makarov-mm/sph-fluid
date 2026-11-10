#include "scene.hpp"

#include <array>
#include <cmath>
#include <numbers>

std::unique_ptr<Sph> createDamBreak(bool withObstacle, ThreadPool& pool) {
    SphParams params{
        .h = 1.0f,
        .boxMin = {0.0f, 0.0f, 0.0f},
        .boxMax = {34.0f, 34.0f, 24.0f},
    };
    if (withObstacle) params.obstacle = SphereCollider{.center = {20.0f, 8.0f, 12.0f}, .radius = 4.8f};

    auto sim = std::make_unique<Sph>(params, pool);
    sim->emitBlock({1.5f, 1.5f, 1.5f}, {13.0f, 32.0f, 22.5f}, 0.6f);
    return sim;
}

std::vector<float> buildSceneWireframe(const SphParams& params) {
    std::vector<float> lines;
    const auto push = [&lines](const Vec3& p) { lines.insert(lines.end(), {p.x, p.y, p.z}); };

    const Vec3 a = params.boxMin;
    const Vec3 b = params.boxMax;
    const std::array<Vec3, 8> corners{{
        {a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, b.y, a.z}, {a.x, b.y, a.z},
        {a.x, a.y, b.z}, {b.x, a.y, b.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z},
    }};
    constexpr std::array<std::size_t, 24> edges{0, 1, 1, 2, 2, 3, 3, 0,  4, 5, 5, 6, 6, 7, 7, 4,
                                                0, 4, 1, 5, 2, 6, 3, 7};
    for (const std::size_t corner : edges) push(corners[corner]);

    if (params.obstacle) {
        const SphereCollider& sphere = *params.obstacle;
        constexpr int segments = 72;
        constexpr float step = 2.0f * std::numbers::pi_v<float> / segments;
        const auto ring = [&](auto pointOnCircle) {
            for (int i = 0; i < segments; ++i) {
                push(sphere.center + pointOnCircle(step * static_cast<float>(i)) * sphere.radius);
                push(sphere.center + pointOnCircle(step * static_cast<float>(i + 1)) * sphere.radius);
            }
        };
        ring([](float t) { return Vec3{std::cos(t), 0.0f, std::sin(t)}; });  // XZ plane
        ring([](float t) { return Vec3{std::cos(t), std::sin(t), 0.0f}; });  // XY plane
        ring([](float t) { return Vec3{0.0f, std::cos(t), std::sin(t)}; });  // YZ plane
    }
    return lines;
}
