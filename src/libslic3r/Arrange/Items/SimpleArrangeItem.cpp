
#include "SimpleArrangeItem.hpp"
#include "libslic3r/Arrange/ArrangeImpl.hpp"
#include "libslic3r/Arrange/Tasks/ArrangeTaskImpl.hpp"
#include "libslic3r/Arrange/Tasks/FillBedTaskImpl.hpp"
#include "libslic3r/Arrange/Tasks/MultiplySelectionTaskImpl.hpp"

namespace Slic3r { namespace arr2 {

Polygon SimpleArrangeItem::outline() const
{
    Polygon ret = shape();
    ret.rotate(m_rotation);
    ret.translate(m_translation);

    return ret;
}

template class  ArrangeableToItemConverter<SimpleArrangeItem>;
template struct ArrangeTask<SimpleArrangeItem>;
template struct FillBedTask<SimpleArrangeItem>;
template struct MultiplySelectionTask<SimpleArrangeItem>;
template class  Arranger<SimpleArrangeItem>;

}} // namespace Slic3r::arr2
