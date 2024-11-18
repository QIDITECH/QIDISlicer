#include <cassert>

#include "Exception.hpp"
#include "Preset.hpp"
#include "AppConfig.hpp"
#include "I18N.hpp"

#ifdef _MSC_VER
    #define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
    #define NOMINMAX
#endif
    #include <Windows.h>
#endif /* _MSC_VER */


#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <boost/range/join.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <boost/nowide/convert.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/locale.hpp>
#include <boost/log/trivial.hpp>

#include "libslic3r.h"
#include "Utils.hpp"
#include "PlaceholderParser.hpp"
#include "libslic3r/GCode/Thumbnails.hpp"

#include "PresetBundle.hpp"

using boost::property_tree::ptree;

namespace Slic3r {

ConfigFileType guess_config_file_type(const ptree &tree)
{
    size_t app_config   = 0;
    size_t bundle       = 0;
    size_t config       = 0;
    for (const ptree::value_type &v : tree) {
        if (v.second.empty()) {
            if (v.first == "background_processing" ||
                v.first == "last_output_path" ||
                v.first == "no_controller" ||
                v.first == "no_defaults")
                ++ app_config;
            else if (v.first == "nozzle_diameter" ||
                v.first == "filament_diameter")
                ++ config;
        } else if (boost::algorithm::starts_with(v.first, "print:") ||
            boost::algorithm::starts_with(v.first, "filament:") ||
            boost::algorithm::starts_with(v.first, "printer:") ||
            v.first == "settings")
            ++ bundle;
        else if (v.first == "presets") {
            ++ app_config;
            ++ bundle;
        } else if (v.first == "recent") {
            for (auto &kvp : v.second)
                if (kvp.first == "config_directory" || kvp.first == "skein_directory")
                    ++ app_config;
        }
    }
    return (app_config > bundle && app_config > config) ? CONFIG_FILE_TYPE_APP_CONFIG :
           (bundle > config) ? CONFIG_FILE_TYPE_CONFIG_BUNDLE : CONFIG_FILE_TYPE_CONFIG;
}


VendorProfile VendorProfile::from_ini(const boost::filesystem::path &path, bool load_all)
{
    ptree tree;
    boost::nowide::ifstream ifs(path.string());
    boost::property_tree::read_ini(ifs, tree);
    return VendorProfile::from_ini(tree, path, load_all);
}

static const std::unordered_map<std::string, std::string> pre_family_model_map {{
    { "MK3",        "MK3" },
    { "MK3MMU2",    "MK3" },
    { "MK2.5",      "MK2.5" },
    { "MK2.5MMU2",  "MK2.5" },
    { "MK2S",       "MK2" },
    { "MK2SMM",     "MK2" },
    { "SL1",        "SL1" },
}};

VendorProfile VendorProfile::from_ini(const ptree &tree, const boost::filesystem::path &path, bool load_all)
{
    static const std::string printer_model_key = "printer_model:";
    static const std::string filaments_section = "default_filaments";
    static const std::string materials_section = "default_sla_materials";

    const std::string id = path.stem().string();

    if (! boost::filesystem::exists(path)) {
        throw Slic3r::RuntimeError((boost::format("Cannot load Vendor Config Bundle `%1%`: File not found: `%2%`.") % id % path).str());
    }

    VendorProfile res(id);

    // Helper to get compulsory fields
    auto get_or_throw = [&](const ptree &tree, const std::string &key) -> ptree::const_assoc_iterator
    {
        auto res = tree.find(key);
        if (res == tree.not_found()) {
            throw Slic3r::RuntimeError((boost::format("Vendor Config Bundle `%1%` is not valid: Missing secion or key: `%2%`.") % id % key).str());
        }
        return res;
    };

    // Load the header
    const auto &vendor_section = get_or_throw(tree, "vendor")->second;
    res.name = get_or_throw(vendor_section, "name")->second.data();

    auto config_version_str = get_or_throw(vendor_section, "config_version")->second.data();
    auto config_version = Semver::parse(config_version_str);
    if (! config_version) {
        throw Slic3r::RuntimeError((boost::format("Vendor Config Bundle `%1%` is not valid: Cannot parse config_version: `%2%`.") % id % config_version_str).str());
    } else {
        res.config_version = std::move(*config_version);
    }

    // Load URLs
    const auto config_update_url = vendor_section.find("config_update_url");
    if (config_update_url != vendor_section.not_found()) {
        res.config_update_url = config_update_url->second.data();
    }

    const auto changelog_url = vendor_section.find("changelog_url");
    if (changelog_url != vendor_section.not_found()) {
        res.changelog_url = changelog_url->second.data();
    }

    const auto templates_profile = vendor_section.find("templates_profile");
    if (templates_profile != vendor_section.not_found()) {
        res.templates_profile = templates_profile->second.data() == "1";
    }

    const auto repo_id = vendor_section.find("repo_id");
    if (repo_id != vendor_section.not_found()) {
        res.repo_id = repo_id->second.data();
    } else {
        // For backward compatibility assume all profiles without repo_id are from "prod" repo
        // DK: "No, dont!"
        res.repo_id = "";
    }

    const auto repo_prefix = vendor_section.find("repo_prefix");
    if (repo_prefix != vendor_section.not_found()) {
         res.repo_prefix = repo_prefix->second.data();
    } else {
        res.repo_prefix = "";
    }

    if (! load_all) {
        return res;
    }

    // Load printer models
    for (auto &section : tree) {
        if (boost::starts_with(section.first, printer_model_key)) {
            VendorProfile::PrinterModel model;
            model.id = section.first.substr(printer_model_key.size());
            model.name = section.second.get<std::string>("name", model.id);

            const char *technology_fallback = boost::algorithm::starts_with(model.id, "SL") ? "SLA" : "FFF";

            auto technology_field = section.second.get<std::string>("technology", technology_fallback);
            if (! ConfigOptionEnum<PrinterTechnology>::from_string(technology_field, model.technology)) {
                BOOST_LOG_TRIVIAL(error) << boost::format("Vendor bundle: `%1%`: Invalid printer technology field: `%2%`") % id % technology_field;
                model.technology = ptFFF;
            }

            model.family = section.second.get<std::string>("family", std::string());
            if (model.family.empty() && res.name == "QIDI Technology") {
                // If no family is specified, it can be inferred for known printers
                const auto from_pre_map = pre_family_model_map.find(model.id);
                if (from_pre_map != pre_family_model_map.end()) { model.family = from_pre_map->second; }
            }
#if 0
            // Remove SLA printers from the initial alpha.
            if (model.technology == ptSLA)
                continue;
#endif
            section.second.get<std::string>("variants", "");
            const auto variants_field = section.second.get<std::string>("variants", "");
            std::vector<std::string> variants;
            if (Slic3r::unescape_strings_cstyle(variants_field, variants)) {
                for (const std::string &variant_name : variants) {
                    if (model.variant(variant_name) == nullptr)
                        model.variants.emplace_back(VendorProfile::PrinterVariant(variant_name));
                }
            } else {
                BOOST_LOG_TRIVIAL(error) << boost::format("Vendor bundle: `%1%`: Malformed variants field: `%2%`") % id % variants_field;
            }
            auto default_materials_field = section.second.get<std::string>("default_materials", "");
            if (default_materials_field.empty())
            	default_materials_field = section.second.get<std::string>("default_filaments", "");
            if (Slic3r::unescape_strings_cstyle(default_materials_field, model.default_materials)) {
            	Slic3r::sort_remove_duplicates(model.default_materials);
            	if (! model.default_materials.empty() && model.default_materials.front().empty())
            		// An empty material was inserted into the list of default materials. Remove it.
            		model.default_materials.erase(model.default_materials.begin());
            } else {
                BOOST_LOG_TRIVIAL(error) << boost::format("Vendor bundle: `%1%`: Malformed default_materials field: `%2%`") % id % default_materials_field;
            }
            model.bed_model   = section.second.get<std::string>("bed_model", "");
            model.bed_texture = section.second.get<std::string>("bed_texture", "");
            model.thumbnail   = section.second.get<std::string>("thumbnail", "");
            if (model.thumbnail.empty())
                model.thumbnail = model.id + "_thumbnail.png";

            if (! model.id.empty() && ! model.variants.empty())
                res.models.push_back(std::move(model));
        }
    }

    // Load filaments and sla materials to be installed by default
    const auto filaments = tree.find(filaments_section);
    if (filaments != tree.not_found()) {
        for (auto &pair : filaments->second) {
            if (pair.second.data() == "1") {
                res.default_filaments.insert(pair.first);
            }
        }
    }
    const auto materials = tree.find(materials_section);
    if (materials != tree.not_found()) {
        for (auto &pair : materials->second) {
            if (pair.second.data() == "1") {
                res.default_sla_materials.insert(pair.first);
            }
        }
    }

    return res;
}

std::vector<std::string> VendorProfile::families() const
{
    std::vector<std::string> res;
    unsigned num_familiies = 0;

    for (auto &model : models) {
        if (std::find(res.begin(), res.end(), model.family) == res.end()) {
            res.push_back(model.family);
            num_familiies++;
        }
    }

    return res;
}

// Suffix to be added to a modified preset name in the combo box.
static std::string g_suffix_modified = " (modified)";
const std::string& Preset::suffix_modified()
{
    return g_suffix_modified;
}

void Preset::update_suffix_modified(const std::string& new_suffix_modified)
{
    g_suffix_modified = new_suffix_modified;
}
// Remove an optional "(modified)" suffix from a name.
// This converts a UI name to a unique preset identifier.
std::string Preset::remove_suffix_modified(const std::string &name)
{
    return boost::algorithm::ends_with(name, g_suffix_modified) ?
        name.substr(0, name.size() - g_suffix_modified.size()) :
        name;
}

// Update new extruder fields at the printer profile.
void Preset::normalize(DynamicPrintConfig &config)
{
    auto *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(config.option("nozzle_diameter"));
    if (nozzle_diameter != nullptr)
        // Loaded the FFF Printer settings. Verify, that all extruder dependent values have enough values.
        config.set_num_extruders((unsigned int)nozzle_diameter->values.size());
    if (config.option("filament_diameter") != nullptr) {
        // This config contains single or multiple filament presets.
        // Ensure that the filament preset vector options contain the correct number of values.
        size_t n = (nozzle_diameter == nullptr) ? 1 : nozzle_diameter->values.size();
        const auto &defaults = FullPrintConfig::defaults();
        for (const std::string &key : Preset::filament_options()) {
            if (key == "compatible_prints" || key == "compatible_printers")
                continue;
            auto *opt = config.option(key, false);
            /*assert(opt != nullptr);
            assert(opt->is_vector());*/
            if (opt != nullptr && opt->is_vector())
                static_cast<ConfigOptionVectorBase*>(opt)->resize(n, defaults.option(key));
        }
        // The following keys are mandatory for the UI, but they are not part of FullPrintConfig, therefore they are handled separately.
        for (const std::string &key : { "filament_settings_id" }) {
            auto *opt = config.option(key, false);
            assert(opt == nullptr || opt->type() == coStrings);
            if (opt != nullptr && opt->type() == coStrings)
                static_cast<ConfigOptionStrings*>(opt)->values.resize(n, std::string());
        }
    }
    if (const auto *gap_fill_speed = config.option<ConfigOptionFloat>("gap_fill_speed", false); gap_fill_speed && gap_fill_speed->value <= 0.) {
        // Legacy conversion. If the gap fill speed is zero, it means the gap fill is not enabled.
        // Set the new gap_fill_enabled value, so that it will show up in the UI as disabled.
        if (auto *gap_fill_enabled = config.option<ConfigOptionBool>("gap_fill_enabled", false); gap_fill_enabled)
            gap_fill_enabled->value = false;
    }
    if (auto *first_layer_height = config.option<ConfigOptionFloatOrPercent>("first_layer_height", false); first_layer_height && first_layer_height->percent)
        if (const auto *layer_height = config.option<ConfigOptionFloat>("layer_height", false); layer_height) {
            // Legacy conversion - first_layer_height moved from PrintObject setting to a Print setting, thus we are getting rid of the dependency
            // of first_layer_height on PrintObject specific layer_height. Covert the first layer heigth to an absolute value.
            first_layer_height->value   = first_layer_height->get_abs_value(layer_height->value);
            first_layer_height->percent = false;
        }

    handle_legacy_sla(config);
}

std::string Preset::remove_invalid_keys(DynamicPrintConfig &config, const DynamicPrintConfig &default_config)
{
    std::string incorrect_keys;
    for (const std::string &key : config.keys())
        if (! default_config.has(key)) {
            if (incorrect_keys.empty())
                incorrect_keys = key;
            else {
                incorrect_keys += ", ";
                incorrect_keys += key;
            }
            config.erase(key);
        }
    return incorrect_keys;
}

void Preset::save()
{
    this->config.save(this->file);
}

// Return a label of this preset, consisting of a name and a "(modified)" suffix, if this preset is dirty.
std::string Preset::label() const
{
    return this->name + (this->is_dirty ? g_suffix_modified : "");
}

bool is_compatible_with_print(const PresetWithVendorProfile &preset, const PresetWithVendorProfile &active_print, const PresetWithVendorProfile &active_printer)
{
    // templates_profile vendor profiles should be decided as same vendor profiles
	if (preset.vendor != nullptr && preset.vendor != active_printer.vendor && !preset.vendor->templates_profile)
		// The current profile has a vendor assigned and it is different from the active print's vendor.
		return false;
    auto &condition             = preset.preset.compatible_prints_condition();
    auto *compatible_prints     = dynamic_cast<const ConfigOptionStrings*>(preset.preset.config.option("compatible_prints"));
    bool  has_compatible_prints = compatible_prints != nullptr && ! compatible_prints->values.empty();
    if (! has_compatible_prints && ! condition.empty()) {
        try {
            return PlaceholderParser::evaluate_boolean_expression(condition, active_print.preset.config);
        } catch (const std::runtime_error &err) {
            //FIXME in case of an error, return "compatible with everything".
            printf("Preset::is_compatible_with_print - parsing error of compatible_prints_condition %s:\n%s\n", active_print.preset.name.c_str(), err.what());
            return true;
        }
    }
    return preset.preset.is_default || active_print.preset.name.empty() || ! has_compatible_prints ||
        std::find(compatible_prints->values.begin(), compatible_prints->values.end(), active_print.preset.name) !=
            compatible_prints->values.end();
}

bool is_compatible_with_printer(const PresetWithVendorProfile &preset, const PresetWithVendorProfile &active_printer, const DynamicPrintConfig *extra_config)
{
    // templates_profile vendor profiles should be decided as same vendor profiles
	if (preset.vendor != nullptr && preset.vendor != active_printer.vendor && !preset.vendor->templates_profile)
		// The current profile has a vendor assigned and it is different from the active print's vendor.
		return false;
    auto &condition               = preset.preset.compatible_printers_condition();
    auto *compatible_printers     = dynamic_cast<const ConfigOptionStrings*>(preset.preset.config.option("compatible_printers"));
    bool  has_compatible_printers = compatible_printers != nullptr && ! compatible_printers->values.empty();
    if (! has_compatible_printers && ! condition.empty()) {
        try {
            return PlaceholderParser::evaluate_boolean_expression(condition, active_printer.preset.config, extra_config);
        } catch (const std::runtime_error &err) {
            //FIXME in case of an error, return "compatible with everything".
            printf("Preset::is_compatible_with_printer - parsing error of compatible_printers_condition %s:\n%s\n", active_printer.preset.name.c_str(), err.what());
            return true;
        }
    }
    return preset.preset.is_default || active_printer.preset.name.empty() || ! has_compatible_printers ||
        std::find(compatible_printers->values.begin(), compatible_printers->values.end(), active_printer.preset.name) !=
            compatible_printers->values.end();
}

bool is_compatible_with_printer(const PresetWithVendorProfile &preset, const PresetWithVendorProfile &active_printer)
{
    DynamicPrintConfig config;
    config.set_key_value("printer_preset", new ConfigOptionString(active_printer.preset.name));
    const ConfigOption *opt = active_printer.preset.config.option("nozzle_diameter");
    if (opt)
        config.set_key_value("num_extruders", new ConfigOptionInt((int)static_cast<const ConfigOptionFloats*>(opt)->values.size()));
    return is_compatible_with_printer(preset, active_printer, &config);
}

void Preset::set_visible_from_appconfig(const AppConfig &app_config)
{
    if (vendor == nullptr) { return; }

    if (type == TYPE_PRINTER) {
        const std::string &model = config.opt_string("printer_model");
        const std::string &variant = config.opt_string("printer_variant");
        if (model.empty() || variant.empty())
        	return;
        is_visible = app_config.get_variant(vendor->id, model, variant);
    } else if (type == TYPE_FILAMENT || type == TYPE_SLA_MATERIAL) {
    	const std::string &section_name = (type == TYPE_FILAMENT) ? AppConfig::SECTION_FILAMENTS : AppConfig::SECTION_MATERIALS;
        if (type == TYPE_FILAMENT && app_config.get_bool("no_templates") && vendor && vendor->templates_profile)
            is_visible = false;
    	else if (app_config.has_section(section_name)) {
    		// Check whether this profile is marked as "installed" in QIDISlicer.ini,
    		// or whether a profile is marked as "installed", which this profile may have been renamed from.
	    	const std::map<std::string, std::string> &installed = app_config.get_section(section_name);
	    	auto has = [&installed](const std::string &name) {
	    		auto it = installed.find(name);
				return it != installed.end() && ! it->second.empty();
	    	};
	    	is_visible = has(this->name);
	    	for (auto it = this->renamed_from.begin(); ! is_visible && it != this->renamed_from.end(); ++ it)
	    		is_visible = has(*it);
	    }
        else 
			is_visible = false;
    }
}

static std::vector<std::string> s_Preset_print_options {
    "layer_height", "first_layer_height", "perimeters", "spiral_vase", "slice_closing_radius", "slicing_mode",
    "top_solid_layers", "top_solid_min_thickness", "bottom_solid_layers", "bottom_solid_min_thickness",
    "extra_perimeters", "extra_perimeters_on_overhangs", "avoid_crossing_curled_overhangs", "avoid_crossing_perimeters", "thin_walls", "overhangs",
    "seam_position","staggered_inner_seams", "external_perimeters_first", "fill_density", "fill_pattern", "top_fill_pattern", "bottom_fill_pattern",
    "infill_every_layers", /*"infill_only_where_needed",*/ "solid_infill_every_layers", "fill_angle", "bridge_angle",
    "solid_infill_below_area", "only_retract_when_crossing_perimeters", "infill_first",
    "ironing", "ironing_type", "ironing_flowrate", "ironing_speed", "ironing_spacing",
    "max_print_speed", "max_volumetric_speed", "avoid_crossing_perimeters_max_detour",
    "fuzzy_skin", "fuzzy_skin_thickness", "fuzzy_skin_point_dist",
    "max_volumetric_extrusion_rate_slope_positive", "max_volumetric_extrusion_rate_slope_negative",
    "perimeter_speed", "small_perimeter_speed", "external_perimeter_speed", "infill_speed", "solid_infill_speed",
    "enable_dynamic_overhang_speeds", "overhang_speed_0", "overhang_speed_1", "overhang_speed_2", "overhang_speed_3",
    "top_solid_infill_speed", "support_material_speed", "support_material_xy_spacing", "support_material_interface_speed",
    "bridge_speed", "gap_fill_speed", "gap_fill_enabled", "travel_speed", "travel_speed_z", "first_layer_speed", "first_layer_speed_over_raft", "perimeter_acceleration", "infill_acceleration",
    "external_perimeter_acceleration", "top_solid_infill_acceleration", "solid_infill_acceleration", "travel_acceleration", "wipe_tower_acceleration",
    "bridge_acceleration", "first_layer_acceleration", "first_layer_acceleration_over_raft", "default_acceleration", "skirts", "skirt_distance", "skirt_height", "draft_shield",
    "min_skirt_length", "brim_width", "brim_separation", "brim_type", "support_material", "support_material_auto", "support_material_threshold", "support_material_enforce_layers",
    "raft_layers", "raft_first_layer_density", "raft_first_layer_expansion", "raft_contact_distance", "raft_expansion",
    "support_material_pattern", "support_material_with_sheath", "support_material_spacing", "support_material_closing_radius", "support_material_style",
    "support_material_synchronize_layers", "support_material_angle", "support_material_interface_layers", "support_material_bottom_interface_layers",
    "support_material_interface_pattern", "support_material_interface_spacing", "support_material_interface_contact_loops", 
    "support_material_contact_distance", "support_material_bottom_contact_distance",
    "support_material_buildplate_only", 
    "support_tree_angle", "support_tree_angle_slow", "support_tree_branch_diameter", "support_tree_branch_diameter_angle", "support_tree_branch_diameter_double_wall", 
    "support_tree_top_rate", "support_tree_branch_distance", "support_tree_tip_diameter",
    "dont_support_bridges", "thick_bridges", "notes", "complete_objects", "extruder_clearance_radius",
    "extruder_clearance_height", "gcode_comments", "gcode_label_objects", "output_filename_format", "post_process", "gcode_substitutions", "perimeter_extruder",
    "infill_extruder", "solid_infill_extruder", "support_material_extruder", "support_material_interface_extruder",
    "ooze_prevention", "standby_temperature_delta", "interface_shells", "extrusion_width", "first_layer_extrusion_width",
    "perimeter_extrusion_width", "external_perimeter_extrusion_width", "infill_extrusion_width", "solid_infill_extrusion_width",
    "top_infill_extrusion_width", "support_material_extrusion_width", "infill_overlap", "infill_anchor", "infill_anchor_max", "bridge_flow_ratio",
    "elefant_foot_compensation", "xy_size_compensation", "resolution", "gcode_resolution", "arc_fitting",
    "wipe_tower", "wipe_tower_x", "wipe_tower_y",
    //w12
    "xy_contour_compensation", "xy_hole_compensation",
    "wipe_tower_width", "wipe_tower_cone_angle", "wipe_tower_rotation_angle", "wipe_tower_brim_width", "wipe_tower_bridging", "single_extruder_multi_material_priming", "mmu_segmented_region_max_width",
    "mmu_segmented_region_interlocking_depth", "wipe_tower_extruder", "wipe_tower_no_sparse_layers", "wipe_tower_extra_flow", "wipe_tower_extra_spacing", "compatible_printers", "compatible_printers_condition", "inherits",
    "perimeter_generator", "wall_transition_length", "wall_transition_filter_deviation", "wall_transition_angle",
    "wall_distribution_count", "min_feature_size", "min_bead_width",
    "top_one_perimeter_type", "only_one_perimeter_first_layer",
    //B36
    "first_layer_travel_speed",
    //B37
    "first_layer_infill_speed",
    //w11
    "detect_narrow_internal_solid_infill",
    //Y21
    "seam_gap",
    //w21
    "filter_top_gap_infill",
    //w25
    "slow_down_layers",
    //w26
    "elefant_foot_compensation_layers",
    //w27
    "precise_z_height",
    //w28
    "max_bridge_length",
    //w30
    "top_solid_infill_flow_ratio", "bottom_solid_infill_flow_ratio",
    //w33
    "ironing_pattern",
    //w39
    "precise_outer_wall",
    //Y27
    "resonance_avoidance", "min_resonance_avoidance_speed", "max_resonance_avoidance_speed"
};

static std::vector<std::string> s_Preset_filament_options {
    "filament_colour", "filament_diameter", "filament_type", "filament_soluble", "filament_abrasive", "filament_notes", "filament_max_volumetric_speed", "filament_infill_max_speed", "filament_infill_max_crossing_speed",
    "extrusion_multiplier", "filament_density", "filament_cost", "filament_spool_weight", "filament_loading_speed", "filament_loading_speed_start", "filament_load_time",
    "filament_unloading_speed", "filament_unloading_speed_start", "filament_unload_time", "filament_toolchange_delay", "filament_cooling_moves", "filament_stamping_loading_speed", "filament_stamping_distance",
    "filament_cooling_initial_speed", "filament_purge_multiplier", "filament_cooling_final_speed", "filament_ramming_parameters", "filament_minimal_purge_on_wipe_tower",
    "filament_multitool_ramming", "filament_multitool_ramming_volume", "filament_multitool_ramming_flow", 
    "temperature", "idle_temperature", "first_layer_temperature", "bed_temperature", "first_layer_bed_temperature", "fan_always_on", "cooling", "min_fan_speed",
    "max_fan_speed", "bridge_fan_speed", "disable_fan_first_layers", "full_fan_speed_layer", "fan_below_layer_time", "slowdown_below_layer_time", "min_print_speed",
    "start_filament_gcode", "end_filament_gcode", "enable_dynamic_fan_speeds", "chamber_temperature", "chamber_minimal_temperature",
    "overhang_fan_speed_0", "overhang_fan_speed_1", "overhang_fan_speed_2", "overhang_fan_speed_3",
    // Retract overrides
    "filament_retract_length", "filament_retract_lift", "filament_retract_lift_above", "filament_retract_lift_below", "filament_retract_speed", "filament_deretract_speed", "filament_retract_restart_extra", "filament_retract_before_travel",
    "filament_retract_layer_change", "filament_wipe", "filament_retract_before_wipe", "filament_retract_length_toolchange", "filament_retract_restart_extra_toolchange", "filament_travel_ramping_lift",
    "filament_travel_slope", "filament_travel_max_lift", "filament_travel_lift_before_obstacle",
    // Profile compatibility
    "filament_vendor", "compatible_prints", "compatible_prints_condition", "compatible_printers", "compatible_printers_condition", "inherits",
    // Shrinkage compensation
    "filament_shrinkage_compensation_xy", "filament_shrinkage_compensation_z",
    //B15
    "enable_auxiliary_fan",
    //Y26
    "enable_auxiliary_fan_unseal",
    //B25
    "enable_volume_fan",
    //B26
    "enable_advance_pressure",
    //B26
    "advance_pressure",
    //B26
    "smooth_time",
    //B39
    "disable_rapid_cooling_fan_first_layers",
    //Y26
    "seal_print",
    //Y28
    "dont_slow_down_outer_wall",
    //w15
    "filament_wipe_distance"
};

static std::vector<std::string> s_Preset_machine_limits_options {
    "machine_max_acceleration_extruding", "machine_max_acceleration_retracting", "machine_max_acceleration_travel",
    "machine_max_acceleration_x", "machine_max_acceleration_y", "machine_max_acceleration_z", "machine_max_acceleration_e",
    "machine_max_feedrate_x", "machine_max_feedrate_y", "machine_max_feedrate_z", "machine_max_feedrate_e",
    "machine_min_extruding_rate", "machine_min_travel_rate",
    "machine_max_jerk_x", "machine_max_jerk_y", "machine_max_jerk_z", "machine_max_jerk_e",
};

static std::vector<std::string> s_Preset_printer_options {
    "printer_technology", "autoemit_temperature_commands",
    "bed_shape", "bed_custom_texture", "bed_custom_model", "binary_gcode", "z_offset", "gcode_flavor", "use_relative_e_distances",
    "use_firmware_retraction", "use_volumetric_e", "variable_layer_height", "prefer_clockwise_movements",
    //FIXME the print host keys are left here just for conversion from the Printer preset to Physical Printer preset.
    "host_type", "print_host", "printhost_apikey", "printhost_cafile",
    "single_extruder_multi_material", "start_gcode", "end_gcode", "before_layer_gcode", "layer_gcode", "toolchange_gcode",
    "color_change_gcode", "pause_print_gcode", "template_custom_gcode",
    "between_objects_gcode", "printer_vendor", "printer_model", "printer_variant", "printer_notes", "cooling_tube_retraction",
    "cooling_tube_length", "high_current_on_filament_swap", "parking_pos_retraction", "extra_loading_move", "multimaterial_purging",
    "max_print_height", "default_print_profile", "inherits",
    "remaining_times", "silent_mode",
    "machine_limits_usage", "thumbnails", "thumbnails_format",
    "nozzle_high_flow",
//Y20 //B52
    "bed_exclude_area",
//Y25
    "wipe_device",
//Y16
    "chamber_temperature_control", "auxiliary_fan", "chamber_fan"
};

static std::vector<std::string> s_Preset_sla_print_options {
    "layer_height",
    "faded_layers",
    "supports_enable",
    "support_tree_type",

    "support_head_front_diameter",
    "support_head_penetration",
    "support_head_width",
    "support_pillar_diameter",
    "support_small_pillar_diameter_percent",
    "support_max_bridges_on_pillar",
    "support_max_weight_on_model",
    "support_pillar_connection_mode",
    "support_buildplate_only",
    "support_enforcers_only",
    "support_pillar_widening_factor",
    "support_base_diameter",
    "support_base_height",
    "support_base_safety_distance",
    "support_critical_angle",
    "support_max_bridge_length",
    "support_max_pillar_link_distance",
    "support_object_elevation",

    "branchingsupport_head_front_diameter",
    "branchingsupport_head_penetration",
    "branchingsupport_head_width",
    "branchingsupport_pillar_diameter",
    "branchingsupport_small_pillar_diameter_percent",
    "branchingsupport_max_bridges_on_pillar",
    "branchingsupport_max_weight_on_model",
    "branchingsupport_pillar_connection_mode",
    "branchingsupport_buildplate_only",
    "branchingsupport_pillar_widening_factor",
    "branchingsupport_base_diameter",
    "branchingsupport_base_height",
    "branchingsupport_base_safety_distance",
    "branchingsupport_critical_angle",
    "branchingsupport_max_bridge_length",
    "branchingsupport_max_pillar_link_distance",
    "branchingsupport_object_elevation",

    "support_points_density_relative",
    "support_points_minimal_distance",
    "slice_closing_radius",
    "slicing_mode",
    "pad_enable",
    "pad_wall_thickness",
    "pad_wall_height",
    "pad_brim_size",
    "pad_max_merge_distance",
    // "pad_edge_radius",
    "pad_wall_slope",
    "pad_object_gap",
    "pad_around_object",
    "pad_around_object_everywhere",
    "pad_object_connector_stride",
    "pad_object_connector_width",
    "pad_object_connector_penetration",
    "hollowing_enable",
    "hollowing_min_thickness",
    "hollowing_quality",
    "hollowing_closing_distance",
    "output_filename_format",
    "default_sla_print_profile",
    "compatible_printers",
    "compatible_printers_condition",
    "inherits"
};

static std::vector<std::string> s_Preset_sla_material_options {
    "material_colour",
    "material_type",
    "initial_layer_height",
    "bottle_cost",
    "bottle_volume",
    "bottle_weight",
    "material_density",
    "exposure_time",
    "initial_exposure_time",
    "material_correction",
    "material_correction_x",
    "material_correction_y",
    "material_correction_z",
    "material_notes",
    "material_vendor",
    "material_print_speed",
    "area_fill",
    "default_sla_material_profile",
    "zcorrection_layers",
    "compatible_prints", "compatible_prints_condition",
    "compatible_printers", "compatible_printers_condition", "inherits",

    // overriden options
    "material_ow_support_head_front_diameter",
    "material_ow_support_head_penetration",
    "material_ow_support_head_width",
    "material_ow_support_pillar_diameter",

    "material_ow_branchingsupport_head_front_diameter",
    "material_ow_branchingsupport_head_penetration",
    "material_ow_branchingsupport_head_width",
    "material_ow_branchingsupport_pillar_diameter",

    "material_ow_support_points_density_relative",
    "material_ow_absolute_correction",
    "material_ow_elefant_foot_compensation"
};

static std::vector<std::string> s_Preset_sla_tilt_options{
     "delay_before_exposure"
    ,"delay_after_exposure"
    ,"tower_hop_height"
    ,"tower_speed"
    ,"use_tilt"
    ,"tilt_down_initial_speed"
    ,"tilt_down_offset_steps"
    ,"tilt_down_offset_delay"
    ,"tilt_down_finish_speed"
    ,"tilt_down_cycles"
    ,"tilt_down_delay"
    ,"tilt_up_initial_speed"
    ,"tilt_up_offset_steps"
    ,"tilt_up_offset_delay"
    ,"tilt_up_finish_speed"
    ,"tilt_up_cycles"
    ,"tilt_up_delay"
};
const std::vector<std::string>& tilt_options() { return s_Preset_sla_tilt_options; }

static std::vector<std::string> s_Preset_sla_material_options_all = boost::copy_range<std::vector<std::string>>(boost::join(s_Preset_sla_material_options, s_Preset_sla_tilt_options));

static std::vector<std::string> s_Preset_sla_printer_options {
    "printer_technology",
    "bed_shape", "bed_custom_texture", "bed_custom_model", "max_print_height",
    "display_width", "display_height", "display_pixels_x", "display_pixels_y",
    "display_mirror_x", "display_mirror_y",
    "display_orientation",
    "fast_tilt_time", "slow_tilt_time", "high_viscosity_tilt_time", //"area_fill",
    "relative_correction",
    "relative_correction_x",
    "relative_correction_y",
    "relative_correction_z",
    "absolute_correction",
    "elefant_foot_compensation",
    "elefant_foot_min_width",
    "gamma_correction",
    "min_exposure_time", "max_exposure_time",
    "min_initial_exposure_time", "max_initial_exposure_time", "sla_archive_format", "sla_output_precision",
    //FIXME the print host keys are left here just for conversion from the Printer preset to Physical Printer preset.
    "print_host", "printhost_apikey", "printhost_cafile",
    "printer_notes",
    "inherits",
    //B52
    "bed_exclude_area",
};

const std::vector<std::string>& Preset::print_options()          { return s_Preset_print_options; }
const std::vector<std::string>& Preset::filament_options()       { return s_Preset_filament_options; }
const std::vector<std::string>& Preset::machine_limits_options() { return s_Preset_machine_limits_options; }
// The following nozzle options of a printer profile will be adjusted to match the size
// of the nozzle_diameter vector.
const std::vector<std::string>& Preset::nozzle_options()         { return print_config_def.extruder_option_keys(); }
const std::vector<std::string>& Preset::sla_print_options()      { return s_Preset_sla_print_options; }
const std::vector<std::string>& Preset::sla_material_options()   { return s_Preset_sla_material_options_all; }
const std::vector<std::string>& Preset::sla_printer_options()    { return s_Preset_sla_printer_options; }

const std::vector<std::string>& Preset::printer_options()
{
    static std::vector<std::string> s_opts = [](){
        std::vector<std::string> opts = s_Preset_printer_options;
        append(opts, s_Preset_machine_limits_options);
        append(opts, Preset::nozzle_options());
        return opts;
    }();
    return s_opts;
}

PresetCollection::PresetCollection(Preset::Type type, const std::vector<std::string> &keys, const Slic3r::StaticPrintConfig &defaults, const std::string &default_name) :
    m_type(type),
    m_edited_preset(type, "", false),
    m_saved_preset(type, "", false),
    m_idx_selected(0)
{
    // Insert just the default preset.
    this->add_default_preset(keys, defaults, default_name);
    m_edited_preset.config.apply(m_presets.front().config);
    update_saved_preset_from_current_preset();
}

void PresetCollection::reset(bool delete_files)
{
    if (m_presets.size() > m_num_default_presets) {
        if (delete_files) {
            // Erase the preset files.
            for (Preset &preset : m_presets)
                if (! preset.is_default && ! preset.is_external && ! preset.is_system)
                    boost::nowide::remove(preset.file.c_str());
        }
        // Don't use m_presets.resize() here as it requires a default constructor for Preset.
        m_presets.erase(m_presets.begin() + m_num_default_presets, m_presets.end());
        this->select_preset(0);
    }
    m_map_alias_to_profile_name.clear();
    m_map_system_profile_renamed.clear();
}

void PresetCollection::add_default_preset(const std::vector<std::string> &keys, const Slic3r::StaticPrintConfig &defaults, const std::string &preset_name)
{
    // Insert just the default preset.
    m_presets.emplace_back(Preset(this->type(), preset_name, true));
    m_presets.back().config.apply_only(defaults, keys.empty() ? defaults.keys() : keys);
    m_presets.back().loaded = true;
    ++ m_num_default_presets;
}

// Load all presets found in dir_path.
// Throws an exception on error.
void PresetCollection::load_presets(
    const std::string &dir_path, const std::string &subdir, 
    PresetsConfigSubstitutions& substitutions, ForwardCompatibilitySubstitutionRule substitution_rule)
{
    // Don't use boost::filesystem::canonical() on Windows, it is broken in regard to reparse points,
    // see https://github.com/QIDITECH/QIDISlicer/issues/732
    boost::filesystem::path dir = boost::filesystem::absolute(boost::filesystem::path(dir_path) / subdir).make_preferred();
    m_dir_path = dir.string();
    std::string errors_cummulative;
    // Store the loaded presets into a new vector, otherwise the binary search for already existing presets would be broken.
    // (see the "Preset already present, not loading" message).
    std::deque<Preset> presets_loaded;
    for (auto &dir_entry : boost::filesystem::directory_iterator(dir))
        if (Slic3r::is_ini_file(dir_entry)) {
            std::string name = dir_entry.path().filename().string();
            // Remove the .ini suffix.
            name.erase(name.size() - 4);
            if (this->find_preset(name, false)) {
                // This happens when there's is a preset (most likely legacy one) with the same name as a system preset
                // that's already been loaded from a bundle.
                BOOST_LOG_TRIVIAL(warning) << "Preset already present, not loading: " << name;
                continue;
            }
            try {
                Preset preset(m_type, name, false);
                preset.file = dir_entry.path().string();
                // Load the preset file, apply preset values on top of defaults.
                try {
                    DynamicPrintConfig config;
                    ConfigSubstitutions config_substitutions = config.load_from_ini(preset.file, substitution_rule);
                    if (! config_substitutions.empty())
                        substitutions.push_back({ preset.name, m_type, PresetConfigSubstitutions::Source::UserFile, preset.file, std::move(config_substitutions) });
                    // Find a default preset for the config. The PrintPresetCollection provides different default preset based on the "printer_technology" field.
                    const Preset &default_preset = this->default_preset_for(config);
                    preset.config = default_preset.config;
                    preset.config.apply(std::move(config));
                    Preset::normalize(preset.config);
                    // Report configuration fields, which are misplaced into a wrong group.
                    std::string incorrect_keys = Preset::remove_invalid_keys(preset.config, default_preset.config);
                    if (! incorrect_keys.empty())
                        BOOST_LOG_TRIVIAL(error) << "Error in a preset file: The preset \"" <<
                            preset.file << "\" contains the following incorrect keys: " << incorrect_keys << ", which were removed";
                    preset.loaded = true;
                } catch (const std::ifstream::failure &err) {
                    throw Slic3r::RuntimeError(std::string("The selected preset cannot be loaded: ") + preset.file + "\n\tReason: " + err.what());
                } catch (const std::runtime_error &err) {
                    throw Slic3r::RuntimeError(std::string("Failed loading the preset file: ") + preset.file + "\n\tReason: " + err.what());
                }
                presets_loaded.emplace_back(preset);
            } catch (const std::runtime_error &err) {
                errors_cummulative += err.what();
                errors_cummulative += "\n";
            }
        }
    m_presets.insert(m_presets.end(), std::make_move_iterator(presets_loaded.begin()), std::make_move_iterator(presets_loaded.end()));
    std::sort(m_presets.begin() + m_num_default_presets, m_presets.end());
    this->select_preset(first_visible_idx());
    if (! errors_cummulative.empty())
        throw Slic3r::RuntimeError(errors_cummulative);
}

// Load a preset from an already parsed config file, insert it into the sorted sequence of presets
// and select it, losing previous modifications.
Preset& PresetCollection::load_preset(const std::string &path, const std::string &name, const DynamicPrintConfig &config, bool select)
{
    DynamicPrintConfig cfg(this->default_preset().config);
    cfg.apply_only(config, cfg.keys(), true);
    return this->load_preset(path, name, std::move(cfg), select);
}

static bool profile_print_params_same(const DynamicPrintConfig &cfg_old, const DynamicPrintConfig &cfg_new)
{
    t_config_option_keys diff = cfg_old.diff(cfg_new);
    // Following keys are used by the UI, not by the slicing core, therefore they are not important
    // when comparing profiles for equality. Ignore them.
    for (const char *key : { "compatible_prints", "compatible_prints_condition",
                             "compatible_printers", "compatible_printers_condition", "inherits",
                             "print_settings_id", "filament_settings_id", "sla_print_settings_id", "sla_material_settings_id", "printer_settings_id", "filament_vendor",
                             "printer_model", "printer_variant", "default_print_profile", "default_filament_profile", "default_sla_print_profile", "default_sla_material_profile",
                             //FIXME remove the print host keys?
                             "print_host", "printhost_apikey", "printhost_cafile" })
        diff.erase(std::remove(diff.begin(), diff.end(), key), diff.end());
    // Preset with the same name as stored inside the config exists.
    return diff.empty();
}

// Load a preset from an already parsed config file, insert it into the sorted sequence of presets
// and select it, losing previous modifications.
// Only a single profile could be edited at at the same time, which introduces complexity when loading
// filament profiles for multi-extruder printers.
ExternalPreset PresetCollection::load_external_preset(
    // Path to the profile source file (a G-code, an AMF or 3MF file, a config file)
    const std::string           &path,
    // Name of the profile, derived from the source file name.
    const std::string           &name,
    // Original name of the profile, extracted from the loaded config. Empty, if the name has not been stored.
    const std::string           &original_name,
    // Config to initialize the preset from. It may contain configs of all presets merged in a single dictionary!
    const DynamicPrintConfig    &combined_config,
    // Select the preset after loading?
    LoadAndSelect                select)
{
    // Load the preset over a default preset, so that the missing fields are filled in from the default preset.
    DynamicPrintConfig cfg(this->default_preset_for(combined_config).config);
    t_config_option_keys keys = cfg.keys();
    cfg.apply_only(combined_config, keys, true);
    std::string                 &inherits = Preset::inherits(cfg);
    if (select == LoadAndSelect::Never) {
        // Some filament profile has been selected and modified already.
        // Check whether this profile is equal to the modified edited profile.
        const Preset &edited = this->get_edited_preset();
        if ((edited.name == original_name || edited.name == inherits) && profile_print_params_same(edited.config, cfg))
            // Just point to that already selected and edited profile.
            return ExternalPreset(&(*this->find_preset_internal(edited.name)), false);
    }
    // Is there a preset already loaded with the name stored inside the config?
    std::deque<Preset>::iterator it       = this->find_preset_internal(original_name);
    bool                         found    = it != m_presets.end() && it->name == original_name;
    if (! found) {
    	// Try to match the original_name against the "renamed_from" profile names of loaded system profiles.
		it = this->find_preset_renamed(original_name);
		found = it != m_presets.end();
    }
    if (found && profile_print_params_same(it->config, cfg) && it->is_visible) {
        // The preset exists and is visible and it matches the values stored inside config.
        if (select == LoadAndSelect::Always)
            this->select_preset(it - m_presets.begin());
        return ExternalPreset(&(*it), false);
    }
    if (! found && select != LoadAndSelect::Never && ! inherits.empty()) {
        // Try to use a system profile as a base to select the system profile
        // and override its settings with the loaded ones.
        assert(it == m_presets.end());
        it    = this->find_preset_internal(inherits);
        found = it != m_presets.end() && it->name == inherits;
        if (found && profile_print_params_same(it->config, cfg)) {
            // The system preset exists and it matches the values stored inside config.
            if (select == LoadAndSelect::Always)
                this->select_preset(it - m_presets.begin());
            return ExternalPreset(&(*it), false);
        }
    }
    if (found) {
        if (select != LoadAndSelect::Never) {
            const size_t idx = it - m_presets.begin();
            // The newly selected preset can be activated AND have to be make as visible.
            bool is_installed = !m_presets[idx].is_visible;

            // Select the existing preset and override it with new values, so that
            // the differences will be shown in the preset editor against the referenced profile.
            this->select_preset(idx);

            // update dirty state only if it's needed
            if (!profile_print_params_same(it->config, cfg)) {
                // The source config may contain keys from many possible preset types. Just copy those that relate to this preset.

                // Following keys are not used neither by the UI nor by the slicing core, therefore they are not important 
                // Erase them from config apply to avoid redundant "dirty" parameter in loaded preset.
                for (const char* key : { "print_settings_id", "filament_settings_id", "sla_print_settings_id", "sla_material_settings_id", "printer_settings_id", "filament_vendor", 
                                         "printer_model", "printer_variant", "default_print_profile", "default_filament_profile", "default_sla_print_profile", "default_sla_material_profile" })
                    keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());

                this->get_edited_preset().config.apply_only(combined_config, keys, true);
                this->update_dirty();
                // Don't save the newly loaded project as a "saved into project" state.
                //update_saved_preset_from_current_preset();
                assert(this->get_edited_preset().is_dirty);
            }
            return ExternalPreset(&(*it), this->get_edited_preset().is_dirty, is_installed);
        }
        if (inherits.empty()) {
            // Update the "inherits" field.
            // There is a profile with the same name already loaded. Should we update the "inherits" field?
            inherits = it->vendor ? it->name : it->inherits();
        }
    }

