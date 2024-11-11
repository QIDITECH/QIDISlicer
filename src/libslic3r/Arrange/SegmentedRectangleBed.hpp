
#ifndef SEGMENTEDRECTANGLEBED_HPP
#define SEGMENTEDRECTANGLEBED_HPP

#include "libslic3r/Arrange/Core/Beds.hpp"

namespace Slic3r { namespace arr2 {

enum class RectPivots {
    Center, BottomLeft, BottomRight, TopLeft, TopRight
};

template<class T> struct IsSegmentedBed_ : public std::false_type {};
template<class T> constexpr bool IsSegmentedBed = IsSegmentedBed_<StripCVRef<T>>::value;

template<class SegX = void, class SegY = void, class Pivot = void>
struct SegmentedRectangleBed {
    Vec<2, size_t> segments = Vec<2, size_t>::Ones();
    BoundingBox bb;
    RectPivots pivot = RectPivots::Center;

    SegmentedRectangleBed() = default;
    SegmentedRectangleBed(const BoundingBox &bb,
                          size_t segments_x,
                          size_t segments_y,
                          const RectPivots pivot = RectPivots::Center)
        : segments{segments_x, segments_y}, bb{bb}, pivot{pivot}
    {}

    size_t segments_x() const noexcept { return segments.x(); }
    size_t segments_y() const noexcept { return segments.y(); }

    auto alignment() const noexcept { return pivot; }
};

template<size_t SegX, size_t SegY>
struct SegmentedRectangleBed<std::integral_constant<size_t, SegX>,
                             std::integral_constant<size_t, SegY>>
{
    BoundingBox bb;
    RectPivots pivot = RectPivots::Center;

    SegmentedRectangleBed() = default;

    explicit SegmentedRectangleBed(const BoundingBox &b,
                                   const RectPivots pivot = RectPivots::Center)
        : bb{b}
    {}

    size_t segments_x() const noexcept { return SegX; }
    size_t segments_y() const noexcept { return SegY; }

    auto alignment() const noexcept { return pivot; }
};

template<size_t SegX, size_t SegY, RectPivots pivot>
struct SegmentedRectangleBed<std::integral_constant<size_t, SegX>,
                             std::integral_constant<size_t, SegY>,
                             std::integral_constant<RectPivots, pivot>>
{
    BoundingBox bb;

    SegmentedRectangleBed() = default;

    explicit SegmentedRectangleBed(const BoundingBox &b) : bb{b} {}

    size_t segments_x() const noexcept { return SegX; }
    size_t segments_y() const noexcept { return SegY; }

    auto alignment() const noexcept { return pivot; }
};

template<class... Args>
struct IsSegmentedBed_<SegmentedRectangleBed<Args...>>
    : public std::true_type {};

template<class... Args>
auto offset(const SegmentedRectangleBed<Args...> &bed, coord_t val_scaled)
{
    auto cpy = bed;
    cpy.bb.offset(val_scaled);

    return cpy;
}

template<class...Args>
auto bounding_box(const SegmentedRectangleBed<Args...> &bed)
{
    return bed.bb;
}

template<class...Args>
auto area(const SegmentedRectangleBed<Args...> &bed)
{
    return arr2::area(bed.bb);
}

template<class...Args>
ExPolygons to_expolygons(const SegmentedRectangleBed<Args...> &bed)
{
    return to_expolygons(RectangleBed{bed.bb});
}

template<class SegB>
struct IsRectangular_<SegB, std::enable_if_t<IsSegmentedBed<SegB>, void>> : public std::true_type
{};

}} // namespace Slic3r::arr2

#endif // SEGMENTEDRECTANGLEBED_HPP
