#pragma once
#include "vec3.hpp"

#include <cstddef>
#include <span>
#include <vector>

class ThreadPool;

struct SurfaceVertex {
    Vec3 position;
    Vec3 normal;
};

struct SurfaceSettings {
    float cellSize = 0.75f;     // field grid spacing, adjusted per axis so the walls fall on grid planes
    float kernelRadius = 1.8f;  // ~3x the particle spacing gives a watertight skin
    float isoLevel = 0.42f;
    int blurPasses = 1;         // separable field blur that removes terracing (0 = off)
};

// Reconstructs the water surface from particles. Each particle adds a smooth
// radial kernel (1 - r^2/R^2)^3 to a scalar field on a regular grid; the water is
// where the field reaches the iso level, inside the side walls and above the
// floor. Its boundary is triangulated with marching tetrahedra: every grid cube
// is split into six tetrahedra, which needs no case table and has no ambiguous
// configurations.
class SurfaceExtractor {
public:
    SurfaceExtractor() = default;
    explicit SurfaceExtractor(const SurfaceSettings& settings) : settings_(settings) {}

    // Rebuilds the mesh for particles inside the container [boxMin, boxMax].
    void build(std::span<const Vec3> particles, const Vec3& boxMin, const Vec3& boxMax, ThreadPool& pool);

    // The mesh as unindexed triangles, in one chunk per worker task.
    std::span<const std::vector<SurfaceVertex>> chunks() const { return chunks_; }
    std::size_t vertexCount() const { return vertexCount_; }

private:
    void layOutGrid(const Vec3& boxMin, const Vec3& boxMax);
    void depositField(std::span<const Vec3> particles, ThreadPool& pool);
    void blurField(ThreadPool& pool);
    void polygonize(ThreadPool& pool);

    std::size_t index(int x, int y, int z) const {
        return static_cast<std::size_t>(x) +
               static_cast<std::size_t>(nx_) * (static_cast<std::size_t>(y) + static_cast<std::size_t>(ny_) * static_cast<std::size_t>(z));
    }
    float sampleClamped(int x, int y, int z) const;
    Vec3 positionAt(int x, int y, int z) const;
    Vec3 normalAt(int x, int y, int z) const;

    SurfaceSettings settings_;
    Vec3 boxMin_{}, boxMax_{};
    Vec3 origin_{};
    Vec3 cell_{};
    int nx_ = 0, ny_ = 0, nz_ = 0;
    std::vector<float> wallX_, wallY_, wallZ_;  // per grid plane: distance to the nearest wall, positive inside
    std::vector<float> field_;
    std::vector<float> scratch_;
    std::vector<std::vector<SurfaceVertex>> chunks_;
    std::size_t vertexCount_ = 0;
};
