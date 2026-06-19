#pragma once
#include "vec3.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Uniform spatial-hash grid for neighbour search. Cell size = smoothing radius,
// so all neighbours of a particle lie in the 3x3x3 block of cells around it.
// Built each step with a counting sort: O(N).
class Grid {
public:
    void rebuild(const std::vector<Vec3>& pos, float cellSize) {
        cell_ = cellSize;
        inv_ = 1.0f / cellSize;
        const int n = static_cast<int>(pos.size());

        // bounding box
        min_ = pos.empty() ? Vec3{} : pos[0];
        Vec3 max = min_;
        for (const auto& p : pos) {
            min_.x = std::min(min_.x, p.x); max.x = std::max(max.x, p.x);
            min_.y = std::min(min_.y, p.y); max.y = std::max(max.y, p.y);
            min_.z = std::min(min_.z, p.z); max.z = std::max(max.z, p.z);
        }
        dx_ = std::max(1, static_cast<int>((max.x - min_.x) * inv_) + 1);
        dy_ = std::max(1, static_cast<int>((max.y - min_.y) * inv_) + 1);
        dz_ = std::max(1, static_cast<int>((max.z - min_.z) * inv_) + 1);
        const int cells = dx_ * dy_ * dz_;

        cellOf_.resize(n);
        cellStart_.assign(cells + 1, 0);
        for (int i = 0; i < n; ++i) {
            int c = cellIndex(pos[i]);
            cellOf_[i] = c;
            ++cellStart_[c + 1];
        }
        for (int c = 0; c < cells; ++c) cellStart_[c + 1] += cellStart_[c];

        sorted_.resize(n);
        std::vector<int> cursor(cellStart_.begin(), cellStart_.end() - 1);
        for (int i = 0; i < n; ++i) sorted_[cursor[cellOf_[i]]++] = i;
    }

    // Invoke fn(j) for every particle j whose cell is within the 3x3x3 block
    // around `p`. The caller does the distance test.
    template <class Fn>
    void forEachNeighbor(const Vec3& p, Fn&& fn) const {
        int cx = clampI(static_cast<int>((p.x - min_.x) * inv_), dx_);
        int cy = clampI(static_cast<int>((p.y - min_.y) * inv_), dy_);
        int cz = clampI(static_cast<int>((p.z - min_.z) * inv_), dz_);
        for (int gz = std::max(0, cz - 1); gz <= std::min(dz_ - 1, cz + 1); ++gz)
        for (int gy = std::max(0, cy - 1); gy <= std::min(dy_ - 1, cy + 1); ++gy)
        for (int gx = std::max(0, cx - 1); gx <= std::min(dx_ - 1, cx + 1); ++gx) {
            int c = (gz * dy_ + gy) * dx_ + gx;
            for (int k = cellStart_[c]; k < cellStart_[c + 1]; ++k) fn(sorted_[k]);
        }
    }

private:
    int cellIndex(const Vec3& p) const {
        int cx = clampI(static_cast<int>((p.x - min_.x) * inv_), dx_);
        int cy = clampI(static_cast<int>((p.y - min_.y) * inv_), dy_);
        int cz = clampI(static_cast<int>((p.z - min_.z) * inv_), dz_);
        return (cz * dy_ + cy) * dx_ + cx;
    }
    static int clampI(int v, int hi) { return v < 0 ? 0 : (v >= hi ? hi - 1 : v); }

    float cell_ = 1, inv_ = 1;
    Vec3 min_;
    int dx_ = 1, dy_ = 1, dz_ = 1;
    std::vector<int> cellOf_;
    std::vector<int> cellStart_;
    std::vector<int> sorted_;
};
