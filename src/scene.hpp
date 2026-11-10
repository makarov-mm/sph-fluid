#pragma once
#include "sph.hpp"

#include <memory>
#include <vector>

class ThreadPool;

// The viewer's scenario: a dam break in a 34 x 34 x 24 tank, optionally with a
// solid sphere in the path of the collapsing column.
std::unique_ptr<Sph> createDamBreak(bool withObstacle, ThreadPool& pool);

// Line-list vertices (x, y, z per vertex): the tank edges and, if there is an
// obstacle, three great circles of the sphere.
std::vector<float> buildSceneWireframe(const SphParams& params);
