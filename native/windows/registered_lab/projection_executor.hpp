#pragma once
#include "raster_executor.hpp"
namespace nr::raster {
using ClipPosition = std::array<float, 4>;
struct Viewport { float x, y, width, height, min_depth, max_depth; };
struct ProjectedVertex {
    Point snapped;
    std::array<float, 2> screen;
    float depth, inverse_w;
};
class ProjectionExecutor {
    struct State;
    std::unique_ptr<State> state;
public:
    ProjectionExecutor(uint32_t vertices, const std::string &backend);
    ~ProjectionExecutor();
    ProjectionExecutor(const ProjectionExecutor &) = delete;
    ProjectionExecutor &operator=(const ProjectionExecutor &) = delete;
    struct Result { std::vector<ProjectedVertex> vertices; std::string execution_device; };
    // Finite, already depth-clipped vertices with positive w. Clipping itself
    // is not implemented. Host validation rejects unsupported requests.
    Result execute(const std::vector<ClipPosition> &vertices, const Viewport &viewport);
};
}