    // The external preset does not match an internal preset, load the external preset.
    std::string new_name;
    for (size_t idx = 0;; ++ idx) {
        std::string suffix;
        if (original_name.empty()) {
            if (idx > 0)
                suffix = " (" + std::to_string(idx) + ")";
        } else {
            if (idx == 0)
                suffix = " (" + original_name + ")";
            else
                suffix = " (" + original_name + "-" + std::to_string(idx) + ")";
        }
        new_name = name + suffix;
        it = this->find_preset_internal(new_name);
        if (it == m_presets.end() || it->name != new_name)
            // Unique profile name. Insert a new profile.
            break;
        if (profile_print_params_same(it->config, cfg)) {
            // The preset exists and it matches the values stored inside config.
            if (select == LoadAndSelect::Always)
                this->select_preset(it - m_presets.begin());
            return ExternalPreset(&(*it), false);
        }
        // Form another profile name.
    }
    // Insert a new profile.
    Preset &preset = this->load_preset(path, new_name, std::move(cfg), select == LoadAndSelect::Always);
    preset.is_external = true;
    if (this->m_idx_selected != size_t(-1) && &this->get_selected_preset() == &preset)
        this->get_edited_preset().is_external = true;

    return ExternalPreset(&preset, false);
}

Preset& PresetCollection::load_preset(const std::string &path, const std::string &name, DynamicPrintConfig &&config, bool select)
{
    auto it = this->find_preset_internal(name);
    if (it == m_presets.end() || it->name != name) {
        // The preset was not found. Create a new preset.
        it = m_presets.emplace(it, Preset(m_type, name, false));
    }
    Preset &preset = *it;
    preset.file = path;
    preset.config = std::move(config);
    preset.loaded = true;
    preset.is_dirty = false;
    if (select)
        this->select_preset_by_name(name, true);
    return preset;
}

