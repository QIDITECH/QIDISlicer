#include <algorithm>
#include <vector>
#include <cmath>
#include <cstddef>

#include "Exception.hpp"
#include "Print.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

// 1-based extruder identifier for this region and role.
unsigned int PrintRegion::extruder(FlowRole role) const
{
    size_t extruder = 0;
    if (role == frPerimeter || role == frExternalPerimeter)
        extruder = m_config.perimeter_extruder;
    else if (role == frInfill)
        extruder = m_config.infill_extruder;
    else if (role == frSolidInfill || role == frTopSolidInfill)
        extruder = m_config.solid_infill_extruder;
    else
        throw Slic3r::InvalidArgument("Unknown role");
    return extruder;
}

Flow PrintRegion::flow(const PrintObject &object, FlowRole role, double layer_height, bool first_layer) const
{
    const PrintConfig          &print_config = object.print()->config();
    ConfigOptionFloatOrPercent  config_width;
    // Get extrusion width from configuration.
    // (might be an absolute value, or a percent value, or zero for auto)
    if (first_layer && print_config.first_layer_extrusion_width.value > 0) {
        config_width = print_config.first_layer_extrusion_width;
    } else if (role == frExternalPerimeter) {
        config_width = m_config.external_perimeter_extrusion_width;
    } else if (role == frPerimeter) {
        config_width = m_config.perimeter_extrusion_width;
    } else if (role == frInfill) {
        config_width = m_config.infill_extrusion_width;
    } else if (role == frSolidInfill) {
        config_width = m_config.solid_infill_extrusion_width;
    } else if (role == frTopSolidInfill) {
        config_width = m_config.top_infill_extrusion_width;
    } else {
        throw Slic3r::InvalidArgument("Unknown role");
    }

    if (config_width.value == 0)
        config_width = object.config().extrusion_width;
    
    // Get the configured nozzle_diameter for the extruder associated to the flow role requested.
    // Here this->extruder(role) - 1 may underflow to MAX_INT, but then the get_at() will follback to zero'th element, so everything is all right.
    auto nozzle_diameter = float(print_config.nozzle_diameter.get_at(this->extruder(role) - 1));
    return Flow::new_from_config_width(role, config_width, nozzle_diameter, float(layer_height));
}

coordf_t PrintRegion::nozzle_dmr_avg(const PrintConfig &print_config) const
{
    return (print_config.nozzle_diameter.get_at(m_config.perimeter_extruder.value    - 1) + 
            print_config.nozzle_diameter.get_at(m_config.infill_extruder.value       - 1) + 
            print_config.nozzle_diameter.get_at(m_config.solid_infill_extruder.value - 1)) / 3.;
}

coordf_t PrintRegion::bridging_height_avg(const PrintConfig &print_config) const
{
    return this->nozzle_dmr_avg(print_config) * sqrt(m_config.bridge_flow_ratio.value);
}

void PrintRegion::collect_object_printing_extruders(const PrintConfig &print_config, const PrintRegionConfig &region_config, const bool has_brim, std::vector<unsigned int> &object_extruders)
{
    // These checks reflect the same logic used in the GUI for enabling/disabling extruder selection fields.
    auto num_extruders = (int)print_config.nozzle_diameter.size();
    auto emplace_extruder = [num_extruders, &object_extruders](int extruder_id) {
    	int i = std::max(0, extruder_id - 1);
        object_extruders.emplace_back((i >= num_extruders) ? 0 : i);
    };
    if (region_config.perimeters.value > 0 || has_brim)
    	emplace_extruder(region_config.perimeter_extruder);
    if (region_config.fill_density.value > 0)
    	emplace_extruder(region_config.infill_extruder);
    if (region_config.top_solid_layers.value > 0 || region_config.bottom_solid_layers.value > 0)
    	emplace_extruder(region_config.solid_infill_extruder);
}

void PrintRegion::collect_object_printing_extruders(const Print &print, std::vector<unsigned int> &object_extruders) const
{
    // PrintRegion, if used by some PrintObject, shall have all the extruders set to an existing printer extruder.
    // If not, then there must be something wrong with the Print::apply() function.
#ifndef NDEBUG
    auto num_extruders = int(print.config().nozzle_diameter.size());
    assert(this->config().perimeter_extruder    <= num_extruders);
    assert(this->config().infill_extruder       <= num_extruders);
    assert(this->config().solid_infill_extruder <= num_extruders);
#endif
    collect_object_printing_extruders(print.config(), this->config(), print.has_brim(), object_extruders);
}

}
