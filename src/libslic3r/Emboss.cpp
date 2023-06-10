#include "Emboss.hpp"
#include <stdio.h>
#include <cstdlib>
#include <boost/nowide/convert.hpp>
#include <boost/log/trivial.hpp>
#include <ClipperUtils.hpp> // union_ex + for boldness(polygon extend(offset))
#include "IntersectionPoints.hpp"

#define STB_TRUETYPE_IMPLEMENTATION // force following include to generate implementation
#include "imgui/imstb_truetype.h" // stbtt_fontinfo
#include "Utils.hpp" // ScopeGuard

#include <Triangulation.hpp> // CGAL project
#include "libslic3r.h"

// to heal shape
#include "ExPolygonsIndex.hpp"
#include "libslic3r/AABBTreeLines.hpp" // search structure for found close points
#include "libslic3r/Line.hpp"
#include "libslic3r/BoundingBox.hpp"

using namespace Slic3r;
using namespace Emboss;
using fontinfo_opt = std::optional<stbtt_fontinfo>;

// for try approach to heal shape by Clipper::Closing
//#define HEAL_WITH_CLOSING

// functionality to remove all spikes from shape
//#define REMOVE_SPIKES

// do not expose out of this file stbtt_ data types
namespace priv{
using Polygon = Slic3r::Polygon;
bool is_valid(const FontFile &font, unsigned int index);
fontinfo_opt load_font_info(const unsigned char *data, unsigned int index = 0);
std::optional<Glyph> get_glyph(const stbtt_fontinfo &font_info, int unicode_letter, float flatness);

// take glyph from cache
const Glyph* get_glyph(int unicode, const FontFile &font, const FontProp &font_prop, 
        Glyphs &cache, fontinfo_opt &font_info_opt);

EmbossStyle create_style(std::wstring name, std::wstring path);

// scale and convert float to int coordinate
Point to_point(const stbtt__point &point);

// bad is contour smaller than 3 points
void remove_bad(Polygons &polygons);
void remove_bad(ExPolygons &expolygons);

// helpr for heal shape
// Return true when erase otherwise false
bool remove_same_neighbor(Polygon &points);
bool remove_same_neighbor(Polygons &polygons);
bool remove_same_neighbor(ExPolygons &expolygons);

// Try to remove self intersection by subtracting rect 2x2 px
bool remove_self_intersections(ExPolygons &shape, unsigned max_iteration = 10);
ExPolygon create_bounding_rect(const ExPolygons &shape);

void remove_small_islands(ExPolygons &shape, double minimal_area);

// NOTE: expolygons can't contain same_neighbor
Points collect_close_points(const ExPolygons &expolygons, double distance = .6);

// Heal duplicates points and self intersections
bool heal_dupl_inter(ExPolygons &shape, unsigned max_iteration);

// for debug purpose
void visualize_heal(const std::string& svg_filepath, const ExPolygons &expolygons);

const Points pts_2x2({Point(0, 0), Point(1, 0), Point(1, 1), Point(0, 1)});
const Points pts_3x3({Point(-1, -1), Point(1, -1), Point(1, 1), Point(-1, 1)});

struct SpikeDesc
{
    // cosinus of max spike angle
    double cos_angle; // speed up to skip acos

    // Half of Wanted bevel size
    double half_bevel; 

    /// <summary>
    /// Calculate spike description
    /// </summary>
    /// <param name="bevel_size">Size of spike width after cut of the tip, has to be grater than 2.5</param>
    /// <param name="pixel_spike_length">When spike has same or more pixels with width less than 1 pixel</param>
    SpikeDesc(double bevel_size, double pixel_spike_length = 6)
    {
        // create min angle given by spike_length
        // Use it as minimal height of 1 pixel base spike
        double angle  = 2. * atan2(pixel_spike_length, .5); // [rad]
        cos_angle = std::fabs(cos(angle));

        // When remove spike this angle is set.
        // Value must be grater than min_angle
        half_bevel = bevel_size / 2;
    }
};

// return TRUE when remove point. It could create polygon with 2 points.
bool remove_when_spike(Polygon &polygon, size_t index, const SpikeDesc &spike_desc);
void remove_spikes_in_duplicates(ExPolygons &expolygons, const Points &duplicates);

#ifdef REMOVE_SPIKES
// Remove long sharp corners aka spikes 
// by adding points to bevel tip of spikes - Not printable parts
// Try to not modify long sides of spike and add points on it's side
void remove_spikes(Polygon &polygon, const SpikeDesc &spike_desc);
void remove_spikes(Polygons &polygons, const SpikeDesc &spike_desc);
void remove_spikes(ExPolygons &expolygons, const SpikeDesc &spike_desc);
#endif

};

bool priv::remove_when_spike(Polygon &polygon, size_t index, const SpikeDesc &spike_desc) {

    std::optional<Point> add;
    bool do_erase = false;
    Points &pts = polygon.points;
    {
        size_t  pts_size = pts.size();
        if (pts_size < 3)
            return false;

        const Point &a = (index == 0) ? pts.back() : pts[index - 1];
        const Point &b = pts[index];
        const Point &c = (index == (pts_size - 1)) ? pts.front() : pts[index + 1];

        // calc sides
        Vec2d ba = (a - b).cast<double>();
        Vec2d bc = (c - b).cast<double>();

        double dot_product = ba.dot(bc);

        // sqrt together after multiplication save one sqrt
        double ba_size_sq = ba.squaredNorm();
        double bc_size_sq = bc.squaredNorm();
        double norm       = sqrt(ba_size_sq * bc_size_sq);
        double cos_angle  = dot_product / norm;

        // small angle are around 1 --> cos(0) = 1
        if (cos_angle < spike_desc.cos_angle)
            return false; // not a spike

        // has to be in range <-1, 1>
        // Due to preccission of floating point number could be sligtly out of range
        if (cos_angle > 1.)
            cos_angle = 1.;
        // if (cos_angle < -1.)
        //     cos_angle = -1.;

        // Current Spike angle
        double angle          = acos(cos_angle);
        double wanted_size    = spike_desc.half_bevel / cos(angle / 2.);
        double wanted_size_sq = wanted_size * wanted_size;

        bool is_ba_short = ba_size_sq < wanted_size_sq;
        bool is_bc_short = bc_size_sq < wanted_size_sq;

        auto a_side = [&b, &ba, &ba_size_sq, &wanted_size]() -> Point {
            Vec2d ba_norm = ba / sqrt(ba_size_sq);
            return b + (wanted_size * ba_norm).cast<coord_t>();
        };
        auto c_side = [&b, &bc, &bc_size_sq, &wanted_size]() -> Point {
            Vec2d bc_norm = bc / sqrt(bc_size_sq);
            return b + (wanted_size * bc_norm).cast<coord_t>();
        };

        if (is_ba_short && is_bc_short) {
            // remove short spike
            do_erase = true;
        } else if (is_ba_short) {
            // move point B on C-side
            pts[index] = c_side();
        } else if (is_bc_short) {
            // move point B on A-side
            pts[index] = a_side();
        } else {
            // move point B on C-side and add point on A-side(left - before)
            pts[index] = c_side();
            add = a_side();
            if (*add == pts[index]) {
                // should be very rare, when SpikeDesc has small base
                // will be fixed by remove B point
                add.reset();
                do_erase = true;
            }
        }
    }
    if (do_erase) {
        pts.erase(pts.begin() + index);
        return true;
    }
    if (add.has_value())
        pts.insert(pts.begin() + index, *add);
    return false;
}

void priv::remove_spikes_in_duplicates(ExPolygons &expolygons, const Points &duplicates) { 

    auto check = [](Polygon &polygon, const Point &d) -> bool {
        double spike_bevel = 1 / SHAPE_SCALE;
        double spike_length = 5.;
        const static SpikeDesc sd(spike_bevel, spike_length);
        Points& pts = polygon.points;
        bool exist_remove = false;
        for (size_t i = 0; i < pts.size(); i++) {
            if (pts[i] != d)
                continue;
            exist_remove |= remove_when_spike(polygon, i, sd);
        }
        return exist_remove && pts.size() < 3;
    };

    bool exist_remove = false;
    for (ExPolygon &expolygon : expolygons) {
        BoundingBox bb(to_points(expolygon.contour));
        for (const Point &d : duplicates) {
            if (!bb.contains(d))
                continue;
            exist_remove |= check(expolygon.contour, d);
            for (Polygon &hole : expolygon.holes)
                exist_remove |= check(hole, d);
        }
    }

    if (exist_remove)
        remove_bad(expolygons);
}