bool PresetCollection::save_current_preset(const std::string &new_name, bool detach)
{
    bool is_saved_as_new{ false };
    // 1) Find the preset with a new_name or create a new one,
    // initialize it with the edited config.
    auto it = this->find_preset_internal(new_name);
    if (it != m_presets.end() && it->name == new_name) {
        // Preset with the same name found.
        Preset &preset = *it;
        if (preset.is_default || preset.is_external || preset.is_system)
            // Cannot overwrite the default preset.
            return false;
        // Overwriting an existing preset.
        preset.config = std::move(m_edited_preset.config);
        // The newly saved preset will be activated -> make it visible.
        preset.is_visible = true;
        if (detach) {
            // Clear the link to the parent profile.
            preset.vendor = nullptr;
			preset.inherits().clear();
			preset.alias.clear();
			preset.renamed_from.clear();
        }
    } else {
        is_saved_as_new = true;
        // Creating a new preset.
        Preset       &preset   = *m_presets.insert(it, m_edited_preset);
        std::string  &inherits = preset.inherits();
        std::string   old_name = preset.name;
        preset.name = new_name;
        preset.file = this->path_from_name(new_name);
        preset.vendor = nullptr;
		preset.alias.clear();
        preset.renamed_from.clear();
        if (detach) {
        	// Clear the link to the parent profile.
        	inherits.clear();
        } else if (preset.is_system) {
            // Inheriting from a system preset.
            inherits = /* preset.vendor->name + "/" + */ old_name;
        } else if (inherits.empty()) {
            // Inheriting from a user preset. Link the new preset to the old preset.
            // inherits = old_name;
        } else {
            // Inherited from a user preset. Just maintain the "inherited" flag,
            // meaning it will inherit from either the system preset, or the inherited user preset.
        }
        preset.is_default  = false;
        preset.is_system   = false;
        preset.is_external = false;
        // The newly saved preset will be activated -> make it visible.
        preset.is_visible  = true;
        // Just system presets have aliases
        preset.alias.clear();
    }
    // 2) Activate the saved preset.
    this->select_preset_by_name(new_name, true);
    // 2) Store the active preset to disk.
    this->get_selected_preset().save();

    return is_saved_as_new;
}

