#include "Range.hpp"

#include <algorithm>

namespace libvgcode {

void Range::set(Interval::value_type min, Interval::value_type max)
{
    if (max < min)
        std::swap(min, max);
    m_range[0] = min;
    m_range[1] = max;
}

void Range::clamp(Range& other)
{
    other.m_range[0] = std::clamp(other.m_range[0], m_range[0], m_range[1]);
    other.m_range[1] = std::clamp(other.m_range[1], m_range[0], m_range[1]);
}

} // namespace libvgcode
