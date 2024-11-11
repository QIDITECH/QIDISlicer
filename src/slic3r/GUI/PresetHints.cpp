#include <cassert>

#include "libslic3r/Flow.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/libslic3r.h"

#include "PresetHints.hpp"

#include <wx/intl.h> 

#include "GUI.hpp"
#include "format.hpp"
#include "I18N.hpp"

namespace Slic3r {

#define MIN_BUF_LENGTH	4096
std::string PresetHints::cooling_description(const Preset &preset)
{
	std::string out;

    bool cooling              = preset.config.opt_bool("cooling", 0);
    int  fan_below_layer_time = preset.config.opt_int("fan_below_layer_time", 0);
    int  full_fan_speed_layer = preset.config.opt_int("full_fan_speed_layer", 0);

    if (cooling) {
		int 	slowdown_below_layer_time 	= preset.config.opt_int("slowdown_below_layer_time", 0);
		int 	min_fan_speed 				= preset.config.opt_int("min_fan_speed", 0);
		int 	max_fan_speed 				= preset.config.opt_int("max_fan_speed", 0);
		int 	min_print_speed				= int(preset.config.opt_float("min_print_speed", 0) + 0.5);

        out += GUI::format(_L("If estimated layer time is below ~%1%s, "
                              "fan will run at %2%%% and print speed will be reduced "
                              "so that no less than %3%s are spent on that layer "
                              "(however, speed will never be reduced below %4%mm/s)."),
                              slowdown_below_layer_time, max_fan_speed, slowdown_below_layer_time, min_print_speed);
        if (fan_below_layer_time > slowdown_below_layer_time) {
            out += "\n";
            if (min_fan_speed != max_fan_speed)
                out += GUI::format(_L("If estimated layer time is greater, but still below ~%1%s, "
                               "fan will run at a proportionally decreasing speed between %2%%% and %3%%%."),
                               fan_below_layer_time, max_fan_speed, min_fan_speed);
            else
                out += GUI::format(_L("If estimated layer time is greater, but still below ~%1%s, "
                               "fan will run at %2%%%"),
                               fan_below_layer_time, min_fan_speed);
        }
        out += "\n";
    }
	if (preset.config.opt_bool("fan_always_on", 0)) {
		int 	disable_fan_first_layers 	= preset.config.opt_int("disable_fan_first_layers", 0);
		int 	min_fan_speed 				= preset.config.opt_int("min_fan_speed", 0);

        if (full_fan_speed_layer > disable_fan_first_layers + 1)
            out += GUI::format(_L("Fan speed will be ramped from zero at layer %1% to %2%%% at layer %3%."), disable_fan_first_layers, min_fan_speed, full_fan_speed_layer);
        else {
            out += GUI::format(cooling ? _L("During the other layers, fan will always run at %1%%%") : _L("Fan will always run at %1%%%"), min_fan_speed) + " ";
            if (disable_fan_first_layers > 1)
                out += GUI::format(_L("except for the first %1% layers."), disable_fan_first_layers);
            else if (disable_fan_first_layers == 1)
            	out += GUI::format(_L("except for the first layer."));
        }
    } else
       out += cooling ? _u8L("During the other layers, fan will be turned off.") : _u8L("Fan will be turned off.");

    return out;
}

static const ConfigOptionFloatOrPercent& first_positive(const ConfigOptionFloatOrPercent *v1, const ConfigOptionFloatOrPercent &v2, const ConfigOptionFloatOrPercent &v3)
{
    return (v1 != nullptr && v1->value > 0) ? *v1 : ((v2.value > 0) ? v2 : v3);
}

std::string PresetHints::maximum_volumetric_flow_description(const PresetBundle &preset_bundle)
{
    // Find out, to which nozzle index is the current filament profile assigned.
    int idx_extruder  = 0;
    int num_extruders = (int)preset_bundle.extruders_filaments.size();
    for (; idx_extruder < num_extruders; ++ idx_extruder)
        if (preset_bundle.extruders_filaments[idx_extruder].get_selected_preset_name() == preset_bundle.filaments.get_selected_preset_name())
            break;
    if (idx_extruder == num_extruders)
        // The current filament preset is not active for any extruder.
        idx_extruder = -1;

    const DynamicPrintConfig &print_config    = preset_bundle.prints   .get_edited_preset().config;
    const DynamicPrintConfig &filament_config = preset_bundle.filaments.get_edited_preset().config;
    const DynamicPrintConfig &printer_config  = preset_bundle.printers .get_edited_preset().config;

    // Current printer values.
    float  nozzle_diameter                  = (float)printer_config.opt_float("nozzle_diameter", idx_extruder);

    // Print config values
    double layer_height                     = print_config.opt_float("layer_height");
    assert(! print_config.option<ConfigOptionFloatOrPercent>("first_layer_height")->percent);
    double first_layer_height               = print_config.opt_float("first_layer_height");
    double support_material_speed           = print_config.opt_float("support_material_speed");
    double support_material_interface_speed = print_config.get_abs_value("support_material_interface_speed", support_material_speed);
    double bridge_speed                     = print_config.opt_float("bridge_speed");
    double bridge_flow_ratio                = print_config.opt_float("bridge_flow_ratio");
    double perimeter_speed                  = print_config.opt_float("perimeter_speed");
    double external_perimeter_speed         = print_config.get_abs_value("external_perimeter_speed", perimeter_speed);
    // double gap_fill_speed                   = print_config.opt_bool("gap_fill_enabled") ? print_config.opt_float("gap_fill_speed") : 0.;
    double infill_speed                     = print_config.opt_float("infill_speed");
    double small_perimeter_speed            = print_config.get_abs_value("small_perimeter_speed", perimeter_speed);
    double solid_infill_speed               = print_config.get_abs_value("solid_infill_speed", infill_speed);
    double top_solid_infill_speed           = print_config.get_abs_value("top_solid_infill_speed", solid_infill_speed);
    // Maximum print speed when auto-speed is enabled by setting any of the above speed values to zero.
    double max_print_speed                  = print_config.opt_float("max_print_speed");
    // Maximum volumetric speed allowed for the print profile.
    double max_volumetric_speed             = print_config.opt_float("max_volumetric_speed");

    const auto &extrusion_width                     = *print_config.option<ConfigOptionFloatOrPercent>("extrusion_width");
    const auto &external_perimeter_extrusion_width  = *print_config.option<ConfigOptionFloatOrPercent>("external_perimeter_extrusion_width");
    const auto &first_layer_extrusion_width         = *print_config.option<ConfigOptionFloatOrPercent>("first_layer_extrusion_width");
    const auto &infill_extrusion_width              = *print_config.option<ConfigOptionFloatOrPercent>("infill_extrusion_width");
    const auto &perimeter_extrusion_width           = *print_config.option<ConfigOptionFloatOrPercent>("perimeter_extrusion_width");
    const auto &solid_infill_extrusion_width        = *print_config.option<ConfigOptionFloatOrPercent>("solid_infill_extrusion_width");
    const auto &support_material_extrusion_width    = *print_config.option<ConfigOptionFloatOrPercent>("support_material_extrusion_width");
    const auto &top_infill_extrusion_width          = *print_config.option<ConfigOptionFloatOrPercent>("top_infill_extrusion_width");
    const auto &first_layer_speed                   = *print_config.option<ConfigOptionFloatOrPercent>("first_layer_speed");

    // Index of an extruder assigned to a feature. If set to 0, an active extruder will be used for a multi-material print.
    // If different from idx_extruder, it will not be taken into account for this hint.
    auto feature_extruder_active = [idx_extruder, num_extruders](int i) {
        return i <= 0 || i > num_extruders || idx_extruder == -1 || idx_extruder == i - 1;
    };
    bool perimeter_extruder_active                  = feature_extruder_active(print_config.opt_int("perimeter_extruder"));
    bool infill_extruder_active                     = feature_extruder_active(print_config.opt_int("infill_extruder"));
    bool solid_infill_extruder_active               = feature_extruder_active(print_config.opt_int("solid_infill_extruder"));
    bool support_material_extruder_active           = feature_extruder_active(print_config.opt_int("support_material_extruder"));
    bool support_material_interface_extruder_active = feature_extruder_active(print_config.opt_int("support_material_interface_extruder"));

    // Current filament values
    double filament_diameter                = filament_config.opt_float("filament_diameter", 0);
    double filament_crossection             = M_PI * 0.25 * filament_diameter * filament_diameter;
    // double extrusion_multiplier             = filament_config.opt_float("extrusion_multiplier", 0);
    // The following value will be annotated by this hint, so it does not take part in the calculation.
//    double filament_max_volumetric_speed    = filament_config.opt_float("filament_max_volumetric_speed", 0);

    std::string out;
    for (size_t idx_type = (first_layer_extrusion_width.value == 0) ? 1 : 0; idx_type < 3; ++ idx_type) {
        // First test the maximum volumetric extrusion speed for non-bridging extrusions.
        bool first_layer = idx_type == 0;
        bool bridging    = idx_type == 2;
		const ConfigOptionFloatOrPercent *first_layer_extrusion_width_ptr = (first_layer && first_layer_extrusion_width.value > 0) ?
			&first_layer_extrusion_width : nullptr;
        const float                       lh  = float(first_layer ? first_layer_height : layer_height);
        double                            max_flow = 0.;
        std::string                       max_flow_extrusion_type;
        auto                              limit_by_first_layer_speed = [&first_layer_speed, first_layer](double speed_normal, double speed_max) {
            if (first_layer && first_layer_speed.value > 0)
                // Apply the first layer limit.
                speed_normal = first_layer_speed.get_abs_value(speed_normal);
            return (speed_normal > 0.) ? speed_normal : speed_max;
        };
        auto test_flow =
            [first_layer_extrusion_width_ptr, extrusion_width, nozzle_diameter, lh, bridging, bridge_speed, bridge_flow_ratio, limit_by_first_layer_speed, max_print_speed, &max_flow, &max_flow_extrusion_type]
            (FlowRole flow_role, const ConfigOptionFloatOrPercent &this_extrusion_width, double speed, const char *err_msg) {
            Flow flow = bridging ?
                Flow::new_from_config_width(flow_role, first_positive(first_layer_extrusion_width_ptr, this_extrusion_width, extrusion_width), nozzle_diameter, lh) :
                Flow::bridging_flow(nozzle_diameter * bridge_flow_ratio, nozzle_diameter);
            double volumetric_flow = flow.mm3_per_mm() * (bridging ? bridge_speed : limit_by_first_layer_speed(speed, max_print_speed));
            if (max_flow < volumetric_flow) {
                max_flow = volumetric_flow;
                max_flow_extrusion_type = GUI::into_u8(_(err_msg));
            }
        };
        if (perimeter_extruder_active) {
            test_flow(frExternalPerimeter, external_perimeter_extrusion_width, std::max(external_perimeter_speed, small_perimeter_speed), L("external perimeters"));
            test_flow(frPerimeter,         perimeter_extrusion_width,          std::max(perimeter_speed,          small_perimeter_speed), L("perimeters"));
        }
        if (! bridging && infill_extruder_active)
            test_flow(frInfill, infill_extrusion_width, infill_speed, L("infill"));
        if (solid_infill_extruder_active) {
            test_flow(frInfill, solid_infill_extrusion_width, solid_infill_speed, L("solid infill"));
            if (! bridging)
                test_flow(frInfill, top_infill_extrusion_width, top_solid_infill_speed, L("top solid infill"));
        }
        if (! bridging && support_material_extruder_active)
            test_flow(frSupportMaterial, support_material_extrusion_width, support_material_speed, L("support"));
        if (support_material_interface_extruder_active)
            test_flow(frSupportMaterialInterface, support_material_extrusion_width, support_material_interface_speed, L("support interface"));
        //FIXME handle gap_fill_speed
        if (! out.empty())
            out += "\n";
        out += (first_layer ? _u8L("First layer volumetric") : (bridging ? _u8L("Bridging volumetric") : _u8L("Volumetric")));
        out += " " + _u8L("flow rate is maximized") + " ";
        bool limited_by_max_volumetric_speed = max_volumetric_speed > 0 && max_volumetric_speed < max_flow;
        out += (limited_by_max_volumetric_speed ? 
            _u8L("by the print profile maximum") :
            (_u8L("when printing")+ " " + max_flow_extrusion_type))
            + " " + _u8L("with a volumetric rate")+ " ";
        if (limited_by_max_volumetric_speed)
            max_flow = max_volumetric_speed;

        out += format(_u8L("%3.2f mm³/s at filament speed %3.2f mm/s."), max_flow, (max_flow / filament_crossection));
    }

 	return out;
}

std::string PresetHints::recommended_thin_wall_thickness(const PresetBundle &preset_bundle)
{
    const DynamicPrintConfig &print_config    = preset_bundle.prints   .get_edited_preset().config;
    const DynamicPrintConfig &printer_config  = preset_bundle.printers .get_edited_preset().config;

    float   layer_height                        = float(print_config.opt_float("layer_height"));
    int     num_perimeters                      = print_config.opt_int("perimeters");
    bool    thin_walls                          = print_config.opt_bool("thin_walls");
    float   nozzle_diameter                     = float(printer_config.opt_float("nozzle_diameter", 0));
    
    std::string out;
	if (layer_height <= 0.f) {
		out += _u8L("Recommended object thin wall thickness: Not available due to invalid layer height.");
		return out;
	}
    
    if (num_perimeters > 0) {
        int num_lines = std::min(num_perimeters * 2, 10);
        out += (boost::format(_u8L("Recommended object thin wall thickness for layer height %.2f and")) % layer_height).str() + " ";
        // Start with the width of two closely spaced 
        try {
            Flow external_perimeter_flow = Flow::new_from_config_width(
                frExternalPerimeter, 
                *print_config.opt<ConfigOptionFloatOrPercent>("external_perimeter_extrusion_width"), 
                nozzle_diameter, layer_height);
            Flow perimeter_flow          = Flow::new_from_config_width(
                frPerimeter, 
                *print_config.opt<ConfigOptionFloatOrPercent>("perimeter_extrusion_width"), 
                nozzle_diameter, layer_height);
	        double width = external_perimeter_flow.width() + external_perimeter_flow.spacing();
	        for (int i = 2; i <= num_lines; thin_walls ? ++ i : i += 2) {
	            if (i > 2)
	                out += ", ";
	            out += (boost::format(_u8L("%d lines: %.2f mm")) % i %  width).str() + " ";
	            width += perimeter_flow.spacing() * (thin_walls ? 1.f : 2.f);
	        }
	    } catch (const FlowErrorNegativeSpacing &) {
            out = _u8L("Recommended object thin wall thickness: Not available due to excessively small extrusion width.");
        }
    }
    return out;
}


// Produce a textual explanation of the combined effects of the top/bottom_solid_layers
// versus top/bottom_min_shell_thickness. Which of the two values wins depends
// on the active layer height.
std::string PresetHints::top_bottom_shell_thickness_explanation(const PresetBundle &preset_bundle)
{
    const DynamicPrintConfig &print_config    = preset_bundle.prints   .get_edited_preset().config;
    const DynamicPrintConfig &printer_config  = preset_bundle.printers .get_edited_preset().config;

    std::string out;

    int 	top_solid_layers                = print_config.opt_int("top_solid_layers");
    int 	bottom_solid_layers             = print_config.opt_int("bottom_solid_layers");
    bool    has_top_layers 					= top_solid_layers > 0;
    bool    has_bottom_layers 				= bottom_solid_layers > 0;
    double  top_solid_min_thickness        	= print_config.opt_float("top_solid_min_thickness");
    double  bottom_solid_min_thickness  	= print_config.opt_float("bottom_solid_min_thickness");
    double  layer_height                    = print_config.opt_float("layer_height");
    bool    variable_layer_height			= printer_config.opt_bool("variable_layer_height");
    //FIXME the following line takes into account the 1st extruder only.
    double  min_layer_height				= variable_layer_height ? Slicing::min_layer_height_from_nozzle(printer_config, 1) : layer_height;

	if (layer_height <= 0.f) {
		out += _u8L("Top / bottom shell thickness hint: Not available due to invalid layer height.");
		return out;
	}

    if (has_top_layers) {
    	double top_shell_thickness = top_solid_layers * layer_height;
    	if (top_shell_thickness < top_solid_min_thickness) {
    		// top_solid_min_shell_thickness triggers even in case of normal layer height. Round the top_shell_thickness up
    		// to an integer multiply of layer_height.
    		double n = ceil(top_solid_min_thickness / layer_height);
    		top_shell_thickness = n * layer_height;
    	}
    	double top_shell_thickness_minimum = std::max(top_solid_min_thickness, top_solid_layers * min_layer_height);
        out += (boost::format(_u8L("Top shell is %1% mm thick for layer height %2% mm.")) % top_shell_thickness % layer_height).str();
        if (variable_layer_height && top_shell_thickness_minimum < top_shell_thickness) {
        	out += " ";
	        out += (boost::format(_u8L("Minimum top shell thickness is %1% mm.")) % top_shell_thickness_minimum).str();        	
        }
    } else
        out += _u8L("Top is open.");

    out += "\n";

    if (has_bottom_layers) {
    	double bottom_shell_thickness = bottom_solid_layers * layer_height;
    	if (bottom_shell_thickness < bottom_solid_min_thickness) {
    		// bottom_solid_min_shell_thickness triggers even in case of normal layer height. Round the bottom_shell_thickness up
    		// to an integer multiply of layer_height.
    		double n = ceil(bottom_solid_min_thickness / layer_height);
    		bottom_shell_thickness = n * layer_height;
    	}
    	double bottom_shell_thickness_minimum = std::max(bottom_solid_min_thickness, bottom_solid_layers * min_layer_height);
        out += (boost::format(_u8L("Bottom shell is %1% mm thick for layer height %2% mm.")) % bottom_shell_thickness % layer_height).str();
        if (variable_layer_height && bottom_shell_thickness_minimum < bottom_shell_thickness) {
        	out += " ";
	        out += (boost::format(_u8L("Minimum bottom shell thickness is %1% mm.")) % bottom_shell_thickness_minimum).str();        	
        }
    } else 
        out += _u8L("Bottom is open.");

    return out;
}

}; // namespace Slic3r