Preset& PresetCollection::get_preset_with_name(const std::string& new_name, const Preset* initial_preset)
{
    // 1) Find the preset with a new_name or create a new one,
    // initialize it with the preset_to config.
    auto it = this->find_preset_internal(new_name);
    if (it != m_presets.end() && it->name == new_name) {
        // Preset with the same name found.
        Preset& preset = *it;
        if (!preset.is_default && !preset.is_external && !preset.is_system && initial_preset->name != new_name) {
            // Overwriting an existing preset if it isn't default/external/system or isn't an initial_preset
            preset.config = initial_preset->config;
            // The newly saved preset can be activated -> make it visible.
            preset.is_visible = true;
        }
        return preset;
    }

    const std::string selected_preset_name = this->get_selected_preset_name();

    // Creating a new preset.
    Preset& preset = *m_presets.insert(it, *initial_preset);
    std::string& inherits = preset.inherits();
    std::string old_name = preset.name;
    preset.name = new_name;
    preset.file = this->path_from_name(new_name);
    preset.vendor = nullptr;
    preset.alias.clear();
    preset.renamed_from.clear();
    if (preset.is_system) {
        // Inheriting from a system preset.
        inherits = old_name;
    }
    else if (inherits.empty()) {
        // Inheriting from a user preset. Link the new preset to the old preset.
        // inherits = old_name;
    }
    else {
        // Inherited from a user preset. Just maintain the "inherited" flag,
        // meaning it will inherit from either the system preset, or the inherited user preset.
    }
    preset.is_default = false;
    preset.is_system = false;
    preset.is_external = false;
    // The newly saved preset can be activated -> make it visible.
    preset.is_visible = true;
    // Just system presets have aliases
    preset.alias.clear();

    // sort printers and get new it
    std::sort(m_presets.begin(), m_presets.end());

    // set initial preset selection
    this->select_preset_by_name(selected_preset_name, true);

    it = this->find_preset_internal(new_name);
    assert(it != m_presets.end());

    return *it;
}

bool PresetCollection::delete_current_preset()
{
    const Preset &selected = this->get_selected_preset();
    if (selected.is_default)
        return false;
    if (! selected.is_external && ! selected.is_system) {
        // Erase the preset file.
        boost::nowide::remove(selected.file.c_str());
    }
    // Remove the preset from the list.
    m_presets.erase(m_presets.begin() + m_idx_selected);
    // Find the next visible preset.
    size_t new_selected_idx = m_idx_selected;
    if (new_selected_idx < m_presets.size())
        for (; new_selected_idx < m_presets.size() && ! m_presets[new_selected_idx].is_visible; ++ new_selected_idx) ;
    if (new_selected_idx == m_presets.size())
        for (--new_selected_idx; new_selected_idx > 0 && !m_presets[new_selected_idx].is_visible; --new_selected_idx);
    this->select_preset(new_selected_idx);
    return true;
}

bool PresetCollection::delete_preset(const std::string& name)
{
    if (name == this->get_selected_preset().name)
        return delete_current_preset();

    const std::string selected_preset_name = this->get_selected_preset_name();

    auto it = this->find_preset_internal(name);

    const Preset& preset = *it;
    if (preset.is_default)
        return false;
    if (!preset.is_external && !preset.is_system) {
        // Erase the preset file.
        boost::nowide::remove(preset.file.c_str());
    }
    m_presets.erase(it);

    // update selected preset
    this->select_preset_by_name(selected_preset_name, true);

    return true;
}

