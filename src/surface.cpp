#include "surface.hpp"
#include "thread_pool.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {

// Cube corners as (x, y, z) offsets, and the six tetrahedra around the 0-6 diagonal.
constexpr std::array<std::array<int, 3>, 8> kCorners{{
    {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1},
}};
constexpr std::array<std::array<std::size_t, 4>, 6> kTetrahedra{{
    {0, 5, 1, 6}, {0, 1, 2, 6}, {0, 2, 3, 6}, {0, 3, 7, 6}, {0, 7, 4, 6}, {0, 4, 5, 6},
}};

// [begin, end) of band `i` when n slices are split into `bands` contiguous bands.
std::pair<int, int> band(int n, unsigned bands, unsigned i) {
    const auto edge = [&](unsigned k) { return static_cast<int>(static_cast<long long>(n) * k / bands); };
    return {edge(i), edge(i + 1)};
}

unsigned bandCount(const ThreadPool& pool, int slices) {
    return std::clamp(pool.threadCount(), 1u, static_cast<unsigned>(std::max(slices, 1)));
}

// Grid planes along one axis.
struct GridAxis {
    float origin;
    float cell;
    int reach;  // planes within kernel reach on either side of a point

    // Planes within reach of `coord`, clipped to [lo, hi], and their squared
    // distances to it (from `first` on). False if there are none.
    bool window(float coord, int lo, int hi, std::vector<float>& squared, int& first, int& last) const {
        const int center = static_cast<int>(std::floor((coord - origin) / cell));
        first = std::max(lo, center - reach);
        last = std::min(hi, center + reach);
        for (int i = first; i <= last; ++i) {
            const float d = origin + cell * static_cast<float>(i) - coord;
            squared[static_cast<std::size_t>(i - first)] = d * d;
        }
        return first <= last;
    }
};

// Outward normal of the side wall or floor nearest to p (the top of the tank is open).
Vec3 nearestWallNormal(const Vec3& p, const Vec3& lo, const Vec3& hi) {
    float nearest = p.x - lo.x;
    Vec3 normal{-1.0f, 0.0f, 0.0f};
    const auto consider = [&](float distance, const Vec3& n) {
        if (distance < nearest) {
            nearest = distance;
            normal = n;
        }
    };
    consider(hi.x - p.x, {1.0f, 0.0f, 0.0f});
    consider(p.z - lo.z, {0.0f, 0.0f, -1.0f});
    consider(hi.z - p.z, {0.0f, 0.0f, 1.0f});
    consider(p.y - lo.y, {0.0f, -1.0f, 0.0f});
    return normal;
}

// A grid point as the polygonizer sees it.
struct Corner {
    Vec3 position;
    Vec3 normal;  // from the field gradient
    float value;  // field
    float wall;   // distance to the nearest side wall or the floor, positive inside
};

// Water: the field reaches the iso level and the point is inside the walls.
bool isWater(const Corner& c, float iso) { return c.value >= iso && c.wall >= 0.0f; }

// Where the edge a-b leaves the water: on the iso-surface or on a wall, whichever
// lies nearer to the end that is in the water. Both vary linearly along the edge.
SurfaceVertex crossing(const Corner& a, const Corner& b, float iso, const Vec3& boxMin, const Vec3& boxMax) {
    const bool fromA = isWater(a, iso);
    float t = fromA ? 1.0f : 0.0f;
    if ((a.value >= iso) != (b.value >= iso)) {
        const float delta = b.value - a.value;
        t = std::clamp((iso - a.value) / (delta == 0.0f ? 1.0f : delta), 0.0f, 1.0f);
    }
    bool onWall = false;
    if ((a.wall >= 0.0f) != (b.wall >= 0.0f)) {
        const float tWall = a.wall / (a.wall - b.wall);
        if (fromA ? tWall < t : tWall > t) {
            t = tWall;
            onWall = true;
        }
    }
    const Vec3 position = a.position + (b.position - a.position) * t;
    const Vec3 normal = onWall ? nearestWallNormal(position, boxMin, boxMax)
                               : (a.normal + (b.normal - a.normal) * t).normalized();
    return {position, normal};
}

