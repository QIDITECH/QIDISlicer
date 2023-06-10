#include "IntersectionPoints.hpp"

//#define USE_CGAL_SWEEP_LINE
#ifdef USE_CGAL_SWEEP_LINE

#include <CGAL/Cartesian.h>
#include <CGAL/MP_Float.h>
#include <CGAL/Quotient.h>
#include <CGAL/Arr_segment_traits_2.h>
#include <CGAL/Sweep_line_2_algorithms.h>

using NT       = CGAL::Quotient<CGAL::MP_Float>;
using Kernel   = CGAL::Cartesian<NT>;
using P2       = Kernel::Point_2;
using Traits_2 = CGAL::Arr_segment_traits_2<Kernel>;
using Segment  = Traits_2::Curve_2;
using Segments = std::vector<Segment>;

namespace priv {

P2            convert(const Slic3r::Point &p) { return P2(p.x(), p.y()); }
Slic3r::Vec2d convert(const P2 &p)
{
    return Slic3r::Vec2d(CGAL::to_double(p.x()), CGAL::to_double(p.y()));
}

Slic3r::Pointfs compute_intersections(const Segments &segments)
{
    std::vector<P2> intersections;
    // Compute all intersection points.
    CGAL::compute_intersection_points(segments.begin(), segments.end(),
                                      std::back_inserter(intersections));
    if (intersections.empty()) return {};
    Slic3r::Pointfs pts;
    pts.reserve(intersections.size());
    for (const P2 &p : intersections) pts.push_back(convert(p));
    return pts;
}

void add_polygon(const Slic3r::Polygon &polygon, Segments &segments)
{
    if (polygon.points.size() < 2) return;
    P2 prev_point = priv::convert(polygon.last_point());
    for (const Slic3r::Point &p : polygon.points) {
        P2 act_point = priv::convert(p);
        if (prev_point == act_point) continue;
        segments.emplace_back(prev_point, act_point);
        prev_point = act_point;
    }
}
Slic3r::Pointfs Slic3r::intersection_points(const Lines &lines)
{
    return priv::compute_intersections2(lines);
    Segments segments;
    segments.reserve(lines.size());
    for (Line l : lines)
        segments.emplace_back(priv::convert(l.a), priv::convert(l.b));
    return priv::compute_intersections(segments);
}

Slic3r::Pointfs Slic3r::intersection_points(const Polygon &polygon)
{
    Segments segments;
    segments.reserve(polygon.points.size());
    priv::add_polygon(polygon, segments);
    return priv::compute_intersections(segments);
}

Slic3r::Pointfs Slic3r::intersection_points(const Polygons &polygons)
{
    Segments segments;
    segments.reserve(count_points(polygons));
    for (const Polygon &polygon : polygons)
        priv::add_polygon(polygon, segments);
    return priv::compute_intersections(segments);
}

Slic3r::Pointfs Slic3r::intersection_points(const ExPolygon &expolygon)
{
    Segments segments;
    segments.reserve(count_points(expolygon));
    priv::add_polygon(expolygon.contour, segments);
    for (const Polygon &hole : expolygon.holes)
        priv::add_polygon(hole, segments);
    return priv::compute_intersections(segments);
}

Slic3r::Pointfs Slic3r::intersection_points(const ExPolygons &expolygons)
{
    Segments segments;
    segments.reserve(count_points(expolygons));
    for (const ExPolygon &expolygon : expolygons) {
        priv::add_polygon(expolygon.contour, segments);
        for (const Polygon &hole : expolygon.holes)
            priv::add_polygon(hole, segments);
    }
    return priv::compute_intersections(segments);
}


} // namespace priv

#else // USE_CGAL_SWEEP_LINE

// use bounding boxes
#include <libslic3r/BoundingBox.hpp>

namespace priv {
//FIXME O(n^2) complexity!
Slic3r::Pointfs compute_intersections(const Slic3r::Lines &lines)
{
    using namespace Slic3r;
    // IMPROVE0: BoundingBoxes of Polygons
    // IMPROVE1: Polygon's neighbor lines can't intersect
    // e.g. use indices to Point to find same points
    // IMPROVE2: Use BentleyOttmann algorithm
    // https://doc.cgal.org/latest/Surface_sweep_2/index.html -- CGAL implementation is significantly slower
    // https://stackoverflow.com/questions/4407493/is-there-a-robust-c-implementation-of-the-bentley-ottmann-algorithm
    Pointfs pts;
    Point   i;
    for (size_t li = 0; li < lines.size(); ++li) {
        const Line  &l = lines[li];
        const Point &a = l.a;
        const Point &b = l.b;
        Point min(std::min(a.x(), b.x()), std::min(a.y(), b.y()));
        Point max(std::max(a.x(), b.x()), std::max(a.y(), b.y()));
        BoundingBox  bb(min, max);
        for (size_t li_ = li + 1; li_ < lines.size(); ++li_) {
            const Line  &l_ = lines[li_];
            const Point &a_ = l_.a;
            const Point &b_ = l_.b;
            if (a == b_ || b == a_ || a == a_ || b == b_) continue;
            Point min_(std::min(a_.x(), b_.x()), std::min(a_.y(), b_.y()));
            Point max_(std::max(a_.x(), b_.x()), std::max(a_.y(), b_.y()));
            BoundingBox bb_(min_, max_);
            // intersect of BB compare min max
            if (bb.overlap(bb_) &&
                l.intersection(l_, &i))
                pts.push_back(i.cast<double>());
        }
    }
    return pts;
}
} // namespace priv

Slic3r::Pointfs Slic3r::intersection_points(const Lines &lines)
{
    return priv::compute_intersections(lines);
}

Slic3r::Pointfs Slic3r::intersection_points(const Polygon &polygon)
{
    return priv::compute_intersections(to_lines(polygon));
}

Slic3r::Pointfs Slic3r::intersection_points(const Polygons &polygons)
{
    return priv::compute_intersections(to_lines(polygons));
}

Slic3r::Pointfs Slic3r::intersection_points(const ExPolygon &expolygon)
{
    return priv::compute_intersections(to_lines(expolygon));
}

Slic3r::Pointfs Slic3r::intersection_points(const ExPolygons &expolygons)
{
    return priv::compute_intersections(to_lines(expolygons));
}

#endif // USE_CGAL_SWEEP_LINE