bool priv::is_valid(const FontFile &font, unsigned int index) {
    if (font.data == nullptr) return false;
    if (font.data->empty()) return false;
    if (index >= font.infos.size()) return false;
    return true;
}

fontinfo_opt priv::load_font_info(
    const unsigned char *data, unsigned int index)
{
    int font_offset = stbtt_GetFontOffsetForIndex(data, index);
    if (font_offset < 0) {
        assert(false);
        // "Font index(" << index << ") doesn't exist.";
        return {};        
    }
    stbtt_fontinfo font_info;
    if (stbtt_InitFont(&font_info, data, font_offset) == 0) {
        // Can't initialize font.
        assert(false);
        return {};
    }
    return font_info;
}

void priv::remove_bad(Polygons &polygons) {
    polygons.erase(
        std::remove_if(polygons.begin(), polygons.end(), 
            [](const Polygon &p) { return p.size() < 3; }), 
        polygons.end());
}

void priv::remove_bad(ExPolygons &expolygons) {
    expolygons.erase(
        std::remove_if(expolygons.begin(), expolygons.end(), 
            [](const ExPolygon &p) { return p.contour.size() < 3; }),
        expolygons.end());

    for (ExPolygon &expolygon : expolygons)
         remove_bad(expolygon.holes);
}

bool priv::remove_same_neighbor(Slic3r::Polygon &polygon)
{
    Points &points = polygon.points;
    if (points.empty()) return false;
    auto last = std::unique(points.begin(), points.end());
    
    // remove first and last neighbor duplication
    if (const Point& last_point = *(last - 1);
        last_point == points.front()) {
         --last;
    }

    // no duplicits
    if (last == points.end()) return false;

    points.erase(last, points.end());
    return true;
}

bool priv::remove_same_neighbor(Polygons &polygons) {
    if (polygons.empty()) return false;
    bool exist = false;
    for (Polygon& polygon : polygons) 
        exist |= remove_same_neighbor(polygon);
    // remove empty polygons
    polygons.erase(
        std::remove_if(polygons.begin(), polygons.end(), 
            [](const Polygon &p) { return p.points.size() <= 2; }),
        polygons.end());
    return exist;
}

bool priv::remove_same_neighbor(ExPolygons &expolygons) {
    if(expolygons.empty()) return false;
    bool remove_from_holes = false;
    bool remove_from_contour = false;
    for (ExPolygon &expoly : expolygons) {
        remove_from_contour |= remove_same_neighbor(expoly.contour);
        remove_from_holes |= remove_same_neighbor(expoly.holes);
    }
    // Removing of expolygons without contour
    if (remove_from_contour)
        expolygons.erase(
            std::remove_if(expolygons.begin(), expolygons.end(),
                [](const ExPolygon &p) { return p.contour.points.size() <=2; }),
            expolygons.end());
    return remove_from_holes || remove_from_contour;
}

Points priv::collect_close_points(const ExPolygons &expolygons, double distance) { 
    if (expolygons.empty()) return {};
    if (distance < 0.) return {};
    
    // IMPROVE: use int(insted of double) lines and tree
    const ExPolygonsIndices ids(expolygons);
    const std::vector<Linef> lines = Slic3r::to_linesf(expolygons, ids.get_count());
    AABBTreeIndirect::Tree<2, double> tree = AABBTreeLines::build_aabb_tree_over_indexed_lines(lines);
    // Result close points
    Points res; 
    size_t point_index = 0;
    auto collect_close = [&res, &point_index, &lines, &tree, &distance, &ids, &expolygons](const Points &pts) {
        for (const Point &p : pts) {
            Vec2d p_d = p.cast<double>();
            std::vector<size_t> close_lines = AABBTreeLines::all_lines_in_radius(lines, tree, p_d, distance);
            for (size_t index : close_lines) {
                // skip point neighbour lines indices
                if (index == point_index) continue;
                if (&p != &pts.front()) { 
                    if (index == point_index - 1) continue;
                } else if (index == (pts.size()-1)) continue;

                // do not doubled side point of segment
                const ExPolygonsIndex id = ids.cvt(index);
                const ExPolygon &expoly = expolygons[id.expolygons_index];
                const Polygon &poly = id.is_contour() ? expoly.contour : expoly.holes[id.hole_index()];
                const Points &poly_pts = poly.points;
                const Point &line_a = poly_pts[id.point_index];
                const Point &line_b = (!ids.is_last_point(id)) ? poly_pts[id.point_index + 1] : poly_pts.front();
                assert(line_a == lines[index].a.cast<int>());
                assert(line_b == lines[index].b.cast<int>());
                if (p == line_a || p == line_b) continue;
                res.push_back(p);
            }
            ++point_index;
        }
    };
    for (const ExPolygon &expoly : expolygons) { 
        collect_close(expoly.contour.points);
        for (const Polygon &hole : expoly.holes) 
            collect_close(hole.points);
    }
    if (res.empty()) return {};
    std::sort(res.begin(), res.end());
    // only unique points
    res.erase(std::unique(res.begin(), res.end()), res.end());
    return res;
}

bool Emboss::divide_segments_for_close_point(ExPolygons &expolygons, double distance)
{
    if (expolygons.empty()) return false;
    if (distance < 0.) return false;

    // ExPolygons can't contain same neigbours
    priv::remove_same_neighbor(expolygons);

    // IMPROVE: use int(insted of double) lines and tree
    const ExPolygonsIndices ids(expolygons);
    const std::vector<Linef> lines = Slic3r::to_linesf(expolygons, ids.get_count());
    AABBTreeIndirect::Tree<2, double> tree = AABBTreeLines::build_aabb_tree_over_indexed_lines(lines);
    using Div = std::pair<Point, size_t>;
    std::vector<Div> divs;
    size_t point_index = 0;
    auto check_points = [&divs, &point_index, &lines, &tree, &distance, &ids, &expolygons](const Points &pts) {
        for (const Point &p : pts) {
            Vec2d p_d = p.cast<double>();
            std::vector<size_t> close_lines = AABBTreeLines::all_lines_in_radius(lines, tree, p_d, distance);
            for (size_t index : close_lines) {
                // skip point neighbour lines indices
                if (index == point_index) continue;
                if (&p != &pts.front()) { 
                    if (index == point_index - 1) continue;
                } else if (index == (pts.size()-1)) continue;

                // do not doubled side point of segment
                const ExPolygonsIndex id = ids.cvt(index);
                const ExPolygon &expoly = expolygons[id.expolygons_index];
                const Polygon &poly = id.is_contour() ? expoly.contour : expoly.holes[id.hole_index()];
                const Points &poly_pts = poly.points;
                const Point &line_a = poly_pts[id.point_index];
                const Point &line_b = (!ids.is_last_point(id)) ? poly_pts[id.point_index + 1] : poly_pts.front();
                assert(line_a == lines[index].a.cast<int>());
                assert(line_b == lines[index].b.cast<int>());
                if (p == line_a || p == line_b) continue;

                divs.emplace_back(p, index);
            }
            ++point_index;
        }
    };
    for (const ExPolygon &expoly : expolygons) { 
        check_points(expoly.contour.points);
        for (const Polygon &hole : expoly.holes) 
            check_points(hole.points);
    }

    // check if exist division
    if (divs.empty()) return false;

    // sort from biggest index to zero
    // to be able add points and not interupt indices
    std::sort(divs.begin(), divs.end(), 
        [](const Div &d1, const Div &d2) { return d1.second > d2.second; });
    
    auto it = divs.begin();
    // divide close line
    while (it != divs.end()) {
        // colect division of a line segmen
        size_t index = it->second;
        auto it2 = it+1;
        while (it2 != divs.end() && it2->second == index) ++it2;

        ExPolygonsIndex id = ids.cvt(index);
        ExPolygon &expoly = expolygons[id.expolygons_index];
        Polygon &poly = id.is_contour() ? expoly.contour : expoly.holes[id.hole_index()];
        Points &pts = poly.points;        
        size_t count = it2 - it;

        // add points into polygon to divide in place of near point
        if (count == 1) {
            pts.insert(pts.begin() + id.point_index + 1, it->first);
            ++it;
        } else {
            // collect points to add into polygon
            Points points;
            points.reserve(count);
            for (; it < it2; ++it) 
                points.push_back(it->first);            

            // need sort by line direction
            const Linef &line = lines[index];
            Vec2d        dir  = line.b - line.a;
            // select mayorit direction
            int axis  = (abs(dir.x()) > abs(dir.y())) ? 0 : 1;
            using Fnc = std::function<bool(const Point &, const Point &)>;
            Fnc fnc   = (dir[axis] < 0) ? Fnc([axis](const Point &p1, const Point &p2) { return p1[axis] > p2[axis]; }) :
                                          Fnc([axis](const Point &p1, const Point &p2) { return p1[axis] < p2[axis]; }) ;
            std::sort(points.begin(), points.end(), fnc);

            // use only unique points
            points.erase(std::unique(points.begin(), points.end()), points.end());

            // divide line by adding points into polygon
            pts.insert(pts.begin() + id.point_index + 1,
                points.begin(), points.end());
        }
        assert(it == it2);
    }
    return true;
}

