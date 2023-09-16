
#include "ArrangeSettingsDb_AppCfg.hpp"

namespace Slic3r {

ArrangeSettingsDb_AppCfg::ArrangeSettingsDb_AppCfg(AppConfig *appcfg) : m_appcfg{appcfg}
{
    m_settings_fff.postfix = "_fff";
    m_settings_fff_seq.postfix = "_fff_seq_print";
    m_settings_sla.postfix = "_sla";

    std::string dist_fff_str =
        m_appcfg->get("arrange", "min_object_distance_fff");

    std::string dist_bed_fff_str =
        m_appcfg->get("arrange", "min_bed_distance_fff");

    std::string dist_fff_seq_print_str =
        m_appcfg->get("arrange", "min_object_distance_fff_seq_print");

    std::string dist_bed_fff_seq_print_str =
        m_appcfg->get("arrange", "min_bed_distance_fff_seq_print");

    std::string dist_sla_str =
        m_appcfg->get("arrange", "min_object_distance_sla");

    std::string dist_bed_sla_str =
        m_appcfg->get("arrange", "min_bed_distance_sla");

    std::string en_rot_fff_str =
        m_appcfg->get("arrange", "enable_rotation_fff");

    std::string en_rot_fff_seqp_str =
        m_appcfg->get("arrange", "enable_rotation_fff_seq_print");

    std::string en_rot_sla_str =
        m_appcfg->get("arrange", "enable_rotation_sla");

    //    std::string alignment_fff_str =
    //        m_appcfg->get("arrange", "alignment_fff");

    //    std::string alignment_fff_seqp_str =
    //        m_appcfg->get("arrange", "alignment_fff_seq_pring");

    //    std::string alignment_sla_str =
    //        m_appcfg->get("arrange", "alignment_sla");

    // Override default alignment and save save/load it to a temporary slot "alignment_xl"
    std::string alignment_xl_str =
        m_appcfg->get("arrange", "alignment_xl");

    std::string geom_handling_str =
        m_appcfg->get("arrange", "geometry_handling");

    std::string strategy_str =
        m_appcfg->get("arrange", "arrange_strategy");

    if (!dist_fff_str.empty())
        m_settings_fff.vals.d_obj = string_to_float_decimal_point(dist_fff_str);

    if (!dist_bed_fff_str.empty())
        m_settings_fff.vals.d_bed = string_to_float_decimal_point(dist_bed_fff_str);

    if (!dist_fff_seq_print_str.empty())
        m_settings_fff_seq.vals.d_obj = string_to_float_decimal_point(dist_fff_seq_print_str);

    if (!dist_bed_fff_seq_print_str.empty())
        m_settings_fff_seq.vals.d_bed = string_to_float_decimal_point(dist_bed_fff_seq_print_str);

    if (!dist_sla_str.empty())
        m_settings_sla.vals.d_obj = string_to_float_decimal_point(dist_sla_str);

    if (!dist_bed_sla_str.empty())
        m_settings_sla.vals.d_bed = string_to_float_decimal_point(dist_bed_sla_str);

    if (!en_rot_fff_str.empty())
        m_settings_fff.vals.rotations = (en_rot_fff_str == "1" || en_rot_fff_str == "yes");

    if (!en_rot_fff_seqp_str.empty())
        m_settings_fff_seq.vals.rotations = (en_rot_fff_seqp_str == "1" || en_rot_fff_seqp_str == "yes");

    if (!en_rot_sla_str.empty())
        m_settings_sla.vals.rotations = (en_rot_sla_str == "1" || en_rot_sla_str == "yes");

    //    if (!alignment_sla_str.empty())
    //        m_arrange_settings_sla.alignment = std::stoi(alignment_sla_str);

    //    if (!alignment_fff_str.empty())
    //        m_arrange_settings_fff.alignment = std::stoi(alignment_fff_str);

    //    if (!alignment_fff_seqp_str.empty())
    //        m_arrange_settings_fff_seq_print.alignment = std::stoi(alignment_fff_seqp_str);

    // Override default alignment and save save/load it to a temporary slot "alignment_xl"
    ArrangeSettingsView::XLPivots arr_alignment = ArrangeSettingsView::xlpFrontLeft;
    if (!alignment_xl_str.empty()) {
        int align_val = std::stoi(alignment_xl_str);

        if (align_val >= 0 && align_val < ArrangeSettingsView::xlpCount)
            arr_alignment =
                static_cast<ArrangeSettingsView::XLPivots>(align_val);
    }

    m_settings_sla.vals.xl_align = arr_alignment ;
    m_settings_fff.vals.xl_align = arr_alignment ;
    m_settings_fff_seq.vals.xl_align = arr_alignment ;

    ArrangeSettingsView::GeometryHandling geom_handl = arr2::ArrangeSettingsView::ghConvex;
    if (!geom_handling_str.empty()) {
        int gh = std::stoi(geom_handling_str);
        if(gh >= 0 && gh < ArrangeSettingsView::GeometryHandling::ghCount)
            geom_handl = static_cast<ArrangeSettingsView::GeometryHandling>(gh);
    }

    m_settings_sla.vals.geom_handling = geom_handl;
    m_settings_fff.vals.geom_handling = geom_handl;
    m_settings_fff_seq.vals.geom_handling = geom_handl;

    ArrangeSettingsView::ArrangeStrategy arr_strategy = arr2::ArrangeSettingsView::asAuto;
    if (!strategy_str.empty()) {
        int strateg = std::stoi(strategy_str);
        if(strateg >= 0 && strateg < ArrangeSettingsView::ArrangeStrategy::asCount)
            arr_strategy = static_cast<ArrangeSettingsView::ArrangeStrategy>(strateg);
    }

    m_settings_sla.vals.arr_strategy = arr_strategy;
    m_settings_fff.vals.arr_strategy = arr_strategy;
    m_settings_fff_seq.vals.arr_strategy = arr_strategy;
}

void ArrangeSettingsDb_AppCfg::distance_from_obj_range(float &min,
                                                       float &max) const
{
    min = get_slot(this).dobj_range.minval;
    max = get_slot(this).dobj_range.maxval;
}

void ArrangeSettingsDb_AppCfg::distance_from_bed_range(float &min,
                                                       float &max) const
{
    min = get_slot(this).dbed_range.minval;
    max = get_slot(this).dbed_range.maxval;
}

arr2::ArrangeSettingsDb& ArrangeSettingsDb_AppCfg::set_distance_from_objects(float v)
{
    Slot &slot = get_slot(this);
    slot.vals.d_obj = v;
    m_appcfg->set("arrange", "min_object_distance" + slot.postfix,
                  float_to_string_decimal_point(v));

    return *this;
}

arr2::ArrangeSettingsDb& ArrangeSettingsDb_AppCfg::set_distance_from_bed(float v)
{
    Slot &slot = get_slot(this);
    slot.vals.d_bed = v;
    m_appcfg->set("arrange", "min_bed_distance" + slot.postfix,
                  float_to_string_decimal_point(v));

    return *this;
}

arr2::ArrangeSettingsDb& ArrangeSettingsDb_AppCfg::set_rotation_enabled(bool v)
{
    Slot &slot = get_slot(this);
    slot.vals.rotations = v;
    m_appcfg->set("arrange", "enable_rotation" + slot.postfix, v ? "1" : "0");

    return *this;
}

arr2::ArrangeSettingsDb& ArrangeSettingsDb_AppCfg::set_xl_alignment(XLPivots v)
{
    m_settings_fff.vals.xl_align = v;
    m_appcfg->set("arrange", "alignment_xl", std::to_string(v));

    return *this;
}

arr2::ArrangeSettingsDb& ArrangeSettingsDb_AppCfg::set_geometry_handling(GeometryHandling v)
{
    m_settings_fff.vals.geom_handling = v;
    m_appcfg->set("arrange", "geometry_handling", std::to_string(v));

    return *this;
}

arr2::ArrangeSettingsDb& ArrangeSettingsDb_AppCfg::set_arrange_strategy(ArrangeStrategy v)
{
    m_settings_fff.vals.arr_strategy = v;
    m_appcfg->set("arrange", "arrange_strategy", std::to_string(v));

    return *this;
}

} // namespace Slic3r
