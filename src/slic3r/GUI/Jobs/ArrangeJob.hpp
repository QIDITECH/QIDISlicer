#ifndef ARRANGEJOB_HPP
#define ARRANGEJOB_HPP

#include <optional>

#include "Job.hpp"
#include "libslic3r/Arrange.hpp"

namespace Slic3r {

class ModelInstance;

namespace GUI {

class Plater;

class ArrangeJob : public Job
{
    using ArrangePolygon = arrangement::ArrangePolygon;
    using ArrangePolygons = arrangement::ArrangePolygons;

    ArrangePolygons m_selected, m_unselected, m_unprintable;
    std::vector<ModelInstance*> m_unarranged;
    coord_t m_min_bed_inset = 0.;

    Plater *m_plater;

    // clear m_selected and m_unselected, reserve space for next usage
    void clear_input();

    // Prepare all objects on the bed regardless of the selection
    void prepare_all();

    // Prepare the selected and unselected items separately. If nothing is
    // selected, behaves as if everything would be selected.
    void prepare_selected();

    ArrangePolygon get_arrange_poly_(ModelInstance *mi);

public:

    void prepare();

    void process(Ctl &ctl) override;

    ArrangeJob();

    int status_range() const
    {
        return int(m_selected.size() + m_unprintable.size());
    }

    void finalize(bool canceled, std::exception_ptr &e) override;
};

std::optional<arrangement::ArrangePolygon> get_wipe_tower_arrangepoly(const Plater &);

// The gap between logical beds in the x axis expressed in ratio of
// the current bed width.
static const constexpr double LOGICAL_BED_GAP = 1. / 5.;

// Stride between logical beds
double bed_stride(const Plater *plater);

template<class T> struct PtrWrapper
{
    T *ptr;

    explicit PtrWrapper(T *p) : ptr{p} {}

    arrangement::ArrangePolygon get_arrange_polygon() const
    {
        return ptr->get_arrange_polygon();
    }

    void apply_arrange_result(const Vec2d &t, double rot)
    {
        ptr->apply_arrange_result(t, rot);
    }
};

// Set up arrange polygon for a ModelInstance and Wipe tower
template<class T>
arrangement::ArrangePolygon get_arrange_poly(T obj, const Plater *plater)
{
    using ArrangePolygon = arrangement::ArrangePolygon;

    ArrangePolygon ap = obj.get_arrange_polygon();
    ap.bed_idx        = get_extents(ap.transformed_poly()).min.x() / bed_stride(plater);
    ap.setter         = [obj, plater](const ArrangePolygon &p) {
        if (p.is_arranged()) {
            Vec2d t = p.translation.cast<double>();
            t.x() += p.bed_idx * bed_stride(plater);
            T{obj}.apply_arrange_result(t, p.rotation);
        }
    };

    return ap;
}

template<>
arrangement::ArrangePolygon get_arrange_poly(ModelInstance *inst,
                                             const Plater * plater);

arrangement::ArrangeParams get_arrange_params(Plater *p);

coord_t get_skirt_offset(const Plater* plater);

}} // namespace Slic3r::GUI

#endif // ARRANGEJOB_HPP
