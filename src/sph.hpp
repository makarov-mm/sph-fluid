#pragma once
#include "grid.hpp"
#include "kernels.hpp"
#include "vec3.hpp"
#include <vector>

struct SphParams {
    float h = 1.0f;               // smoothing radius
    float restDensity = 1000.0f;  // rho0
    float soundSpeed = 80.0f;     // c, sets EOS stiffness (Tait); keep ~10x max flow speed
    float artVisc = 0.18f;        // Monaghan artificial viscosity alpha
    float mass = 1.0f;            // per-particle (auto-calibrated by emitBlock)
    Vec3  gravity{0.0f, -9.8f, 0.0f};
    Vec3  boxMin{0.0f, 0.0f, 0.0f};
    Vec3  boxMax{40.0f, 40.0f, 40.0f};
    float restitution = 0.2f;     // wall bounce
    float boundaryWidth = 2.0f;   // repulsive cushion thickness at vertical walls (0=off)
    float boundaryStiffness = 60.0f; // strength of that cushion
    float boundaryDrag = 3.0f;    // viscous drag inside the wall layer (kills climbing)
    float dt = 0.0025f;

    // Optional solid sphere obstacle. Particles are projected out of the sphere
    // during integration, so it behaves as a simple collider for visual scenarios.
    bool sphereObstacle = false;
    Vec3 sphereCenter{17.0f, 10.0f, 12.0f};
    float sphereRadius = 5.0f;
    float sphereRestitution = 0.05f;
    float sphereFriction = 0.55f;

    static constexpr float gamma = 7.0f; // Tait exponent
};

class Sph {
public:
    explicit Sph(const SphParams& p);

    // Fill an axis-aligned block on a regular lattice (spacing = h*spacingFrac)
    // and calibrate particle mass so the packed config sits at rest density.
    void emitBlock(const Vec3& lo, const Vec3& hi, float spacingFrac = 0.6f);

    void step();

    std::size_t size() const { return pos_.size(); }
    const std::vector<Vec3>& positions() const { return pos_; }
    const std::vector<Vec3>& velocities() const { return vel_; }
    const std::vector<float>& densities() const { return density_; }
    const SphParams& params() const { return p_; }
    void setSphereObstacle(bool enabled, const Vec3& center, float radius) {
        p_.sphereObstacle = enabled;
        p_.sphereCenter = center;
        p_.sphereRadius = radius;
    }
    bool sphereObstacleEnabled() const { return p_.sphereObstacle; }
    void setThreads(int t) { threads_ = t < 1 ? 1 : t; }

private:
    void computeDensityPressure();
    void computeAccel();
    void integrate();
    float calibrateMass();

    SphParams p_;
    Kernels k_;
    Grid grid_;
    float taitB_;
    int threads_ = 1;

    std::vector<Vec3> pos_, vel_, accel_;
    std::vector<float> density_, pressure_;
};
