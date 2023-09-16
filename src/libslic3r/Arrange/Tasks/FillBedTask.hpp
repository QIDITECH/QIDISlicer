
#ifndef FILLBEDTASK_HPP
#define FILLBEDTASK_HPP

#include "MultiplySelectionTask.hpp"

#include "libslic3r/Arrange/Arrange.hpp"

namespace Slic3r { namespace arr2 {

struct FillBedTaskResult: public MultiplySelectionTaskResult {};

template<class ArrItem>
struct FillBedTask: public ArrangeTaskBase
{
    std::optional<ArrItem> prototype_item;

    std::vector<ArrItem> selected, unselected;

    ArrangeSettings settings;
    ExtendedBed bed;
    size_t selected_existing_count = 0;

    std::unique_ptr<FillBedTaskResult> process_native(Ctl &ctl);
    std::unique_ptr<FillBedTaskResult> process_native(Ctl &&ctl)
    {
        return process_native(ctl);
    }

    std::unique_ptr<ArrangeResult> process(Ctl &ctl) override
    {
        return process_native(ctl);
    }

    int item_count_to_process() const override
    {
        return selected.size();
    }

    static std::unique_ptr<FillBedTask> create(
        const Scene &sc,
        const ArrangeableToItemConverter<ArrItem> &converter);

    static std::unique_ptr<FillBedTask> create(const Scene &sc)
    {
        auto conv = ArrangeableToItemConverter<ArrItem>::create(sc);
        return create(sc, *conv);
    }
};

} // namespace arr2
} // namespace Slic3r

#endif // FILLBEDTASK_HPP