bool priv::remove_self_intersections(ExPolygons &shape, unsigned max_iteration) {
    if (shape.empty())
        return true;

    Pointfs intersections_f = intersection_points(shape);
    if (intersections_f.empty())
        return true;

    // create loop permanent memory
    Polygons holes;
    Points intersections;

    while (--max_iteration) {        
        // convert intersections into Points
        assert(intersections.empty());
        intersections.reserve(intersections_f.size());
        std::transform(intersections_f.begin(), intersections_f.end(), std::back_inserter(intersections),
                       [](const Vec2d &p) { return Point(std::floor(p.x()), std::floor(p.y())); });

        // intersections should be unique poits
        std::sort(intersections.begin(), intersections.end());
        auto it = std::unique(intersections.begin(), intersections.end());
        intersections.erase(it, intersections.end());

        assert(holes.empty());
        holes.reserve(intersections.size());

        // Fix self intersection in result by subtracting hole 2x2
        for (const Point &p : intersections) {
            Polygon hole(priv::pts_2x2);
            hole.translate(p);
            holes.push_back(hole);
        }
        // Union of overlapped holes is not neccessary
        // Clipper calculate winding number separately for each input parameter
        // if (holes.size() > 1) holes = Slic3r::union_(holes);
        shape = Slic3r::diff_ex(shape, holes, ApplySafetyOffset::Yes);
        
        // TODO: find where diff ex could create same neighbor
        priv::remove_same_neighbor(shape);

        // find new intersections made by diff_ex
        intersections_f = intersection_points(shape);
        if (intersections_f.empty())
            return true;
        else {
            // clear permanent vectors
            holes.clear();
            intersections.clear();
        }
    }
    assert(max_iteration == 0);
    assert(!intersections_f.empty());
    return false;
}

ExPolygons Emboss::heal_shape(const Polygons &shape)
{
    // When edit this code check that font 'ALIENATE.TTF' and glyph 'i' still work
    // fix of self intersections
    // http://www.angusj.com/delphi/clipper/documentation/Docs/Units/ClipperLib/Functions/SimplifyPolygon.htm
    ClipperLib::Paths paths = ClipperLib::SimplifyPolygons(ClipperUtils::PolygonsProvider(shape), ClipperLib::pftNonZero);
    const double clean_distance = 1.415; // little grater than sqrt(2)
    ClipperLib::CleanPolygons(paths, clean_distance);
    Polygons polygons = to_polygons(paths);
    polygons.erase(std::remove_if(polygons.begin(), polygons.end(), [](const Polygon &p) { return p.size() < 3; }), polygons.end());
                
    // Do not remove all duplicates but do it better way
    // Overlap all duplicit points by rectangle 3x3
    Points duplicits = collect_duplicates(to_points(polygons));
    if (!duplicits.empty()) {
        polygons.reserve(polygons.size() + duplicits.size());
        for (const Point &p : duplicits) {
            Polygon rect_3x3(priv::pts_3x3);
            rect_3x3.translate(p);
            polygons.push_back(rect_3x3);
        }
    }

    // TrueTypeFonts use non zero winding number
    // https://docs.microsoft.com/en-us/typography/opentype/spec/ttch01
    // https://developer.apple.com/fonts/TrueType-Reference-Manual/RM01/Chap1.html
    ExPolygons res = Slic3r::union_ex(polygons, ClipperLib::pftNonZero);        
    heal_shape(res);
    return res;
}

#include "libslic3r/SVG.hpp"
void priv::visualize_heal(const std::string &svg_filepath, const ExPolygons &expolygons) {
    Points pts = to_points(expolygons);
    BoundingBox bb(pts);
    //double svg_scale = SHAPE_SCALE / unscale<double>(1.);
    // bb.scale(svg_scale);
    SVG svg(svg_filepath, bb);
    svg.draw(expolygons);
    
    Points duplicits = collect_duplicates(pts);
    svg.draw(duplicits, "black", 7 / SHAPE_SCALE);

    Pointfs intersections_f = intersection_points(expolygons);
    Points intersections;
    intersections.reserve(intersections_f.size());
    std::transform(intersections_f.begin(), intersections_f.end(), std::back_inserter(intersections),
                   [](const Vec2d &p) { return p.cast<int>(); });
    svg.draw(intersections, "red", 8 / SHAPE_SCALE);
}

bool Emboss::heal_shape(ExPolygons &shape, unsigned max_iteration)
{
    return priv::heal_dupl_inter(shape, max_iteration);  
}

#ifndef HEAL_WITH_CLOSING
bool priv::heal_dupl_inter(ExPolygons &shape, unsigned max_iteration)
{    
    if (shape.empty()) return true;

    // create loop permanent memory
    Polygons holes;
    Points intersections;
    while (--max_iteration) {
        priv::remove_same_neighbor(shape);
        Pointfs intersections_f = intersection_points(shape);

        // convert intersections into Points
        assert(intersections.empty());
        intersections.reserve(intersections_f.size());
        std::transform(intersections_f.begin(), intersections_f.end(), std::back_inserter(intersections),
                       [](const Vec2d &p) { return Point(std::floor(p.x()), std::floor(p.y())); });

        // intersections should be unique poits
        std::sort(intersections.begin(), intersections.end());
        auto it = std::unique(intersections.begin(), intersections.end());
        intersections.erase(it, intersections.end());

        Points duplicates = collect_duplicates(to_points(shape));
        // duplicates are already uniqua and sorted

        // Check whether shape is already healed
        if (intersections.empty() && duplicates.empty())
            return true;

        assert(holes.empty());
        holes.reserve(intersections.size() + duplicates.size());

        remove_spikes_in_duplicates(shape, duplicates);

        // Fix self intersection in result by subtracting hole 2x2
        for (const Point &p : intersections) {
            Polygon hole(priv::pts_2x2);
            hole.translate(p);
            holes.push_back(hole);
        }

        // Fix duplicit points by hole 3x3 around duplicit point
        for (const Point &p : duplicates) {
            Polygon hole(priv::pts_3x3);
            hole.translate(p);
            holes.push_back(hole);
        }

        shape = Slic3r::diff_ex(shape, holes, ApplySafetyOffset::Yes);

        // prepare for next loop
        holes.clear();
        intersections.clear();
    }

    //priv::visualize_heal("C:/data/temp/heal.svg", shape);
    assert(false);
    shape = {priv::create_bounding_rect(shape)};
    return false;
}
#else
bool priv::heal_dupl_inter(ExPolygons &shape, unsigned max_iteration)
{
    priv::remove_same_neighbor(shape);

    const float                delta    = 2.f;
    const ClipperLib::JoinType joinType = ClipperLib::JoinType::jtRound;

    // remove double points
    while (max_iteration) {
        --max_iteration;

        // if(!priv::remove_self_intersections(shape, max_iteration)) break;
        shape = Slic3r::union_ex(shape);
        shape = Slic3r::closing_ex(shape, delta, joinType);

        // double minimal_area = 1000;
        // priv::remove_small_islands(shape, minimal_area);

        // check that duplicates and intersections do NOT exists
        Points  duplicits       = collect_duplicates(to_points(shape));
        Pointfs intersections_f = intersection_points(shape);
        if (duplicits.empty() && intersections_f.empty())
            return true;
    }

    // priv::visualize_heal("C:/data/temp/heal.svg", shape);
    assert(false);
    shape = {priv::create_bounding_rect(shape)};
    return false;
}
#endif // !HEAL_WITH_CLOSING

