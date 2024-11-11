
#ifndef NFPCONCAVE_TESSELATE_HPP
#define NFPCONCAVE_TESSELATE_HPP

#include <libslic3r/ExPolygon.hpp>

#include "libslic3r/Polygon.hpp"

namespace Slic3r {

Polygons convex_decomposition_tess(const Polygon &expoly);
Polygons convex_decomposition_tess(const ExPolygon &expoly);
Polygons convex_decomposition_tess(const ExPolygons &expolys);
ExPolygons nfp_concave_concave_tess(const ExPolygon &fixed, const ExPolygon &movable);

} // namespace Slic3r

#endif // NFPCONCAVE_TESSELATE_HPP