const Preset* PresetCollection::get_selected_preset_parent() const
{
    if (this->get_selected_idx() == size_t(-1))
        // This preset collection has no preset activated yet. Only the get_edited_preset() is valid.
        return nullptr;

    const Preset 	  &selected_preset = this->get_selected_preset();
    if (selected_preset.is_system || selected_preset.is_default)
        return &selected_preset;

    const Preset 	  &edited_preset   = this->get_edited_preset();
    const std::string &inherits        = edited_preset.inherits();
    const Preset      *preset          = nullptr;
    if (inherits.empty()) {
        if (selected_preset.is_external)
            return nullptr;
        preset = &this->default_preset(m_type == Preset::Type::TYPE_PRINTER && edited_preset.printer_technology() == ptSLA ? 1 : 0);
    } else
        preset = this->find_preset(inherits, false);
    if (preset == nullptr) {
	    // Resolve the "renamed_from" field.
    	assert(! inherits.empty());
    	auto it = this->find_preset_renamed(inherits);
		if (it != m_presets.end())
			preset = &(*it);
    }
    return (preset == nullptr/* || preset->is_default*/ || preset->is_external) ? nullptr : preset;
}

const Preset* PresetCollection::get_preset_parent(const Preset& child) const
{
    const std::string &inherits = child.inherits();
    if (inherits.empty())
// 		return this->get_selected_preset().is_system ? &this->get_selected_preset() : nullptr;
        return nullptr;
    const Preset* preset = this->find_preset(inherits, false);
    if (preset == nullptr) {
    	auto it = this->find_preset_renamed(inherits);
		if (it != m_presets.end())
			preset = &(*it);
    }
    return
         // not found
        (preset == nullptr/* || preset->is_default */||
         // this should not happen, user profile should not derive from an external profile
         preset->is_external ||
         // this should not happen, however people are creative, see GH #4996
         preset == &child) ?
            nullptr :
            preset;
}

// Return vendor of the first parent profile, for which the vendor is defined, or null if such profile does not exist.
PresetWithVendorProfile PresetCollection::get_preset_with_vendor_profile(const Preset &preset) const
{
	const Preset		*p = &preset;
	const VendorProfile *v = nullptr;
	do {
		if (p->vendor != nullptr) {
			v = p->vendor;
			break;
		}
		p = this->get_preset_parent(*p);
	} while (p != nullptr);
	return PresetWithVendorProfile(preset, v);
}

const std::string& PresetCollection::get_preset_name_by_alias(const std::string& alias) const
{
	for (
		// Find the 1st profile name with the alias.
		auto it = Slic3r::lower_bound_by_predicate(m_map_alias_to_profile_name.begin(), m_map_alias_to_profile_name.end(), [&alias](auto &l){ return l.first < alias; });
		// Continue over all profile names with the same alias.
		it != m_map_alias_to_profile_name.end() && it->first == alias; ++ it)
		if (auto it_preset = this->find_preset_internal(it->second);
			it_preset != m_presets.end() && it_preset->name == it->second &&
            it_preset->is_visible && (it_preset->is_compatible || size_t(it_preset - m_presets.begin()) == m_idx_selected))
	        return it_preset->name;
    return alias;
}

const std::string& PresetCollection::get_preset_name_by_alias_invisible(const std::string& alias) const
{
    for (
        // Find the 1st profile name with the alias.
        auto it = Slic3r::lower_bound_by_predicate(m_map_alias_to_profile_name.begin(), m_map_alias_to_profile_name.end(), [&alias](auto& l) { return l.first < alias; });
        // Continue over all profile names with the same alias.
        it != m_map_alias_to_profile_name.end() && it->first == alias; ++it)
        if (auto it_preset = this->find_preset_internal(it->second);
            it_preset != m_presets.end() && it_preset->name == it->second &&
            it_preset->is_compatible)
            return it_preset->name;
    return alias;
}

const std::string* PresetCollection::get_preset_name_renamed(const std::string &old_name) const
{
	auto it_renamed = m_map_system_profile_renamed.find(old_name);
	if (it_renamed != m_map_system_profile_renamed.end())
		return &it_renamed->second;
	return nullptr;
}

const std::string& PresetCollection::get_suffix_modified() {
    return g_suffix_modified;
}

// Return a preset by its name. If the preset is active, a temporary copy is returned.
// If a preset is not found by its name, null is returned.
Preset* PresetCollection::find_preset(const std::string &name, bool first_visible_if_not_found, bool respect_active_preset /*= true*/)
{
    Preset key(m_type, name, false);
    auto it = this->find_preset_internal(name);
    // Ensure that a temporary copy is returned if the preset found is currently selected.
    return (it != m_presets.end() && it->name == key.name) ? &this->preset(it - m_presets.begin(), respect_active_preset) :
        first_visible_if_not_found ? &this->first_visible() : nullptr;
}

size_t PresetCollection::get_preset_idx_by_name(const std::string name) const
{
    auto it = this->find_preset_internal(name);
    return it != m_presets.end() ? it - m_presets.begin() : size_t(-1);
}

// Return index of the first visible preset. Certainly at least the '- default -' preset shall be visible.
//B40
size_t PresetCollection::first_visible_idx() const
{
    size_t first_visible = -1;
    size_t idx = m_default_suppressed ? m_num_default_presets : 0;
    for (; idx < m_presets.size(); ++ idx)
        if (m_presets[idx].is_visible) {
            if (first_visible == -1)
                first_visible = idx;
            if (m_type != Preset::TYPE_FILAMENT)
                break;
            else {
                if (m_presets[idx].name.find("PLA") != std::string::npos) {
                    first_visible = idx;
                    break;
                }
            }
        }
    if (first_visible == -1)
        first_visible = 0;
    return first_visible;
}

void PresetCollection::set_default_suppressed(bool default_suppressed)
{
    if (m_default_suppressed != default_suppressed) {
        m_default_suppressed = default_suppressed;
        bool default_visible = ! default_suppressed || m_idx_selected < m_num_default_presets;
        for (size_t i = 0; i < m_num_default_presets; ++ i)
            m_presets[i].is_visible = default_visible;
    }
}

size_t PresetCollection::update_compatible_internal(const PresetWithVendorProfile &active_printer, const PresetWithVendorProfile *active_print, PresetSelectCompatibleType unselect_if_incompatible)
{
    DynamicPrintConfig config;
    config.set_key_value("printer_preset", new ConfigOptionString(active_printer.preset.name));
    const ConfigOption *opt = active_printer.preset.config.option("nozzle_diameter");
    if (opt)
        config.set_key_value("num_extruders", new ConfigOptionInt((int)static_cast<const ConfigOptionFloats*>(opt)->values.size()));
    bool some_compatible = false;
    std::vector<size_t> indices_of_template_presets;
    for (size_t idx_preset = m_num_default_presets; idx_preset < m_presets.size(); ++ idx_preset) {
        bool    selected        = idx_preset == m_idx_selected;
        Preset &preset_selected = m_presets[idx_preset];
        Preset &preset_edited   = selected ? m_edited_preset : preset_selected;

        const PresetWithVendorProfile this_preset_with_vendor_profile = this->get_preset_with_vendor_profile(preset_edited);
        bool    was_compatible  = preset_edited.is_compatible;
        preset_edited.is_compatible = is_compatible_with_printer(this_preset_with_vendor_profile, active_printer, &config);
        some_compatible |= preset_edited.is_compatible;
	    if (active_print != nullptr)
	        preset_edited.is_compatible &= is_compatible_with_print(this_preset_with_vendor_profile, *active_print, active_printer);
        if (! preset_edited.is_compatible && selected &&
        	(unselect_if_incompatible == PresetSelectCompatibleType::Always || (unselect_if_incompatible == PresetSelectCompatibleType::OnlyIfWasCompatible && was_compatible)))
            m_idx_selected = size_t(-1);
        if (selected)
            preset_selected.is_compatible = preset_edited.is_compatible;
        if (preset_edited.vendor && preset_edited.vendor->templates_profile) {
            if (preset_selected.is_visible)
                indices_of_template_presets.push_back(idx_preset);
            else {
                preset_selected.is_compatible = false;
                if (selected)
                    m_idx_selected = size_t(-1);
            }
        }
    }
    // filter out template profiles where profile with same alias and compability exists
    if (!indices_of_template_presets.empty()) {
        for (size_t idx_preset = m_num_default_presets; idx_preset < m_presets.size(); ++idx_preset) {
            if (m_presets[idx_preset].vendor && !m_presets[idx_preset].vendor->templates_profile && m_presets[idx_preset].is_compatible) {
                std::string preset_alias = m_presets[idx_preset].alias;
                for (size_t idx_of_template_in_presets : indices_of_template_presets) {
                    if (m_presets[idx_of_template_in_presets].alias == preset_alias) {
                        // unselect selected template filament if there is non-template alias compatible
                        if (idx_of_template_in_presets == m_idx_selected && (unselect_if_incompatible == PresetSelectCompatibleType::Always || unselect_if_incompatible == PresetSelectCompatibleType::OnlyIfWasCompatible)) {
                            m_idx_selected = size_t(-1);
                        }
                        m_presets[idx_of_template_in_presets].is_compatible = false;
                        break;
                    }
                }
            }
        }
    }
    
    // Update visibility of the default profiles here if the defaults are suppressed, the current profile is not compatible and we don't want to select another compatible profile.
    if (m_idx_selected >= m_num_default_presets && m_default_suppressed)
	    for (size_t i = 0; i < m_num_default_presets; ++ i)
	        m_presets[i].is_visible = ! some_compatible;
    return m_idx_selected;
}

// Update a dirty flag of the current preset
// Return true if the dirty flag changed.
bool PresetCollection::update_dirty()
{
    bool was_dirty = this->get_selected_preset().is_dirty;
    bool is_dirty  = current_is_dirty();
    this->get_selected_preset().is_dirty = is_dirty;
    this->get_edited_preset().is_dirty = is_dirty;

    return was_dirty != is_dirty;
}

template<class T>
void add_correct_opts_to_diff(const std::string &opt_key, t_config_option_keys& vec, const ConfigBase &other, const ConfigBase &this_c)
{
    const T* opt_init = static_cast<const T*>(other.option(opt_key));
    const T* opt_cur = static_cast<const T*>(this_c.option(opt_key));
    int opt_init_max_id = opt_init->values.size() - 1;
    for (int i = 0; i < int(opt_cur->values.size()); i++)
    {
        int init_id = i <= opt_init_max_id ? i : 0;
        if (opt_cur->values[i] != opt_init->values[init_id])
            vec.emplace_back(opt_key + "#" + std::to_string(i));
    }
}

// list of options with vector variable, which is independent from number of extruders
static const std::set<std::string> independent_from_extruder_number_options = {
    "bed_shape",
    "compatible_printers",
    "compatible_prints",
    "filament_ramming_parameters",
    "gcode_substitutions",
    "post_process",
//Y20 //B52
    "bed_exclude_area"
};

bool PresetCollection::is_independent_from_extruder_number_option(const std::string& opt_key)
{
    return independent_from_extruder_number_options.find(opt_key) != independent_from_extruder_number_options.end();
}

// Use deep_diff to correct return of changed options, considering individual options for each extruder.
inline t_config_option_keys deep_diff(const ConfigBase &config_this, const ConfigBase &config_other)
{
    t_config_option_keys diff;
    for (const t_config_option_key &opt_key : config_this.keys()) {
        const ConfigOption *this_opt  = config_this.option(opt_key);
        const ConfigOption *other_opt = config_other.option(opt_key);
        if (this_opt != nullptr && other_opt != nullptr && *this_opt != *other_opt)
        {
            if (PresetCollection::is_independent_from_extruder_number_option(opt_key)) {
                // Scalar variable, or a vector variable, which is independent from number of extruders,
                // thus the vector is presented to the user as a single input.
                diff.emplace_back(opt_key);
            } else if (opt_key == "default_filament_profile") {
                // Ignore this field, it is not presented to the user, therefore showing a "modified" flag for this parameter does not help.
                // Also the length of this field may differ, which may lead to a crash if the block below is used.
            } else if (opt_key == "thumbnails") {
                // "thumbnails" can not contain extensions in old config but they are valid and use PNG extension by default
                // So, check if "thumbnails" is really changed
                // We will compare full thumbnails instead of exactly config values
                auto [thumbnails, er]         = GCodeThumbnails::make_and_check_thumbnail_list(config_this);
                auto [thumbnails_new, er_new] = GCodeThumbnails::make_and_check_thumbnail_list(config_other);
                if (thumbnails != thumbnails_new || er != er_new)
                    // if those strings are actually the same, erase them from the list of dirty oprions
                    diff.emplace_back(opt_key);
            } else {
                switch (other_opt->type()) {
                case coInts:    add_correct_opts_to_diff<ConfigOptionInts       >(opt_key, diff, config_other, config_this);  break;
                case coBools:   add_correct_opts_to_diff<ConfigOptionBools      >(opt_key, diff, config_other, config_this);  break;
                case coFloats:  add_correct_opts_to_diff<ConfigOptionFloats     >(opt_key, diff, config_other, config_this);  break;
                case coStrings: add_correct_opts_to_diff<ConfigOptionStrings    >(opt_key, diff, config_other, config_this);  break;
                case coPercents:add_correct_opts_to_diff<ConfigOptionPercents   >(opt_key, diff, config_other, config_this);  break;
                case coPoints:  add_correct_opts_to_diff<ConfigOptionPoints     >(opt_key, diff, config_other, config_this);  break;
                case coFloatsOrPercents:    add_correct_opts_to_diff<ConfigOptionFloatsOrPercents   >(opt_key, diff, config_other, config_this);  break;
                case coEnums:   add_correct_opts_to_diff<ConfigOptionEnumsGeneric>(opt_key, diff, config_other, config_this);  break;
                default:        diff.emplace_back(opt_key);     break;
                }
                // "nozzle_diameter" is a vector option which contain info about diameter for each nozzle
                // But in the same time size of this vector indicates about count of extruders,
                // So, we need to add it to the diff if its size is changed.
                if (opt_key == "nozzle_diameter" && 
                    static_cast<const ConfigOptionFloats*>(this_opt)->size() != static_cast<const ConfigOptionFloats*>(other_opt)->size())
                    diff.emplace_back(opt_key);
            }
        }
    }
    return diff;
}