ExPolygon priv::create_bounding_rect(const ExPolygons &shape) {
    BoundingBox bb   = get_extents(shape);
    Point       size = bb.size();
    if (size.x() < 10)
        bb.max.x() += 10;
    if (size.y() < 10)
        bb.max.y() += 10;

    Polygon rect({// CCW
        bb.min,
        {bb.max.x(), bb.min.y()},
        bb.max,
        {bb.min.x(), bb.max.y()}});

    Point   offset = bb.size() * 0.1;
    Polygon hole({// CW
        bb.min + offset,
        {bb.min.x() + offset.x(), bb.max.y() - offset.y()},
        bb.max - offset,
        {bb.max.x() - offset.x(), bb.min.y() + offset.y()}});

    return ExPolygon(rect, hole);
}

void priv::remove_small_islands(ExPolygons &expolygons, double minimal_area) {
    if (expolygons.empty())
        return;

    // remove small expolygons contours
    auto expoly_it = std::remove_if(expolygons.begin(), expolygons.end(), 
        [&minimal_area](const ExPolygon &p) { return p.contour.area() < minimal_area; });
    expolygons.erase(expoly_it, expolygons.end());

    // remove small holes in expolygons
    for (ExPolygon &expoly : expolygons) {
        Polygons& holes = expoly.holes;
        auto it = std::remove_if(holes.begin(), holes.end(), 
            [&minimal_area](const Polygon &p) { return -p.area() < minimal_area; });
        holes.erase(it, holes.end());
    }
}

std::optional<Glyph> priv::get_glyph(const stbtt_fontinfo &font_info, int unicode_letter, float flatness)
{
    int glyph_index = stbtt_FindGlyphIndex(&font_info, unicode_letter);
    if (glyph_index == 0) {
        //wchar_t wchar = static_cast<wchar_t>(unicode_letter); 
        //<< "Character unicode letter ("
        //<< "decimal value = " << std::dec << unicode_letter << ", "
        //<< "hexadecimal value = U+" << std::hex << unicode_letter << std::dec << ", "
        //<< "wchar value = " << wchar
        //<< ") is NOT defined inside of the font. \n";
        return {};
    }

    Glyph glyph;
    stbtt_GetGlyphHMetrics(&font_info, glyph_index, &glyph.advance_width, &glyph.left_side_bearing);

    stbtt_vertex *vertices;
    int num_verts = stbtt_GetGlyphShape(&font_info, glyph_index, &vertices);
    if (num_verts <= 0) return glyph; // no shape
    ScopeGuard sg1([&vertices]() { free(vertices); });

    int *contour_lengths = NULL;
    int  num_countour_int = 0;
    stbtt__point *points = stbtt_FlattenCurves(vertices, num_verts,
        flatness, &contour_lengths, &num_countour_int, font_info.userdata);
    if (!points) return glyph; // no valid flattening
    ScopeGuard sg2([&contour_lengths, &points]() {
        free(contour_lengths); 
        free(points); 
    });

    size_t   num_contour = static_cast<size_t>(num_countour_int);
    Polygons glyph_polygons;
    glyph_polygons.reserve(num_contour);
    size_t pi = 0; // point index
    for (size_t ci = 0; ci < num_contour; ++ci) {
        int length = contour_lengths[ci];
        // check minimal length for triangle
        if (length < 4) {
            // weird font
            pi+=length;
            continue;
        }
        // last point is first point
        --length;
        Points pts;
        pts.reserve(length);
        for (int i = 0; i < length; ++i) 
            pts.emplace_back(to_point(points[pi++]));
        
        // last point is first point --> closed contour
        assert(pts.front() == to_point(points[pi]));
        ++pi;

        // change outer cw to ccw and inner ccw to cw order
        std::reverse(pts.begin(), pts.end());
        glyph_polygons.emplace_back(pts);
    }
    if (!glyph_polygons.empty())
        glyph.shape = Emboss::heal_shape(glyph_polygons);
    return glyph;
}

const Glyph* priv::get_glyph(
    int              unicode,
    const FontFile & font,
    const FontProp & font_prop,
    Glyphs &         cache,
    fontinfo_opt &font_info_opt)
{
    // TODO: Use resolution by printer configuration, or add it into FontProp
    const float RESOLUTION = 0.0125f; // [in mm]
    auto glyph_item = cache.find(unicode);
    if (glyph_item != cache.end()) return &glyph_item->second;

    unsigned int font_index = font_prop.collection_number.value_or(0);
    if (!is_valid(font, font_index)) return nullptr;

    if (!font_info_opt.has_value()) {
        
        font_info_opt  = priv::load_font_info(font.data->data(), font_index);
        // can load font info?
        if (!font_info_opt.has_value()) return nullptr;
    }

    float flatness = font.infos[font_index].ascent * RESOLUTION / font_prop.size_in_mm;

    // Fix for very small flatness because it create huge amount of points from curve
    if (flatness < RESOLUTION) flatness = RESOLUTION;

    std::optional<Glyph> glyph_opt =
        priv::get_glyph(*font_info_opt, unicode, flatness);

    // IMPROVE: multiple loadig glyph without data
    // has definition inside of font?
    if (!glyph_opt.has_value()) return nullptr;

    if (font_prop.char_gap.has_value()) 
        glyph_opt->advance_width += *font_prop.char_gap;

    // scale glyph size
    glyph_opt->advance_width = 
        static_cast<int>(glyph_opt->advance_width / SHAPE_SCALE);
    glyph_opt->left_side_bearing = 
        static_cast<int>(glyph_opt->left_side_bearing / SHAPE_SCALE);

    if (!glyph_opt->shape.empty()) {
        if (font_prop.boldness.has_value()) {
            float delta = *font_prop.boldness / SHAPE_SCALE /
                          font_prop.size_in_mm;
            glyph_opt->shape = Slic3r::union_ex(offset_ex(glyph_opt->shape, delta));
        }
        if (font_prop.skew.has_value()) {
            double ratio = *font_prop.skew;
            auto skew = [&ratio](Polygon &polygon) {
                for (Slic3r::Point &p : polygon.points)
                    p.x() += static_cast<Point::coord_type>(std::round(p.y() * ratio));
            };
            for (ExPolygon &expolygon : glyph_opt->shape) {
                skew(expolygon.contour);
                for (Polygon &hole : expolygon.holes) skew(hole);
            }
        }
    }
    auto it = cache.insert({unicode, std::move(*glyph_opt)});
    assert(it.second);
    return &it.first->second;
}

EmbossStyle priv::create_style(std::wstring name, std::wstring path) {
    return { boost::nowide::narrow(name.c_str()),
             boost::nowide::narrow(path.c_str()),
             EmbossStyle::Type::file_path, FontProp() };
}

Point priv::to_point(const stbtt__point &point) {
    return Point(static_cast<int>(std::round(point.x / SHAPE_SCALE)),
                 static_cast<int>(std::round(point.y / SHAPE_SCALE)));
}

#ifdef _WIN32
#include <windows.h>
#include <wingdi.h>
#include <windef.h>
#include <WinUser.h>

