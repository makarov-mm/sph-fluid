#pragma once
#include "vec3.hpp"
#include <array>
#include <cmath>

#undef near
#undef far

// Column-major 4x4 matrix, OpenGL convention.
struct Mat4 {
    std::array<float, 16> m{};

    static Mat4 identity() {
        Mat4 r; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f; return r;
    }

    static Mat4 perspective(float fovy, float aspect, float near, float far) {
        float f = 1.0f / std::tan(fovy * 0.5f);
        Mat4 r;
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (far + near) / (near - far);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * far * near) / (near - far);
        return r;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = (center - eye).normalized();
        Vec3 s = f.cross(up).normalized();
        Vec3 u = s.cross(f);
        Mat4 r;
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -s.dot(eye);
        r.m[13] = -u.dot(eye);
        r.m[14] = f.dot(eye);
        r.m[15] = 1.0f;
        return r;
    }

    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row) {
                float sum = 0;
                for (int k = 0; k < 4; ++k) sum += m[k * 4 + row] * b.m[col * 4 + k];
                r.m[col * 4 + row] = sum;
            }
        return r;
    }
    const float* data() const { return m.data(); }
};

// Orbital camera.
struct OrbitCamera {
    Vec3 target{};
    float distance = 60, yaw = 0.7f, pitch = 0.5f;
    float fov = 55.0f * 3.14159265f / 180.0f, aspect = 1.6f, near = 0.1f, far = 5000.0f;

    Vec3 eye() const {
        float cp = std::cos(pitch);
        return target + Vec3{cp * std::cos(yaw), std::sin(pitch), cp * std::sin(yaw)} * distance;
    }
    Mat4 viewProj() const {
        return Mat4::perspective(fov, aspect, near, far) *
               Mat4::lookAt(eye(), target, {0, 1, 0});
    }
    void rotate(float dx, float dy) {
        yaw += dx * 0.005f; pitch += dy * 0.005f;
        if (pitch > 1.55f) pitch = 1.55f;
        if (pitch < -1.55f) pitch = -1.55f;
    }
    void zoom(float a) {
        distance *= std::exp(-a * 0.12f);
        if (distance < 3) distance = 3;
        if (distance > 4000) distance = 4000;
    }
};
