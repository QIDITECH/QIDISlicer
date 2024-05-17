#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "../Surface.hpp"

#include "Fill3DHoneycomb.hpp"

namespace Slic3r {

//w36
template <typename T> int sgn(T val) {
  return (T(0) < val) - (val < T(0));
}
//w36
static coordf_t triWave(coordf_t pos, coordf_t gridSize)
{
  float t = (pos / (gridSize * 2.)) + 0.25; 
  t = t - (int)t; 
  return((1. - abs(t * 8. - 4.)) * (gridSize / 4.) + (gridSize / 4.));
}
//w36
static coordf_t troctWave(coordf_t pos, coordf_t gridSize, coordf_t Zpos)
{
    coordf_t Zcycle     = triWave(Zpos, gridSize);
    coordf_t perpOffset = Zcycle / 2;
    coordf_t y          = triWave(pos, gridSize);
    return ((abs(y) > abs(perpOffset)) ? (sgn(y) * perpOffset) : (y * sgn(perpOffset)));
}
//w36
static std::vector<coordf_t> getCriticalPoints(coordf_t Zpos, coordf_t gridSize)
{
    std::vector<coordf_t> res        = {0.};
    coordf_t              perpOffset = abs(triWave(Zpos, gridSize) / 2.);

    coordf_t normalisedOffset = perpOffset / gridSize;
    if (normalisedOffset > 0) {
        res.push_back(gridSize * (0. + normalisedOffset));
        res.push_back(gridSize * (1. - normalisedOffset));
        res.push_back(gridSize * (1. + normalisedOffset));
        res.push_back(gridSize * (2. - normalisedOffset));
    }
    return (res);
}
/*
Creates a contiguous sequence of points at a specified height that make
up a horizontal slice of the edges of a space filling truncated
octahedron tesselation. The octahedrons are oriented so that the
square faces are in the horizontal plane with edges parallel to the X
and Y axes.

Credits: David Eccles (gringer).
*/

// Generate an array of points that are in the same direction as the
// basic printing line (i.e. Y points for columns, X points for rows)
// Note: a negative offset only causes a change in the perpendicular
// direction
//w36
static std::vector<coordf_t> colinearPoints(const coordf_t Zpos, coordf_t gridSize, std::vector<coordf_t> critPoints,
					     const size_t baseLocation, size_t gridLength)
{
    //w36
    std::vector<coordf_t> points;
    //w36
    points.push_back(baseLocation);
    for (coordf_t cLoc = baseLocation; cLoc < gridLength; cLoc += (gridSize * 2)) {
        for (size_t pi = 0; pi < critPoints.size(); pi++) {
            points.push_back(baseLocation + cLoc + critPoints[pi]);
        }
    }
    //w36
    points.push_back(gridLength);
    return points;
}

// Generate an array of points for the dimension that is perpendicular to
// the basic printing line (i.e. X points for columns, Y points for rows)
//w36
static std::vector<coordf_t> perpendPoints(const coordf_t        Zpos,
                                           coordf_t              gridSize,
                                           std::vector<coordf_t> critPoints,
                                           size_t                baseLocation,
                                           size_t                gridLength,
                                           size_t                offsetBase,
                                           coordf_t              perpDir)
{
    //w36
    std::vector<coordf_t> points;
    //w36
    points.push_back(offsetBase);
    for (coordf_t cLoc = baseLocation; cLoc < gridLength; cLoc += gridSize * 2) {
        for (size_t pi = 0; pi < critPoints.size(); pi++) {
            coordf_t offset = troctWave(critPoints[pi], gridSize, Zpos);
            points.push_back(offsetBase + (offset * perpDir));
        }
    }
    //w36
    points.push_back(offsetBase);
    return points;
}

// Trims an array of points to specified rectangular limits. Point
// components that are outside these limits are set to the limits.
static inline void trim(Pointfs &pts, coordf_t minX, coordf_t minY, coordf_t maxX, coordf_t maxY)
{
    for (Vec2d &pt : pts) {
        pt.x() = std::clamp(pt.x(), minX, maxX);
        pt.y() = std::clamp(pt.y(), minY, maxY);
    }
}

static inline Pointfs zip(const std::vector<coordf_t> &x, const std::vector<coordf_t> &y)
{
    assert(x.size() == y.size());
    Pointfs out;
    out.reserve(x.size());
    for (size_t i = 0; i < x.size(); ++ i)
        out.push_back(Vec2d(x[i], y[i]));
    return out;
}

// Generate a set of curves (array of array of 2d points) that describe a
// horizontal slice of a truncated regular octahedron with edge length 1.
// curveType specifies which lines to print, 1 for vertical lines
// (columns), 2 for horizontal lines (rows), and 3 for both.
//w36
static std::vector<Pointfs> makeActualGrid(coordf_t Zpos, coordf_t gridSize, size_t boundsX, size_t boundsY)
{
    //w36
    std::vector<Pointfs>  points;
    std::vector<coordf_t> critPoints = getCriticalPoints(Zpos, gridSize);
    coordf_t              zCycle     = fmod(Zpos + gridSize / 2, gridSize * 2.) / (gridSize * 2.);
    bool                  printVert  = zCycle < 0.5;
    if (printVert) {
        int perpDir = -1;
        for (coordf_t x = 0; x <= (boundsX); x += gridSize, perpDir *= -1) {
            points.push_back(Pointfs());
            Pointfs &newPoints = points.back();
            newPoints          = zip(perpendPoints(Zpos, gridSize, critPoints, 0, boundsY, x, perpDir),
                            colinearPoints(Zpos, gridSize, critPoints, 0, boundsY));
            if (perpDir == 1)
                std::reverse(newPoints.begin(), newPoints.end());
        }
    } else {
        int perpDir = 1;
        for (coordf_t y = gridSize; y <= (boundsY); y += gridSize, perpDir *= -1) {
            points.push_back(Pointfs());
            Pointfs &newPoints = points.back();
            newPoints          = zip(colinearPoints(Zpos, gridSize, critPoints, 0, boundsX),
                            perpendPoints(Zpos, gridSize, critPoints, 0, boundsX, y, perpDir));
            if (perpDir == -1)
                std::reverse(newPoints.begin(), newPoints.end());
        }
    }
    return points;
}

// Generate a set of curves (array of array of 2d points) that describe a
// horizontal slice of a truncated regular octahedron with a specified
// grid square size.
//w36
static Polylines makeGrid(coordf_t z, coordf_t gridSize, coordf_t boundWidth, coordf_t boundHeight, bool fillEvenly)
{
    //w36
    std::vector<Pointfs> polylines = makeActualGrid(z, gridSize, boundWidth, boundHeight);
    Polylines            result;
    result.reserve(polylines.size());
    for (std::vector<Pointfs>::const_iterator it_polylines = polylines.begin(); it_polylines != polylines.end(); ++it_polylines) {
        result.push_back(Polyline());
        Polyline &polyline = result.back();
        //w36
        for (Pointfs::const_iterator it = it_polylines->begin(); it != it_polylines->end(); ++it)
            polyline.points.push_back(Point(coord_t((*it)(0)), coord_t((*it)(1))));
    }
    return result;
}

void Fill3DHoneycomb::_fill_surface_single(
    const FillParams                &params, 
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction, 
    ExPolygon                        expolygon,
    Polylines                       &polylines_out)
{
    // no rotation is supported for this infill pattern
    //w36
    auto infill_angle = float(this->angle);
    if (std::abs(infill_angle) >= EPSILON)
        expolygon.rotate(-infill_angle);
    BoundingBox bb = expolygon.contour.bounding_box();
    //w36
    // align bounding box to a multiple of our honeycomb grid module
    // (a module is 2*$distance since one $distance half-module is
    // growing while the other $distance half-module is shrinking)
    //w36
    coordf_t zScale = sqrt(2);
    coordf_t gridSize = (scale_(this->spacing) * ((zScale + 1.) / 2.) / params.density);


    coordf_t layerHeight = scale_(thickness_layers);
   
    coordf_t layersPerModule = floor((gridSize * 2) / (zScale * layerHeight) + 0.05);
    if (params.density > 0.42) { 
        layersPerModule = 2;
        gridSize = (scale_(this->spacing) * 1.1 / params.density);
        zScale = (gridSize * 2) / (layersPerModule * layerHeight);
    } else {
        if (layersPerModule < 2) {
            layersPerModule = 2;
        }
        zScale = (gridSize * 2) / (layersPerModule * layerHeight);
        gridSize = (scale_(this->spacing) * ((zScale + 1.) / 2.) / params.density);
        layersPerModule = floor((gridSize * 2) / (zScale * layerHeight) + 0.05);
        if (layersPerModule < 2) {
            layersPerModule = 2;
        }
        zScale = (gridSize * 2) / (layersPerModule * layerHeight);
    }

    bb.merge(align_to_grid(bb.min, Point(gridSize * 4, gridSize * 4)));
    
    Polylines polylines =
      makeGrid(
	       scale_(this->z) * zScale,
	       gridSize,
	       bb.size()(0),
	       bb.size()(1),
	       !params.dont_adjust);
    // move pattern in place
    for (Polyline &pl : polylines) {
        pl.translate(bb.min);
    }
    // clip pattern to boundaries, chain the clipped polylines
    polylines = intersection_pl(polylines, to_polygons(expolygon));
    if (!polylines.empty()) {
        int infill_start_idx = polylines_out.size(); 
        if (params.dont_connect() || polylines.size() <= 1)
            append(polylines_out, chain_polylines(std::move(polylines)));
        else
            this->connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);

        if (std::abs(infill_angle) >= EPSILON) {
            for (auto it = polylines_out.begin() + infill_start_idx; it != polylines_out.end(); ++it)
                it->rotate(infill_angle);
        }
    }
}

} // namespace Slic3r
