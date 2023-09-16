
#include "ArrangeJob2.hpp"

#include <numeric>
#include <iterator>

#include <libslic3r/Model.hpp>
#include <libslic3r/TriangleMeshSlicer.hpp>
#include <libslic3r/Geometry/ConvexHull.hpp>

#include <libslic3r/SLAPrint.hpp>
#include <libslic3r/Print.hpp>

#include <slic3r/GUI/Plater.hpp>
#include <slic3r/GUI/GLCanvas3D.hpp>
#include <slic3r/GUI/GUI_App.hpp>

#include <boost/log/trivial.hpp>

namespace Slic3r { namespace GUI {

class GUISelectionMask: public arr2::SelectionMask {
    const Selection *m_sel;

public:
    explicit GUISelectionMask(const Selection *sel) : m_sel{sel} {}

    bool is_wipe_tower() const override
    {
        return m_sel->is_wipe_tower();
    }

    std::vector<bool> selected_objects() const override
    {
        auto selmap = m_sel->get_object_idxs();

        std::vector<bool> ret(m_sel->get_model()->objects.size(), false);

        for (auto sel : selmap) {
            ret[sel] = true;
        }

        return ret;
    }

    std::vector<bool> selected_instances(int obj_id) const override
    {
        auto objcnt = static_cast<int>(m_sel->get_model()->objects.size());
        auto icnt   = obj_id < objcnt ?
                          m_sel->get_model()->objects[obj_id]->instances.size() :
                          0;

        std::vector<bool> ret(icnt, false);

        auto selmap = m_sel->get_content();
        auto objit = selmap.find(obj_id);

        if (objit != selmap.end() && obj_id < objcnt) {
            ret = std::vector<bool>(icnt, false);
            for (auto sel : objit->second) {
                ret[sel] = true;
            }
        }

        return ret;
    }
};

static Polygon get_wtpoly(const GLCanvas3D::WipeTowerInfo &wti)
{

    auto bb = scaled(wti.bounding_box());
    Polygon poly = Polygon({
        {bb.min},
        {bb.max.x(), bb.min.y()},
        {bb.max},
        {bb.min.x(), bb.max.y()}
    });

    poly.rotate(wti.rotation());
    poly.translate(scaled(wti.pos()));

    return poly;
}

// Wipe tower logic based on GLCanvas3D::WipeTowerInfo implements the Arrangeable
// interface with this class:
class ArrangeableWT: public arr2::ArrangeableWipeTowerBase
{
    BoundingBox m_xl_bb;
    Vec2d m_orig_tr;
    double m_orig_rot;

public:
    explicit ArrangeableWT(const ObjectID                  &oid,
                           const GLCanvas3D::WipeTowerInfo &wti,
                           std::function<bool()>            sel_pred,
                           const BoundingBox                xl_bb = {})
        : arr2::ArrangeableWipeTowerBase{oid, get_wtpoly(wti), std::move(sel_pred)}
        , m_orig_tr{wti.pos()}
        , m_orig_rot{wti.rotation()}
        , m_xl_bb{xl_bb}
    {}

    // Rotation is disabled for wipe tower in arrangement
    void transform(const Vec2d &transl, double /*rot*/) override
    {
        GLCanvas3D::WipeTowerInfo::apply_wipe_tower(m_orig_tr + transl, m_orig_rot);
    }

    void imbue_data(arr2::AnyWritable &datastore) const override
    {
        // For XL printers, there is a requirement that the wipe tower
        // needs to be placed right beside the extruders which reside at the
        // top edge of the bed.
        if (m_xl_bb.defined) {
            Vec2crd xl_center = m_xl_bb.center();
            datastore.write("sink", Vec2crd{xl_center.x(), 2 * m_xl_bb.max.y()});
        }

        arr2::ArrangeableWipeTowerBase::imbue_data(datastore);
    }
};

// Now the wipe tower handler implementation for GLCanvas3D::WipeTowerInfo
// This is what creates the ArrangeableWT when the arrangement requests it.
// An object of this class is installed into the arrangement Scene.
struct WTH : public arr2::WipeTowerHandler
{
    GLCanvas3D::WipeTowerInfo wti;
    ObjectID oid;
    std::function<bool()> sel_pred;
    BoundingBox xl_bb;

    WTH(const ObjectID &objid,
        const GLCanvas3D::WipeTowerInfo &w,
        std::function<bool()> sel_predicate = [] { return false; })
        : wti(w), oid{objid}, sel_pred{std::move(sel_predicate)}
    {}

    template<class Self, class Fn>
    static void visit_(Self &&self, Fn &&fn)
    {
        ArrangeableWT wta{self.oid, self.wti, self.sel_pred, self.xl_bb};
        fn(wta);
    }

    void visit(std::function<void(arr2::Arrangeable &)> fn) override
    {
        visit_(*this, fn);
    }

    void visit(std::function<void(const arr2::Arrangeable &)> fn) const override
    {
        visit_(*this, fn);
    }

    void set_selection_predicate(std::function<bool()> pred) override
    {
        sel_pred = std::move(pred);
    }
};

arr2::SceneBuilder build_scene(Plater &plater, ArrangeSelectionMode mode)
{
    arr2::SceneBuilder builder;

    if (mode == ArrangeSelectionMode::SelectionOnly) {
        auto sel = std::make_unique<GUISelectionMask>(&plater.get_selection());
        builder.set_selection(std::move(sel));
    }

    builder.set_arrange_settings(plater.canvas3D()->get_arrange_settings_view());

    auto wti = plater.canvas3D()->get_wipe_tower_info();

    AnyPtr<WTH> wth;

    if (wti) {
        wth = std::make_unique<WTH>(plater.model().wipe_tower.id(), wti);
    }

    if (plater.config()) {
        builder.set_bed(*plater.config());
        if (wth && is_XL_printer(*plater.config())) {
            wth->xl_bb = bounding_box(get_bed_shape(*plater.config()));
        }
    }

    builder.set_wipe_tower_handler(std::move(wth));
    builder.set_model(plater.model());

    if (plater.printer_technology() == ptSLA)
        builder.set_sla_print(&plater.sla_print());
    else
        builder.set_fff_print(&plater.fff_print());

    return builder;
}

FillBedJob2::FillBedJob2(arr2::Scene &&scene, const Callbacks &cbs) : Base(std::move(scene), _u8L("Filling bed"), cbs) {}

ArrangeJob2::ArrangeJob2(arr2::Scene &&scene, const Callbacks &cbs) : Base(std::move(scene), _u8L("Arranging"), cbs) {}

}} // namespace Slic3r