// Appends the triangles (at most two) where the water boundary crosses one tetrahedron.
void polygonizeTetrahedron(std::vector<SurfaceVertex>& out, const std::array<Corner, 8>& corner,
                           const std::array<std::size_t, 4>& tet, float iso, const Vec3& boxMin, const Vec3& boxMax) {
    std::array<std::size_t, 4> inside{}, outside{};
    std::size_t insideCount = 0, outsideCount = 0;
    for (const std::size_t c : tet) {
        if (isWater(corner[c], iso))
            inside[insideCount++] = c;
        else
            outside[outsideCount++] = c;
    }
    const auto edge = [&](std::size_t from, std::size_t to) {
        return crossing(corner[from], corner[to], iso, boxMin, boxMax);
    };
    const auto emit = [&out](const SurfaceVertex& a, const SurfaceVertex& b, const SurfaceVertex& c) {
        // Vertices that land on a grid point (as on the walls) can coincide; skip
        // the zero-area triangles that would give.
        if (a.position == b.position || b.position == c.position || c.position == a.position) return;
        out.insert(out.end(), {a, b, c});
    };
    switch (insideCount) {
    case 1:
        emit(edge(inside[0], outside[0]), edge(inside[0], outside[1]), edge(inside[0], outside[2]));
        break;
    case 3:
        emit(edge(outside[0], inside[0]), edge(outside[0], inside[2]), edge(outside[0], inside[1]));
        break;
    case 2: {
        const SurfaceVertex a = edge(inside[0], outside[0]);
        const SurfaceVertex b = edge(inside[0], outside[1]);
        const SurfaceVertex c = edge(inside[1], outside[0]);
        const SurfaceVertex d = edge(inside[1], outside[1]);
        emit(a, b, c);
        emit(b, d, c);
        break;
    }
    default:  // 0 or 4: the tetrahedron lies entirely on one side
        break;
    }
}

}  // namespace

void SurfaceExtractor::build(std::span<const Vec3> particles, const Vec3& boxMin, const Vec3& boxMax, ThreadPool& pool) {
    layOutGrid(boxMin, boxMax);
    depositField(particles, pool);
    blurField(pool);
    polygonize(pool);
}

void SurfaceExtractor::layOutGrid(const Vec3& boxMin, const Vec3& boxMax) {
    boxMin_ = boxMin;
    boxMax_ = boxMax;

    // Cells are sized so that the walls fall exactly on grid planes, plus one plane
    // of padding beyond each wall. The mirrored particles keep the field high right
    // up to the walls, so the iso-surface alone would run through them and stay
    // open at the edge of the grid (seen through the glass, the water would look
    // like a hollow shell); the cubes that straddle a wall close the body with a
    // flat face exactly on it, and the waterline on the glass is interpolated
    // within the wall plane.
    const Vec3 extent = boxMax - boxMin;
    const auto cellsAlong = [this](float length) {
        return std::max(1, static_cast<int>(std::lround(length / settings_.cellSize)));
    };
    const int cellsX = cellsAlong(extent.x), cellsY = cellsAlong(extent.y), cellsZ = cellsAlong(extent.z);
    cell_ = {extent.x / static_cast<float>(cellsX), extent.y / static_cast<float>(cellsY),
             extent.z / static_cast<float>(cellsZ)};
    origin_ = boxMin - cell_;
    nx_ = cellsX + 3;
    ny_ = cellsY + 3;
    nz_ = cellsZ + 3;
    field_.assign(static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_) * static_cast<std::size_t>(nz_), 0.0f);

    // Plane i lies at boxMin + (i - 1) cells, so the walls are planes 1 and cells + 1;
    // computing the distances from the index keeps them exactly zero there.
    const auto distances = [](std::vector<float>& out, int planes, float cell, auto cellsInside) {
        out.resize(static_cast<std::size_t>(planes));
        for (int i = 0; i < planes; ++i) out[static_cast<std::size_t>(i)] = cell * static_cast<float>(cellsInside(i));
    };
    distances(wallX_, nx_, cell_.x, [cellsX](int i) { return std::min(i - 1, cellsX + 1 - i); });
    distances(wallY_, ny_, cell_.y, [](int i) { return i - 1; });  // floor only: the top is open
    distances(wallZ_, nz_, cell_.z, [cellsZ](int i) { return std::min(i - 1, cellsZ + 1 - i); });
}