static constexpr const std::initializer_list<const char*> optional_keys { "compatible_prints", "compatible_printers" };

bool PresetCollection::is_dirty(const Preset *edited, const Preset *reference)
{
    if (edited != nullptr && reference != nullptr) {
        // Only compares options existing in both configs.
        bool is_dirty = !reference->config.equals(edited->config);
        if (is_dirty && edited->type != Preset::TYPE_FILAMENT) {
            // for non-filaments preset check deep difference for compared configs
            // there can be cases (as for thumbnails), when configs can logically equal
            // even when their values are not equal.
            is_dirty = !deep_diff(edited->config, reference->config).empty();
        }
        if (is_dirty)
            return true;
        // The "compatible_printers" option key is handled differently from the others:
        // It is not mandatory. If the key is missing, it means it is compatible with any printer.
        // If the key exists and it is empty, it means it is compatible with no printer.
        for (auto &opt_key : optional_keys)
            if (reference->config.has(opt_key) != edited->config.has(opt_key))
                return true;
    }
    return false;
}

std::vector<std::string> PresetCollection::dirty_options(const Preset *edited, const Preset *reference, const bool deep_compare /*= false*/)
{
    std::vector<std::string> changed;
    if (edited != nullptr && reference != nullptr) {
        // Only compares options existing in both configs.
        changed = deep_compare ?
                deep_diff(edited->config, reference->config) :
                reference->config.diff(edited->config);
        // The "compatible_printers" option key is handled differently from the others:
        // It is not mandatory. If the key is missing, it means it is compatible with any printer.
        // If the key exists and it is empty, it means it is compatible with no printer.
        for (auto &opt_key : optional_keys)
            if (reference->config.has(opt_key) != edited->config.has(opt_key))
                changed.emplace_back(opt_key);
    }
    return changed;
}

// Select a new preset. This resets all the edits done to the currently selected preset.
// If the preset with index idx does not exist, a first visible preset is selected.
Preset& PresetCollection::select_preset(size_t idx)
{
    for (Preset &preset : m_presets)
        preset.is_dirty = false;
    if (idx >= m_presets.size())
        idx = first_visible_idx();
    m_idx_selected = idx;
    if (!m_presets[idx].is_visible)
        // The newly selected preset can be activated -> make it visible.
        m_presets[idx].is_visible = true;

    m_edited_preset = m_presets[idx];
    bool default_visible = ! m_default_suppressed || m_idx_selected < m_num_default_presets;
    for (size_t i = 0; i < m_num_default_presets; ++i)
        m_presets[i].is_visible = default_visible;
    return m_presets[idx];
}

bool PresetCollection::select_preset_by_name(const std::string &name_w_suffix, bool force, bool force_invisible /*= false*/)
{
    std::string name = Preset::remove_suffix_modified(name_w_suffix);
    // 1) Try to find the preset by its name.
    auto it = this->find_preset_internal(name);
    size_t idx = 0;
    if (it != m_presets.end() && it->name == name && (force_invisible || it->is_visible))
        // Preset found by its name and it is visible.
        idx = it - m_presets.begin();
    else {
        // Find the first visible preset.
        for (size_t i = m_default_suppressed ? m_num_default_presets : 0; i < m_presets.size(); ++ i)
            if (m_presets[i].is_visible) {
                idx = i;
                break;
            }
        // If the first visible preset was not found, return the 0th element, which is the default preset.
    }

    // 2) Select the new preset.
    if (m_idx_selected != idx || force) {
        this->select_preset(idx);
        return true;
    }

    return false;
}

bool PresetCollection::select_preset_by_name_strict(const std::string &name)
{
    // 1) Try to find the preset by its name.
    auto it = this->find_preset_internal(name);
    size_t idx = (size_t)-1;
    if (it != m_presets.end() && it->name == name && it->is_visible)
        // Preset found by its name.
        idx = it - m_presets.begin();
    // 2) Select the new preset.
    if (idx != (size_t)-1) {
        this->select_preset(idx);
        return true;
    }
    m_idx_selected = idx;
    return false;
}

// Merge one vendor's presets with the other vendor's presets, report duplicates.
std::vector<std::string> PresetCollection::merge_presets(PresetCollection &&other, const VendorMap &new_vendors)
{
    std::vector<std::string> duplicates;
    for (Preset &preset : other.m_presets) {
        if (preset.is_default || preset.is_external)
            continue;
        Preset key(m_type, preset.name);
        auto it = std::lower_bound(m_presets.begin() + m_num_default_presets, m_presets.end(), key);
        if (it == m_presets.end() || it->name != preset.name) {
            if (preset.vendor != nullptr) {
                // Re-assign a pointer to the vendor structure in the new PresetBundle.
                auto it = new_vendors.find(preset.vendor->id);
                assert(it != new_vendors.end());
                preset.vendor = &it->second;
            }
            m_presets.emplace(it, std::move(preset));
        } else
            duplicates.emplace_back(std::move(preset.name));
    }
    return duplicates;
}

void PresetCollection::update_vendor_ptrs_after_copy(const VendorMap &new_vendors)
{
    for (Preset &preset : m_presets)
        if (preset.vendor != nullptr) {
            assert(! preset.is_default && ! preset.is_external);
            // Re-assign a pointer to the vendor structure in the new PresetBundle.
            auto it = new_vendors.find(preset.vendor->id);
            assert(it != new_vendors.end());
            preset.vendor = &it->second;
        }
}

void PresetCollection::update_map_alias_to_profile_name()
{
	m_map_alias_to_profile_name.clear();
	for (const Preset &preset : m_presets)
		m_map_alias_to_profile_name.emplace_back(preset.alias, preset.name);
	std::sort(m_map_alias_to_profile_name.begin(), m_map_alias_to_profile_name.end(), [](auto &l, auto &r) { return l.first < r.first; });
}

void PresetCollection::update_map_system_profile_renamed()
{
	m_map_system_profile_renamed.clear();
	for (Preset &preset : m_presets)
		for (const std::string &renamed_from : preset.renamed_from) {
            const auto [it, success] = m_map_system_profile_renamed.insert(std::pair<std::string, std::string>(renamed_from, preset.name));
			if (! success)
                BOOST_LOG_TRIVIAL(error) << boost::format("Preset name \"%1%\" was marked as renamed from \"%2%\", though preset name \"%3%\" was marked as renamed from \"%2%\" as well.") % preset.name % renamed_from % it->second;
		}
}

std::string PresetCollection::name() const
{
    switch (this->type()) {
    case Preset::TYPE_PRINT:        return L("print");
    case Preset::TYPE_FILAMENT:     return L("filament");
    case Preset::TYPE_SLA_PRINT:    return L("SLA print");
    case Preset::TYPE_SLA_MATERIAL: return L("SLA material");
    case Preset::TYPE_PRINTER:      return L("printer");
    default:                        return "invalid";
    }
}

std::string PresetCollection::section_name() const
{
    switch (this->type()) {
    case Preset::TYPE_PRINT:        return "print";
    case Preset::TYPE_FILAMENT:     return "filament";
    case Preset::TYPE_SLA_PRINT:    return "sla_print";
    case Preset::TYPE_SLA_MATERIAL: return "sla_material";
    case Preset::TYPE_PRINTER:      return "printer";
    default:                        return "invalid";
    }
}

// Used for validating the "inherits" flag when importing user's config bundles.
// Returns names of all system presets including the former names of these presets.
std::vector<std::string> PresetCollection::system_preset_names() const
{
    size_t num = 0;
    for (const Preset &preset : m_presets)
        if (preset.is_system)
            ++ num;
    std::vector<std::string> out;
    out.reserve(num);
    for (const Preset &preset : m_presets)
        if (preset.is_system) {
            out.emplace_back(preset.name);
            out.insert(out.end(), preset.renamed_from.begin(), preset.renamed_from.end());
        }
    std::sort(out.begin(), out.end());
    return out;
}

// Generate a file path from a profile name. Add the ".ini" suffix if it is missing.
std::string PresetCollection::path_from_name(const std::string &new_name) const
{
    std::string file_name = boost::iends_with(new_name, ".ini") ? new_name : (new_name + ".ini");
    return (boost::filesystem::path(m_dir_path) / file_name).make_preferred().string();
}

const Preset& PrinterPresetCollection::default_preset_for(const DynamicPrintConfig &config) const
{
    const ConfigOptionEnumGeneric *opt_printer_technology = config.opt<ConfigOptionEnumGeneric>("printer_technology");
    return this->default_preset((opt_printer_technology == nullptr || opt_printer_technology->value == ptFFF) ? 0 : 1);
}

const Preset* PrinterPresetCollection::find_system_preset_by_model_and_variant(const std::string &model_id, const std::string& variant) const
{
    if (model_id.empty()) { return nullptr; }

    const auto it = std::find_if(cbegin(), cend(), [&](const Preset &preset) {
        if (!preset.is_system || preset.config.opt_string("printer_model") != model_id)
            return false;
        if (variant.empty())
            return true;
        return preset.config.opt_string("printer_variant") == variant;
    });

    return it != cend() ? &*it : nullptr;
}

bool  PrinterPresetCollection::only_default_printers() const
{
    for (const auto& printer : get_presets()) {
        if (! printer.is_default)
            return false;
    }
    return true;
}
// -------------------------
// ***  PhysicalPrinter  ***
// -------------------------

std::string PhysicalPrinter::separator()
{
    return " * ";
}

static std::vector<std::string> s_PhysicalPrinter_opts {
    "preset_name", // temporary option to compatibility with older Slicer
    "preset_names",
    "printer_technology",
    "host_type",
    "print_host",
    "printhost_apikey",
    "printhost_cafile",
    "printhost_port",
    "printhost_authorization_type",
    // HTTP digest authentization (RFC 2617)
    "printhost_user",
    "printhost_password",
    "printhost_ssl_ignore_revoke"
};

const std::vector<std::string>& PhysicalPrinter::printer_options()
{
    return s_PhysicalPrinter_opts;
}

static constexpr auto legacy_print_host_options = {
    "print_host",
    "printhost_apikey",
    "printhost_cafile",
};

std::vector<std::string> PhysicalPrinter::presets_with_print_host_information(const PrinterPresetCollection& printer_presets)
{
    std::vector<std::string> presets;
    for (const Preset& preset : printer_presets)
        if (has_print_host_information(preset.config))
            presets.emplace_back(preset.name);

    return presets;
}

bool PhysicalPrinter::has_print_host_information(const DynamicPrintConfig& config)
{
    for (const char *opt : legacy_print_host_options)
        if (!config.opt_string(opt).empty())
            return true;

    return false;
}

const std::set<std::string>& PhysicalPrinter::get_preset_names() const
{
    return preset_names;
}

bool PhysicalPrinter::has_empty_config() const
{
    return  config.opt_string("print_host"        ).empty() &&
            config.opt_string("printhost_apikey"  ).empty() &&
            config.opt_string("printhost_cafile"  ).empty() &&
            config.opt_string("printhost_port"    ).empty() &&
            config.opt_string("printhost_user"    ).empty() &&
            config.opt_string("printhost_password").empty();
}

// temporary workaround for compatibility with older Slicer
static void update_preset_name_option(const std::set<std::string>& preset_names, DynamicPrintConfig& config)
{
    std::string name;
    for (auto el : preset_names)
        name += el + ";";
    name.pop_back();
    config.set_key_value("preset_name", new ConfigOptionString(name));
}

void PhysicalPrinter::update_preset_names_in_config()
{
    if (!preset_names.empty()) {
        std::vector<std::string>& values = config.option<ConfigOptionStrings>("preset_names")->values;
        values.clear();
        for (const std::string& preset : preset_names)
            values.push_back(preset);

        // temporary workaround for compatibility with older Slicer
        update_preset_name_option(preset_names, config);
    }
}

void PhysicalPrinter::save(const std::string& file_name_from, const std::string& file_name_to)
{
    // rename the file
    boost::nowide::rename(file_name_from.data(), file_name_to.data());
    this->file = file_name_to;
    // save configuration
    this->config.save(this->file);
}

