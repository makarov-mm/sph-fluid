#include "sph.hpp"
#include "thread_pool.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>

namespace {

constexpr std::size_t kGrain = 256;  // particles per parallel task

// dst[k] = src[order[k]]; the old buffer is kept in `scratch` for the next call.
void applyOrder(std::vector<Vec3>& values, std::span<const std::uint32_t> order, std::vector<Vec3>& scratch) {
    scratch.resize(values.size());
    for (std::size_t k = 0; k < order.size(); ++k) scratch[k] = values[order[k]];
    values.swap(scratch);
}

// Clamps one coordinate into [lo, hi] and reflects the velocity with restitution e.
void bounce(float& x, float& v, float lo, float hi, float e) {
    if (x < lo) {
        x = lo;
        v = -v * e;
    } else if (x > hi) {
        x = hi;
        v = -v * e;
    }
}

void collide(const SphereCollider& sphere, Vec3& x, Vec3& v) {
    const Vec3 d = x - sphere.center;
    const float d2 = d.lengthSquared();
    if (d2 >= sphere.radius * sphere.radius) return;

    const Vec3 n = d * (1.0f / std::sqrt(std::max(d2, 1e-12f)));
    x = sphere.center + n * sphere.radius;

    const float vn = v.dot(n);
    Vec3 normal = n * vn;
    const Vec3 tangent = (v - normal) * std::clamp(1.0f - sphere.friction, 0.0f, 1.0f);
    if (vn < 0.0f) normal *= -sphere.restitution;
    v = normal + tangent;
}

}  // namespace

Sph::Sph(const SphParams& params, ThreadPool& pool)
    : params_(params),
      kernels_(params.h),
      pool_(pool),
      taitB_(params.restDensity * params.soundSpeed * params.soundSpeed / SphParams::gamma) {}

void Sph::emitBlock(const Vec3& lo, const Vec3& hi, float spacingFactor) {
    const float spacing = params_.h * spacingFactor;
    // Count the layers up front: stepping a float coordinate accumulates rounding
    // error and can drop the last layer when (hi - lo) is a multiple of the spacing.
    const auto layers = [spacing](float from, float to) {
        return to < from ? 0 : static_cast<int>(std::floor((to - from) / spacing + 1e-3f)) + 1;
    };
    const int nx = layers(lo.x, hi.x), ny = layers(lo.y, hi.y), nz = layers(lo.z, hi.z);

    pos_.reserve(pos_.size() + static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny) * static_cast<std::size_t>(nz));
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
                pos_.push_back(lo + Vec3{static_cast<float>(i), static_cast<float>(j), static_cast<float>(k)} * spacing);

    const std::size_t n = pos_.size();
    vel_.resize(n);
    accel_.resize(n);
    density_.resize(n);
    pressureTerm_.resize(n);
    calibrateMass();
}

void Sph::calibrateMass() {
    // Edge particles see fewer neighbours, so the mean density of the packed block
    // sits below the interior value; scale the mass so that the mean is rho0.
    sortByCell();
    mass_ = 1.0f;
    computeDensityPressure();
    const double sum = std::accumulate(density_.begin(), density_.end(), 0.0);
    const double mean = density_.empty() ? 0.0 : sum / static_cast<double>(density_.size());
    const float scale = mean > 0.0 ? static_cast<float>(params_.restDensity / mean) : 1.0f;
    mass_ = scale;
    for (float& rho : density_) rho *= scale;
}

void Sph::step() {
    sortByCell();
    computeDensityPressure();
    computeAccelerations();
    integrate();
}

void Sph::sortByCell() {
    // Neighbours end up next to each other in memory, which is what makes the two
    // neighbour passes below cache-friendly.
    const auto order = grid_.build(pos_, params_.h, params_.boxMin, params_.boxMax);
    applyOrder(pos_, order, scratch_);
    applyOrder(vel_, order, scratch_);
}

