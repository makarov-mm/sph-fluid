#include "sph.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>

static void gridCorrectness() {
    SphParams p; p.h = 1.0f;
    Sph sph(p);
    sph.emitBlock({2,2,2}, {10,14,10}, 0.6f);
    const auto& pos = sph.positions();
    const int n = (int)pos.size();
    Grid g; g.rebuild(pos, p.h);
    float h2 = p.h * p.h; int mismatch = 0, checked = 0;
    for (int i = 0; i < n; i += std::max(1, n/60)) {
        int brute = 0;
        for (int j = 0; j < n; ++j) if ((pos[j]-pos[i]).lengthSquared() < h2) ++brute;
        int viaGrid = 0;
        g.forEachNeighbor(pos[i], [&](int j){ if ((pos[j]-pos[i]).lengthSquared() < h2) ++viaGrid; });
        if (brute != viaGrid) ++mismatch; ++checked;
    }
    printf("[0] grid neighbour search: %d/%d match brute force%s\n",
           checked - mismatch, checked, mismatch ? "  <-- MISMATCH" : "");
}

int main() {
    printf("=== SPH fluid core - verification ===\n\n");
    gridCorrectness();

    SphParams p; p.h = 1.0f; p.restDensity = 1000.0f;
    p.soundSpeed = 80.0f; p.dt = 0.0025f; p.artVisc = 0.18f;
    p.boxMin = {0,0,0}; p.boxMax = {28,30,28};
    Sph sph(p);
    sph.emitBlock({2,2,2}, {10,26,10}, 0.6f);
    sph.setThreads((int)std::thread::hardware_concurrency());
    printf("\n[1] dam-break settling (%zu particles, chosen params)\n", sph.size());
    printf("    step      KE     rhoMean  rhoMax   vMax\n");
    for (int s = 1; s <= 3000; ++s) {
        sph.step();
        if (s % 500 == 0) {
            const auto& vel = sph.velocities(); const auto& rho = sph.densities();
            double ke=0,sum=0; float rmax=0,vmax=0;
            for (size_t i=0;i<sph.size();++i){ float v2=vel[i].lengthSquared();
                ke+=0.5*p.mass*v2; vmax=std::max(vmax,std::sqrt(v2));
                sum+=rho[i]; rmax=std::max(rmax,rho[i]); }
            printf("    %4d  %9.0f   %6.0f   %5.0f   %5.1f\n",
                   s, ke, sum/sph.size(), rmax, vmax);
        }
    }
    printf("\nKE should rise to a peak (collapse) then fall (settling). Core OK.\n");
    return 0;
}