void PhysicalPrinter::update_from_preset(const Preset& preset)
{
    config.apply_only(preset.config, printer_options(), true);
    // add preset names to the options list
    preset_names.emplace(preset.name);
    update_preset_names_in_config();
}

void PhysicalPrinter::update_from_config(const DynamicPrintConfig& new_config)
{
    config.apply_only(new_config, printer_options(), false);

    const std::vector<std::string>& values = config.option<ConfigOptionStrings>("preset_names")->values;

    if (values.empty())
        preset_names.clear();
    else {
        for (const std::string& val : values)
            preset_names.emplace(val);
        // temporary workaround for compatibility with older Slicer
        update_preset_name_option(preset_names, config);
    }
}

void PhysicalPrinter::reset_presets()
{
    return preset_names.clear();
}

bool PhysicalPrinter::add_preset(const std::string& preset_name)
{
    return preset_names.emplace(preset_name).second;
}

bool PhysicalPrinter::delete_preset(const std::string& preset_name)
{
    return preset_names.erase(preset_name) > 0;
}

PhysicalPrinter::PhysicalPrinter(const std::string& name, const DynamicPrintConfig& default_config) :
    name(name), config(default_config)
{
    update_from_config(config);
}

PhysicalPrinter::PhysicalPrinter(const std::string& name, const DynamicPrintConfig &default_config, const Preset& preset) :
    name(name), config(default_config)
{
    update_from_preset(preset);
}

void PhysicalPrinter::set_name(const std::string& name)
{
    this->name = name;
}

std::string PhysicalPrinter::get_full_name(const std::string& preset_name) const
{
    return name + separator() + preset_name;
}

std::string PhysicalPrinter::get_short_name(std::string full_name)
{
    int pos = full_name.find(separator());
    if (pos > 0)
        boost::erase_tail(full_name, full_name.length() - pos);
    return full_name;
}

std::string PhysicalPrinter::get_preset_name(std::string name)
{
    int pos = name.find(separator());
    boost::erase_head(name, pos + 3);
    return Preset::remove_suffix_modified(name);
}


// -----------------------------------
// ***  PhysicalPrinterCollection  ***
// -----------------------------------

PhysicalPrinterCollection::PhysicalPrinterCollection( const std::vector<std::string>& keys, PresetBundle* preset_bundle)
    :m_preset_bundle_owner(preset_bundle)
{
    // Default config for a physical printer containing all key/value pairs of PhysicalPrinter::printer_options().
    for (const std::string &key : keys) {
        const ConfigOptionDef *opt = print_config_def.get(key);
        assert(opt);
        assert(opt->default_value);
        m_default_config.set_key_value(key, opt->default_value->clone());
    }
}

static void update_preset_names_if_were_renamed(std::set<std::string>& preset_names, const PrinterPresetCollection& printer_presets)
{
    std::set<std::string> new_names;

    bool was_renamed{ false };
    for (const std::string& preset_name : preset_names) {
        const Preset* preset = printer_presets.find_preset(preset_name);
        if (preset)
            new_names.emplace(preset_name);
        else {
            if (const std::string* new_name = printer_presets.get_preset_name_renamed(preset_name)) {
                BOOST_LOG_TRIVIAL(warning) << "Printer preset present " << preset_name << "was renamed to: " << *new_name << std::endl;
                new_names.emplace(*new_name);
                was_renamed = true;
            }
        }
    }

    if (was_renamed)
        preset_names = new_names;
}

// Load all printers found in dir_path.
// Throws an exception on error.
void PhysicalPrinterCollection::load_printers(
    const std::string& dir_path, const std::string& subdir, 
    PresetsConfigSubstitutions& substitutions, ForwardCompatibilitySubstitutionRule substitution_rule)
{
    // Don't use boost::filesystem::canonical() on Windows, it is broken in regard to reparse points,
    // see https://github.com/QIDITECH/QIDISlicer/issues/732
    boost::filesystem::path dir = boost::filesystem::absolute(boost::filesystem::path(dir_path) / subdir).make_preferred();
    m_dir_path = dir.string();
    std::string errors_cummulative;
    // Store the loaded printers into a new vector, otherwise the binary search for already existing presets would be broken.
    std::deque<PhysicalPrinter> printers_loaded;
    for (auto& dir_entry : boost::filesystem::directory_iterator(dir))
        if (Slic3r::is_ini_file(dir_entry)) {
            std::string name = dir_entry.path().filename().string();
            // Remove the .ini suffix.
            name.erase(name.size() - 4);
            if (PhysicalPrinter* found_printer = this->find_printer(name, false)) {
                // This happens when there's is a preset (most likely legacy one) with the same name as a system preset
                // that's already been loaded from a bundle.
                BOOST_LOG_TRIVIAL(warning) << "Printer already present, not loading: " << name;

                // But some of used printer_preset might have been renamed.
                // Check it and replace with new name(s) if it's needed
                update_preset_names_if_were_renamed(found_printer->preset_names, m_preset_bundle_owner->printers);

                continue;
            }
            try {
                PhysicalPrinter printer(name, this->default_config());
                printer.file = dir_entry.path().string();
                // Load the preset file, apply preset values on top of defaults.
                try {
                    DynamicPrintConfig config;
                    ConfigSubstitutions config_substitutions = config.load_from_ini(printer.file, substitution_rule);
                    if (! config_substitutions.empty())
                        substitutions.push_back({ name, Preset::TYPE_PHYSICAL_PRINTER, PresetConfigSubstitutions::Source::UserFile, printer.file, std::move(config_substitutions) });
                    printer.update_from_config(config);
                    printer.loaded = true;
                    // Some of used printer_preset might have been renamed.
                    // Check it and replace with new name(s) if it's needed
                    update_preset_names_if_were_renamed(printer.preset_names, m_preset_bundle_owner->printers);
                }
                catch (const std::ifstream::failure& err) {
                    throw Slic3r::RuntimeError(std::string("The selected preset cannot be loaded: ") + printer.file + "\n\tReason: " + err.what());
                }
                catch (const std::runtime_error& err) {
                    throw Slic3r::RuntimeError(std::string("Failed loading the preset file: ") + printer.file + "\n\tReason: " + err.what());
                }
                printers_loaded.emplace_back(printer);
            }
            catch (const std::runtime_error& err) {
                errors_cummulative += err.what();
                errors_cummulative += "\n";
            }
        }
    m_printers.insert(m_printers.end(), std::make_move_iterator(printers_loaded.begin()), std::make_move_iterator(printers_loaded.end()));
    std::sort(m_printers.begin(), m_printers.end());
    if (!errors_cummulative.empty())
        throw Slic3r::RuntimeError(errors_cummulative);
}

void PhysicalPrinterCollection::load_printer(const std::string& path, const std::string& name, DynamicPrintConfig&& config, bool select, bool save/* = false*/)
{
    auto it = this->find_printer_internal(name);
    if (it == m_printers.end() || it->name != name) {
        // The preset was not found. Create a new preset.
        it = m_printers.emplace(it, PhysicalPrinter(name, config));
    }

    it->file = path;
    it->config = std::move(config);
    it->loaded = true;
    // Some of used printer_preset might have been renamed.
    // Check it and replace with new name(s) if it's needed
    update_preset_names_if_were_renamed(it->preset_names, m_preset_bundle_owner->printers);
    if (select)
        this->select_printer(*it);

    if (save)
        it->save();
}

// if there is saved user presets, contains information about "Print Host upload",
// Create default printers with this presets
// Note! "Print Host upload" options will be cleared after physical printer creations
void PhysicalPrinterCollection::load_printers_from_presets(PrinterPresetCollection& printer_presets)
{
    int cnt=0;
    for (Preset& preset: printer_presets) {
        DynamicPrintConfig& config = preset.config;
        for(const char* option : legacy_print_host_options) {
            if (!config.opt_string(option).empty()) {
                // check if printer with those "Print Host upload" options already exist
                PhysicalPrinter* existed_printer = find_printer_with_same_config(config);
                if (existed_printer)
                    // just add preset for this printer
                    existed_printer->add_preset(preset.name);
                else {
                    std::string new_printer_name = (boost::format("Printer %1%") % ++cnt ).str();
                    while (find_printer(new_printer_name))
                        new_printer_name = (boost::format("Printer %1%") % ++cnt).str();

                    // create new printer from this preset
                    PhysicalPrinter printer(new_printer_name, this->default_config(), preset);
                    printer.loaded = true;
                    save_printer(printer);
                }

                // erase "Print Host upload" information from the preset
                for (const char *opt : legacy_print_host_options)
                    config.opt_string(opt).clear();
                // save changes for preset
                preset.save();

                // update those changes for edited preset if it's equal to the preset
                Preset& edited = printer_presets.get_edited_preset();
                if (preset.name == edited.name) {
                    for (const char *opt : legacy_print_host_options)
                        edited.config.opt_string(opt).clear();
                }

                break;
            }
        }
    }
}

PhysicalPrinter* PhysicalPrinterCollection::find_printer( const std::string& name, bool case_sensitive_search)
{
    auto it = this->find_printer_internal(name, case_sensitive_search);

    // Ensure that a temporary copy is returned if the preset found is currently selected.
    auto is_equal_name = [name, case_sensitive_search](const std::string& in_name) {
        if (case_sensitive_search)
            return in_name == name;
        return boost::to_lower_copy<std::string>(in_name) == boost::to_lower_copy<std::string>(name);
    };

    if (it == m_printers.end() || !is_equal_name(it->name))
        return nullptr;
    return &this->printer(it - m_printers.begin());
}

std::deque<PhysicalPrinter>::iterator PhysicalPrinterCollection::find_printer_internal(const std::string& name, bool case_sensitive_search/* = true*/)
{
    if (case_sensitive_search)
        return Slic3r::lower_bound_by_predicate(m_printers.begin(), m_printers.end(), [&name](const auto& l) { return l.name < name;  });

    std::string low_name = boost::to_lower_copy<std::string>(name);

    size_t i = 0;
    for (const PhysicalPrinter& printer : m_printers) {
        if (boost::to_lower_copy<std::string>(printer.name) == low_name)
            break;
        i++;
    }
    if (i == m_printers.size())
        return m_printers.end();

    return m_printers.begin() + i;
}

PhysicalPrinter* PhysicalPrinterCollection::find_printer_with_same_config(const DynamicPrintConfig& config)
{
    for (const PhysicalPrinter& printer :*this) {
        bool is_equal = true;
        for (const char *opt : legacy_print_host_options)
            if (is_equal && printer.config.opt_string(opt) != config.opt_string(opt))
                is_equal = false;

        if (is_equal)
            return find_printer(printer.name);
    }
    return nullptr;
}

// Generate a file path from a profile name. Add the ".ini" suffix if it is missing.
std::string PhysicalPrinterCollection::path_from_name(const std::string& new_name) const
{
    std::string file_name = boost::iends_with(new_name, ".ini") ? new_name : (new_name + ".ini");
    return (boost::filesystem::path(m_dir_path) / file_name).make_preferred().string();
}

void PhysicalPrinterCollection::save_printer(PhysicalPrinter& edited_printer, const std::string& renamed_from/* = ""*/)
{
    // controll and update preset_names in edited_printer config
    edited_printer.update_preset_names_in_config();

    std::string name = renamed_from.empty() ? edited_printer.name : renamed_from;
    // 1) Find the printer with a new_name or create a new one,
    // initialize it with the edited config.
    auto it = this->find_printer_internal(name);
    if (it != m_printers.end() && it->name == name) {
        // Printer with the same name found.
        // Overwriting an existing preset.
        it->config = std::move(edited_printer.config);
        it->name = edited_printer.name;
        it->preset_names = edited_printer.preset_names;
        // sort printers and get new it
        std::sort(m_printers.begin(), m_printers.end());
        it = this->find_printer_internal(edited_printer.name);
    }
    else {
        // Creating a new printer.
        it = m_printers.emplace(it, edited_printer);
    }
    assert(it != m_printers.end());

    // 2) Save printer
    PhysicalPrinter& printer = *it;
    if (printer.file.empty())
        printer.file = this->path_from_name(printer.name);

    if (printer.file == this->path_from_name(printer.name))
        printer.save();
    else
        // if printer was renamed, we should rename a file and than save the config
        printer.save(printer.file, this->path_from_name(printer.name));

    // update idx_selected
    m_idx_selected = it - m_printers.begin();
}

bool PhysicalPrinterCollection::delete_printer(const std::string& name)
{
    auto it = this->find_printer_internal(name);
    if (it == m_printers.end())
        return false;

    const PhysicalPrinter& printer = *it;
    // Erase the preset file.
    boost::nowide::remove(printer.file.c_str());
    m_printers.erase(it);
    return true;
}

bool PhysicalPrinterCollection::delete_selected_printer()
{
    if (!has_selection())
        return false;
    const PhysicalPrinter& printer = this->get_selected_printer();

    // Erase the preset file.
    boost::nowide::remove(printer.file.c_str());
    // Remove the preset from the list.
    m_printers.erase(m_printers.begin() + m_idx_selected);
    // unselect all printers
    unselect_printer();

    return true;
}