// Get system font file path
std::optional<std::wstring> Emboss::get_font_path(const std::wstring &font_face_name)
{
//    static const LPWSTR fontRegistryPath = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";
    static const LPCWSTR fontRegistryPath = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";
    HKEY hKey;
    LONG result;

    // Open Windows font registry key
    result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, fontRegistryPath, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) return {};    

    DWORD maxValueNameSize, maxValueDataSize;
    result = RegQueryInfoKey(hKey, 0, 0, 0, 0, 0, 0, 0, &maxValueNameSize, &maxValueDataSize, 0, 0);
    if (result != ERROR_SUCCESS) return {};

    DWORD valueIndex = 0;
    LPWSTR valueName = new WCHAR[maxValueNameSize];
    LPBYTE valueData = new BYTE[maxValueDataSize];
    DWORD valueNameSize, valueDataSize, valueType;
    std::wstring wsFontFile;

    // Look for a matching font name
    do {
        wsFontFile.clear();
        valueDataSize = maxValueDataSize;
        valueNameSize = maxValueNameSize;

        result = RegEnumValue(hKey, valueIndex, valueName, &valueNameSize, 0, &valueType, valueData, &valueDataSize);

        valueIndex++;
        if (result != ERROR_SUCCESS || valueType != REG_SZ) {
            continue;
        }

        std::wstring wsValueName(valueName, valueNameSize);

        // Found a match
        if (_wcsnicmp(font_face_name.c_str(), wsValueName.c_str(), font_face_name.length()) == 0) {

            wsFontFile.assign((LPWSTR)valueData, valueDataSize);
            break;
        }
    }while (result != ERROR_NO_MORE_ITEMS);

    delete[] valueName;
    delete[] valueData;

    RegCloseKey(hKey);

    if (wsFontFile.empty()) return {};
    
    // Build full font file path
    WCHAR winDir[MAX_PATH];
    GetWindowsDirectory(winDir, MAX_PATH);

    std::wstringstream ss;
    ss << winDir << "\\Fonts\\" << wsFontFile;
    wsFontFile = ss.str();

    return wsFontFile;
}

EmbossStyles Emboss::get_font_list()
{
    //EmbossStyles list1 = get_font_list_by_enumeration();
    //EmbossStyles list2 = get_font_list_by_register();
    //EmbossStyles list3 = get_font_list_by_folder();
    return get_font_list_by_register();
}

EmbossStyles Emboss::get_font_list_by_register() {
//    static const LPWSTR fontRegistryPath = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";
    static const LPCWSTR fontRegistryPath = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";
    HKEY hKey;
    LONG result;

    // Open Windows font registry key
    result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, fontRegistryPath, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        assert(false);
        //std::wcerr << L"Can not Open register key (" << fontRegistryPath << ")" 
        //    << L", function 'RegOpenKeyEx' return code: " << result <<  std::endl;
        return {}; 
    }

    DWORD maxValueNameSize, maxValueDataSize;
    result = RegQueryInfoKey(hKey, 0, 0, 0, 0, 0, 0, 0, &maxValueNameSize,
                             &maxValueDataSize, 0, 0);
    if (result != ERROR_SUCCESS) {
        assert(false);
        // Can not earn query key, function 'RegQueryInfoKey' return code: result
        return {}; 
    }

    // Build full font file path
    WCHAR winDir[MAX_PATH];
    GetWindowsDirectory(winDir, MAX_PATH);
    std::wstring font_path = std::wstring(winDir) + L"\\Fonts\\";

    EmbossStyles font_list;
    DWORD    valueIndex = 0;
    // Look for a matching font name
    LPWSTR font_name = new WCHAR[maxValueNameSize];
    LPBYTE fileTTF_name = new BYTE[maxValueDataSize];
    DWORD  font_name_size, fileTTF_name_size, valueType;
    do {
        fileTTF_name_size = maxValueDataSize;
        font_name_size = maxValueNameSize;

        result = RegEnumValue(hKey, valueIndex, font_name, &font_name_size, 0,
                              &valueType, fileTTF_name, &fileTTF_name_size);
        valueIndex++;
        if (result != ERROR_SUCCESS || valueType != REG_SZ) continue;
        std::wstring font_name_w(font_name, font_name_size);
        std::wstring file_name_w((LPWSTR) fileTTF_name, fileTTF_name_size);
        std::wstring path_w = font_path + file_name_w;

        // filtrate .fon from lists
        size_t pos = font_name_w.rfind(L" (TrueType)");
        if (pos >= font_name_w.size()) continue;
        // remove TrueType text from name
        font_name_w = std::wstring(font_name_w, 0, pos);
        font_list.emplace_back(priv::create_style(font_name_w, path_w));
    } while (result != ERROR_NO_MORE_ITEMS);
    delete[] font_name;
    delete[] fileTTF_name;

    RegCloseKey(hKey);
    return font_list;
}

// TODO: Fix global function
bool CALLBACK EnumFamCallBack(LPLOGFONT       lplf,
                              LPNEWTEXTMETRIC lpntm,
                              DWORD           FontType,
                              LPVOID          aFontList)
{
    std::vector<std::wstring> *fontList =
        (std::vector<std::wstring> *) (aFontList);
    if (FontType & TRUETYPE_FONTTYPE) {
        std::wstring name = lplf->lfFaceName;
        fontList->push_back(name);
    }
    return true;
    // UNREFERENCED_PARAMETER(lplf);
    UNREFERENCED_PARAMETER(lpntm);
}

EmbossStyles Emboss::get_font_list_by_enumeration() {   

    HDC                       hDC = GetDC(NULL);
    std::vector<std::wstring> font_names;
    EnumFontFamilies(hDC, (LPCTSTR) NULL, (FONTENUMPROC) EnumFamCallBack,
                     (LPARAM) &font_names);

    EmbossStyles font_list;
    for (const std::wstring &font_name : font_names) {
        font_list.emplace_back(priv::create_style(font_name, L""));
    }    
    return font_list;
}

EmbossStyles Emboss::get_font_list_by_folder() {
    EmbossStyles result;
    WCHAR winDir[MAX_PATH];
    UINT winDir_size = GetWindowsDirectory(winDir, MAX_PATH);
    std::wstring search_dir = std::wstring(winDir, winDir_size) + L"\\Fonts\\";
    WIN32_FIND_DATA fd;
    HANDLE          hFind;
    // By https://en.wikipedia.org/wiki/TrueType has also suffix .tte
    std::vector<std::wstring> suffixes = {L"*.ttf", L"*.ttc", L"*.tte"};
    for (const std::wstring &suffix : suffixes) {
        hFind = ::FindFirstFile((search_dir + suffix).c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) continue;
        do {
            // skip folder . and ..
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring file_name(fd.cFileName);
            // TODO: find font name instead of filename
            result.emplace_back(priv::create_style(file_name, search_dir + file_name));
        } while (::FindNextFile(hFind, &fd));
        ::FindClose(hFind);
    }
    return result;
}

#else
EmbossStyles Emboss::get_font_list() { 
    // not implemented
    return {}; 
}

std::optional<std::wstring> Emboss::get_font_path(const std::wstring &font_face_name){
    // not implemented
    return {};
}
#endif

std::unique_ptr<FontFile> Emboss::create_font_file(
    std::unique_ptr<std::vector<unsigned char>> data)
{
    int collection_size = stbtt_GetNumberOfFonts(data->data());
    // at least one font must be inside collection
    if (collection_size < 1) {
        assert(false);
        // There is no font collection inside font data
        return nullptr;
    }

    unsigned int c_size = static_cast<unsigned int>(collection_size);
    std::vector<FontFile::Info> infos;
    infos.reserve(c_size);
    for (unsigned int i = 0; i < c_size; ++i) {
        auto font_info = priv::load_font_info(data->data(), i);
        if (!font_info.has_value()) return nullptr;

        const stbtt_fontinfo *info = &(*font_info);
        // load information about line gap
        int ascent, descent, linegap;
        stbtt_GetFontVMetrics(info, &ascent, &descent, &linegap);

        float pixels       = 1000.; // value is irelevant
        float em_pixels    = stbtt_ScaleForMappingEmToPixels(info, pixels);
        int   units_per_em = static_cast<int>(std::round(pixels / em_pixels));

        infos.emplace_back(FontFile::Info{ascent, descent, linegap, units_per_em});
    }
    return std::make_unique<FontFile>(std::move(data), std::move(infos));
}

