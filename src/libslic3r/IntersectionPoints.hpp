#ifndef slic3r_IntersectionPoints_hpp_
#define slic3r_IntersectionPoints_hpp_

#include "ExPolygon.hpp"

namespace Slic3r {

// collect all intersecting points
//FIXME O(n^2) complexity!
Pointfs intersection_points(const Lines &lines);
Pointfs intersection_points(const Polygon &polygon);
Pointfs intersection_points(const Polygons &polygons);
Pointfs intersection_points(const ExPolygon &expolygon);
Pointfs intersection_points(const ExPolygons &expolygons);

} // namespace Slic3r
#endif // slic3r_IntersectionPoints_hpp_