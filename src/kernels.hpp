#pragma once
#include "vec3.hpp"
#include <numbers>

// SPH smoothing kernels (Müller et al. 2003), 3D. Coefficients are precomputed
// from the smoothing radius h.
struct Kernels {
    float h, h2;
    float poly6Coef;      // density
    float spikyGradCoef;  // pressure gradient (carries its negative sign)
    float viscLapCoef;    // viscosity laplacian

    explicit Kernels(float h_) : h(h_), h2(h_ * h_) {
        const float pi = std::numbers::pi_v<float>;
        float h3 = h * h * h;
        float h6 = h3 * h3;
        float h9 = h6 * h3;
        poly6Coef = 315.0f / (64.0f * pi * h9);
        spikyGradCoef = -45.0f / (pi * h6);
        viscLapCoef = 45.0f / (pi * h6);
    }

    // W_poly6 as a function of squared distance (avoids a sqrt in the density loop).
    float poly6(float r2) const {
        if (r2 >= h2) return 0.0f;
        float d = h2 - r2;
        return poly6Coef * d * d * d;
    }

    // ∇W_spiky for separation vector rij = xi - xj, |rij| = r (> 0, < h).
    Vec3 spikyGrad(const Vec3& rij, float r) const {
        float d = h - r;
        return rij * (spikyGradCoef * d * d / r);
    }

    // ∇²W_viscosity for distance r (< h).
    float viscLap(float r) const {
        return viscLapCoef * (h - r);
    }
};
