#pragma once
#include "vec3.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

// Uniform grid for neighbour search over a fixed domain. With the cell size equal
// to the smoothing radius, every neighbour of a point lies in the 3x3x3 cells
// around it.
//
// build() bins the points with a counting sort, O(N), and returns the permutation
// that orders them by cell. Once the caller stores its particles in that order,
// each cell (and each run of adjacent cells along x) is one contiguous index
// range, so the neighbour loops read memory sequentially.
class Grid {
public:
    // Returns order: order[k] is the index of the point that goes to position k.
    // Points outside the domain are clamped into the border cells, which keeps the
    // search correct and only costs speed.
    std::span<const std::uint32_t> build(std::span<const Vec3> points, float cellSize,
                                         const Vec3& domainMin, const Vec3& domainMax) {
        invCell_ = 1.0f / cellSize;
        min_ = domainMin;
        const auto cellsAlong = [this](float extent) { return std::max(1, static_cast<int>(extent * invCell_) + 1); };
        nx_ = cellsAlong(domainMax.x - domainMin.x);
        ny_ = cellsAlong(domainMax.y - domainMin.y);
        nz_ = cellsAlong(domainMax.z - domainMin.z);
        const auto cellCount = static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_) * static_cast<std::size_t>(nz_);

        cellOf_.resize(points.size());
        cellStart_.assign(cellCount + 1, 0);
        for (std::size_t i = 0; i < points.size(); ++i) {
            const std::uint32_t c = cellIndex(points[i]);
            cellOf_[i] = c;
            ++cellStart_[c + 1];
        }
        std::inclusive_scan(cellStart_.begin() + 1, cellStart_.end(), cellStart_.begin() + 1);

        cursor_.assign(cellStart_.begin(), cellStart_.end() - 1);
        order_.resize(points.size());
        for (std::size_t i = 0; i < points.size(); ++i) order_[cursor_[cellOf_[i]]++] = static_cast<std::uint32_t>(i);
        return order_;
    }

    // Calls fn(first, last) for the index ranges covering the 3x3x3 cells around p.
    // Valid once the points are stored in the order returned by build().
    template <class Fn>
    void forEachNeighborRange(const Vec3& p, Fn&& fn) const {
        const int cx = cellCoord(p.x - min_.x, nx_);
        const int cy = cellCoord(p.y - min_.y, ny_);
        const int cz = cellCoord(p.z - min_.z, nz_);
        const int x0 = std::max(cx - 1, 0), x1 = std::min(cx + 1, nx_ - 1);
        const int y0 = std::max(cy - 1, 0), y1 = std::min(cy + 1, ny_ - 1);
        const int z0 = std::max(cz - 1, 0), z1 = std::min(cz + 1, nz_ - 1);
        for (int z = z0; z <= z1; ++z)
            for (int y = y0; y <= y1; ++y) {
                const auto row = static_cast<std::size_t>((z * ny_ + y) * nx_);
                fn(cellStart_[row + static_cast<std::size_t>(x0)], cellStart_[row + static_cast<std::size_t>(x1) + 1]);
            }
    }

private:
    int cellCoord(float offset, int cells) const {
        return std::clamp(static_cast<int>(offset * invCell_), 0, cells - 1);
    }

    std::uint32_t cellIndex(const Vec3& p) const {
        const int cx = cellCoord(p.x - min_.x, nx_);
        const int cy = cellCoord(p.y - min_.y, ny_);
        const int cz = cellCoord(p.z - min_.z, nz_);
        return static_cast<std::uint32_t>((cz * ny_ + cy) * nx_ + cx);
    }

    float invCell_ = 1.0f;
    Vec3 min_{};
    int nx_ = 1, ny_ = 1, nz_ = 1;
    std::vector<std::uint32_t> cellOf_;
    std::vector<std::uint32_t> cellStart_;  // cellStart_[c]..cellStart_[c + 1] = particles of cell c
    std::vector<std::uint32_t> cursor_;
    std::vector<std::uint32_t> order_;
};
