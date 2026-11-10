#pragma once
#include "vec3.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

// Column-major 4x4 matrix (OpenGL convention).
struct Mat4 {
    std::array<float, 16> m{};

    static Mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
        const float f = 1.0f / std::tan(fovY * 0.5f);
        Mat4 r;
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (zFar + zNear) / (zNear - zFar);
        r.m[11] = -1.0f;
        r.m[14] = 2.0f * zFar * zNear / (zNear - zFar);
        return r;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        const Vec3 f = (center - eye).normalized();
        const Vec3 s = f.cross(up).normalized();
        const Vec3 u = s.cross(f);
        Mat4 r;
        r.m = {s.x,         u.x,         -f.x,       0.0f,
               s.y,         u.y,         -f.y,       0.0f,
               s.z,         u.z,         -f.z,       0.0f,
               -s.dot(eye), -u.dot(eye), f.dot(eye), 1.0f};
        return r;
    }

    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (std::size_t col = 0; col < 4; ++col)
            for (std::size_t row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (std::size_t k = 0; k < 4; ++k) sum += m[k * 4 + row] * b.m[col * 4 + k];
                r.m[col * 4 + row] = sum;
            }
        return r;
    }

    const float* data() const { return m.data(); }
};

// Camera orbiting a target point; angles in radians.
struct OrbitCamera {
    Vec3 target{};
    float distance = 60.0f;
    float yaw = 0.7f;
    float pitch = 0.5f;
    float fovY = 55.0f * std::numbers::pi_v<float> / 180.0f;
    float aspect = 1.6f;
    float zNear = 0.1f;
    float zFar = 5000.0f;

    Vec3 eye() const {
        const float cp = std::cos(pitch);
        return target + Vec3{cp * std::cos(yaw), std::sin(pitch), cp * std::sin(yaw)} * distance;
    }

    Mat4 viewProj() const {
        return Mat4::perspective(fovY, aspect, zNear, zFar) * Mat4::lookAt(eye(), target, {0.0f, 1.0f, 0.0f});
    }

    // dx, dy in pixels at 96 DPI.
    void rotate(float dx, float dy) {
        yaw += dx * 0.005f;
        pitch = std::clamp(pitch + dy * 0.005f, -1.55f, 1.55f);
    }

    // steps in mouse-wheel notches; positive zooms in.
    void zoom(float steps) {
        distance = std::clamp(distance * std::exp(-steps * 0.12f), 3.0f, 4000.0f);
    }
};
