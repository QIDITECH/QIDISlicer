
#ifndef ARRANGESETTINGSVIEW_HPP
#define ARRANGESETTINGSVIEW_HPP

namespace Slic3r { namespace arr2 {

class ArrangeSettingsView
{
public:
    enum GeometryHandling { ghConvex, ghBalanced, ghAdvanced, ghCount };
    enum ArrangeStrategy { asAuto, asPullToCenter, asCount };
    enum XLPivots {
        xlpCenter,
        xlpRearLeft,
        xlpFrontLeft,
        xlpFrontRight,
        xlpRearRight,
        xlpRandom,
        xlpCount
    };

    virtual ~ArrangeSettingsView() = default;

    virtual float get_distance_from_objects() const = 0;
    virtual float get_distance_from_bed() const     = 0;
    virtual bool  is_rotation_enabled() const       = 0;

    virtual XLPivots         get_xl_alignment() const      = 0;
    virtual GeometryHandling get_geometry_handling() const = 0;
    virtual ArrangeStrategy  get_arrange_strategy() const  = 0;
};

class ArrangeSettingsDb: public ArrangeSettingsView
{
public:

    virtual void distance_from_obj_range(float &min, float &max) const = 0;
    virtual void distance_from_bed_range(float &min, float &max) const = 0;

    virtual ArrangeSettingsDb& set_distance_from_objects(float v) = 0;
    virtual ArrangeSettingsDb& set_distance_from_bed(float v) = 0;
    virtual ArrangeSettingsDb& set_rotation_enabled(bool v) = 0;

    virtual ArrangeSettingsDb& set_xl_alignment(XLPivots v) = 0;
    virtual ArrangeSettingsDb& set_geometry_handling(GeometryHandling v) = 0;
    virtual ArrangeSettingsDb& set_arrange_strategy(ArrangeStrategy v) = 0;

    struct Values {
        float d_obj = 6.f, d_bed = 0.f;
        bool rotations = false;
        XLPivots xl_align = XLPivots::xlpFrontLeft;
        GeometryHandling geom_handling = GeometryHandling::ghConvex;
        ArrangeStrategy  arr_strategy = ArrangeStrategy::asAuto;

        Values() = default;
        Values(const ArrangeSettingsView &sv)
        {
            d_bed = sv.get_distance_from_bed();
            d_obj = sv.get_distance_from_objects();
            arr_strategy = sv.get_arrange_strategy();
            geom_handling = sv.get_geometry_handling();
            rotations = sv.is_rotation_enabled();
            xl_align = sv.get_xl_alignment();
        }
    };

    virtual Values get_defaults() const { return {}; }

    ArrangeSettingsDb& set_from(const ArrangeSettingsView &sv)
    {
        set_distance_from_bed(sv.get_distance_from_bed());
        set_distance_from_objects(sv.get_distance_from_objects());
        set_arrange_strategy(sv.get_arrange_strategy());
        set_geometry_handling(sv.get_geometry_handling());
        set_rotation_enabled(sv.is_rotation_enabled());
        set_xl_alignment(sv.get_xl_alignment());

        return *this;
    }
};

class ArrangeSettings: public Slic3r::arr2::ArrangeSettingsDb
{
    ArrangeSettingsDb::Values m_v = {};

public:
    explicit ArrangeSettings(
        const ArrangeSettingsDb::Values &v = {})
        : m_v{v}
    {}

    explicit ArrangeSettings(const ArrangeSettingsView &v)
        : m_v{v}
    {}

    float get_distance_from_objects() const override { return m_v.d_obj; }
    float get_distance_from_bed() const override { return m_v.d_bed; }
    bool  is_rotation_enabled() const override { return m_v.rotations; }
    XLPivots get_xl_alignment() const override { return m_v.xl_align; }
    GeometryHandling get_geometry_handling() const override { return m_v.geom_handling; }
    ArrangeStrategy get_arrange_strategy() const override { return m_v.arr_strategy; }

    void distance_from_obj_range(float &min, float &max) const override { min = 0.f; max = 100.f; }
    void distance_from_bed_range(float &min, float &max) const override { min = 0.f; max = 100.f; }

    ArrangeSettings& set_distance_from_objects(float v) override { m_v.d_obj = v; return *this; }
    ArrangeSettings& set_distance_from_bed(float v) override { m_v.d_bed = v; return *this; }
    ArrangeSettings& set_rotation_enabled(bool v) override { m_v.rotations = v; return *this; }
    ArrangeSettings& set_xl_alignment(XLPivots v) override { m_v.xl_align = v; return *this; }
    ArrangeSettings& set_geometry_handling(GeometryHandling v) override { m_v.geom_handling = v; return *this; }
    ArrangeSettings& set_arrange_strategy(ArrangeStrategy v) override { m_v.arr_strategy = v; return *this; }

    auto & values() const { return m_v; }
    auto & values() { return m_v; }
};

}} // namespace Slic3r::arr2

#endif // ARRANGESETTINGSVIEW_HPP
