#ifndef SRC_LIBSLIC3R_TRIANGLESETSAMPLING_HPP_
#define SRC_LIBSLIC3R_TRIANGLESETSAMPLING_HPP_

#include <admesh/stl.h>
#include <stddef.h>
#include <vector>
#include <cstddef>

#include "libslic3r/Point.hpp"

struct indexed_triangle_set;

namespace Slic3r {

struct TriangleSetSamples {
    float total_area;
    std::vector<Vec3f> positions;
    std::vector<Vec3f> normals;
    std::vector<size_t> triangle_indices;
};

TriangleSetSamples sample_its_uniform_parallel(size_t samples_count, const indexed_triangle_set &triangle_set);

}

#endif /* SRC_LIBSLIC3R_TRIANGLESETSAMPLING_HPP_ */
