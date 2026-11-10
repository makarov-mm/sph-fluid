#pragma once
#include "vec3.hpp"

#include <numbers>

// SPH smoothing kernels in 3D (Müller et al. 2003), coefficients precomputed for
// the smoothing radius h.
struct Kernels {
    float h;
    float h2;
    float poly6Coef;      // density kernel
    float spikyGradCoef;  // pressure-gradient kernel, sign included

    explicit Kernels(float radius) : h(radius), h2(radius * radius) {
        constexpr float pi = std::numbers::pi_v<float>;
        const float h3 = h * h * h;
        const float h6 = h3 * h3;
        const float h9 = h6 * h3;
        poly6Coef = 315.0f / (64.0f * pi * h9);
        spikyGradCoef = -45.0f / (pi * h6);
    }

    // W_poly6 as a function of the squared distance (no sqrt in the density loop).
    float poly6(float r2) const {
        if (r2 >= h2) return 0.0f;
        const float d = h2 - r2;
        return poly6Coef * d * d * d;
    }

    // ∇W_spiky for the separation rij = xi - xj with |rij| = r, 0 < r < h.
    Vec3 spikyGrad(const Vec3& rij, float r) const {
        const float d = h - r;
        return rij * (spikyGradCoef * d * d / r);
    }
};
