#pragma once
#include "grid.hpp"
#include "kernels.hpp"
#include "vec3.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

class ThreadPool;

// Solid sphere the fluid flows around. Particles that end up inside are projected
// back onto its surface during integration: a cheap, stable collider rather than
// a sampled boundary, so the flow right at the sphere is only approximate.
struct SphereCollider {
    Vec3 center{};
    float radius = 5.0f;
    float restitution = 0.05f;  // share of the normal velocity reflected on impact
    float friction = 0.55f;     // share of the tangential velocity removed per contact
};

struct SphParams {
    float h = 1.0f;                     // smoothing radius
    float restDensity = 1000.0f;        // rho0
    float soundSpeed = 80.0f;           // c: Tait EOS stiffness, ~10x the peak flow speed
    float artificialViscosity = 0.18f;  // Monaghan alpha
    Vec3 gravity{0.0f, -9.8f, 0.0f};
    Vec3 boxMin{0.0f, 0.0f, 0.0f};
    Vec3 boxMax{40.0f, 40.0f, 40.0f};
    float wallRestitution = 0.2f;       // bounce off the container walls
    float boundaryWidth = 2.0f;         // repulsive cushion at the vertical walls (0 = off)
    float boundaryStiffness = 60.0f;    // strength of that cushion
    float boundaryDrag = 3.0f;          // viscous drag inside the cushion (stops wall climbing)
    float dt = 0.0025f;                 // fixed step, inside the CFL limit h / (c + |v|max)
    std::optional<SphereCollider> obstacle;

    static constexpr float gamma = 7.0f;  // Tait exponent
};

// Weakly compressible SPH: poly6 density, Tait equation of state, symmetric
// spiky-gradient pressure force, Monaghan artificial viscosity, semi-implicit
// Euler. Particles are re-sorted by grid cell every step, so their order changes.
class Sph {
public:
    Sph(const SphParams& params, ThreadPool& pool);

    // Fills the box [lo, hi] with a cubic lattice of spacing h * spacingFactor and
    // calibrates the particle mass so that the packed fluid sits at rest density.
    void emitBlock(const Vec3& lo, const Vec3& hi, float spacingFactor = 0.6f);

    void step();

    std::size_t size() const { return pos_.size(); }
    std::span<const Vec3> positions() const { return pos_; }
    std::span<const Vec3> velocities() const { return vel_; }
    std::span<const float> densities() const { return density_; }
    const SphParams& params() const { return params_; }
    float particleMass() const { return mass_; }

private:
    void calibrateMass();
    void sortByCell();
    void computeDensityPressure();
    void computeAccelerations();
    void integrate();
    Vec3 boundaryAcceleration(const Vec3& x, const Vec3& v) const;

    SphParams params_;
    Kernels kernels_;
    ThreadPool& pool_;
    Grid grid_;
    float taitB_;
    float mass_ = 1.0f;

    std::vector<Vec3> pos_, vel_, accel_;
    std::vector<float> density_;
    std::vector<float> pressureTerm_;  // p / rho^2, shared by every pair in the force pass
    std::vector<Vec3> scratch_;
};