void Sph::computeDensityPressure() {
    const float rho0 = params_.restDensity;
    pool_.parallelFor(pos_.size(), kGrain, [&](std::size_t first, std::size_t last) {
        for (std::size_t i = first; i < last; ++i) {
            const Vec3 xi = pos_[i];
            float sum = 0.0f;
            grid_.forEachNeighborRange(xi, [&](std::uint32_t begin, std::uint32_t end) {
                for (std::uint32_t j = begin; j < end; ++j) sum += kernels_.poly6((pos_[j] - xi).lengthSquared());
            });
            const float rho = mass_ * sum;

            // Tait equation of state; the steep exponent keeps the fluid within a
            // few percent of rest density. Suction is clamped to avoid clumping.
            const float ratio = rho / rho0;
            const float ratio2 = ratio * ratio;
            const float pressure = std::max(0.0f, taitB_ * (ratio2 * ratio2 * ratio2 * ratio - 1.0f));
            density_[i] = rho;
            pressureTerm_[i] = pressure / (rho * rho);
        }
    });
}

void Sph::computeAccelerations() {
    const float h2 = kernels_.h2;
    const float soften = 0.01f * h2;  // keeps the viscosity finite as r -> 0
    const float viscosityScale = params_.artificialViscosity * params_.soundSpeed * params_.h;
    pool_.parallelFor(pos_.size(), kGrain, [&](std::size_t first, std::size_t last) {
        for (std::size_t i = first; i < last; ++i) {
            const Vec3 xi = pos_[i];
            const Vec3 vi = vel_[i];
            const float rhoI = density_[i];
            const float termI = pressureTerm_[i];
            Vec3 a{};
            grid_.forEachNeighborRange(xi, [&](std::uint32_t begin, std::uint32_t end) {
                for (std::uint32_t j = begin; j < end; ++j) {
                    const Vec3 rij = xi - pos_[j];
                    const float r2 = rij.lengthSquared();
                    if (r2 >= h2 || r2 <= 1e-12f) continue;  // out of reach, or i itself

                    // Symmetric pressure term (conserves momentum) plus Monaghan
                    // artificial viscosity for particles moving towards each other.
                    float coupling = termI + pressureTerm_[j];
                    const float vr = (vi - vel_[j]).dot(rij);
                    if (vr < 0.0f) {
                        const float mu = vr / (r2 + soften);
                        coupling -= viscosityScale * mu / (0.5f * (rhoI + density_[j]));
                    }
                    a -= kernels_.spikyGrad(rij, std::sqrt(r2)) * (mass_ * coupling);
                }
            });
            accel_[i] = a + params_.gravity + boundaryAcceleration(xi, vi);
        }
    });
}

Vec3 Sph::boundaryAcceleration(const Vec3& x, const Vec3& v) const {
    // Particles next to a wall lack the neighbours that would sit behind it, so
    // their density reads low and the wall never pushes back: sloshing water
    // climbs the walls and sticks. A thin repulsive cushion at the vertical walls,
    // with drag inside it, counters that.
    const float width = params_.boundaryWidth;
    if (width <= 0.0f) return {};

    const float k = params_.boundaryStiffness;
    const float left = x.x - params_.boxMin.x;
    const float right = params_.boxMax.x - x.x;
    const float back = x.z - params_.boxMin.z;
    const float front = params_.boxMax.z - x.z;

    Vec3 a{};
    if (left < width) a.x += k * (1.0f - left / width);
    if (right < width) a.x -= k * (1.0f - right / width);
    if (back < width) a.z += k * (1.0f - back / width);
    if (front < width) a.z -= k * (1.0f - front / width);

    if (params_.boundaryDrag > 0.0f) {
        const float nearest = std::min({left, right, back, front});
        if (nearest < width) a -= v * (params_.boundaryDrag * (1.0f - nearest / width));
    }
    return a;
}

void Sph::integrate() {
    const float dt = params_.dt;
    const float e = params_.wallRestitution;
    const Vec3 lo = params_.boxMin;
    const Vec3 hi = params_.boxMax;
    const SphereCollider* sphere = params_.obstacle ? &*params_.obstacle : nullptr;
    pool_.parallelFor(pos_.size(), kGrain, [&](std::size_t first, std::size_t last) {
        for (std::size_t i = first; i < last; ++i) {
            Vec3& x = pos_[i];
            Vec3& v = vel_[i];
            v += accel_[i] * dt;  // semi-implicit Euler: velocity first, then position
            x += v * dt;
            bounce(x.x, v.x, lo.x, hi.x, e);
            bounce(x.y, v.y, lo.y, hi.y, e);
            bounce(x.z, v.z, lo.z, hi.z, e);
            if (sphere) collide(*sphere, x, v);
        }
    });
}
