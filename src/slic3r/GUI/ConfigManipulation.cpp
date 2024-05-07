// #include "libslic3r/GCodeSender.hpp"
#include "ConfigManipulation.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "format.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "MsgDialog.hpp"

#include <string>
#include <wx/msgdlg.h>

namespace Slic3r {
namespace GUI {

void ConfigManipulation::apply(DynamicPrintConfig* config, DynamicPrintConfig* new_config)
{
    bool modified = false;
    for (auto opt_key : config->diff(*new_config)) {
        config->set_key_value(opt_key, new_config->option(opt_key)->clone());
        modified = true;
    }

    if (modified && load_config != nullptr)
        load_config();
}

void ConfigManipulation::toggle_field(const std::string& opt_key, const bool toggle, int opt_index/* = -1*/)
{
    if (local_config) {
        if (local_config->option(opt_key) == nullptr)
            return;
    }
    cb_toggle_field(opt_key, toggle, opt_index);
}

void ConfigManipulation::update_print_fff_config(DynamicPrintConfig* config, const bool is_global_config)
{
    // #ys_FIXME_to_delete
    //! Temporary workaround for the correct updates of the TextCtrl (like "layer_height"):
    // KillFocus() for the wxSpinCtrl use CallAfter function. So,
    // to except the duplicate call of the update() after dialog->ShowModal(),
    // let check if this process is already started.
    if (is_msg_dlg_already_exist)
        return;

    // layer_height shouldn't be equal to zero
    if (config->opt_float("layer_height") < EPSILON)
    {
        const wxString msg_text = _(L("Layer height is not valid.\n\nThe layer height will be reset to 0.01."));
        MessageDialog dialog(m_msg_dlg_parent, msg_text, _(L("Layer height")), wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist = true;
        dialog.ShowModal();
        new_conf.set_key_value("layer_height", new ConfigOptionFloat(0.01));
        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }

    if (config->option<ConfigOptionFloatOrPercent>("first_layer_height")->value < EPSILON)
    {
        const wxString msg_text = _(L("First layer height is not valid.\n\nThe first layer height will be reset to 0.01."));
        MessageDialog dialog(m_msg_dlg_parent, msg_text, _(L("First layer height")), wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist = true;
        dialog.ShowModal();
        new_conf.set_key_value("first_layer_height", new ConfigOptionFloatOrPercent(0.01, false));
        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }

    double fill_density = config->option<ConfigOptionPercent>("fill_density")->value;

    if (config->opt_bool("spiral_vase") &&
        ! (config->opt_int("perimeters") == 1 && 
           config->opt_int("top_solid_layers") == 0 &&
           fill_density == 0 &&
           ! config->opt_bool("support_material") &&
           config->opt_int("support_material_enforce_layers") == 0 &&
           ! config->opt_bool("thin_walls")))
    {
        wxString msg_text = _(L("The Spiral Vase mode requires:\n"
                                "- one perimeter\n"
                                "- no top solid layers\n"
                                "- 0% fill density\n"
                                "- no support material\n"
               					"- Detect thin walls disabled"));
        if (is_global_config)
            msg_text += "\n\n" + _(L("Shall I adjust those settings in order to enable Spiral Vase?"));
        MessageDialog dialog(m_msg_dlg_parent, msg_text, _(L("Spiral Vase")),
                               wxICON_WARNING | (is_global_config ? wxYES | wxNO : wxOK));
        DynamicPrintConfig new_conf = *config;
        auto answer = dialog.ShowModal();
        bool support = true;
        if (!is_global_config || answer == wxID_YES) {
            new_conf.set_key_value("perimeters", new ConfigOptionInt(1));
            new_conf.set_key_value("top_solid_layers", new ConfigOptionInt(0));
            new_conf.set_key_value("fill_density", new ConfigOptionPercent(0));
            new_conf.set_key_value("support_material", new ConfigOptionBool(false));
            new_conf.set_key_value("support_material_enforce_layers", new ConfigOptionInt(0));
            new_conf.set_key_value("thin_walls", new ConfigOptionBool(false));            
            fill_density = 0;
            support = false;
        }
        else {
            new_conf.set_key_value("spiral_vase", new ConfigOptionBool(false));
        }
        apply(config, &new_conf);
        if (cb_value_change) {
            cb_value_change("fill_density", fill_density);
            if (!support)
                cb_value_change("support_material", false);
        }
    }

    if (config->opt_bool("wipe_tower") && config->opt_bool("support_material") && 
        // Organic supports are always synchronized with object layers as of now.
        config->opt_enum<SupportMaterialStyle>("support_material_style") != smsOrganic) {
        if (config->opt_float("support_material_contact_distance") == 0) {
            if (!config->opt_bool("support_material_synchronize_layers")) {
                wxString msg_text = _(L("For the Wipe Tower to work with the soluble supports, the support layers\n"
                                        "need to be synchronized with the object layers."));
                if (is_global_config)
                    msg_text += "\n\n" + _(L("Shall I synchronize support layers in order to enable the Wipe Tower?"));
                MessageDialog dialog(m_msg_dlg_parent, msg_text, _(L("Wipe Tower")),
                                       wxICON_WARNING | (is_global_config ? wxYES | wxNO : wxOK));
                DynamicPrintConfig new_conf = *config;
                auto answer = dialog.ShowModal();
                if (!is_global_config || answer == wxID_YES) {
                    new_conf.set_key_value("support_material_synchronize_layers", new ConfigOptionBool(true));
                }
                else
                    new_conf.set_key_value("wipe_tower", new ConfigOptionBool(false));
                apply(config, &new_conf);
            }
        } else {
            if ((config->opt_int("support_material_extruder") != 0 || config->opt_int("support_material_interface_extruder") != 0)) {
                wxString msg_text = _(L("The Wipe Tower currently supports the non-soluble supports only "
                                        "if they are printed with the current extruder without triggering a tool change. "
                                        "(both support_material_extruder and support_material_interface_extruder need to be set to 0)."));
                if (is_global_config)
                    msg_text += "\n\n" + _(L("Shall I adjust those settings in order to enable the Wipe Tower?"));
                MessageDialog dialog (m_msg_dlg_parent, msg_text, _(L("Wipe Tower")),
                                        wxICON_WARNING | (is_global_config ? wxYES | wxNO : wxOK));
                DynamicPrintConfig new_conf = *config;
                auto answer = dialog.ShowModal();
                if (!is_global_config || answer == wxID_YES) {
                    new_conf.set_key_value("support_material_extruder", new ConfigOptionInt(0));
                    new_conf.set_key_value("support_material_interface_extruder", new ConfigOptionInt(0));
                }
                else
                    new_conf.set_key_value("wipe_tower", new ConfigOptionBool(false));
                apply(config, &new_conf);
            }
        }
    }

    // Check "support_material" and "overhangs" relations only on global settings level
    if (is_global_config && config->opt_bool("support_material")) {
        // Ask only once.
        if (!m_support_material_overhangs_queried) {
            m_support_material_overhangs_queried = true;
            if (!config->opt_bool("overhangs")/* != 1*/) {
                wxString msg_text = _(L("Supports work better, if the following feature is enabled:\n"
                                        "- Detect bridging perimeters"));
                if (is_global_config)
                    msg_text += "\n\n" + _(L("Shall I adjust those settings for supports?"));
                MessageDialog dialog(m_msg_dlg_parent, msg_text, _L("Support Generator"), wxICON_WARNING | wxYES | wxNO);
                DynamicPrintConfig new_conf = *config;
                auto answer = dialog.ShowModal();
                if (answer == wxID_YES) {
                    // Enable "detect bridging perimeters".
                    new_conf.set_key_value("overhangs", new ConfigOptionBool(true));
                }
                //else Do nothing, leave supports on and "detect bridging perimeters" off.
                apply(config, &new_conf);
            }
        }
    }
    else {
        m_support_material_overhangs_queried = false;
    }

    //w12
    if (abs(config->option<ConfigOptionFloat>("xy_hole_compensation")->value) > 2) {
        const wxString     msg_text = _(L("This setting is only used for model size tunning with small value in some cases.\n"
                                      "For example, when model size has small error and hard to be assembled.\n"
                                      "For large size tuning, please use model scale function.\n\n"
                                      "The value will be reset to 0."));
        MessageDialog      dialog(m_msg_dlg_parent, msg_text, "", wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist    = true;
        dialog.ShowModal();
        new_conf.set_key_value("xy_hole_compensation", new ConfigOptionFloat(0));
        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }

    if (abs(config->option<ConfigOptionFloat>("xy_contour_compensation")->value) > 2) {
        const wxString     msg_text = _(L("This setting is only used for model size tunning with small value in some cases.\n"
                                      "For example, when model size has small error and hard to be assembled.\n"
                                      "For large size tuning, please use model scale function.\n\n"
                                      "The value will be reset to 0."));
        MessageDialog      dialog(m_msg_dlg_parent, msg_text, "", wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist    = true;
        dialog.ShowModal();
        new_conf.set_key_value("xy_contour_compensation", new ConfigOptionFloat(0));
        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }

    if (config->option<ConfigOptionPercent>("fill_density")->value == 100) {
        const int fill_pattern = config->option<ConfigOptionEnum<InfillPattern>>("fill_pattern")->value;
        if (bool correct_100p_fill = config->option_def("top_fill_pattern")->enum_def->enum_to_index(fill_pattern).has_value(); 
            ! correct_100p_fill) {
            // get fill_pattern name from enum_labels for using this one at dialog_msg
            const ConfigOptionDef *fill_pattern_def = config->option_def("fill_pattern");
            assert(fill_pattern_def != nullptr);
            if (auto label = fill_pattern_def->enum_def->enum_to_label(fill_pattern); label.has_value()) {
                wxString msg_text = GUI::format_wxstr(_L("The %1% infill pattern is not supposed to work at 100%% density."), _(*label));
                if (is_global_config)
                    msg_text += "\n\n" + _L("Shall I switch to rectilinear fill pattern?");
                MessageDialog dialog(m_msg_dlg_parent, msg_text, _L("Infill"),
                                                  wxICON_WARNING | (is_global_config ? wxYES | wxNO : wxOK) );
                DynamicPrintConfig new_conf = *config;
                auto answer = dialog.ShowModal();
                if (!is_global_config || answer == wxID_YES) {
                    new_conf.set_key_value("fill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinear));
                    fill_density = 100;
                }
                else
                    fill_density = wxGetApp().preset_bundle->prints.get_selected_preset().config.option<ConfigOptionPercent>("fill_density")->value;
                new_conf.set_key_value("fill_density", new ConfigOptionPercent(fill_density));
                apply(config, &new_conf);
                if (cb_value_change)
                    cb_value_change("fill_density", fill_density);
            }
        }
    }
}

void ConfigManipulation::toggle_print_fff_options(DynamicPrintConfig* config)
{
    bool have_perimeters = config->opt_int("perimeters") > 0;
    for (auto el : { "extra_perimeters","extra_perimeters_on_overhangs", "thin_walls", "overhangs",
                    // Y21
                    "seam_position","seam_gap","staggered_inner_seams", "external_perimeters_first", "external_perimeter_extrusion_width",
                    "perimeter_speed", "small_perimeter_speed", "external_perimeter_speed", "enable_dynamic_overhang_speeds"})
        toggle_field(el, have_perimeters);

    for (size_t i = 0; i < 4; i++) {
        toggle_field("overhang_speed_" + std::to_string(i), config->opt_bool("enable_dynamic_overhang_speeds"));
    }

    bool have_infill = config->option<ConfigOptionPercent>("fill_density")->value > 0;
    // infill_extruder uses the same logic as in Print::extruders()
    for (auto el : { "fill_pattern", "infill_every_layers", "infill_only_where_needed",
                    "solid_infill_every_layers", "solid_infill_below_area", "infill_extruder", "infill_anchor_max" })
        toggle_field(el, have_infill);
    // Only allow configuration of open anchors if the anchoring is enabled.
    bool has_infill_anchors = have_infill && config->option<ConfigOptionFloatOrPercent>("infill_anchor_max")->value > 0;
    toggle_field("infill_anchor", has_infill_anchors);

    bool has_spiral_vase         = config->opt_bool("spiral_vase");
    bool has_top_solid_infill 	 = config->opt_int("top_solid_layers") > 0;
    bool has_bottom_solid_infill = config->opt_int("bottom_solid_layers") > 0;
    bool has_solid_infill 		 = has_top_solid_infill || has_bottom_solid_infill;
    // solid_infill_extruder uses the same logic as in Print::extruders()
    for (auto el : { "top_fill_pattern", "bottom_fill_pattern", "infill_first", "solid_infill_extruder",
                    "solid_infill_extrusion_width", "solid_infill_speed" })
        toggle_field(el, has_solid_infill);

    for (auto el : { "fill_angle", "bridge_angle", "infill_extrusion_width",
                    "infill_speed", "bridge_speed" })
        toggle_field(el, have_infill || has_solid_infill);

    toggle_field("top_solid_min_thickness", ! has_spiral_vase && has_top_solid_infill);
    toggle_field("bottom_solid_min_thickness", ! has_spiral_vase && has_bottom_solid_infill);

    // Gap fill is newly allowed in between perimeter lines even for empty infill (see GH #1476).
    toggle_field("gap_fill_speed", have_perimeters);

    for (auto el : { "top_infill_extrusion_width", "top_solid_infill_speed" })
        toggle_field(el, has_top_solid_infill || (has_spiral_vase && has_bottom_solid_infill));

    bool have_default_acceleration = config->opt_float("default_acceleration") > 0;
    for (auto el : { "perimeter_acceleration", "infill_acceleration", "top_solid_infill_acceleration",
                    "solid_infill_acceleration", "external_perimeter_acceleration",
                    "bridge_acceleration", "first_layer_acceleration", "wipe_tower_acceleration"})
        toggle_field(el, have_default_acceleration);

    bool have_skirt = config->opt_int("skirts") > 0;
    toggle_field("skirt_height", have_skirt && config->opt_enum<DraftShield>("draft_shield") != dsEnabled);
    for (auto el : { "skirt_distance", "draft_shield", "min_skirt_length" })
        toggle_field(el, have_skirt);

    bool have_brim = config->opt_enum<BrimType>("brim_type") != btNoBrim;
    for (auto el : { "brim_width", "brim_separation" })
        toggle_field(el, have_brim);
    // perimeter_extruder uses the same logic as in Print::extruders()
    toggle_field("perimeter_extruder", have_perimeters || have_brim);

    bool have_raft = config->opt_int("raft_layers") > 0;
    bool have_support_material = config->opt_bool("support_material") || have_raft;
    bool have_support_material_auto = have_support_material && config->opt_bool("support_material_auto");
    bool have_support_interface = config->opt_int("support_material_interface_layers") > 0;
    bool have_support_soluble = have_support_material && config->opt_float("support_material_contact_distance") == 0;
    auto support_material_style = config->opt_enum<SupportMaterialStyle>("support_material_style");
    for (auto el : { "support_material_style", "support_material_pattern", "support_material_with_sheath",
                    "support_material_spacing", "support_material_angle", 
                    "support_material_interface_pattern", "support_material_interface_layers",
                    "dont_support_bridges", "support_material_extrusion_width", "support_material_contact_distance",
                    "support_material_xy_spacing" })
        toggle_field(el, have_support_material);
    toggle_field("support_material_threshold", have_support_material_auto);
    toggle_field("support_material_bottom_contact_distance", have_support_material && ! have_support_soluble);
    toggle_field("support_material_closing_radius", have_support_material && support_material_style == smsSnug);
    //w28
    bool can_remove_bridge = have_support_material && !config->opt_bool("dont_support_bridges") ;
    toggle_field("max_bridge_length", can_remove_bridge && support_material_style == smsOrganic);

    const bool has_organic_supports = support_material_style == smsOrganic && 
                                     (config->opt_bool("support_material") || 
                                      config->opt_int("support_material_enforce_layers") > 0);
    for (const std::string& key : { "support_tree_angle", "support_tree_angle_slow", "support_tree_branch_diameter",
                                    "support_tree_branch_diameter_angle", "support_tree_branch_diameter_double_wall", 
                                    "support_tree_tip_diameter", "support_tree_branch_distance", "support_tree_top_rate" })
        toggle_field(key, has_organic_supports);

    //w34
    for (auto el : { "support_material_bottom_interface_layers", "support_material_interface_spacing", "support_material_interface_extruder",
                    "support_material_interface_speed", "support_material_interface_contact_loops","support_material_synchronize_layers" })
        toggle_field(el, have_support_material && have_support_interface);
    //toggle_field("support_material_synchronize_layers", have_support_soluble);

    toggle_field("perimeter_extrusion_width", have_perimeters || have_skirt || have_brim);
    toggle_field("support_material_extruder", have_support_material || have_skirt);
    toggle_field("support_material_speed", have_support_material || have_brim || have_skirt);

    toggle_field("raft_contact_distance", have_raft && !have_support_soluble);
    for (auto el : { "raft_expansion", "first_layer_acceleration_over_raft", "first_layer_speed_over_raft" })
        toggle_field(el, have_raft);

    bool has_ironing = config->opt_bool("ironing");
    //w33
    for (auto el : {"ironing_type", "ironing_flowrate", "ironing_spacing", "ironing_speed", "ironing_pattern"})
    	toggle_field(el, has_ironing);

    bool have_sequential_printing = config->opt_bool("complete_objects");
    for (auto el : { "extruder_clearance_radius", "extruder_clearance_height" })
        toggle_field(el, have_sequential_printing);

    bool have_ooze_prevention = config->opt_bool("ooze_prevention");
    toggle_field("standby_temperature_delta", have_ooze_prevention);

    bool have_wipe_tower = config->opt_bool("wipe_tower");
    for (auto el : { "wipe_tower_x", "wipe_tower_y", "wipe_tower_width", "wipe_tower_rotation_angle", "wipe_tower_brim_width", "wipe_tower_cone_angle",
                     "wipe_tower_extra_spacing", "wipe_tower_extra_flow", "wipe_tower_bridging", "wipe_tower_no_sparse_layers", "single_extruder_multi_material_priming" })
        toggle_field(el, have_wipe_tower);

    bool have_non_zero_mmu_segmented_region_max_width = config->opt_float("mmu_segmented_region_max_width") > 0.;
    toggle_field("mmu_segmented_region_interlocking_depth", have_non_zero_mmu_segmented_region_max_width);

    toggle_field("avoid_crossing_curled_overhangs", !config->opt_bool("avoid_crossing_perimeters"));
    toggle_field("avoid_crossing_perimeters", !config->opt_bool("avoid_crossing_curled_overhangs"));

    bool have_avoid_crossing_perimeters = config->opt_bool("avoid_crossing_perimeters");
    toggle_field("avoid_crossing_perimeters_max_detour", have_avoid_crossing_perimeters);

    bool have_arachne = config->opt_enum<PerimeterGeneratorType>("perimeter_generator") == PerimeterGeneratorType::Arachne;
    toggle_field("wall_transition_length", have_arachne);
    toggle_field("wall_transition_filter_deviation", have_arachne);
    toggle_field("wall_transition_angle", have_arachne);
    toggle_field("wall_distribution_count", have_arachne);
    toggle_field("min_feature_size", have_arachne);
    toggle_field("min_bead_width", have_arachne);
    toggle_field("thin_walls", !have_arachne);
    //w21
    bool is_top_one_wall = config->opt_enum<TopOneWallType>("top_one_wall_type") != TopOneWallType::Disable;
    toggle_field("top_area_threshold", is_top_one_wall);

}

void ConfigManipulation::toggle_print_sla_options(DynamicPrintConfig* config)
{
    bool supports_en = config->opt_bool("supports_enable");
    sla::SupportTreeType treetype = config->opt_enum<sla::SupportTreeType>("support_tree_type");
    bool is_default_tree = treetype == sla::SupportTreeType::Default;
    bool is_branching_tree = treetype == sla::SupportTreeType::Branching;

    toggle_field("support_tree_type", supports_en);

    toggle_field("support_head_front_diameter", supports_en && is_default_tree);
    toggle_field("support_head_penetration", supports_en && is_default_tree);
    toggle_field("support_head_width", supports_en && is_default_tree);
    toggle_field("support_pillar_diameter", supports_en && is_default_tree);
    toggle_field("support_small_pillar_diameter_percent", supports_en && is_default_tree);
    toggle_field("support_max_bridges_on_pillar", supports_en && is_default_tree);
    toggle_field("support_pillar_connection_mode", supports_en && is_default_tree);
    toggle_field("support_buildplate_only", supports_en && is_default_tree);
    toggle_field("support_base_diameter", supports_en && is_default_tree);
    toggle_field("support_base_height", supports_en && is_default_tree);
    toggle_field("support_base_safety_distance", supports_en && is_default_tree);
    toggle_field("support_critical_angle", supports_en && is_default_tree);
    toggle_field("support_max_bridge_length", supports_en && is_default_tree);
    toggle_field("support_enforcers_only", supports_en);
    toggle_field("support_max_pillar_link_distance", supports_en && is_default_tree);
    toggle_field("support_pillar_widening_factor", false);
    toggle_field("support_max_weight_on_model", false);

    toggle_field("branchingsupport_head_front_diameter", supports_en && is_branching_tree);
    toggle_field("branchingsupport_head_penetration", supports_en && is_branching_tree);
    toggle_field("branchingsupport_head_width", supports_en && is_branching_tree);
    toggle_field("branchingsupport_pillar_diameter", supports_en && is_branching_tree);
    toggle_field("branchingsupport_small_pillar_diameter_percent", supports_en && is_branching_tree);
    toggle_field("branchingsupport_max_bridges_on_pillar", false);
    toggle_field("branchingsupport_pillar_connection_mode", false);
    toggle_field("branchingsupport_buildplate_only", supports_en && is_branching_tree);
    toggle_field("branchingsupport_base_diameter", supports_en && is_branching_tree);
    toggle_field("branchingsupport_base_height", supports_en && is_branching_tree);
    toggle_field("branchingsupport_base_safety_distance", supports_en && is_branching_tree);
    toggle_field("branchingsupport_critical_angle", supports_en && is_branching_tree);
    toggle_field("branchingsupport_max_bridge_length", supports_en && is_branching_tree);
    toggle_field("branchingsupport_max_pillar_link_distance", false);
    toggle_field("branchingsupport_pillar_widening_factor", supports_en && is_branching_tree);
    toggle_field("branchingsupport_max_weight_on_model", supports_en && is_branching_tree);

    toggle_field("support_points_density_relative", supports_en);
    toggle_field("support_points_minimal_distance", supports_en);

    bool pad_en = config->opt_bool("pad_enable");

    toggle_field("pad_wall_thickness", pad_en);
    toggle_field("pad_wall_height", pad_en);
    toggle_field("pad_brim_size", pad_en);
    toggle_field("pad_max_merge_distance", pad_en);
 // toggle_field("pad_edge_radius", supports_en);
    toggle_field("pad_wall_slope", pad_en);
    toggle_field("pad_around_object", pad_en);
    toggle_field("pad_around_object_everywhere", pad_en);

    bool zero_elev = config->opt_bool("pad_around_object") && pad_en;

    toggle_field("support_object_elevation", supports_en && is_default_tree && !zero_elev);
    toggle_field("branchingsupport_object_elevation", supports_en && is_branching_tree && !zero_elev);
    toggle_field("pad_object_gap", zero_elev);
    toggle_field("pad_around_object_everywhere", zero_elev);
    toggle_field("pad_object_connector_stride", zero_elev);
    toggle_field("pad_object_connector_width", zero_elev);
    toggle_field("pad_object_connector_penetration", zero_elev);
}


} // GUI
} // Slic3r
