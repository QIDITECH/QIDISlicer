#ifndef ARRANGE_HPP
#define ARRANGE_HPP

#include <boost/variant.hpp>

#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/BoundingBox.hpp>

namespace Slic3r {

class BoundingBox;

namespace arrangement {

/// Representing an unbounded bed.
struct InfiniteBed {
    Point center;
    explicit InfiniteBed(const Point &p = {0, 0}): center{p} {}
};

struct RectangleBed {
    BoundingBox bb;
};

/// A geometry abstraction for a circular print bed. Similarly to BoundingBox.
class CircleBed {
    Point center_;
    double radius_;
public:

    inline CircleBed(): center_(0, 0), radius_(NaNd) {}
    explicit inline CircleBed(const Point& c, double r): center_(c), radius_(r) {}

    inline double radius() const { return radius_; }
    inline const Point& center() const { return center_; }
};

struct SegmentedRectangleBed {
    Vec<2, size_t> segments;
    BoundingBox bb;

    SegmentedRectangleBed (const BoundingBox &bb,
                           size_t segments_x,
                           size_t segments_y)
        : segments{segments_x, segments_y}
        , bb{bb}
    {}
};

struct IrregularBed {
    ExPolygon poly;
};

//enum BedType { Infinite, Rectangle, Circle, SegmentedRectangle, Irregular };

using ArrangeBed = boost::variant<InfiniteBed, RectangleBed, CircleBed, SegmentedRectangleBed, IrregularBed>;

BoundingBox bounding_box(const InfiniteBed &bed);
inline BoundingBox bounding_box(const RectangleBed &b) { return b.bb; }
inline BoundingBox bounding_box(const SegmentedRectangleBed &b) { return b.bb; }
inline BoundingBox bounding_box(const CircleBed &b)
{
    auto r = static_cast<coord_t>(std::round(b.radius()));
    Point R{r, r};

    return {b.center() - R, b.center() + R};
}
inline BoundingBox bounding_box(const ArrangeBed &b)
{
    BoundingBox ret;
    auto visitor = [&ret](const auto &b) { ret = bounding_box(b); };
    boost::apply_visitor(visitor, b);

    return ret;
}

ArrangeBed to_arrange_bed(const Points &bedpts);

/// A logical bed representing an object not being arranged. Either the arrange
/// has not yet successfully run on this ArrangePolygon or it could not fit the
/// object due to overly large size or invalid geometry.
static const constexpr int UNARRANGED = -1;

/// Input/Output structure for the arrange() function. The poly field will not
/// be modified during arrangement. Instead, the translation and rotation fields
/// will mark the needed transformation for the polygon to be in the arranged
/// position. These can also be set to an initial offset and rotation.
/// 
/// The bed_idx field will indicate the logical bed into which the
/// polygon belongs: UNARRANGED means no place for the polygon
/// (also the initial state before arrange), 0..N means the index of the bed.
/// Zero is the physical bed, larger than zero means a virtual bed.
struct ArrangePolygon {
    ExPolygon poly;                 /// The 2D silhouette to be arranged
    Vec2crd   translation{0, 0};    /// The translation of the poly
    double    rotation{0.0};        /// The rotation of the poly in radians
    coord_t   inflation = 0;        /// Arrange with inflated polygon
    int       bed_idx{UNARRANGED};  /// To which logical bed does poly belong...
    int       priority{0};

    // If empty, any rotation is allowed (currently unsupported)
    // If only a zero is there, no rotation is allowed
    std::vector<double> allowed_rotations = {0.};

    /// Optional setter function which can store arbitrary data in its closure
    std::function<void(const ArrangePolygon&)> setter = nullptr;
    
    /// Helper function to call the setter with the arrange data arguments
    void apply() const { if (setter) setter(*this); }

    /// Test if arrange() was called previously and gave a successful result.
    bool is_arranged() const { return bed_idx != UNARRANGED; }

    inline ExPolygon transformed_poly() const
    {
        ExPolygon ret = poly;
        ret.rotate(rotation);
        ret.translate(translation.x(), translation.y());

        return ret;
    }
};

using ArrangePolygons = std::vector<ArrangePolygon>;

enum class Pivots {
    Center, TopLeft, BottomLeft, BottomRight, TopRight
};

struct ArrangeParams {

    /// The minimum distance which is allowed for any 
    /// pair of items on the print bed in any direction.
    coord_t min_obj_distance = 0;

    /// The minimum distance of any object from bed edges
    coord_t min_bed_distance = 0;

    /// The accuracy of optimization.
    /// Goes from 0.0 to 1.0 and scales performance as well
    float accuracy = 1.f;

    /// Allow parallel execution.
    bool parallel = true;

    bool allow_rotations = false;

    /// Final alignment of the merged pile after arrangement
    Pivots alignment = Pivots::Center;

    /// Starting position hint for the arrangement
    Pivots starting_point = Pivots::Center;

    /// Progress indicator callback called when an object gets packed. 
    /// The unsigned argument is the number of items remaining to pack.
    std::function<void(unsigned)> progressind;

    std::function<void(const ArrangePolygon &)> on_packed;

    /// A predicate returning true if abort is needed.
    std::function<bool(void)>     stopcondition;

    ArrangeParams() = default;
    explicit ArrangeParams(coord_t md) : min_obj_distance(md) {}
};

/**
 * \brief Arranges the input polygons.
 *
 * WARNING: Currently, only convex polygons are supported by the libnest2d 
 * library which is used to do the arrangement. This might change in the future
 * this is why the interface contains a general polygon capable to have holes.
 *
 * \param items Input vector of ArrangePolygons. The transformation, rotation 
 * and bin_idx fields will be changed after the call finished and can be used
 * to apply the result on the input polygon.
 */
template<class TBed> void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const TBed &bed, const ArrangeParams &params = {});

// A dispatch function that determines the bed shape from a set of points.
template<> void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const Points &bed, const ArrangeParams &params);

extern template void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const BoundingBox &bed, const ArrangeParams &params);
extern template void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const CircleBed &bed, const ArrangeParams &params);
extern template void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const Polygon &bed, const ArrangeParams &params);
extern template void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const InfiniteBed &bed, const ArrangeParams &params);

inline void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const RectangleBed &bed, const ArrangeParams &params)
{
    arrange(items, excludes, bed.bb, params);
}

inline void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const IrregularBed &bed, const ArrangeParams &params)
{
    arrange(items, excludes, bed.poly.contour, params);
}

void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const SegmentedRectangleBed &bed, const ArrangeParams &params);

inline void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const ArrangeBed &bed, const ArrangeParams &params)
{
    auto call_arrange = [&](const auto &realbed) { arrange(items, excludes, realbed, params); };
    boost::apply_visitor(call_arrange, bed);
}

inline void arrange(ArrangePolygons &items, const Points &bed, const ArrangeParams &params = {}) { arrange(items, {}, bed, params); }
inline void arrange(ArrangePolygons &items, const BoundingBox &bed, const ArrangeParams &params = {}) { arrange(items, {}, bed, params); }
inline void arrange(ArrangePolygons &items, const CircleBed &bed, const ArrangeParams &params = {}) { arrange(items, {}, bed, params); }
inline void arrange(ArrangePolygons &items, const Polygon &bed, const ArrangeParams &params = {}) { arrange(items, {}, bed, params); }
inline void arrange(ArrangePolygons &items, const InfiniteBed &bed, const ArrangeParams &params = {}) { arrange(items, {}, bed, params); }

bool is_box(const Points &bed);

}} // namespace Slic3r::arrangement

#endif // MODELARRANGE_HPP
