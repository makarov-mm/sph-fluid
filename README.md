# SPH Fluid

A 3D water simulator written from scratch in C++. The water is thousands of
particles pulling on each other; there's no game engine and no physics library
doing the work. The only outside code in the project opens a window and talks to
OpenGL.

I wrote it to understand how fluids are actually simulated, instead of wrapping
someone else's solver.

![fluid](screenshot.png)

## How it works

The fluid is a cloud of particles. Each one carries mass and velocity, and the
smooth, continuous quantities of real water — density, pressure, the forces
between blobs of fluid — are rebuilt by summing smooth kernels over nearby
particles. That's the whole idea behind SPH (Smoothed Particle Hydrodynamics).

The parts that actually matter for getting water that behaves:

- **Density** comes from the poly6 kernel.
- **Pressure** uses a stiff Tait equation of state, `p ∝ (ρ/ρ₀)⁷ − 1`. The steep
  exponent is what stops the water from squashing: density stays within a few
  percent of rest instead of collapsing.
- **Stability** comes from Monaghan artificial viscosity, which bleeds off energy
  when particles rush together. Without it, a dropped column of water doesn't
  settle, it detonates.
- **Neighbours** are found with a uniform spatial-hash grid (cell size = the
  smoothing radius, built with a counting sort), so each step costs O(N) instead
  of O(N²).
- **Time stepping** is semi-implicit Euler at a CFL-limited step.

There's also a thin repulsive cushion at the tank walls. Particles next to a wall
are missing the neighbours that would sit on the other side of it, so their
density reads low and the wall never pushes back. Left alone, sloshing water
climbs the walls and sticks there. The cushion (plus a little drag in the
boundary layer) fixes it.

Particles are drawn as small lit spheres, blue when calm and white where the
water moves fast, inside a wireframe tank.

## Build

### Visual Studio (Windows)

Open `SphFluidViewer.sln`, set the configuration to **Release | x64**, and run
(`Ctrl+F5`). No external dependencies — it links only `opengl32`, `gdi32` and
`user32` from the Windows SDK, and loads the OpenGL functions it needs at
runtime. Run on a machine with real GPU drivers (over Remote Desktop there's no
OpenGL 3.3).

### CMake (cross-platform)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/sph_fluid        # viewer
./build/verify           # headless core check, no GPU needed
```

CMake pulls GLFW and GLAD automatically, so there's nothing to install by hand.
Needs a C++20 compiler.

## Controls

| Input        | Action                            |
|--------------|-----------------------------------|
| left-drag    | orbit the camera                  |
| mouse wheel  | zoom                              |
| `Space`      | pause / resume                    |
| `↑` / `↓`    | simulation speed (substeps/frame) |
| `R`          | reset the dam-break               |
| `Esc`        | quit                              |

## What I checked

The `verify` build runs without a GPU and sanity-checks the core:

- the grid returns exactly the same neighbours as a brute-force O(N²) scan;
- under a dam-break, peak density stays within a few percent of rest, so the
  fluid really is close to incompressible;
- kinetic energy rises as the column falls and then decays as it settles,
  instead of growing forever, which is the difference between a stable scheme and
  one that's quietly adding energy.

## Layout

```
src/vec3.hpp      hand-rolled 3D vector
src/mat4.hpp      column-major matrix + orbital camera
src/kernels.hpp   SPH smoothing kernels
src/grid.hpp      uniform spatial-hash grid
src/sph.hpp/.cpp  the solver: density, pressure, forces, integration
src/viewer_*.cpp  OpenGL viewers (native Win32, and GLFW for CMake)
src/verify.cpp    headless core check
```

## License

MIT.