std::unique_ptr<FontFile> Emboss::create_font_file(const char *file_path)
{
    FILE *file = std::fopen(file_path, "rb");
    if (file == nullptr) {
        assert(false);
        BOOST_LOG_TRIVIAL(error) << "Couldn't open " << file_path << " for reading.";
        return nullptr;
    }
    ScopeGuard sg([&file]() { std::fclose(file); });

    // find size of file
    if (fseek(file, 0L, SEEK_END) != 0) {
        assert(false);
        BOOST_LOG_TRIVIAL(error) << "Couldn't fseek file " << file_path << " for size measure.";
        return nullptr;
    }
    size_t size = ftell(file);
    if (size == 0) {
        assert(false);
        BOOST_LOG_TRIVIAL(error) << "Size of font file is zero. Can't read.";
        return nullptr;    
    }
    rewind(file);
    auto buffer = std::make_unique<std::vector<unsigned char>>(size);
    size_t count_loaded_bytes = fread((void *) &buffer->front(), 1, size, file);
    if (count_loaded_bytes != size) {
        assert(false);
        BOOST_LOG_TRIVIAL(error) << "Different loaded(from file) data size.";
        return nullptr;
    }
    return create_font_file(std::move(buffer));
}


#ifdef _WIN32
static bool load_hfont(void* hfont, DWORD &dwTable, DWORD &dwOffset, size_t& size, HDC hdc = nullptr){
    bool del_hdc = false;
    if (hdc == nullptr) { 
        del_hdc = true;
        hdc = ::CreateCompatibleDC(NULL);
        if (hdc == NULL) return false;
    }
    
    // To retrieve the data from the beginning of the file for TrueType
    // Collection files specify 'ttcf' (0x66637474).
    dwTable  = 0x66637474;
    dwOffset = 0;

    ::SelectObject(hdc, hfont);
    size = ::GetFontData(hdc, dwTable, dwOffset, NULL, 0);
    if (size == GDI_ERROR) {
        // HFONT is NOT TTC(collection)
        dwTable = 0;
        size    = ::GetFontData(hdc, dwTable, dwOffset, NULL, 0);
    }

    if (size == 0 || size == GDI_ERROR) {
        if (del_hdc) ::DeleteDC(hdc);
        return false;
    }
    return true;
}

void *Emboss::can_load(void *hfont)
{
    DWORD dwTable=0, dwOffset=0;
    size_t size = 0;
    if (!load_hfont(hfont, dwTable, dwOffset, size)) return nullptr;
    return hfont;
}

std::unique_ptr<FontFile> Emboss::create_font_file(void *hfont)
{
    HDC hdc = ::CreateCompatibleDC(NULL);
    if (hdc == NULL) {
        assert(false);
        BOOST_LOG_TRIVIAL(error) << "Can't create HDC by CreateCompatibleDC(NULL).";
        return nullptr;
    }

    DWORD dwTable=0,dwOffset = 0;
    size_t size;
    if (!load_hfont(hfont, dwTable, dwOffset, size, hdc)) {
        ::DeleteDC(hdc);
        return nullptr;
    }
    auto buffer = std::make_unique<std::vector<unsigned char>>(size);
    size_t loaded_size = ::GetFontData(hdc, dwTable, dwOffset, buffer->data(), size);
    ::DeleteDC(hdc);
    if (size != loaded_size) {
        assert(false);
        BOOST_LOG_TRIVIAL(error) << "Different loaded(from HFONT) data size.";
        return nullptr;    
    }
    return create_font_file(std::move(buffer));
}
#endif // _WIN32

std::optional<Glyph> Emboss::letter2glyph(const FontFile &font,
                                                  unsigned int    font_index,
                                                  int             letter,
                                                  float           flatness)
{
    if (!priv::is_valid(font, font_index)) return {};
    auto font_info_opt = priv::load_font_info(font.data->data(), font_index);
    if (!font_info_opt.has_value()) return {};
    return priv::get_glyph(*font_info_opt, letter, flatness);
}

ExPolygons Emboss::text2shapes(FontFileWithCache    &font_with_cache,
                               const char           *text,
                               const FontProp       &font_prop,
                               std::function<bool()> was_canceled)
{
    assert(font_with_cache.has_value());
    fontinfo_opt font_info_opt;    
    Point    cursor(0, 0);
    ExPolygons result;
    const FontFile& font = *font_with_cache.font_file;
    unsigned int font_index = font_prop.collection_number.has_value()?
        *font_prop.collection_number : 0;
    if (!priv::is_valid(font, font_index)) return {};
    const FontFile::Info& info = font.infos[font_index];
    Glyphs& cache = *font_with_cache.cache;
    std::wstring ws = boost::nowide::widen(text);
    for (wchar_t wc: ws){
        if (wc == '\n') { 
            int line_height = info.ascent - info.descent + info.linegap;
            if (font_prop.line_gap.has_value())
                line_height += *font_prop.line_gap;
            line_height = static_cast<int>(line_height / SHAPE_SCALE);

            cursor.x() = 0;
            cursor.y() -= line_height;
            continue;
        } 
        if (wc == '\t') {
            // '\t' = 4*space => same as imgui
            const int count_spaces = 4;
            const Glyph* space = priv::get_glyph(int(' '), font, font_prop, cache, font_info_opt);
            if (space == nullptr) continue;
            cursor.x() += count_spaces * space->advance_width;
            continue;
        }
        if (wc == '\r') continue;

        int unicode = static_cast<int>(wc);
        // check cancelation only before unknown symbol - loading of symbol could be timeconsuming on slow computer and dificult fonts
        auto it = cache.find(unicode);
        if (it == cache.end() && was_canceled != nullptr && was_canceled()) return {};
        const Glyph *glyph_ptr = (it != cache.end())? &it->second :
            priv::get_glyph(unicode, font, font_prop, cache, font_info_opt);
        if (glyph_ptr == nullptr) continue;
        
        // move glyph to cursor position
        ExPolygons expolygons = glyph_ptr->shape; // copy
        for (ExPolygon &expolygon : expolygons) 
            expolygon.translate(cursor);

        cursor.x() += glyph_ptr->advance_width;
        expolygons_append(result, std::move(expolygons));
    }
    result = Slic3r::union_ex(result);
    heal_shape(result);
    return result;
}

void Emboss::apply_transformation(const FontProp &font_prop, Transform3d &transformation){
    apply_transformation(font_prop.angle, font_prop.distance, transformation);
}

void Emboss::apply_transformation(const std::optional<float>& angle, const std::optional<float>& distance, Transform3d &transformation) {
    if (angle.has_value()) {
        double angle_z = *angle;
        transformation *= Eigen::AngleAxisd(angle_z, Vec3d::UnitZ());
    }
    if (distance.has_value()) {
        Vec3d translate = Vec3d::UnitZ() * (*distance);
        transformation.translate(translate);
    }
}

