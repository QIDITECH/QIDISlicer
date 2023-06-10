#ifndef slic3r_NSVGUtils_hpp_
#define slic3r_NSVGUtils_hpp_

#include "Polygon.hpp"
#include "ExPolygon.hpp"
#include "nanosvg/nanosvg.h"    // load SVG file

namespace Slic3r {

// Helper function to work with nano svg
class NSVGUtils
{
public:
    NSVGUtils() = delete;

    // inspired by nanosvgrast.h function nsvgRasterize->nsvg__flattenShape
    static void flatten_cubic_bez(Polygon &polygon,
                                  float    tessTol,
                                  Vec2f    p1,
                                  Vec2f    p2,
                                  Vec2f    p3,
                                  Vec2f    p4,
                                  int      level);
    // convert svg image to ExPolygons
    static ExPolygons to_ExPolygons(NSVGimage *image,
                                    float      tessTol   = 10.,
                                    int        max_level = 10);
    // convert svg paths to Polygons
    static Polygons to_polygons(NSVGimage *image,
                                float      tessTol   = 10.,
                                int        max_level = 10);
};
} // namespace Slic3r
#endif // slic3r_NSVGUtils_hpp_
