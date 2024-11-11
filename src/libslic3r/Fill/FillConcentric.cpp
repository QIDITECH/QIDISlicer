#include <algorithm>
#include <vector>
#include <cassert>
#include <cstddef>

#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "libslic3r/Arachne/WallToolPaths.hpp"
#include "FillConcentric.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

void FillConcentric::_fill_surface_single(
    const FillParams                &params,
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction,
    ExPolygon                        expolygon,
    Polylines                       &polylines_out)
{
    // no rotation is supported for this infill pattern
    BoundingBox bounding_box = expolygon.contour.bounding_box();

    coord_t min_spacing = scaled<coord_t>(this->spacing);
    coord_t distance    = coord_t(min_spacing / params.density);

    if (params.density > 0.9999f && !params.dont_adjust) {
        distance      = Slic3r::FillConcentric::_adjust_solid_spacing(bounding_box.size()(0), distance);
        this->spacing = unscale<double>(distance);
    }

    Polygons   loops = to_polygons(expolygon);
    ExPolygons last { std::move(expolygon) };
    while (! last.empty()) {
        last = offset2_ex(last, -(distance + min_spacing/2), +min_spacing/2);
        append(loops, to_polygons(last));
    }

    // generate paths from the outermost to the innermost, to avoid
    // adhesion problems of the first central tiny loops
    loops = union_pt_chained_outside_in(loops);

    // split paths using a nearest neighbor search
    size_t iPathFirst = polylines_out.size();
    Point last_pos(0, 0);
    for (const Polygon &loop : loops) {
        polylines_out.emplace_back(loop.split_at_index(nearest_point_index(loop.points, last_pos)));
        last_pos = polylines_out.back().last_point();
    }

    // clip the paths to prevent the extruder from getting exactly on the first point of the loop
    // Keep valid paths only.
    size_t j = iPathFirst;
    for (size_t i = iPathFirst; i < polylines_out.size(); ++i) {
        polylines_out[i].clip_end(this->loop_clipping);
        if (polylines_out[i].is_valid()) {
            if (params.prefer_clockwise_movements)
                polylines_out[i].reverse();

            if (j < i)
                polylines_out[j] = std::move(polylines_out[i]);

            ++j;
        }
    }

    if (j < polylines_out.size())
        polylines_out.erase(polylines_out.begin() + int(j), polylines_out.end());
    //TODO: return ExtrusionLoop objects to get better chained paths,
    // otherwise the outermost loop starts at the closest point to (0, 0).
    // We want the loops to be split inside the G-code generator to get optimum path planning.
}

void FillConcentric::_fill_surface_single(const FillParams              &params,
                                          unsigned int                   thickness_layers,
                                          const std::pair<float, Point> &direction,
                                          ExPolygon                      expolygon,
                                          ThickPolylines                &thick_polylines_out)
{
    assert(params.use_arachne);
    assert(this->print_config != nullptr && this->print_object_config != nullptr);

    // no rotation is supported for this infill pattern
    Point   bbox_size   = expolygon.contour.bounding_box().size();
    coord_t min_spacing = scaled<coord_t>(this->spacing);

    if (params.density > 0.9999f && !params.dont_adjust) {
        coord_t                loops_count = std::max(bbox_size.x(), bbox_size.y()) / min_spacing + 1;
        Polygons               polygons    = offset(expolygon, float(min_spacing) / 2.f);
        Arachne::WallToolPaths wallToolPaths(polygons, min_spacing, min_spacing, loops_count, 0, params.layer_height, *this->print_object_config, *this->print_config);

        std::vector<Arachne::VariableWidthLines>    loops = wallToolPaths.getToolPaths();
        std::vector<const Arachne::ExtrusionLine *> all_extrusions;
        for (Arachne::VariableWidthLines &loop : loops) {
            if (loop.empty())
                continue;

            for (const Arachne::ExtrusionLine &wall : loop)
                all_extrusions.emplace_back(&wall);
        }

        // Split paths using a nearest neighbor search.
        size_t firts_poly_idx = thick_polylines_out.size();
        Point  last_pos(0, 0);
        for (const Arachne::ExtrusionLine *extrusion : all_extrusions) {
            if (extrusion->empty())
                continue;

            ThickPolyline thick_polyline = Arachne::to_thick_polyline(*extrusion);
            if (extrusion->is_closed) {
                // Arachne produces contour with clockwise orientation and holes with counterclockwise orientation.
                if (const bool extrusion_reverse = params.prefer_clockwise_movements ? !extrusion->is_contour() : extrusion->is_contour(); extrusion_reverse)
                    thick_polyline.reverse();

                thick_polyline.start_at_index(nearest_point_index(thick_polyline.points, last_pos));
            }

            thick_polylines_out.emplace_back(std::move(thick_polyline));
            last_pos = thick_polylines_out.back().last_point();
        }

        // clip the paths to prevent the extruder from getting exactly on the first point of the loop
        // Keep valid paths only.
        size_t j = firts_poly_idx;
        for (size_t i = firts_poly_idx; i < thick_polylines_out.size(); ++i) {
            thick_polylines_out[i].clip_end(this->loop_clipping);
            if (thick_polylines_out[i].is_valid()) {
                if (j < i)
                    thick_polylines_out[j] = std::move(thick_polylines_out[i]);
                ++j;
            }
        }
        if (j < thick_polylines_out.size())
            thick_polylines_out.erase(thick_polylines_out.begin() + int(j), thick_polylines_out.end());
    } else {
        Polylines polylines;
        this->_fill_surface_single(params, thickness_layers, direction, expolygon, polylines);
        append(thick_polylines_out, to_thick_polylines(std::move(polylines), min_spacing));
    }
}

} // namespace Slic3r