void SurfaceExtractor::depositField(std::span<const Vec3> particles, ThreadPool& pool) {
    const float radius = settings_.kernelRadius;
    const float radius2 = radius * radius;
    const float invRadius2 = 1.0f / radius2;
    const auto axis = [radius](float origin, float cell) {
        return GridAxis{origin, cell, static_cast<int>(std::ceil(radius / cell))};
    };
    const GridAxis axisX = axis(origin_.x, cell_.x), axisY = axis(origin_.y, cell_.y), axisZ = axis(origin_.z, cell_.z);
    const auto planesInReach = [](const GridAxis& a) { return static_cast<std::size_t>(2 * a.reach + 1); };

    // Each task owns a band of z-slices and writes only inside it, so the scatter
    // needs no locking. Every task scans all particles and skips those whose
    // kernel misses its band, which is cheap next to the splatting itself.
    const unsigned tasks = bandCount(pool, nz_);
    pool.run(tasks, [&](unsigned task) {
        const auto [zBegin, zEnd] = band(nz_, tasks, task);
        // Squared distances from the particle to the grid planes in reach, per axis;
        // a grid point's r^2 is then the sum of three of them.
        std::vector<float> dx2(planesInReach(axisX)), dy2(planesInReach(axisY)), dz2(planesInReach(axisZ));

        const auto splat = [&](const Vec3& p) {
            int x0 = 0, x1 = 0, y0 = 0, y1 = 0, z0 = 0, z1 = 0;
            if (!axisZ.window(p.z, zBegin, zEnd - 1, dz2, z0, z1) || !axisY.window(p.y, 0, ny_ - 1, dy2, y0, y1) ||
                !axisX.window(p.x, 0, nx_ - 1, dx2, x0, x1))
                return;
            const int rowLength = x1 - x0 + 1;
            for (int z = z0; z <= z1; ++z) {
                const float dzSquared = dz2[static_cast<std::size_t>(z - z0)];
                for (int y = y0; y <= y1; ++y) {
                    const float dyz2 = dy2[static_cast<std::size_t>(y - y0)] + dzSquared;
                    if (dyz2 >= radius2) continue;  // the whole row is outside the kernel
                    // Branch-free: points outside the kernel add exactly zero.
                    float* row = field_.data() + index(x0, y, z);
                    for (int k = 0; k < rowLength; ++k) {
                        const float t = std::max(0.0f, 1.0f - (dx2[static_cast<std::size_t>(k)] + dyz2) * invRadius2);
                        row[k] += t * t * t;
                    }
                }
            }
        };

        for (const Vec3& p : particles) {
            splat(p);
            // Mirror particles near the side walls and the floor so the field reaches
            // the iso level right at those walls; the open top is not mirrored.
            if (p.x - boxMin_.x < radius) splat({2.0f * boxMin_.x - p.x, p.y, p.z});
            if (boxMax_.x - p.x < radius) splat({2.0f * boxMax_.x - p.x, p.y, p.z});
            if (p.z - boxMin_.z < radius) splat({p.x, p.y, 2.0f * boxMin_.z - p.z});
            if (boxMax_.z - p.z < radius) splat({p.x, p.y, 2.0f * boxMax_.z - p.z});
            if (p.y - boxMin_.y < radius) splat({p.x, 2.0f * boxMin_.y - p.y, p.z});
        }
    });
}

