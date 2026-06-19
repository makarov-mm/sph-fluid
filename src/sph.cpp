#include "sph.hpp"
#include <algorithm>
#include <cmath>
#include <thread>

Sph::Sph(const SphParams& p) : p_(p), k_(p.h) {
    taitB_ = p_.restDensity * p_.soundSpeed * p_.soundSpeed / SphParams::gamma;
}

template <class Fn>
static void parallelFor(int n, int threads, Fn&& fn) {
    if (threads <= 1 || n < 2048) {
        for (int i = 0; i < n; ++i) fn(i);
        return;
    }
    std::vector<std::thread> pool;
    int chunk = (n + threads - 1) / threads;
    for (int t = 0; t < threads; ++t) {
        int begin = t * chunk, end = std::min(begin + chunk, n);
        if (begin >= end) break;
        pool.emplace_back([&fn, begin, end]() { for (int i = begin; i < end; ++i) fn(i); });
    }
    for (auto& th : pool) th.join();
}

void Sph::emitBlock(const Vec3& lo, const Vec3& hi, float spacingFrac) {
    float s = p_.h * spacingFrac;
    for (float z = lo.z; z <= hi.z; z += s)
        for (float y = lo.y; y <= hi.y; y += s)
            for (float x = lo.x; x <= hi.x; x += s)
                pos_.push_back({x, y, z});

    const std::size_t n = pos_.size();
    vel_.assign(n, Vec3{});
    accel_.assign(n, Vec3{});
    density_.assign(n, 0.0f);
    pressure_.assign(n, 0.0f);

    p_.mass = 1.0f;
    p_.mass = calibrateMass();
}

float Sph::calibrateMass() {
    grid_.rebuild(pos_, p_.h);
    const int n = static_cast<int>(pos_.size());
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        float rho = 0.0f;
        Vec3 pi = pos_[i];
        grid_.forEachNeighbor(pi, [&](int j) { rho += k_.poly6((pos_[j] - pi).lengthSquared()); });
        sum += rho;
    }
    double meanRho = sum / std::max(1, n);
    return meanRho > 0 ? static_cast<float>(p_.restDensity / meanRho) : 1.0f;
}

void Sph::computeDensityPressure() {
    const int n = static_cast<int>(pos_.size());
    const float m = p_.mass;
    parallelFor(n, threads_, [&](int i) {
        Vec3 pi = pos_[i];
        float rho = 0.0f;
        grid_.forEachNeighbor(pi, [&](int j) { rho += m * k_.poly6((pos_[j] - pi).lengthSquared()); });
        density_[i] = rho;
        // Tait equation of state: stiff resistance to compression, ~incompressible.
        float ratio = rho / p_.restDensity;
        float r2 = ratio * ratio;
        float r7 = r2 * r2 * r2 * ratio;
        pressure_[i] = std::max(0.0f, taitB_ * (r7 - 1.0f));
    });
}

void Sph::computeAccel() {
    const int n = static_cast<int>(pos_.size());
    const float m = p_.mass;
    const float c = p_.soundSpeed;
    const float alpha = p_.artVisc;
    parallelFor(n, threads_, [&](int i) {
        Vec3 pi = pos_[i], vi = vel_[i];
        float rhoi = density_[i], pri = pressure_[i];
        Vec3 a = p_.gravity;
        grid_.forEachNeighbor(pi, [&](int j) {
            if (j == i) return;
            Vec3 rij = pi - pos_[j];
            float r2 = rij.lengthSquared();
            if (r2 >= k_.h2 || r2 <= 1e-12f) return;
            float r = std::sqrt(r2);
            float rhoj = density_[j];
            Vec3 gradW = k_.spikyGrad(rij, r);
            // symmetric pressure acceleration (momentum-conserving)
            float pterm = pri / (rhoi * rhoi) + pressure_[j] / (rhoj * rhoj);
            a += gradW * (-m * pterm);
            // Monaghan artificial viscosity (only when approaching)
            Vec3 vij = vi - vel_[j];
            float vr = vij.dot(rij);
            if (vr < 0.0f) {
                float mu = p_.h * vr / (r2 + 0.01f * k_.h2);
                float rhoBar = 0.5f * (rhoi + rhoj);
                float pi_ij = -alpha * c * mu / rhoBar;
                a += gradW * (-m * pi_ij);
            }
        });
        // Repulsive cushion at the vertical walls: counters the near-wall density
        // deficit so sloshing fluid doesn't glue itself to a wall.
        if (p_.boundaryWidth > 0.0f) {
            float bw = p_.boundaryWidth, bs = p_.boundaryStiffness;
            float dxl = pi.x - p_.boxMin.x; if (dxl < bw) a.x += bs * (1.0f - dxl / bw);
            float dxr = p_.boxMax.x - pi.x; if (dxr < bw) a.x -= bs * (1.0f - dxr / bw);
            float dzl = pi.z - p_.boxMin.z; if (dzl < bw) a.z += bs * (1.0f - dzl / bw);
            float dzr = p_.boxMax.z - pi.z; if (dzr < bw) a.z -= bs * (1.0f - dzr / bw);
            // viscous drag inside the wall layer drains climbing momentum
            if (p_.boundaryDrag > 0.0f) {
                float dmin = std::min(std::min(dxl, dxr), std::min(dzl, dzr));
                if (dmin < bw) {
                    float t = 1.0f - dmin / bw;          // 0 at edge, 1 at wall
                    a += vi * (-p_.boundaryDrag * t);
                }
            }
        }
        accel_[i] = a;
    });
}

void Sph::integrate() {
    const int n = static_cast<int>(pos_.size());
    const float dt = p_.dt, e = p_.restitution;
    parallelFor(n, threads_, [&](int i) {
        vel_[i] += accel_[i] * dt;     // semi-implicit Euler
        pos_[i] += vel_[i] * dt;
        Vec3& x = pos_[i];
        Vec3& v = vel_[i];
        if (x.x < p_.boxMin.x) { x.x = p_.boxMin.x; v.x = -v.x * e; }
        if (x.x > p_.boxMax.x) { x.x = p_.boxMax.x; v.x = -v.x * e; }
        if (x.y < p_.boxMin.y) { x.y = p_.boxMin.y; v.y = -v.y * e; }
        if (x.y > p_.boxMax.y) { x.y = p_.boxMax.y; v.y = -v.y * e; }
        if (x.z < p_.boxMin.z) { x.z = p_.boxMin.z; v.z = -v.z * e; }
        if (x.z > p_.boxMax.z) { x.z = p_.boxMax.z; v.z = -v.z * e; }
    });
}

void Sph::step() {
    grid_.rebuild(pos_, p_.h);
    computeDensityPressure();
    computeAccel();
    integrate();
}