bool Emboss::is_italic(const FontFile &font, unsigned int font_index)
{
    if (font_index >= font.infos.size()) return false;
    fontinfo_opt font_info_opt = priv::load_font_info(font.data->data(), font_index);

    if (!font_info_opt.has_value()) return false;
    stbtt_fontinfo *info = &(*font_info_opt);

    // https://docs.microsoft.com/cs-cz/typography/opentype/spec/name
    // https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6name.html
    // 2 ==> Style / Subfamily name
    int name_id = 2;
    int length;
    const char* value = stbtt_GetFontNameString(info, &length,
                                               STBTT_PLATFORM_ID_MICROSOFT,
                                               STBTT_MS_EID_UNICODE_BMP,
                                               STBTT_MS_LANG_ENGLISH,                            
                                               name_id);

    // value is big endian utf-16 i need extract only normal chars
    std::string value_str;
    value_str.reserve(length / 2);
    for (int i = 1; i < length; i += 2)
        value_str.push_back(value[i]);

    // lower case
    std::transform(value_str.begin(), value_str.end(), value_str.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    const std::vector<std::string> italics({"italic", "oblique"});
    for (const std::string &it : italics) { 
        if (value_str.find(it) != std::string::npos) { 
            return true; 
        }
    }
    return false; 
}

std::string Emboss::create_range_text(const std::string &text,
                                      const FontFile    &font,
                                      unsigned int       font_index,
                                      bool              *exist_unknown)
{
    if (!priv::is_valid(font, font_index)) return {};
            
    std::wstring ws = boost::nowide::widen(text);

    // need remove symbols not contained in font
    std::sort(ws.begin(), ws.end());

    auto font_info_opt = priv::load_font_info(font.data->data(), 0);
    if (!font_info_opt.has_value()) return {};
    const stbtt_fontinfo *font_info = &(*font_info_opt);

    if (exist_unknown != nullptr) *exist_unknown = false;
    int prev_unicode = -1;
    ws.erase(std::remove_if(ws.begin(), ws.end(),
        [&prev_unicode, font_info, exist_unknown](wchar_t wc) -> bool {
            int unicode = static_cast<int>(wc);

            // skip white spaces
            if (unicode == '\n' || 
                unicode == '\r' || 
                unicode == '\t') return true;

            // is duplicit?
            if (prev_unicode == unicode) return true;
            prev_unicode = unicode;

            // can find in font?
            bool is_unknown = !stbtt_FindGlyphIndex(font_info, unicode);
            if (is_unknown && exist_unknown != nullptr)
                *exist_unknown = true;
            return is_unknown;
        }), ws.end());

    return boost::nowide::narrow(ws);
}

double Emboss::get_shape_scale(const FontProp &fp, const FontFile &ff)
{
    size_t font_index  = fp.collection_number.value_or(0);
    const FontFile::Info &info = ff.infos[font_index];
    double scale  = fp.size_in_mm / (double) info.unit_per_em;
    // Shape is scaled for store point coordinate as integer
    return scale * SHAPE_SCALE;
}

namespace priv {

void add_quad(uint32_t              i1,
              uint32_t              i2,
              indexed_triangle_set &result,
              uint32_t              count_point)
{
    // bottom indices
    uint32_t i1_ = i1 + count_point;
    uint32_t i2_ = i2 + count_point;
    result.indices.emplace_back(i2, i2_, i1);
    result.indices.emplace_back(i1_, i1, i2_);
};

indexed_triangle_set polygons2model_unique(
    const ExPolygons          &shape2d,
    const IProjection &projection,
    const Points              &points)
{
    // CW order of triangle indices
    std::vector<Vec3i> shape_triangles=Triangulation::triangulate(shape2d, points);
    uint32_t           count_point     = points.size();

    indexed_triangle_set result;
    result.vertices.reserve(2 * count_point);
    std::vector<Vec3f> &front_points = result.vertices; // alias
    std::vector<Vec3f>  back_points;
    back_points.reserve(count_point);

    for (const Point &p : points) {
        auto p2 = projection.create_front_back(p);
        front_points.push_back(p2.first.cast<float>());
        back_points.push_back(p2.second.cast<float>());
    }    
    
    // insert back points, front are already in
    result.vertices.insert(result.vertices.end(),
                           std::make_move_iterator(back_points.begin()),
                           std::make_move_iterator(back_points.end()));
    result.indices.reserve(shape_triangles.size() * 2 + points.size() * 2);
    // top triangles - change to CCW
    for (const Vec3i &t : shape_triangles)
        result.indices.emplace_back(t.x(), t.z(), t.y());
    // bottom triangles - use CW
    for (const Vec3i &t : shape_triangles)
        result.indices.emplace_back(t.x() + count_point, 
                                    t.y() + count_point,
                                    t.z() + count_point);

    // quads around - zig zag by triangles
    size_t polygon_offset = 0;
    auto add_quads = [&polygon_offset,&result, &count_point]
    (const Polygon& polygon) {
        uint32_t polygon_points = polygon.points.size();
        // previous index
        uint32_t prev = polygon_offset + polygon_points - 1;
        for (uint32_t p = 0; p < polygon_points; ++p) { 
            uint32_t index = polygon_offset + p;
            add_quad(prev, index, result, count_point);
            prev = index;
        }
        polygon_offset += polygon_points;
    };

    for (const ExPolygon &expolygon : shape2d) {
        add_quads(expolygon.contour);
        for (const Polygon &hole : expolygon.holes) add_quads(hole);
    }   

    return result;
}

indexed_triangle_set polygons2model_duplicit(
    const ExPolygons          &shape2d,
    const IProjection &projection,
    const Points              &points,
    const Points              &duplicits)
{
    // CW order of triangle indices
    std::vector<uint32_t> changes = Triangulation::create_changes(points, duplicits);
    std::vector<Vec3i> shape_triangles = Triangulation::triangulate(shape2d, points, changes);
    uint32_t count_point = *std::max_element(changes.begin(), changes.end()) + 1;

    indexed_triangle_set result;
    result.vertices.reserve(2 * count_point);
    std::vector<Vec3f> &front_points = result.vertices;
    std::vector<Vec3f>  back_points;
    back_points.reserve(count_point);

    uint32_t max_index = std::numeric_limits<uint32_t>::max();
    for (uint32_t i = 0; i < changes.size(); ++i) { 
        uint32_t index = changes[i];
        if (max_index != std::numeric_limits<uint32_t>::max() &&
            index <= max_index) continue; // duplicit point
        assert(index == max_index + 1);
        assert(front_points.size() == index);
        assert(back_points.size() == index);
        max_index = index;
        const Point &p = points[i];
        auto p2 = projection.create_front_back(p);
        front_points.push_back(p2.first.cast<float>());
        back_points.push_back(p2.second.cast<float>());
    }
    assert(max_index+1 == count_point);    
    
    // insert back points, front are already in
    result.vertices.insert(result.vertices.end(),
                           std::make_move_iterator(back_points.begin()),
                           std::make_move_iterator(back_points.end()));

    result.indices.reserve(shape_triangles.size() * 2 + points.size() * 2);
    // top triangles - change to CCW
    for (const Vec3i &t : shape_triangles)
        result.indices.emplace_back(t.x(), t.z(), t.y());
    // bottom triangles - use CW
    for (const Vec3i &t : shape_triangles)
        result.indices.emplace_back(t.x() + count_point, t.y() + count_point,
                                    t.z() + count_point);

    // quads around - zig zag by triangles
    size_t polygon_offset = 0;
    auto add_quads = [&polygon_offset, &result, count_point, &changes]
    (const Polygon &polygon) {
        uint32_t polygon_points = polygon.points.size();
        // previous index
        uint32_t prev = changes[polygon_offset + polygon_points - 1];
        for (uint32_t p = 0; p < polygon_points; ++p) {
            uint32_t index = changes[polygon_offset + p];
            if (prev == index) continue;
            add_quad(prev, index, result, count_point);
            prev = index;
        }
        polygon_offset += polygon_points;
    };

    for (const ExPolygon &expolygon : shape2d) {
        add_quads(expolygon.contour);
        for (const Polygon &hole : expolygon.holes) add_quads(hole);
    }
    return result;
}
} // namespace priv

indexed_triangle_set Emboss::polygons2model(const ExPolygons &shape2d,
                                            const IProjection &projection)
{
    Points points = to_points(shape2d);    
    Points duplicits = collect_duplicates(points);
    return (duplicits.empty()) ?
        priv::polygons2model_unique(shape2d, projection, points) :
        priv::polygons2model_duplicit(shape2d, projection, points, duplicits);
}

std::pair<Vec3d, Vec3d> Emboss::ProjectZ::create_front_back(const Point &p) const
{
    Vec3d front(
        p.x() * SHAPE_SCALE,
        p.y() * SHAPE_SCALE,
        0.);
    return std::make_pair(front, project(front));
}

Vec3d Emboss::ProjectZ::project(const Vec3d &point) const 
{
    Vec3d res = point; // copy
    res.z() = m_depth;
    return res;
}

std::optional<Vec2d> Emboss::ProjectZ::unproject(const Vec3d &p, double *depth) const {
    if (depth != nullptr) *depth /= SHAPE_SCALE;
    return Vec2d(p.x() / SHAPE_SCALE, p.y() / SHAPE_SCALE);
}


Vec3d Emboss::suggest_up(const Vec3d normal, double up_limit) 
{
    // Normal must be 1
    assert(is_approx(normal.squaredNorm(), 1.));

    // wanted up direction of result
    Vec3d wanted_up_side = 
        (std::fabs(normal.z()) > up_limit)?
        Vec3d::UnitY() : Vec3d::UnitZ();

    // create perpendicular unit vector to surface triangle normal vector
    // lay on surface of triangle and define up vector for text
    Vec3d wanted_up_dir = normal.cross(wanted_up_side).cross(normal);
    // normal3d is NOT perpendicular to normal_up_dir
    wanted_up_dir.normalize();

    return wanted_up_dir;
}

std::optional<float> Emboss::calc_up(const Transform3d &tr, double up_limit)
{
    auto tr_linear = tr.linear();
    // z base of transformation ( tr * UnitZ )
    Vec3d normal = tr_linear.col(2);
    // scaled matrix has base with different size
    normal.normalize();
    Vec3d suggested = suggest_up(normal);
    assert(is_approx(suggested.squaredNorm(), 1.));

    Vec3d up = tr_linear.col(1); // tr * UnitY()
    up.normalize();
    
    double dot = suggested.dot(up);
    if (dot >= 1. || dot <= -1.)
        return {}; // zero angle

    Matrix3d m;
    m.row(0) = up;
    m.row(1) = suggested;
    m.row(2) = normal;
    double det = m.determinant();

    return -atan2(det, dot);
}

Transform3d Emboss::create_transformation_onto_surface(const Vec3d &position,
                                                       const Vec3d &normal,
                                                       double       up_limit)
{
    // is normalized ?
    assert(is_approx(normal.squaredNorm(), 1.));

    // up and emboss direction for generated model
    Vec3d up_dir     = Vec3d::UnitY();
    Vec3d emboss_dir = Vec3d::UnitZ();

    // after cast from float it needs to be normalized again
    Vec3d wanted_up_dir = suggest_up(normal, up_limit);

    // perpendicular to emboss vector of text and normal
    Vec3d axis_view;
    double angle_view;
    if (normal == -Vec3d::UnitZ()) {
        // text_emboss_dir has opposit direction to wanted_emboss_dir
        axis_view = Vec3d::UnitY();
        angle_view = M_PI;
    } else {
        axis_view = emboss_dir.cross(normal);
        angle_view = std::acos(emboss_dir.dot(normal)); // in rad
        axis_view.normalize();
    }

    Eigen::AngleAxis view_rot(angle_view, axis_view);
    Vec3d wanterd_up_rotated = view_rot.matrix().inverse() * wanted_up_dir;
    wanterd_up_rotated.normalize();
    double angle_up = std::acos(up_dir.dot(wanterd_up_rotated));

    Vec3d text_view = up_dir.cross(wanterd_up_rotated);
    Vec3d diff_view  = emboss_dir - text_view;
    if (std::fabs(diff_view.x()) > 1. ||
        std::fabs(diff_view.y()) > 1. ||
        std::fabs(diff_view.z()) > 1.) // oposit direction
        angle_up *= -1.;

    Eigen::AngleAxis up_rot(angle_up, emboss_dir);

    Transform3d transform = Transform3d::Identity();
    transform.translate(position);
    transform.rotate(view_rot);
    transform.rotate(up_rot);
    return transform;
}


// OrthoProject

std::pair<Vec3d, Vec3d> Emboss::OrthoProject::create_front_back(const Point &p) const {
    Vec3d front(p.x(), p.y(), 0.);
    Vec3d front_tr = m_matrix * front;
    return std::make_pair(front_tr, project(front_tr));
}

Vec3d Emboss::OrthoProject::project(const Vec3d &point) const
{
    return point + m_direction;
}

std::optional<Vec2d> Emboss::OrthoProject::unproject(const Vec3d &p, double *depth) const
{
    Vec3d pp = m_matrix_inv * p;
    if (depth != nullptr) *depth = pp.z();
    return Vec2d(pp.x(), pp.y());
}

#ifdef REMOVE_SPIKES
#include <Geometry.hpp>
void priv::remove_spikes(Polygon &polygon, const SpikeDesc &spike_desc)
{
    enum class Type {
        add, // Move with point B on A-side and add new point on C-side
        move, // Only move with point B
        erase // left only points A and C without move 
    };
    struct SpikeHeal
    {
        Type   type;
        size_t index;
        Point  b;
        Point  add;
    };
    using SpikeHeals = std::vector<SpikeHeal>;
    SpikeHeals heals;

    size_t count = polygon.size();
    if (count < 3)
        return;

    const Point *ptr_a = &polygon[count - 2];
    const Point *ptr_b = &polygon[count - 1];
    for (const Point &c : polygon) {
        const Point &a = *ptr_a;
        const Point &b = *ptr_b;
        ScopeGuard sg([&ptr_a, &ptr_b, &c]() {
            // prepare for next loop
            ptr_a = ptr_b;
            ptr_b = &c;
        });

        // calc sides
        Point ba = a - b;
        Point bc = c - b;

        Vec2d ba_f = ba.cast<double>();
        Vec2d bc_f = bc.cast<double>();
        double dot_product = ba_f.dot(bc_f);

        // sqrt together after multiplication save one sqrt
        double ba_size_sq = ba_f.squaredNorm();
        double bc_size_sq = bc_f.squaredNorm();
        double norm = sqrt(ba_size_sq * bc_size_sq);
        double cos_angle = dot_product / norm;

        // small angle are around 1 --> cos(0) = 1
        if (cos_angle < spike_desc.cos_angle)
            continue;

        SpikeHeal heal;
        heal.index = &b - &polygon.points.front();

        // has to be in range <-1, 1>
        // Due to preccission of floating point number could be sligtly out of range
        if (cos_angle > 1.)
            cos_angle = 1.;
        if (cos_angle < -1.)
            cos_angle = -1.;

        // Current Spike angle
        double angle = acos(cos_angle);
        double wanted_size = spike_desc.half_bevel / cos(angle / 2.);
        double wanted_size_sq = wanted_size * wanted_size;

        bool is_ba_short = ba_size_sq < wanted_size_sq;
        bool is_bc_short = bc_size_sq < wanted_size_sq;
        auto a_side = [&b, &ba_f, &ba_size_sq, &wanted_size]() {
            Vec2d ba_norm = ba_f / sqrt(ba_size_sq);
            return b + (wanted_size * ba_norm).cast<coord_t>();
        };
        auto c_side = [&b, &bc_f, &bc_size_sq, &wanted_size]() {
            Vec2d bc_norm = bc_f / sqrt(bc_size_sq);
            return b + (wanted_size * bc_norm).cast<coord_t>();
        };
        if (is_ba_short && is_bc_short) {
            // remove short spike
            heal.type = Type::erase;
        } else if (is_ba_short){
            // move point B on C-side
            heal.type = Type::move;
            heal.b    = c_side();
        } else if (is_bc_short) {
            // move point B on A-side
            heal.type = Type::move;
            heal.b    = a_side();
        } else {
            // move point B on A-side and add point on C-side
            heal.type = Type::add;
            heal.b    = a_side();
            heal.add  = c_side();           
        }
        heals.push_back(heal);
    }

    if (heals.empty())
        return;

    // sort index from high to low
    if (heals.front().index == (count - 1))
        std::rotate(heals.begin(), heals.begin()+1, heals.end());
    std::reverse(heals.begin(), heals.end());

    int extend = 0;
    int curr_extend = 0;
    for (const SpikeHeal &h : heals)
        switch (h.type) {
        case Type::add:
            ++curr_extend;
            if (extend < curr_extend)
                extend = curr_extend;
            break;
        case Type::erase:
            --curr_extend;
        }

    Points &pts = polygon.points;
    if (extend > 0)
        pts.reserve(pts.size() + extend);

    for (const SpikeHeal &h : heals) {
        switch (h.type) {
        case Type::add:
            pts[h.index] = h.b;
            pts.insert(pts.begin() + h.index+1, h.add);
            break;
        case Type::erase:
            pts.erase(pts.begin() + h.index);
            break;
        case Type::move:
            pts[h.index] = h.b; 
            break;
        default: break;
        }
    }
}

void priv::remove_spikes(Polygons &polygons, const SpikeDesc &spike_desc)
{
    for (Polygon &polygon : polygons)
        remove_spikes(polygon, spike_desc);
    remove_bad(polygons);
}

void priv::remove_spikes(ExPolygons &expolygons, const SpikeDesc &spike_desc)
{
    for (ExPolygon &expolygon : expolygons) {
        remove_spikes(expolygon.contour, spike_desc);
        remove_spikes(expolygon.holes, spike_desc);    
    }
    remove_bad(expolygons);
}

#endif // REMOVE_SPIKES