void SurfaceExtractor::blurField(ThreadPool& pool) {
    // Separable [1 2 1] / 4 filter, one pass per axis, clamped at the borders.
    const std::array<int, 3> extent{nx_, ny_, nz_};
    const std::array<std::size_t, 3> stride{1, index(0, 1, 0), index(0, 0, 1)};
    const unsigned tasks = bandCount(pool, nz_);
    scratch_.resize(field_.size());

    for (int pass = 0; pass < settings_.blurPasses; ++pass)
        for (std::size_t axis = 0; axis < 3; ++axis) {
            pool.run(tasks, [&](unsigned task) {
                const auto [zBegin, zEnd] = band(nz_, tasks, task);
                for (int z = zBegin; z < zEnd; ++z)
                    for (int y = 0; y < ny_; ++y)
                        for (int x = 0; x < nx_; ++x) {
                            const int along = axis == 0 ? x : axis == 1 ? y : z;
                            const std::size_t i = index(x, y, z);
                            const std::size_t lo = along > 0 ? i - stride[axis] : i;
                            const std::size_t hi = along + 1 < extent[axis] ? i + stride[axis] : i;
                            scratch_[i] = 0.25f * field_[lo] + 0.5f * field_[i] + 0.25f * field_[hi];
                        }
            });
            field_.swap(scratch_);
        }
}

void SurfaceExtractor::polygonize(ThreadPool& pool) {
    const float iso = settings_.isoLevel;
    const int layers = nz_ - 1;  // layers of cubes along z
    const unsigned tasks = bandCount(pool, layers);
    chunks_.resize(tasks);

    std::array<std::size_t, 8> cornerOffset{};
    for (std::size_t c = 0; c < 8; ++c) cornerOffset[c] = index(kCorners[c][0], kCorners[c][1], kCorners[c][2]);

    pool.run(tasks, [&](unsigned task) {
        std::vector<SurfaceVertex>& out = chunks_[task];
        out.clear();
        const auto [zBegin, zEnd] = band(layers, tasks, task);
        std::array<Corner, 8> corner{};
        for (int z = zBegin; z < zEnd; ++z)
            for (int y = 0; y + 1 < ny_; ++y)
                for (int x = 0; x + 1 < nx_; ++x) {
                    const std::size_t base = index(x, y, z);
                    unsigned waterMask = 0;
                    for (std::size_t c = 0; c < 8; ++c) {
                        const auto cx = static_cast<std::size_t>(x + kCorners[c][0]);
                        const auto cy = static_cast<std::size_t>(y + kCorners[c][1]);
                        const auto cz = static_cast<std::size_t>(z + kCorners[c][2]);
                        corner[c].value = field_[base + cornerOffset[c]];
                        corner[c].wall = std::min({wallX_[cx], wallY_[cy], wallZ_[cz]});
                        if (isWater(corner[c], iso)) waterMask |= 1u << c;
                    }
                    if (waterMask == 0 || waterMask == 0xFFu) continue;  // the boundary does not cross this cube

                    for (std::size_t c = 0; c < 8; ++c) {
                        const int cx = x + kCorners[c][0], cy = y + kCorners[c][1], cz = z + kCorners[c][2];
                        corner[c].position = positionAt(cx, cy, cz);
                        corner[c].normal = normalAt(cx, cy, cz);
                    }
                    for (const auto& tet : kTetrahedra) polygonizeTetrahedron(out, corner, tet, iso, boxMin_, boxMax_);
                }
    });

    vertexCount_ = 0;
    for (const auto& chunk : chunks_) vertexCount_ += chunk.size();
}

float SurfaceExtractor::sampleClamped(int x, int y, int z) const {
    return field_[index(std::clamp(x, 0, nx_ - 1), std::clamp(y, 0, ny_ - 1), std::clamp(z, 0, nz_ - 1))];
}

Vec3 SurfaceExtractor::positionAt(int x, int y, int z) const {
    return {origin_.x + cell_.x * static_cast<float>(x), origin_.y + cell_.y * static_cast<float>(y),
            origin_.z + cell_.z * static_cast<float>(z)};
}

Vec3 SurfaceExtractor::normalAt(int x, int y, int z) const {
    // Central differences. The field grows towards the fluid interior, so the
    // outward normal is the negated gradient.
    const Vec3 gradient{(sampleClamped(x + 1, y, z) - sampleClamped(x - 1, y, z)) / cell_.x,
                        (sampleClamped(x, y + 1, z) - sampleClamped(x, y - 1, z)) / cell_.y,
                        (sampleClamped(x, y, z + 1) - sampleClamped(x, y, z - 1)) / cell_.z};
    return (-gradient).normalized();
}