bool PhysicalPrinterCollection::delete_preset_from_printers( const std::string& preset_name)
{
    std::vector<std::string> printers_for_delete;
    for (PhysicalPrinter& printer : m_printers) {
        if (printer.preset_names.size() == 1 && *printer.preset_names.begin() == preset_name)
            printers_for_delete.emplace_back(printer.name);
        else if (printer.delete_preset(preset_name))
            save_printer(printer);
    }

    if (!printers_for_delete.empty())
        for (const std::string& printer_name : printers_for_delete)
            delete_printer(printer_name);

    unselect_printer();
    return true;
}

void PhysicalPrinterCollection::rename_preset_in_printers(const std::string& old_preset_name, const std::string& new_preset_name)
{
    for (PhysicalPrinter& printer : m_printers)
        if (printer.delete_preset(old_preset_name)) {
            printer.add_preset(new_preset_name);
            printer.update_preset_names_in_config();
            printer.save();
        }
}

// Get list of printers which have more than one preset and "preset_names" preset is one of them
std::vector<std::string> PhysicalPrinterCollection::get_printers_with_preset(const std::string& preset_name, bool respect_only_preset /*= true*/)
{
    std::vector<std::string> printers;

    for (auto printer : m_printers) {
        if (!respect_only_preset && printer.preset_names.size() == 1)
            continue;
        if (printer.preset_names.find(preset_name) != printer.preset_names.end())
            printers.emplace_back(printer.name);
    }

    return printers;
}

// Get list of printers which has only "preset_names" preset
std::vector<std::string> PhysicalPrinterCollection::get_printers_with_only_preset(const std::string& preset_name)
{
    std::vector<std::string> printers;

    for (const PhysicalPrinter& printer : m_printers)
        if (printer.preset_names.size() == 1 && *printer.preset_names.begin() == preset_name)
            printers.emplace_back(printer.name);

    return printers;
}

std::string PhysicalPrinterCollection::get_selected_full_printer_name() const
{
    return (m_idx_selected == size_t(-1)) ? std::string() : this->get_selected_printer().get_full_name(m_selected_preset);
}

void PhysicalPrinterCollection::select_printer(const std::string& full_name)
{
    std::string printer_name = PhysicalPrinter::get_short_name(full_name);
    auto it = this->find_printer_internal(printer_name);
    if (it == m_printers.end()) {
        unselect_printer();
        return;
    }

    // update idx_selected
    m_idx_selected = it - m_printers.begin();

    // update name of the currently selected preset
    if (printer_name == full_name)
        // use first preset in the list
        m_selected_preset = *it->preset_names.begin();
    else
        m_selected_preset = it->get_preset_name(full_name);

    // Check if selected preset wasn't renamed and replace it with new name
    if (const std::string* new_name = m_preset_bundle_owner->printers.get_preset_name_renamed(m_selected_preset))
        m_selected_preset = *new_name;
}

void PhysicalPrinterCollection::select_printer(const std::string& printer_name, const std::string& preset_name)
{
    if (preset_name.empty())
        return select_printer(printer_name);
    return select_printer(printer_name + PhysicalPrinter::separator() + preset_name);
}

void PhysicalPrinterCollection::select_printer(const PhysicalPrinter& printer)
{
    return select_printer(printer.name);
}

bool PhysicalPrinterCollection::has_selection() const
{
    return m_idx_selected != size_t(-1);
}

void PhysicalPrinterCollection::unselect_printer()
{
    m_idx_selected = size_t(-1);
    m_selected_preset.clear();
}

bool PhysicalPrinterCollection::is_selected(PhysicalPrinterCollection::ConstIterator it, const std::string& preset_name) const
{
    return  m_idx_selected      == size_t(it - m_printers.begin()) &&
            m_selected_preset   == preset_name;
}

ExtruderFilaments::ExtruderFilaments(PresetCollection* filaments_collection, size_t extruder_id, std::string selected_name/* = std::string()*/)
: m_filaments (filaments_collection)
, m_extruder_id(extruder_id)
{
    const std::deque<Preset>& presets = m_filaments->get_presets();
    for (size_t id = 0; id < presets.size(); id ++)
        m_extr_filaments.emplace_back(&(presets[id]));

    select_filament(selected_name.empty() ? m_filaments->get_selected_preset_name() : selected_name);
}

const std::string& ExtruderFilaments::get_preset_name_by_alias(const std::string& alias) const
{
    const auto& aliases_map = m_filaments->map_alias_to_profile_name();
    for (
        // Find the 1st profile name with the alias.
        auto it = Slic3r::lower_bound_by_predicate(aliases_map.begin(), aliases_map.end(), [&alias](auto& l) { return l.first < alias; });
        // Continue over all profile names with the same alias.
        it != aliases_map.end() && it->first == alias; ++it)
        if (auto it_filament = find_filament_internal(it->second);
            it_filament != m_extr_filaments.end() && it_filament->preset->name == it->second &&
            it_filament->preset->is_visible && (it_filament->is_compatible || size_t(it_filament - m_extr_filaments.begin()) == m_idx_selected))
            return it_filament->preset->name;
    return alias;
}

void ExtruderFilaments::select_filament(size_t idx) 
{ 
    assert(idx == size_t(-1) || idx < m_extr_filaments.size());
    // Check idx befor saving it's value to m_idx_selected.
    // Invalidate m_idx_selected, if idx is out of range m_extr_filaments
    m_idx_selected = (idx == size_t(-1) || idx < m_extr_filaments.size()) ? idx : size_t(-1); 
}

bool ExtruderFilaments::select_filament(const std::string &name_w_suffix, bool force/*= false*/)
{
    std::string name = Preset::remove_suffix_modified(name_w_suffix);
    // 1) Try to find the preset by its name.
    auto it = this->find_filament_internal(name);
    size_t idx = 0;
    if (it != m_extr_filaments.end() && it->preset->name == name && it->preset->is_visible)
        // Preset found by its name and it is visible.
        idx = it - m_extr_filaments.begin();
    else {
        // Find the first visible preset.
        for (size_t i = 0; i < m_extr_filaments.size(); ++i)
            if (m_extr_filaments[i].preset->is_visible/* && m_extr_filaments[i].is_compatible*/) {
                idx = i;
                break;
            }
        // If the first visible preset was not found, return the 0th element, which is the default preset.
    }
    // 2) Select the new preset.
    if (m_idx_selected != idx || force) {
        this->select_filament(idx);
        return true;
    }

    return false;
}

size_t ExtruderFilaments::update_compatible_internal(const PresetWithVendorProfile  &active_printer,
                                                     const PresetWithVendorProfile  *active_print,
                                                     PresetSelectCompatibleType      unselect_if_incompatible)
{
    DynamicPrintConfig config;
    config.set_key_value("printer_preset", new ConfigOptionString(active_printer.preset.name));
    const ConfigOption* opt = active_printer.preset.config.option("nozzle_diameter");
    if (opt)
        config.set_key_value("num_extruders", new ConfigOptionInt((int)static_cast<const ConfigOptionFloats*>(opt)->values.size()));

    // Adjust printer preset config to the first extruder from m_extruder_id 
    Preset printer_preset_adjusted = active_printer.preset;
    if (m_extruder_id > 0 && !printer_preset_adjusted.config.opt_bool("single_extruder_multi_material")) {
        DynamicPrintConfig& active_printer_config = printer_preset_adjusted.config;
        for (const std::string& key : print_config_def.extruder_option_keys()) {
            if (key == "default_filament_profile")
                continue;// Ignore this field, because this parameter is not related to the extruder but to whole printer.
            auto* opt = active_printer_config.option(key, false);
            if (opt != nullptr && opt->is_vector())
                static_cast<ConfigOptionVectorBase*>(opt)->set_at(opt, 0, m_extruder_id);
        }
    }
    PresetWithVendorProfile active_printer_adjusted(printer_preset_adjusted, active_printer.vendor);

    std::vector<size_t> indices_of_template_presets;
    indices_of_template_presets.reserve(m_extr_filaments.size());

    size_t num_default_presets = m_filaments->num_default_presets();
    for (size_t idx_preset = num_default_presets; idx_preset < m_extr_filaments.size(); ++idx_preset) {
        const bool    is_selected   = idx_preset == m_idx_selected;
        const Preset* preset        = m_extr_filaments[idx_preset].preset;
        Filament& extr_filament = m_extr_filaments[idx_preset];

        const PresetWithVendorProfile this_preset_with_vendor_profile = m_filaments->get_preset_with_vendor_profile(*preset);
        bool    was_compatible = extr_filament.is_compatible;
        extr_filament.is_compatible = is_compatible_with_printer(this_preset_with_vendor_profile, active_printer_adjusted, &config);
        if (active_print != nullptr)
            extr_filament.is_compatible &= is_compatible_with_print(this_preset_with_vendor_profile, *active_print, active_printer_adjusted);
        if (!extr_filament.is_compatible && is_selected &&
            (unselect_if_incompatible == PresetSelectCompatibleType::Always || (unselect_if_incompatible == PresetSelectCompatibleType::OnlyIfWasCompatible && was_compatible)))
            m_idx_selected = size_t(-1);
        if (preset->vendor && preset->vendor->templates_profile) {
            if (preset->is_visible)
                indices_of_template_presets.push_back(idx_preset);
            else {
                extr_filament.is_compatible = false;
                if (is_selected)
                    m_idx_selected = size_t(-1);
            }
        }
    }

    // filter out template profiles where profile with same alias and compability exists
    if (!indices_of_template_presets.empty()) {
        for (size_t idx = num_default_presets; idx < m_extr_filaments.size(); ++idx) {
            const Filament& filament    = m_extr_filaments[idx];
            const VendorProfile*    vendor      = filament.preset->vendor;
            if (vendor && !vendor->templates_profile && filament.is_compatible) {
                const std::string& preset_alias = filament.preset->alias;
                for (const auto& template_idx : indices_of_template_presets) {
                    if (m_extr_filaments[template_idx].preset->alias == preset_alias) {
                        m_extr_filaments[template_idx].is_compatible = false;
                        // unselect selected template filament if there is non-template alias compatible
                        if (template_idx == m_idx_selected && (unselect_if_incompatible != PresetSelectCompatibleType::Never))
                            m_idx_selected = size_t(-1);
                        break;
                    }
                }
            }
        }
    }

    return m_idx_selected;
}


namespace PresetUtils {
	const VendorProfile::PrinterModel* system_printer_model(const Preset &preset)
	{
		const VendorProfile::PrinterModel *out = nullptr;
		if (preset.vendor != nullptr) {
			const auto *printer_model = preset.config.opt<ConfigOptionString>("printer_model");
			if (printer_model != nullptr && ! printer_model->value.empty()) {
				auto it = std::find_if(preset.vendor->models.begin(), preset.vendor->models.end(), [printer_model](const VendorProfile::PrinterModel &pm) { return pm.id == printer_model->value; });
				if (it != preset.vendor->models.end())
					out = &(*it);
			}
		}
		return out;
	}

    std::string system_printer_bed_model(const Preset& preset)
    {
        std::string out;
        const VendorProfile::PrinterModel* pm = PresetUtils::system_printer_model(preset);
        if (pm != nullptr && !pm->bed_model.empty()) {
            out = Slic3r::data_dir() + "/vendor/" + preset.vendor->id + "/" + pm->bed_model;
            if (!boost::filesystem::exists(boost::filesystem::path(out)))
                out = Slic3r::resources_dir() + "/profiles/" + preset.vendor->id + "/" + pm->bed_model;
        }
        return out;
    }

    std::string system_printer_bed_texture(const Preset& preset)
    {
        std::string out;
        const VendorProfile::PrinterModel* pm = PresetUtils::system_printer_model(preset);
        if (pm != nullptr && !pm->bed_texture.empty()) {
            out = Slic3r::data_dir() + "/vendor/" + preset.vendor->id + "/" + pm->bed_texture;
            if (!boost::filesystem::exists(boost::filesystem::path(out)))
                out = Slic3r::resources_dir() + "/profiles/" + preset.vendor->id + "/" + pm->bed_texture;
        }
        return out;
    }

    bool vendor_profile_has_all_resources(const VendorProfile& vp)
    {
        namespace fs = boost::filesystem;

        std::string vendor_folder = Slic3r::data_dir()      + "/vendor/"   + vp.id + "/";
        std::string rsrc_folder   = Slic3r::resources_dir() + "/profiles/" + vp.id + "/";
        std::string cache_folder  = Slic3r::data_dir()      + "/cache/"    + vp.id + "/";
        for (const VendorProfile::PrinterModel& model : vp.models) {
            for (const std::string& res : { model.bed_texture, model.bed_model, model.thumbnail } ) {
                if (! res.empty()
                 && !fs::exists(fs::path(vendor_folder + res))
                 && !fs::exists(fs::path(rsrc_folder   + res))
                 && !fs::exists(fs::path(cache_folder  + res)))
                    return false;
            }
        }
        return true;
    }
    bool compare_vendor_profile_printers(const VendorProfile& vp_old, const VendorProfile& vp_new, std::vector<std::string>& new_printers)
    {
        for (const VendorProfile::PrinterModel& model : vp_new.models)
        {
            if (std::find_if(vp_old.models.begin(), vp_old.models.end(), [model](const VendorProfile::PrinterModel& pm) { return pm.id == model.id; }) == vp_old.models.end())
                new_printers.push_back(model.name);
        }
        return new_printers.empty();
    }
} // namespace PresetUtils

} // namespace Slic3r
