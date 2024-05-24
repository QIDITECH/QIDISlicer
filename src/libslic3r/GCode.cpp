#include "Config.hpp"
#include "Geometry/Circle.hpp"
#include "libslic3r.h"
#include "GCode/ExtrusionProcessor.hpp"
#include "I18N.hpp"
#include "GCode.hpp"
#include "Exception.hpp"
#include "ExtrusionEntity.hpp"
#include "Geometry/ConvexHull.hpp"
#include "GCode/LabelObjects.hpp"
#include "GCode/PrintExtents.hpp"
#include "GCode/Thumbnails.hpp"
#include "GCode/WipeTower.hpp"
#include "GCode/WipeTowerIntegration.hpp"
#include "GCode/Travels.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "PrintConfig.hpp"
#include "ShortestPath.hpp"
#include "Print.hpp"
#include "Thread.hpp"
#include "Utils.hpp"
#include "ClipperUtils.hpp"
#include "libslic3r.h"
#include "LocalesUtils.hpp"
#include "format.hpp"

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <math.h>
#include <optional>
#include <string>
#include <string_view>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/foreach.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <boost/nowide/iostream.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/cstdlib.hpp>

#include "SVG.hpp"

#include <tbb/parallel_for.h>

// Intel redesigned some TBB interface considerably when merging TBB with their oneAPI set of libraries, see GH #7332.
// We are using quite an old TBB 2017 U7. Before we update our build servers, let's use the old API, which is deprecated in up to date TBB.
#if ! defined(TBB_VERSION_MAJOR)
    #include <tbb/version.h>
#endif
#if ! defined(TBB_VERSION_MAJOR)
    static_assert(false, "TBB_VERSION_MAJOR not defined");
#endif
#if TBB_VERSION_MAJOR >= 2021
    #include <tbb/parallel_pipeline.h>
    using slic3r_tbb_filtermode = tbb::filter_mode;
#else
    #include <tbb/pipeline.h>
    using slic3r_tbb_filtermode = tbb::filter;
#endif

using namespace std::literals::string_view_literals;

#if 0
// Enable debugging and asserts, even in the release build.
#define DEBUG
#define _DEBUG
#undef NDEBUG
#endif

#include <assert.h>

namespace Slic3r {

// Only add a newline in case the current G-code does not end with a newline.
    static inline void check_add_eol(std::string& gcode)
    {
        if (!gcode.empty() && gcode.back() != '\n')
            gcode += '\n';
    }


    // Return true if tch_prefix is found in custom_gcode
    static bool custom_gcode_changes_tool(const std::string& custom_gcode, const std::string& tch_prefix, unsigned next_extruder)
    {
        bool ok = false;
        size_t from_pos = 0;
        size_t pos = 0;
        while ((pos = custom_gcode.find(tch_prefix, from_pos)) != std::string::npos) {
            if (pos + 1 == custom_gcode.size())
                break;
            from_pos = pos + 1;
            // only whitespace is allowed before the command
            while (--pos < custom_gcode.size() && custom_gcode[pos] != '\n') {
                if (!std::isspace(custom_gcode[pos]))
                    goto NEXT;
            }
            {
                // we should also check that the extruder changes to what was expected
                std::istringstream ss(custom_gcode.substr(from_pos, std::string::npos));
                unsigned num = 0;
                if (ss >> num)
                    ok = (num == next_extruder);
            }
        NEXT:;
        }
        return ok;
    }

    std::string OozePrevention::pre_toolchange(GCodeGenerator &gcodegen)
    {
        std::string gcode;

        unsigned int extruder_id = gcodegen.writer().extruder()->id();
        const ConfigOptionIntsNullable& filament_idle_temp = gcodegen.config().idle_temperature;
        if (filament_idle_temp.is_nil(extruder_id)) {
            // There is no idle temperature defined in filament settings.
            // Use the delta value from print config.
            if (gcodegen.config().standby_temperature_delta.value != 0) {
                // we assume that heating is always slower than cooling, so no need to block
                gcode += gcodegen.writer().set_temperature
                (this->_get_temp(gcodegen) + gcodegen.config().standby_temperature_delta.value, false, extruder_id);
                gcode.pop_back();
                gcode += " ;cooldown\n"; // this is a marker for GCodeProcessor, so it can supress the commands when needed
            }
        } else {
            // Use the value from filament settings. That one is absolute, not delta.
            gcode += gcodegen.writer().set_temperature(filament_idle_temp.get_at(extruder_id), false, extruder_id);
            gcode.pop_back();
            gcode += " ;cooldown\n"; // this is a marker for GCodeProcessor, so it can supress the commands when needed
        }

        return gcode;
    }

    std::string OozePrevention::post_toolchange(GCodeGenerator &gcodegen)
    {
        return (gcodegen.config().standby_temperature_delta.value != 0) ?
            gcodegen.writer().set_temperature(this->_get_temp(gcodegen), true, gcodegen.writer().extruder()->id()) :
            std::string();
    }

    int OozePrevention::_get_temp(const GCodeGenerator &gcodegen) const
    {
        return (gcodegen.layer() == nullptr || gcodegen.layer()->id() == 0
             || gcodegen.config().temperature.get_at(gcodegen.writer().extruder()->id()) == 0)
            ? gcodegen.config().first_layer_temperature.get_at(gcodegen.writer().extruder()->id())
            : gcodegen.config().temperature.get_at(gcodegen.writer().extruder()->id());
    }


    const std::vector<std::string> ColorPrintColors::Colors = { "#C0392B", "#E67E22", "#F1C40F", "#27AE60", "#1ABC9C", "#2980B9", "#9B59B6" };

#define EXTRUDER_CONFIG(OPT) m_config.OPT.get_at(m_writer.extruder()->id())

void GCodeGenerator::PlaceholderParserIntegration::reset()
{
    this->failed_templates.clear();
    this->output_config.clear();
    this->opt_position = nullptr;
    this->opt_zhop      = nullptr;
    this->opt_e_position = nullptr;
    this->opt_e_retracted = nullptr;
    this->opt_e_restart_extra = nullptr;
    this->opt_extruded_volume = nullptr;
    this->opt_extruded_weight = nullptr;
    this->opt_extruded_volume_total = nullptr;
    this->opt_extruded_weight_total = nullptr;
    this->num_extruders = 0;
    this->position.clear();
    this->e_position.clear();
    this->e_retracted.clear();
    this->e_restart_extra.clear();
}

void GCodeGenerator::PlaceholderParserIntegration::init(const GCodeWriter &writer)
{
    this->reset();
    const std::vector<Extruder> &extruders = writer.extruders();
    if (! extruders.empty()) {
        this->num_extruders = extruders.back().id() + 1;
        this->e_retracted.assign(num_extruders, 0);
        this->e_restart_extra.assign(num_extruders, 0);
        this->opt_e_retracted = new ConfigOptionFloats(e_retracted);
        this->opt_e_restart_extra = new ConfigOptionFloats(e_restart_extra);
        this->output_config.set_key_value("e_retracted", this->opt_e_retracted);
        this->output_config.set_key_value("e_restart_extra", this->opt_e_restart_extra);
        if (! writer.config.use_relative_e_distances) {
            e_position.assign(num_extruders, 0);
            opt_e_position = new ConfigOptionFloats(e_position);
            this->output_config.set_key_value("e_position", opt_e_position);
        }
    }
    this->opt_extruded_volume = new ConfigOptionFloats(this->num_extruders, 0.f);
    this->opt_extruded_weight = new ConfigOptionFloats(this->num_extruders, 0.f);
    this->opt_extruded_volume_total = new ConfigOptionFloat(0.f);
    this->opt_extruded_weight_total = new ConfigOptionFloat(0.f);
    this->parser.set("extruded_volume", this->opt_extruded_volume);
    this->parser.set("extruded_weight", this->opt_extruded_weight);
    this->parser.set("extruded_volume_total", this->opt_extruded_volume_total);
    this->parser.set("extruded_weight_total", this->opt_extruded_weight_total);

    // Reserve buffer for current position.
    this->position.assign(3, 0);
    this->opt_position = new ConfigOptionFloats(this->position);
    this->output_config.set_key_value("position", this->opt_position);
    // Store zhop variable into the parser itself, it is a read-only variable to the script.
    this->opt_zhop = new ConfigOptionFloat(writer.get_zhop());
    this->parser.set("zhop", this->opt_zhop);
}

void GCodeGenerator::PlaceholderParserIntegration::update_from_gcodewriter(const GCodeWriter &writer, const WipeTowerData& wipe_tower_data)
{
    memcpy(this->position.data(), writer.get_position().data(), sizeof(double) * 3);
    this->opt_position->values = this->position;

    if (this->num_extruders > 0) {
        const std::vector<Extruder> &extruders = writer.extruders();
        assert(! extruders.empty() && num_extruders == extruders.back().id() + 1);
        this->e_retracted.assign(num_extruders, 0);
        this->e_restart_extra.assign(num_extruders, 0);
        this->opt_extruded_volume->values.assign(num_extruders, 0);
        this->opt_extruded_weight->values.assign(num_extruders, 0);
        double total_volume = 0.;
        double total_weight = 0.;
        for (const Extruder &e : extruders) {
            this->e_retracted[e.id()]     = e.retracted();
            this->e_restart_extra[e.id()] = e.restart_extra();
            // Wipe tower filament consumption has to be added separately, because that gcode is not generated by GCodeWriter.
            double wt_vol = 0.;
            const std::vector<std::pair<float, std::vector<float>>>& wtuf = wipe_tower_data.used_filament_until_layer;
            if (!wtuf.empty()) {
                auto it = std::lower_bound(wtuf.begin(), wtuf.end(), writer.get_position().z(),
                                [](const auto& a, const float& val) { return a.first < val; });
                if (it == wtuf.end())
                    it = wtuf.end() - 1;
                wt_vol = it->second[e.id()] * e.filament_crossection();
            }            

            double v = e.extruded_volume() + wt_vol;
            double w = v * e.filament_density() * 0.001;
            this->opt_extruded_volume->values[e.id()] = v;
            this->opt_extruded_weight->values[e.id()] = w;
            total_volume += v;
            total_weight += w;
        }
        opt_extruded_volume_total->value = total_volume;
        opt_extruded_weight_total->value = total_weight;
        opt_e_retracted->values = this->e_retracted;
        opt_e_restart_extra->values = this->e_restart_extra;
        if (! writer.config.use_relative_e_distances) {
            this->e_position.assign(num_extruders, 0);
            for (const Extruder &e : extruders)
                this->e_position[e.id()] = e.position();
            this->opt_e_position->values = this->e_position;
        }
    }
}

// Throw if any of the output vector variables were resized by the script.
void GCodeGenerator::PlaceholderParserIntegration::validate_output_vector_variables()
{
    if (this->opt_position->values.size() != 3)
        throw Slic3r::RuntimeError("\"position\" output variable must not be resized by the script.");
    if (this->num_extruders > 0) {
        if (this->opt_e_position && this->opt_e_position->values.size() != this->num_extruders)
            throw Slic3r::RuntimeError("\"e_position\" output variable must not be resized by the script.");
        if (this->opt_e_retracted->values.size() != this->num_extruders)
            throw Slic3r::RuntimeError("\"e_retracted\" output variable must not be resized by the script.");
        if (this->opt_e_restart_extra->values.size() != this->num_extruders)
            throw Slic3r::RuntimeError("\"e_restart_extra\" output variable must not be resized by the script.");
    }
}

// Collect pairs of object_layer + support_layer sorted by print_z.
// object_layer & support_layer are considered to be on the same print_z, if they are not further than EPSILON.
GCodeGenerator::ObjectsLayerToPrint GCodeGenerator::collect_layers_to_print(const PrintObject& object)
{
    GCodeGenerator::ObjectsLayerToPrint layers_to_print;
    layers_to_print.reserve(object.layers().size() + object.support_layers().size());

    /*
    // Calculate a minimum support layer height as a minimum over all extruders, but not smaller than 10um.
    // This is the same logic as in support generator.
    //FIXME should we use the printing extruders instead?
    double gap_over_supports = object.config().support_material_contact_distance;
    // FIXME should we test object.config().support_material_synchronize_layers ? Currently the support layers are synchronized with object layers iff soluble supports.
    assert(!object.has_support() || gap_over_supports != 0. || object.config().support_material_synchronize_layers);
    if (gap_over_supports != 0.) {
        gap_over_supports = std::max(0., gap_over_supports);
        // Not a soluble support,
        double support_layer_height_min = 1000000.;
        for (auto lh : object.print()->config().min_layer_height.values)
            support_layer_height_min = std::min(support_layer_height_min, std::max(0.01, lh));
        gap_over_supports += support_layer_height_min;
    }*/

    std::vector<std::pair<double, double>> warning_ranges;

    // Pair the object layers with the support layers by z.
    size_t idx_object_layer = 0;
    size_t idx_support_layer = 0;
    const ObjectLayerToPrint* last_extrusion_layer = nullptr;
    while (idx_object_layer < object.layers().size() || idx_support_layer < object.support_layers().size()) {
        ObjectLayerToPrint layer_to_print;
        layer_to_print.object_layer = (idx_object_layer < object.layers().size()) ? object.layers()[idx_object_layer++] : nullptr;
        layer_to_print.support_layer = (idx_support_layer < object.support_layers().size()) ? object.support_layers()[idx_support_layer++] : nullptr;
        if (layer_to_print.object_layer && layer_to_print.support_layer) {
            if (layer_to_print.object_layer->print_z < layer_to_print.support_layer->print_z - EPSILON) {
                layer_to_print.support_layer = nullptr;
                --idx_support_layer;
            }
            else if (layer_to_print.support_layer->print_z < layer_to_print.object_layer->print_z - EPSILON) {
                layer_to_print.object_layer = nullptr;
                --idx_object_layer;
            }
        }

        layers_to_print.emplace_back(layer_to_print);

        bool has_extrusions = (layer_to_print.object_layer && layer_to_print.object_layer->has_extrusions())
            || (layer_to_print.support_layer && layer_to_print.support_layer->has_extrusions());

        // Check that there are extrusions on the very first layer. The case with empty
        // first layer may result in skirt/brim in the air and maybe other issues.
        if (layers_to_print.size() == 1u) {
            if (!has_extrusions)
                throw Slic3r::SlicingError(_u8L("There is an object with no extrusions in the first layer.") + "\n" +
                                           _u8L("Object name") + ": " + object.model_object()->name);
        }

        // In case there are extrusions on this layer, check there is a layer to lay it on.
        if ((layer_to_print.object_layer && layer_to_print.object_layer->has_extrusions())
            // Allow empty support layers, as the support generator may produce no extrusions for non-empty support regions.
            || (layer_to_print.support_layer /* && layer_to_print.support_layer->has_extrusions() */)) {
            double top_cd = object.config().support_material_contact_distance;
            double bottom_cd = object.config().support_material_bottom_contact_distance == 0. ? top_cd : object.config().support_material_bottom_contact_distance;

            double extra_gap = (layer_to_print.support_layer ? bottom_cd : top_cd);

            double maximal_print_z = (last_extrusion_layer ? last_extrusion_layer->print_z() : 0.)
                + layer_to_print.layer()->height
                + std::max(0., extra_gap);
            // Negative support_contact_z is not taken into account, it can result in false positives in cases
            // where previous layer has object extrusions too (https://github.com/qidi3d/QIDISlicer/issues/2752)

            if (has_extrusions && layer_to_print.print_z() > maximal_print_z + 2. * EPSILON)
                warning_ranges.emplace_back(std::make_pair((last_extrusion_layer ? last_extrusion_layer->print_z() : 0.), layers_to_print.back().print_z()));
        }
        // Remember last layer with extrusions.
        if (has_extrusions)
            last_extrusion_layer = &layers_to_print.back();
    }

    if (! warning_ranges.empty()) {
        std::string warning;
        size_t i = 0;
        for (i = 0; i < std::min(warning_ranges.size(), size_t(3)); ++i)
            warning += Slic3r::format(_u8L("Empty layer between %1% and %2%."),
                                      warning_ranges[i].first, warning_ranges[i].second) + "\n";
        if (i < warning_ranges.size())
            warning += _u8L("(Some lines not shown)") + "\n";
        warning += "\n";
        warning += Slic3r::format(_u8L("Object name: %1%"), object.model_object()->name) + "\n\n"
            + _u8L("Make sure the object is printable. This is usually caused by negligibly small extrusions or by a faulty model. "
                "Try to repair the model or change its orientation on the bed.");

        const_cast<Print*>(object.print())->active_step_add_warning(
            PrintStateBase::WarningLevel::CRITICAL, warning);
    }

    return layers_to_print;
}

// Prepare for non-sequential printing of multiple objects: Support resp. object layers with nearly identical print_z
// will be printed for  all objects at once.
// Return a list of <print_z, per object ObjectLayerToPrint> items.
std::vector<std::pair<coordf_t, GCodeGenerator::ObjectsLayerToPrint>> GCodeGenerator::collect_layers_to_print(const Print& print)
{
    struct OrderingItem {
        coordf_t    print_z;
        size_t      object_idx;
        size_t      layer_idx;
    };

    std::vector<ObjectsLayerToPrint>  per_object(print.objects().size(), ObjectsLayerToPrint());
    std::vector<OrderingItem>         ordering;
    for (size_t i = 0; i < print.objects().size(); ++i) {
        per_object[i] = collect_layers_to_print(*print.objects()[i]);
        OrderingItem ordering_item;
        ordering_item.object_idx = i;
        ordering.reserve(ordering.size() + per_object[i].size());
        const ObjectLayerToPrint &front = per_object[i].front();
        for (const ObjectLayerToPrint &ltp : per_object[i]) {
            ordering_item.print_z = ltp.print_z();
            ordering_item.layer_idx = &ltp - &front;
            ordering.emplace_back(ordering_item);
        }
    }

    std::sort(ordering.begin(), ordering.end(), [](const OrderingItem& oi1, const OrderingItem& oi2) { return oi1.print_z < oi2.print_z; });

    std::vector<std::pair<coordf_t, ObjectsLayerToPrint>> layers_to_print;

    // Merge numerically very close Z values.
    for (size_t i = 0; i < ordering.size();) {
        // Find the last layer with roughly the same print_z.
        size_t j = i + 1;
        coordf_t zmax = ordering[i].print_z + EPSILON;
        for (; j < ordering.size() && ordering[j].print_z <= zmax; ++j);
        // Merge into layers_to_print.
        std::pair<coordf_t, ObjectsLayerToPrint> merged;
        // Assign an average print_z to the set of layers with nearly equal print_z.
        merged.first = 0.5 * (ordering[i].print_z + ordering[j - 1].print_z);
        merged.second.assign(print.objects().size(), ObjectLayerToPrint());
        for (; i < j; ++i) {
            const OrderingItem& oi = ordering[i];
            assert(merged.second[oi.object_idx].layer() == nullptr);
            merged.second[oi.object_idx] = std::move(per_object[oi.object_idx][oi.layer_idx]);
        }
        layers_to_print.emplace_back(std::move(merged));
    }

    return layers_to_print;
}

// free functions called by GCodeGenerator::do_export()
namespace DoExport {
//    static void update_print_estimated_times_stats(const GCodeProcessor& processor, PrintStatistics& print_statistics)
//    {
//        const GCodeProcessorResult& result = processor.get_result();
//        print_statistics.estimated_normal_print_time = get_time_dhms(result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].time);
//        print_statistics.estimated_silent_print_time = processor.is_stealth_time_estimator_enabled() ?
//            get_time_dhms(result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].time) : "N/A";
//    }

    static void update_print_estimated_stats(const GCodeProcessor& processor, const std::vector<Extruder>& extruders, PrintStatistics& print_statistics)
    {
        const GCodeProcessorResult& result = processor.get_result();
        print_statistics.estimated_normal_print_time = get_time_dhms(result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].time);
        print_statistics.estimated_silent_print_time = processor.is_stealth_time_estimator_enabled() ?
            get_time_dhms(result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].time) : "N/A";

        // update filament statictics
        double total_extruded_volume = 0.0;
        double total_used_filament   = 0.0;
        double total_weight          = 0.0;
        double total_cost            = 0.0;
        for (auto volume : result.print_statistics.volumes_per_extruder) {
            total_extruded_volume += volume.second;

            size_t extruder_id = volume.first;
            auto extruder = std::find_if(extruders.begin(), extruders.end(), [extruder_id](const Extruder& extr) { return extr.id() == extruder_id; });
            if (extruder == extruders.end())
                continue;

            double s = PI * sqr(0.5* extruder->filament_diameter());
            double weight = volume.second * extruder->filament_density() * 0.001;
            total_used_filament += volume.second/s;
            total_weight        += weight;
            total_cost          += weight * extruder->filament_cost() * 0.001;
        }

        print_statistics.total_extruded_volume = total_extruded_volume;
        print_statistics.total_used_filament   = total_used_filament;
        print_statistics.total_weight          = total_weight;
        print_statistics.total_cost            = total_cost;

        print_statistics.filament_stats        = result.print_statistics.volumes_per_extruder;
    }

    // if any reserved keyword is found, returns a std::vector containing the first MAX_COUNT keywords found
    // into pairs containing:
    // first: source
    // second: keyword
    // to be shown in the warning notification
    // The returned vector is empty if no keyword has been found
    static std::vector<std::pair<std::string, std::string>> validate_custom_gcode(const Print& print) {
        static const unsigned int MAX_TAGS_COUNT = 5;
        std::vector<std::pair<std::string, std::string>> ret;

        auto check = [&ret](const std::string& source, const std::string& gcode) {
            std::vector<std::string> tags;
            if (GCodeProcessor::contains_reserved_tags(gcode, MAX_TAGS_COUNT, tags)) {
                if (!tags.empty()) {
                    size_t i = 0;
                    while (ret.size() < MAX_TAGS_COUNT && i < tags.size()) {
                        ret.push_back({ source, tags[i] });
                        ++i;
                    }
                }
            }
        };

        const GCodeConfig& config = print.config();
        check(_u8L("Start G-code"), config.start_gcode.value);
        if (ret.size() < MAX_TAGS_COUNT) check(_u8L("End G-code"), config.end_gcode.value);
        if (ret.size() < MAX_TAGS_COUNT) check(_u8L("Before layer change G-code"), config.before_layer_gcode.value);
        if (ret.size() < MAX_TAGS_COUNT) check(_u8L("After layer change G-code"), config.layer_gcode.value);
        if (ret.size() < MAX_TAGS_COUNT) check(_u8L("Tool change G-code"), config.toolchange_gcode.value);
        if (ret.size() < MAX_TAGS_COUNT) check(_u8L("Between objects G-code (for sequential printing)"), config.between_objects_gcode.value);
        if (ret.size() < MAX_TAGS_COUNT) check(_u8L("Color Change G-code"), config.color_change_gcode.value);
        if (ret.size() < MAX_TAGS_COUNT) check(_u8L("Pause Print G-code"), config.pause_print_gcode.value);
        if (ret.size() < MAX_TAGS_COUNT) check(_u8L("Template Custom G-code"), config.template_custom_gcode.value);
        if (ret.size() < MAX_TAGS_COUNT) {
            for (const std::string& value : config.start_filament_gcode.values) {
                check(_u8L("Filament Start G-code"), value);
                if (ret.size() == MAX_TAGS_COUNT)
                    break;
            }
        }
        if (ret.size() < MAX_TAGS_COUNT) {
            for (const std::string& value : config.end_filament_gcode.values) {
                check(_u8L("Filament End G-code"), value);
                if (ret.size() == MAX_TAGS_COUNT)
                    break;
            }
        }
        if (ret.size() < MAX_TAGS_COUNT) {
            const CustomGCode::Info& custom_gcode_per_print_z = print.model().custom_gcode_per_print_z;
            for (const auto& gcode : custom_gcode_per_print_z.gcodes) {
                check(_u8L("Custom G-code"), gcode.extra);
                if (ret.size() == MAX_TAGS_COUNT)
                    break;
            }
        }

        return ret;
    }
} // namespace DoExport

GCodeGenerator::GCodeGenerator(const Print* print) :
    m_origin(Vec2d::Zero()),
    m_enable_loop_clipping(true), 
    m_enable_cooling_markers(false), 
    m_enable_extrusion_role_markers(false),
    m_last_processor_extrusion_role(GCodeExtrusionRole::None),
    m_layer_count(0),
    m_layer_index(-1), 
    m_layer(nullptr),
    m_object_layer_over_raft(false),
    m_volumetric_speed(0),
    m_last_extrusion_role(GCodeExtrusionRole::None),
    m_last_width(0.0f),
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    m_last_mm3_per_mm(0.0),
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
    m_brim_done(false),
    m_second_layer_things_done(false),
    m_silent_time_estimator_enabled(false),
    m_print(print)
    {}
void GCodeGenerator::do_export(Print* print, const char* path, GCodeProcessorResult* result, ThumbnailsGeneratorCallback thumbnail_cb)
{
    CNumericLocalesSetter locales_setter;

    // Does the file exist? If so, we hope that it is still valid.
    {
        PrintStateBase::StateWithTimeStamp state = print->step_state_with_timestamp(psGCodeExport);
        if (! state.enabled || (state.is_done() && boost::filesystem::exists(boost::filesystem::path(path))))
            return;
    }

    // Enabled and either not done, or marked as done while the output file is missing.
    print->set_started(psGCodeExport);

    // check if any custom gcode contains keywords used by the gcode processor to
    // produce time estimation and gcode toolpaths
    std::vector<std::pair<std::string, std::string>> validation_res = DoExport::validate_custom_gcode(*print);
    if (!validation_res.empty()) {
        std::string reports;
        for (const auto& [source, keyword] : validation_res) {
            reports += source + ": \"" + keyword + "\"\n";
        }
        print->active_step_add_warning(PrintStateBase::WarningLevel::NON_CRITICAL,
            _u8L("In the custom G-code were found reserved keywords:") + "\n" +
            reports +
            _u8L("This may cause problems in g-code visualization and printing time estimation."));
    }

    BOOST_LOG_TRIVIAL(info) << "Exporting G-code..." << log_memory_info();

    // Remove the old g-code if it exists.
    boost::nowide::remove(path);

    std::string path_tmp(path);
    path_tmp += ".tmp";

    m_processor.initialize(path_tmp);
    m_processor.set_print(print);
    m_processor.get_binary_data() = bgcode::binarize::BinaryData();
    GCodeOutputStream file(boost::nowide::fopen(path_tmp.c_str(), "wb"), m_processor);
    if (! file.is_open())
        throw Slic3r::RuntimeError(std::string("G-code export to ") + path + " failed.\nCannot open the file for writing.\n");

    try {
        this->_do_export(*print, file, thumbnail_cb);
        file.flush();
        if (file.is_error()) {
            file.close();
            boost::nowide::remove(path_tmp.c_str());
            throw Slic3r::RuntimeError(std::string("G-code export to ") + path + " failed\nIs the disk full?\n");
        }
    } catch (std::exception & /* ex */) {
        // Rethrow on any exception. std::runtime_exception and CanceledException are expected to be thrown.
        // Close and remove the file.
        file.close();
        boost::nowide::remove(path_tmp.c_str());
        throw;
    }
    file.close();

    if (! m_placeholder_parser_integration.failed_templates.empty()) {
        // G-code export proceeded, but some of the PlaceholderParser substitutions failed.
        //FIXME localize!
        std::string msg = std::string("G-code export to ") + path + " failed due to invalid custom G-code sections:\n\n";
        for (const auto &name_and_error : m_placeholder_parser_integration.failed_templates)
            msg += name_and_error.first + "\n" + name_and_error.second + "\n";
        msg += "\nPlease inspect the file ";
        msg += path_tmp + " for error messages enclosed between\n";
        msg += "        !!!!! Failed to process the custom G-code template ...\n";
        msg += "and\n";
        msg += "        !!!!! End of an error report for the custom G-code template ...\n";
        msg += "for all macro processing errors.";
        throw Slic3r::PlaceholderParserError(msg);
    }

    BOOST_LOG_TRIVIAL(debug) << "Start processing gcode, " << log_memory_info();
    // Post-process the G-code to update time stamps.
    m_processor.finalize(true);
//    DoExport::update_print_estimated_times_stats(m_processor, print->m_print_statistics);
    DoExport::update_print_estimated_stats(m_processor, m_writer.extruders(), print->m_print_statistics);
    if (result != nullptr) {
        *result = std::move(m_processor.extract_result());
        // set the filename to the correct value
        result->filename = path;
    }
    BOOST_LOG_TRIVIAL(debug) << "Finished processing gcode, " << log_memory_info();

    if (rename_file(path_tmp, path))
        throw Slic3r::RuntimeError(
            std::string("Failed to rename the output G-code file from ") + path_tmp + " to " + path + '\n' +
            "Is " + path_tmp + " locked?" + '\n');

    BOOST_LOG_TRIVIAL(info) << "Exporting G-code finished" << log_memory_info();
    print->set_done(psGCodeExport);
}

// free functions called by GCodeGenerator::_do_export()
namespace DoExport {
    static void init_gcode_processor(const PrintConfig& config, GCodeProcessor& processor, bool& silent_time_estimator_enabled)
    {
        silent_time_estimator_enabled = (config.gcode_flavor == gcfMarlinLegacy || config.gcode_flavor == gcfMarlinFirmware)
                                        && config.silent_mode;
        processor.reset();
        processor.initialize_result_moves();
        processor.apply_config(config);
        processor.enable_stealth_time_estimator(silent_time_estimator_enabled);
    }

	static double autospeed_volumetric_limit(const Print &print)
	{
	    // get the minimum cross-section used in the print
	    std::vector<double> mm3_per_mm;
	    for (auto object : print.objects()) {
	        for (size_t region_id = 0; region_id < object->num_printing_regions(); ++ region_id) {
	            const PrintRegion &region = object->printing_region(region_id);
	            for (auto layer : object->layers()) {
	                const LayerRegion* layerm = layer->regions()[region_id];
	                if (region.config().get_abs_value("perimeter_speed") == 0 ||
	                    region.config().get_abs_value("small_perimeter_speed") == 0 ||
	                    region.config().get_abs_value("external_perimeter_speed") == 0 ||
	                    region.config().get_abs_value("bridge_speed") == 0)
	                    mm3_per_mm.push_back(layerm->perimeters().min_mm3_per_mm());
	                if (region.config().get_abs_value("infill_speed") == 0 ||
	                    region.config().get_abs_value("solid_infill_speed") == 0 ||
	                    region.config().get_abs_value("top_solid_infill_speed") == 0 ||
                        region.config().get_abs_value("bridge_speed") == 0)
                    {
                        // Minimal volumetric flow should not be calculated over ironing extrusions.
                        // Use following lambda instead of the built-it method.
                        // https://github.com/qidi3d/QIDISlicer/issues/5082
                        auto min_mm3_per_mm_no_ironing = [](const ExtrusionEntityCollection& eec) -> double {
                            double min = std::numeric_limits<double>::max();
                            for (const ExtrusionEntity* ee : eec.entities)
                                if (ee->role() != ExtrusionRole::Ironing)
                                    min = std::min(min, ee->min_mm3_per_mm());
                            return min;
                        };

                        mm3_per_mm.push_back(min_mm3_per_mm_no_ironing(layerm->fills()));
                    }
	            }
	        }
	        if (object->config().get_abs_value("support_material_speed") == 0 ||
	            object->config().get_abs_value("support_material_interface_speed") == 0)
	            for (auto layer : object->support_layers())
	                mm3_per_mm.push_back(layer->support_fills.min_mm3_per_mm());
	    }
	    // filter out 0-width segments
	    mm3_per_mm.erase(std::remove_if(mm3_per_mm.begin(), mm3_per_mm.end(), [](double v) { return v < 0.000001; }), mm3_per_mm.end());
	    double volumetric_speed = 0.;
	    if (! mm3_per_mm.empty()) {
	        // In order to honor max_print_speed we need to find a target volumetric
	        // speed that we can use throughout the print. So we define this target 
	        // volumetric speed as the volumetric speed produced by printing the 
	        // smallest cross-section at the maximum speed: any larger cross-section
	        // will need slower feedrates.
	        volumetric_speed = *std::min_element(mm3_per_mm.begin(), mm3_per_mm.end()) * print.config().max_print_speed.value;
	        // limit such volumetric speed with max_volumetric_speed if set
	        if (print.config().max_volumetric_speed.value > 0)
	            volumetric_speed = std::min(volumetric_speed, print.config().max_volumetric_speed.value);
	    }
	    return volumetric_speed;
	}


    static void init_ooze_prevention(const Print &print, OozePrevention &ooze_prevention)
	{
	    ooze_prevention.enable = print.config().ooze_prevention.value && ! print.config().single_extruder_multi_material;
	}

	// Fill in print_statistics and return formatted string containing filament statistics to be inserted into G-code comment section.
    static std::string update_print_stats_and_format_filament_stats(
        const bool                   has_wipe_tower,
	    const WipeTowerData         &wipe_tower_data,
        const FullPrintConfig       &config,
	    const std::vector<Extruder> &extruders,
        unsigned int                 initial_extruder_id,
        int                          total_toolchanges,
        PrintStatistics              &print_statistics,
        bool                         export_binary_data,
        bgcode::binarize::BinaryData &binary_data)
    {
		std::string filament_stats_string_out;

	    print_statistics.clear();
        print_statistics.total_toolchanges = total_toolchanges;
        print_statistics.initial_extruder_id = initial_extruder_id;
        std::vector<std::string> filament_types;
	    if (! extruders.empty()) {
            std::pair<std::string, unsigned int> out_filament_used_mm(PrintStatistics::FilamentUsedMmMask + " ", 0);
            std::pair<std::string, unsigned int> out_filament_used_cm3(PrintStatistics::FilamentUsedCm3Mask + " ", 0);
            std::pair<std::string, unsigned int> out_filament_used_g(PrintStatistics::FilamentUsedGMask + " ", 0);
            std::pair<std::string, unsigned int> out_filament_cost(PrintStatistics::FilamentCostMask + " ", 0);
	        for (const Extruder &extruder : extruders) {
                print_statistics.printing_extruders.emplace_back(extruder.id());
                filament_types.emplace_back(config.filament_type.get_at(extruder.id()));

                double used_filament   = extruder.used_filament() + (has_wipe_tower ? wipe_tower_data.used_filament_until_layer.back().second[extruder.id()] : 0.f);
                double extruded_volume = extruder.extruded_volume() + (has_wipe_tower ? wipe_tower_data.used_filament_until_layer.back().second[extruder.id()] * extruder.filament_crossection() : 0.f); // assumes 1.75mm filament diameter
	            double filament_weight = extruded_volume * extruder.filament_density() * 0.001;
	            double filament_cost   = filament_weight * extruder.filament_cost()    * 0.001;
                auto append = [&extruder](std::pair<std::string, unsigned int> &dst, const char *tmpl, double value) {
                    assert(is_decimal_separator_point());
	                while (dst.second < extruder.id()) {
	                    // Fill in the non-printing extruders with zeros.
	                    dst.first += (dst.second > 0) ? ", 0" : "0";
	                    ++ dst.second;
	                }
	                if (dst.second > 0)
	                    dst.first += ", ";
	                char buf[64];
					sprintf(buf, tmpl, value);
	                dst.first += buf;
	                ++ dst.second;
	            };
                if (!export_binary_data) {
	            append(out_filament_used_mm,  "%.2lf", used_filament);
	            append(out_filament_used_cm3, "%.2lf", extruded_volume * 0.001);
                }
	            if (filament_weight > 0.) {
	                print_statistics.total_weight = print_statistics.total_weight + filament_weight;
                    if (!export_binary_data)
	                append(out_filament_used_g, "%.2lf", filament_weight);
	                if (filament_cost > 0.) {
	                    print_statistics.total_cost = print_statistics.total_cost + filament_cost;
                        if (!export_binary_data)
	                    append(out_filament_cost, "%.2lf", filament_cost);
	                }
	            }
	            print_statistics.total_used_filament += used_filament;
	            print_statistics.total_extruded_volume += extruded_volume;
	            print_statistics.total_wipe_tower_filament += has_wipe_tower ? used_filament - extruder.used_filament() : 0.;
                print_statistics.total_wipe_tower_filament_weight += has_wipe_tower ? (extruded_volume - extruder.extruded_volume()) * extruder.filament_density() * 0.001 : 0.;
	            print_statistics.total_wipe_tower_cost += has_wipe_tower ? (extruded_volume - extruder.extruded_volume())* extruder.filament_density() * 0.001 * extruder.filament_cost() * 0.001 : 0.;
	        }
            if (!export_binary_data) {
	        filament_stats_string_out += out_filament_used_mm.first;
            filament_stats_string_out += "\n" + out_filament_used_cm3.first;
            if (out_filament_used_g.second)
                filament_stats_string_out += "\n" + out_filament_used_g.first;
            if (out_filament_cost.second)
                filament_stats_string_out += "\n" + out_filament_cost.first;
            }
            print_statistics.initial_filament_type = config.filament_type.get_at(initial_extruder_id);
            std::sort(filament_types.begin(), filament_types.end());
            print_statistics.printing_filament_types = filament_types.front();
            for (size_t i = 1; i < filament_types.size(); ++ i) {
                print_statistics.printing_filament_types += ",";
                print_statistics.printing_filament_types += filament_types[i];
            }
        }
        return filament_stats_string_out;
    }
}

#if 0
// Sort the PrintObjects by their increasing Z, likely useful for avoiding colisions on Deltas during sequential prints.
static inline std::vector<const PrintInstance*> sort_object_instances_by_max_z(const Print &print)
{
    std::vector<const PrintObject*> objects(print.objects().begin(), print.objects().end());
    std::sort(objects.begin(), objects.end(), [](const PrintObject *po1, const PrintObject *po2) { return po1->height() < po2->height(); });
    std::vector<const PrintInstance*> instances;
    instances.reserve(objects.size());
    for (const PrintObject *object : objects)
        for (size_t i = 0; i < object->instances().size(); ++ i)
            instances.emplace_back(&object->instances()[i]);
    return instances;
}
#endif

// Produce a vector of PrintObjects in the order of their respective ModelObjects in print.model().
std::vector<const PrintInstance*> sort_object_instances_by_model_order(const Print& print)
{
    // Build up map from ModelInstance* to PrintInstance*
    std::vector<std::pair<const ModelInstance*, const PrintInstance*>> model_instance_to_print_instance;
    model_instance_to_print_instance.reserve(print.num_object_instances());
    for (const PrintObject *print_object : print.objects())
        for (const PrintInstance &print_instance : print_object->instances())
            model_instance_to_print_instance.emplace_back(print_instance.model_instance, &print_instance);
    std::sort(model_instance_to_print_instance.begin(), model_instance_to_print_instance.end(), [](auto &l, auto &r) { return l.first < r.first; });

    std::vector<const PrintInstance*> instances;
    instances.reserve(model_instance_to_print_instance.size());
    for (const ModelObject *model_object : print.model().objects)
        for (const ModelInstance *model_instance : model_object->instances) {
            auto it = std::lower_bound(model_instance_to_print_instance.begin(), model_instance_to_print_instance.end(), std::make_pair(model_instance, nullptr), [](auto &l, auto &r) { return l.first < r.first; });
            if (it != model_instance_to_print_instance.end() && it->first == model_instance)
                instances.emplace_back(it->second);
        }
    return instances;
}

static inline bool arc_welder_enabled(const PrintConfig& print_config)
{
    return
        // Enabled
        print_config.arc_fitting != ArcFittingType::Disabled &&
        // Not a spiral vase print
        !print_config.spiral_vase &&
        // Presure equalizer not used
        print_config.max_volumetric_extrusion_rate_slope_negative == 0. &&
        print_config.max_volumetric_extrusion_rate_slope_positive == 0.;
}

static inline GCode::SmoothPathCache::InterpolationParameters interpolation_parameters(const PrintConfig& print_config)
{
    return {
        scaled<double>(print_config.gcode_resolution.value),
        arc_welder_enabled(print_config) ? Geometry::ArcWelder::default_arc_length_percent_tolerance : 0
    };
}

static inline GCode::SmoothPathCache smooth_path_interpolate_global(const Print& print)
{
    const GCode::SmoothPathCache::InterpolationParameters interpolation_params = interpolation_parameters(print.config());
    GCode::SmoothPathCache out;
    out.interpolate_add(print.skirt(), interpolation_params);
    out.interpolate_add(print.brim(), interpolation_params);
    return out;
}

static inline bool is_mk2_or_mk3(const std::string &printer_model) {
    if (boost::starts_with(printer_model, "MK2")) {
        return true;
    } else if (boost::starts_with(printer_model, "MK3") && (printer_model.size() <= 3 || printer_model[3] != '.')) {
        // Ignore MK3.5 and MK3.9.
        return true;
    }

    return false;
}

static inline std::optional<std::string> find_M84(const std::string &gcode) {
    std::istringstream gcode_is(gcode);
    std::string gcode_line;
    while (std::getline(gcode_is, gcode_line)) {
        boost::trim(gcode_line);

        if (gcode_line == "M84" || boost::starts_with(gcode_line, "M84 ") || boost::starts_with(gcode_line, "M84;")) {
            return gcode_line;
        }
    }

    return std::nullopt;
}
void GCodeGenerator::_do_export(Print& print, GCodeOutputStream &file, ThumbnailsGeneratorCallback thumbnail_cb)
{
    const bool export_to_binary_gcode = print.full_print_config().option<ConfigOptionBool>("binary_gcode")->value;
    // if exporting gcode in binary format: 
    // we generate here the data to be passed to the post-processor, who is responsible to export them to file 
    // 1) generate the thumbnails
    // 2) collect the config data
    if (export_to_binary_gcode) {
        bgcode::binarize::BinaryData& binary_data = m_processor.get_binary_data();

        // Unit tests or command line slicing may not define "thumbnails" or "thumbnails_format".
        // If "thumbnails_format" is not defined, export to PNG.
        auto [thumbnails, errors] = GCodeThumbnails::make_and_check_thumbnail_list(print.full_print_config());

        if (errors != enum_bitmask<ThumbnailError>()) {
            std::string error_str = format("Invalid thumbnails value:");
            error_str += GCodeThumbnails::get_error_string(errors);
            throw Slic3r::ExportError(error_str);
        }

        if (!thumbnails.empty())
            GCodeThumbnails::generate_binary_thumbnails(
                thumbnail_cb, binary_data.thumbnails, thumbnails,
                [&print]() { print.throw_if_canceled(); });

        // file data
        binary_data.file_metadata.raw_data.emplace_back("Producer", std::string(SLIC3R_APP_NAME) + " " + std::string(SLIC3R_VERSION));

        // config data
        encode_full_config(*m_print, binary_data.slicer_metadata.raw_data);

        // printer data - this section contains duplicates from the slicer metadata
        // that we just created. Find and copy the entries that we want to duplicate.
        const auto& slicer_metadata = binary_data.slicer_metadata.raw_data;
        const std::vector<std::string> keys_to_duplicate = { "printer_model", "filament_type", "nozzle_diameter", "bed_temperature",
                      "brim_width", "fill_density", "layer_height", "temperature", "ironing", "support_material", "extruder_colour" };
        assert(std::is_sorted(slicer_metadata.begin(), slicer_metadata.end(),
                              [](const auto& a, const auto& b) { return a.first < b.first; }));
        for (const std::string& key : keys_to_duplicate) {
            auto it = std::lower_bound(slicer_metadata.begin(), slicer_metadata.end(), std::make_pair(key, 0),
                [](const auto& a, const auto& b) { return a.first < b.first; });
            if (it != slicer_metadata.end() && it->first == key)
                binary_data.printer_metadata.raw_data.emplace_back(*it);
        }
        }

    // modifies m_silent_time_estimator_enabled
    DoExport::init_gcode_processor(print.config(), m_processor, m_silent_time_estimator_enabled);

    if (! print.config().gcode_substitutions.values.empty()) {
        m_find_replace = make_unique<GCodeFindReplace>(print.config());
        file.set_find_replace(m_find_replace.get(), false);
    }

    // resets analyzer's tracking data
    m_last_height  = 0.f;
    m_last_layer_z = 0.f;
    m_max_layer_z  = 0.f;
    m_last_width = 0.f;
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    m_last_mm3_per_mm = 0.;
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

    // How many times will be change_layer() called?gcode.cpp
    // change_layer() in turn increments the progress bar status.
    m_layer_count = 0;
    if (print.config().complete_objects.value) {
        // Add each of the object's layers separately.
        for (auto object : print.objects()) {
            std::vector<coordf_t> zs;
            zs.reserve(object->layers().size() + object->support_layers().size());
            for (auto layer : object->layers())
                zs.push_back(layer->print_z);
            for (auto layer : object->support_layers())
                zs.push_back(layer->print_z);
            std::sort(zs.begin(), zs.end());
            m_layer_count += (unsigned int)(object->instances().size() * (std::unique(zs.begin(), zs.end()) - zs.begin()));
        }
    }
    print.throw_if_canceled();

    m_enable_cooling_markers = true;
    this->apply_print_config(print.config());

    m_volumetric_speed = DoExport::autospeed_volumetric_limit(print);
    print.throw_if_canceled();

    if (print.config().spiral_vase.value)
        m_spiral_vase = make_unique<SpiralVase>(print.config());

    if (print.config().max_volumetric_extrusion_rate_slope_positive.value > 0 ||
        print.config().max_volumetric_extrusion_rate_slope_negative.value > 0)
        m_pressure_equalizer = make_unique<PressureEqualizer>(print.config());
    m_enable_extrusion_role_markers = (bool)m_pressure_equalizer;

    if (print.config().avoid_crossing_curled_overhangs){
        this->m_avoid_crossing_curled_overhangs.init_bed_shape(get_bed_shape(print.config()));
    }

    if (!export_to_binary_gcode)
    // Write information on the generator.
    file.write_format("; %s\n\n", Slic3r::header_slic3r_generated().c_str());


    if (! export_to_binary_gcode) {
        // if exporting gcode in ascii format, generate the thumbnails here
        auto [thumbnails, errors] = GCodeThumbnails::make_and_check_thumbnail_list(print.full_print_config());
        if (errors != enum_bitmask<ThumbnailError>()) {
            std::string error_str = format("Invalid thumbnails value:");
            error_str += GCodeThumbnails::get_error_string(errors);
            throw Slic3r::ExportError(error_str);
        }
        if (!thumbnails.empty())
            GCodeThumbnails::export_thumbnails_to_file(thumbnail_cb, thumbnails,
                [&file](const char* sz) { file.write(sz); },
                [&print]() { print.throw_if_canceled(); });
    }

    // Write notes (content of the Print Settings tab -> Notes)
    {
        std::list<std::string> lines;
        boost::split(lines, print.config().notes.value, boost::is_any_of("\n"), boost::token_compress_off);
        for (auto line : lines) {
            // Remove the trailing '\r' from the '\r\n' sequence.
            if (! line.empty() && line.back() == '\r')
                line.pop_back();
            file.write_format("; %s\n", line.c_str());
        }
        if (! lines.empty())
            file.write("\n");
    }
    print.throw_if_canceled();

    // Write some terse information on the slicing parameters.
    const PrintObject *first_object         = print.objects().front();
    const double       layer_height         = first_object->config().layer_height.value;
    assert(! print.config().first_layer_height.percent);
    const double       first_layer_height   = print.config().first_layer_height.value;
    if (!export_to_binary_gcode) {
    for (size_t region_id = 0; region_id < print.num_print_regions(); ++ region_id) {
        const PrintRegion &region = print.get_print_region(region_id);
        file.write_format("; external perimeters extrusion width = %.2fmm\n", region.flow(*first_object, frExternalPerimeter, layer_height).width());
        file.write_format("; perimeters extrusion width = %.2fmm\n",          region.flow(*first_object, frPerimeter,         layer_height).width());
        file.write_format("; infill extrusion width = %.2fmm\n",              region.flow(*first_object, frInfill,            layer_height).width());
        file.write_format("; solid infill extrusion width = %.2fmm\n",        region.flow(*first_object, frSolidInfill,       layer_height).width());
        file.write_format("; top infill extrusion width = %.2fmm\n",          region.flow(*first_object, frTopSolidInfill,    layer_height).width());
        if (print.has_support_material())
            file.write_format("; support material extrusion width = %.2fmm\n", support_material_flow(first_object).width());
        if (print.config().first_layer_extrusion_width.value > 0)
            file.write_format("; first layer extrusion width = %.2fmm\n",   region.flow(*first_object, frPerimeter, first_layer_height, true).width());
        file.write_format("\n");
    }
    print.throw_if_canceled();
    }
    //B41
    // adds tags for time estimators
    // if (print.config().remaining_times.value)
    //     file.write_format(";%s\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::First_Line_M73_Placeholder).c_str());

    // Starting now, the G-code find / replace post-processor will be enabled.
    file.find_replace_enable();

    // Prepare the helper object for replacing placeholders in custom G-code and output filename.
    m_placeholder_parser_integration.parser = print.placeholder_parser();
    m_placeholder_parser_integration.parser.update_timestamp();
    m_placeholder_parser_integration.context.rng = std::mt19937(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    // Enable passing global variables between PlaceholderParser invocations.
    m_placeholder_parser_integration.context.global_config = std::make_unique<DynamicConfig>();
    print.update_object_placeholders(m_placeholder_parser_integration.parser.config_writable(), ".gcode");

    // Get optimal tool ordering to minimize tool switches of a multi-exruder print.
    // For a print by objects, find the 1st printing object.
    ToolOrdering tool_ordering;
    unsigned int initial_extruder_id = (unsigned int)-1;
    unsigned int final_extruder_id   = (unsigned int)-1;
    bool         has_wipe_tower      = false;
    std::vector<const PrintInstance*> 					print_object_instances_ordering;
    std::vector<const PrintInstance*>::const_iterator 	print_object_instance_sequential_active;
    if (print.config().complete_objects.value) {
        // Order object instances for sequential print.
        print_object_instances_ordering = sort_object_instances_by_model_order(print);
//        print_object_instances_ordering = sort_object_instances_by_max_z(print);
        // Find the 1st printing object, find its tool ordering and the initial extruder ID.
        print_object_instance_sequential_active = print_object_instances_ordering.begin();
        for (; print_object_instance_sequential_active != print_object_instances_ordering.end(); ++ print_object_instance_sequential_active) {
            tool_ordering = ToolOrdering(*(*print_object_instance_sequential_active)->print_object, initial_extruder_id);
            if ((initial_extruder_id = tool_ordering.first_extruder()) != static_cast<unsigned int>(-1))
                break;
        }
        if (initial_extruder_id == static_cast<unsigned int>(-1))
            // No object to print was found, cancel the G-code export.
            throw Slic3r::SlicingError(_u8L("No extrusions were generated for objects."));
        // We don't allow switching of extruders per layer by Model::custom_gcode_per_print_z in sequential mode.
        // Use the extruder IDs collected from Regions.
        this->set_extruders(print.extruders());
    } else {
        // Find tool ordering for all the objects at once, and the initial extruder ID.
        // If the tool ordering has been pre-calculated by Print class for wipe tower already, reuse it.
        tool_ordering = print.tool_ordering();
        tool_ordering.assign_custom_gcodes(print);
        if (tool_ordering.all_extruders().empty())
            // No object to print was found, cancel the G-code export.
            throw Slic3r::SlicingError(_u8L("No extrusions were generated for objects."));
        has_wipe_tower = print.has_wipe_tower() && tool_ordering.has_wipe_tower();
        initial_extruder_id = (has_wipe_tower && ! print.config().single_extruder_multi_material_priming) ?
            // The priming towers will be skipped.
            tool_ordering.all_extruders().back() :
            // Don't skip the priming towers.
            tool_ordering.first_extruder();
        // In non-sequential print, the printing extruders may have been modified by the extruder switches stored in Model::custom_gcode_per_print_z.
        // Therefore initialize the printing extruders from there.
        this->set_extruders(tool_ordering.all_extruders());
        // Order object instances using a nearest neighbor search.
        print_object_instances_ordering = chain_print_object_instances(print);
        m_layer_count = tool_ordering.layer_tools().size();
    }
    if (initial_extruder_id == (unsigned int)-1) {
        // Nothing to print!
        initial_extruder_id = 0;
        final_extruder_id   = 0;
    } else {
        final_extruder_id = tool_ordering.last_extruder();
        assert(final_extruder_id != (unsigned int)-1);
    }
    print.throw_if_canceled();

    m_cooling_buffer = make_unique<CoolingBuffer>(*this);
    m_cooling_buffer->set_current_extruder(initial_extruder_id);

    // Emit machine envelope limits for the Marlin firmware.
    this->print_machine_envelope(file, print);

    // Label all objects so printer knows about them since the start.
    m_label_objects.init(print.objects(), print.config().gcode_label_objects, print.config().gcode_flavor);
    //B41
    // file.write(m_label_objects.all_objects_header());
    // Update output variables after the extruders were initialized.
    m_placeholder_parser_integration.init(m_writer);
    // Let the start-up script prime the 1st printing tool.
    this->placeholder_parser().set("initial_tool", initial_extruder_id);
    this->placeholder_parser().set("initial_extruder", initial_extruder_id);
    this->placeholder_parser().set("current_extruder", initial_extruder_id);
    //Set variable for total layer count so it can be used in custom gcode.
    this->placeholder_parser().set("total_layer_count", m_layer_count);
    // Useful for sequential prints.
    this->placeholder_parser().set("current_object_idx", 0);
    // For the start / end G-code to do the priming and final filament pull in case there is no wipe tower provided.
    this->placeholder_parser().set("has_wipe_tower", has_wipe_tower);
    this->placeholder_parser().set("has_single_extruder_multi_material_priming", has_wipe_tower && print.config().single_extruder_multi_material_priming);
    this->placeholder_parser().set("total_toolchanges", tool_ordering.toolchanges_count());
    {
        BoundingBoxf bbox(print.config().bed_shape.values);
        assert(bbox.defined);
        if (! bbox.defined)
            // This should not happen, but let's make the compiler happy.
            bbox.min = bbox.max = Vec2d::Zero();
        this->placeholder_parser().set("print_bed_min",  new ConfigOptionFloats({ bbox.min.x(), bbox.min.y() }));
        this->placeholder_parser().set("print_bed_max",  new ConfigOptionFloats({ bbox.max.x(), bbox.max.y() }));
        this->placeholder_parser().set("print_bed_size", new ConfigOptionFloats({ bbox.size().x(), bbox.size().y() }));
    }
    {
        // Convex hull of the 1st layer extrusions, for bed leveling and placing the initial purge line.
        // It encompasses the object extrusions, support extrusions, skirt, brim, wipe tower.
        // It does NOT encompass user extrusions generated by custom G-code,
        // therefore it does NOT encompass the initial purge line.
        // It does NOT encompass MMU/MMU2 starting (wipe) areas.
        auto pts = std::make_unique<ConfigOptionPoints>();
        pts->values.reserve(print.first_layer_convex_hull().size());
        for (const Point &pt : print.first_layer_convex_hull().points)
            pts->values.emplace_back(unscale(pt));
        BoundingBoxf bbox(pts->values);
        this->placeholder_parser().set("first_layer_print_convex_hull", pts.release());
        this->placeholder_parser().set("first_layer_print_min",  new ConfigOptionFloats({ bbox.min.x(), bbox.min.y() }));
        this->placeholder_parser().set("first_layer_print_max",  new ConfigOptionFloats({ bbox.max.x(), bbox.max.y() }));
        this->placeholder_parser().set("first_layer_print_size", new ConfigOptionFloats({ bbox.size().x(), bbox.size().y() }));
        this->placeholder_parser().set("num_extruders", int(print.config().nozzle_diameter.values.size()));
        // PlaceholderParser currently substitues non-existent vector values with the zero'th value, which is harmful in the case of "is_extruder_used[]"
        // as Slicer may lie about availability of such non-existent extruder.
        // We rather sacrifice 256B of memory before we change the behavior of the PlaceholderParser, which should really only fill in the non-existent
        // vector elements for filament parameters.
        std::vector<unsigned char> is_extruder_used(std::max(size_t(255), print.config().nozzle_diameter.size()), 0);
        for (unsigned int extruder_id : tool_ordering.all_extruders())
            is_extruder_used[extruder_id] = true;
        this->placeholder_parser().set("is_extruder_used", new ConfigOptionBools(is_extruder_used));
    }

    // Enable ooze prevention if configured so.
    DoExport::init_ooze_prevention(print, m_ooze_prevention);

    std::string start_gcode = this->placeholder_parser_process("start_gcode", print.config().start_gcode.value, initial_extruder_id);
    // Set bed temperature if the start G-code does not contain any bed temp control G-codes.
    this->_print_first_layer_bed_temperature(file, print, start_gcode, initial_extruder_id, true);
    // Set extruder(s) temperature before and after start G-code.
    this->_print_first_layer_extruder_temperatures(file, print, start_gcode, initial_extruder_id, false);

    // adds tag for processor
    file.write_format(";%s%s\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Role).c_str(), gcode_extrusion_role_to_string(GCodeExtrusionRole::Custom).c_str());


    //B41
    if (this->config().gcode_flavor == gcfKlipper)
        file.write(set_object_range(print));
    else
        set_object_range(print);
    //B17
    // adds tags for time estimators
    if (print.config().remaining_times.value)
        file.write_format(";%s\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::First_Line_M73_Placeholder).c_str());
    // Write the custom start G-code
    file.writeln(start_gcode);

    this->_print_first_layer_extruder_temperatures(file, print, start_gcode, initial_extruder_id, true);
    print.throw_if_canceled();

    // Set other general things.
    file.write(this->preamble());

    print.throw_if_canceled();

    // Collect custom seam data from all objects.
    std::function<void(void)> throw_if_canceled_func = [&print]() { print.throw_if_canceled();};
    m_seam_placer.init(print, throw_if_canceled_func);

    if (! (has_wipe_tower && print.config().single_extruder_multi_material_priming)) {
        // Set initial extruder only after custom start G-code.
        // Ugly hack: Do not set the initial extruder if the extruder is primed using the MMU priming towers at the edge of the print bed.
        file.write(this->set_extruder(initial_extruder_id, 0.));
    }

    GCode::SmoothPathCache smooth_path_cache_global = smooth_path_interpolate_global(print);
    // Do all objects for each layer.
    if (print.config().complete_objects.value) {
        size_t finished_objects = 0;
        const PrintObject *prev_object = (*print_object_instance_sequential_active)->print_object;
        for (; print_object_instance_sequential_active != print_object_instances_ordering.end(); ++ print_object_instance_sequential_active) {
            const PrintObject &object = *(*print_object_instance_sequential_active)->print_object;
            if (&object != prev_object || tool_ordering.first_extruder() != final_extruder_id) {
                tool_ordering = ToolOrdering(object, final_extruder_id);
                unsigned int new_extruder_id = tool_ordering.first_extruder();
                if (new_extruder_id == (unsigned int)-1)
                    // Skip this object.
                    continue;
                initial_extruder_id = new_extruder_id;
                final_extruder_id   = tool_ordering.last_extruder();
                assert(final_extruder_id != (unsigned int)-1);
            }
            print.throw_if_canceled();
            this->set_origin(unscale((*print_object_instance_sequential_active)->shift));
            if (finished_objects > 0) {
                // Move to the origin position for the copy we're going to print.
                // This happens before Z goes down to layer 0 again, so that no collision happens hopefully.
                m_enable_cooling_markers = false; // we're not filtering these moves through CoolingBuffer
                m_avoid_crossing_perimeters.use_external_mp_once = true;
                file.write(this->retract_and_wipe());
                file.write(m_label_objects.maybe_stop_instance());
                const double last_z{this->writer().get_position().z()};
                file.write(this->writer().get_travel_to_z_gcode(last_z, "ensure z position"));
                file.write(this->travel_to(*this->last_position, Point(0, 0), ExtrusionRole::None, "move to origin position for next object", [](){return "";}));
                m_enable_cooling_markers = true;
                // Disable motion planner when traveling to first object point.
                m_avoid_crossing_perimeters.disable_once();
                // Ff we are printing the bottom layer of an object, and we have already finished
                // another one, set first layer temperatures. This happens before the Z move
                // is triggered, so machine has more time to reach such temperatures.
                this->placeholder_parser().set("current_object_idx", int(finished_objects));
                std::string between_objects_gcode = this->placeholder_parser_process("between_objects_gcode", print.config().between_objects_gcode.value, initial_extruder_id);
                // Set first layer bed and extruder temperatures, don't wait for it to reach the temperature.
                this->_print_first_layer_bed_temperature(file, print, between_objects_gcode, initial_extruder_id, false);
                this->_print_first_layer_extruder_temperatures(file, print, between_objects_gcode, initial_extruder_id, false);
                file.writeln(between_objects_gcode);
            }
            // Reset the cooling buffer internal state (the current position, feed rate, accelerations).
            m_cooling_buffer->reset(this->writer().get_position());
            m_cooling_buffer->set_current_extruder(initial_extruder_id);
            // Process all layers of a single object instance (sequential mode) with a parallel pipeline:
            // Generate G-code, run the filters (vase mode, cooling buffer), run the G-code analyser
            // and export G-code into file.
            this->process_layers(print, tool_ordering, collect_layers_to_print(object),
                *print_object_instance_sequential_active - object.instances().data(), 
                smooth_path_cache_global, file);
            ++ finished_objects;
            // Flag indicating whether the nozzle temperature changes from 1st to 2nd layer were performed.
            // Reset it when starting another object from 1st layer.
            m_second_layer_things_done = false;
            prev_object = &object;
        }
        file.write(m_label_objects.maybe_stop_instance());
    } else {
        // Sort layers by Z.
        // All extrusion moves with the same top layer height are extruded uninterrupted.
        std::vector<std::pair<coordf_t, ObjectsLayerToPrint>> layers_to_print = collect_layers_to_print(print);
        // QIDI Multi-Material wipe tower.
        if (has_wipe_tower && ! layers_to_print.empty()) {
            m_wipe_tower = std::make_unique<GCode::WipeTowerIntegration>(print.config(), *print.wipe_tower_data().priming.get(), print.wipe_tower_data().tool_changes, *print.wipe_tower_data().final_purge.get());
            // Set position for wipe tower generation.
            Vec3d new_position = this->writer().get_position();
            new_position.z() = first_layer_height;
            this->writer().update_position(new_position);
            if (print.config().single_extruder_multi_material_priming) {
                file.write(m_wipe_tower->prime(*this));
                // Verify, whether the print overaps the priming extrusions.
                BoundingBoxf bbox_print(get_print_extrusions_extents(print));
                coordf_t twolayers_printz = ((layers_to_print.size() == 1) ? layers_to_print.front() : layers_to_print[1]).first + EPSILON;
                for (const PrintObject *print_object : print.objects())
                    bbox_print.merge(get_print_object_extrusions_extents(*print_object, twolayers_printz));
                bbox_print.merge(get_wipe_tower_extrusions_extents(print, twolayers_printz));
                BoundingBoxf bbox_prime(get_wipe_tower_priming_extrusions_extents(print));
                bbox_prime.offset(0.5f);
                bool overlap = bbox_prime.overlap(bbox_print);

                if (print.config().gcode_flavor == gcfMarlinLegacy || print.config().gcode_flavor == gcfMarlinFirmware) {
                    file.write(this->retract_and_wipe());
                    file.write("M300 S800 P500\n"); // Beep for 500ms, tone 800Hz.
                    if (overlap) {
                        // Wait for the user to remove the priming extrusions.
                        file.write("M1 Remove priming towers and click button.\n");
                    } else {
                        // Just wait for a bit to let the user check, that the priming succeeded.
                        //TODO Add a message explaining what the printer is waiting for. This needs a firmware fix.
                        file.write("M1 S10\n");
                    }
                } else {
                    // This is not Marlin, M1 command is probably not supported.
                    // (See https://github.com/qidi3d/QIDISlicer/issues/5441.)
                    if (overlap) {
                        print.active_step_add_warning(PrintStateBase::WarningLevel::CRITICAL,
                            _u8L("Your print is very close to the priming regions. "
                              "Make sure there is no collision."));
                    } else {
                        // Just continue printing, no action necessary.
                    }

                }
            }
            print.throw_if_canceled();
        }
        // Process all layers of all objects (non-sequential mode) with a parallel pipeline:
        // Generate G-code, run the filters (vase mode, cooling buffer), run the G-code analyser
        // and export G-code into file.
        this->process_layers(print, tool_ordering, print_object_instances_ordering, layers_to_print, 
            smooth_path_cache_global, file);
        file.write(m_label_objects.maybe_stop_instance());
        if (m_wipe_tower)
            // Purge the extruder, pull out the active filament.
            file.write(m_wipe_tower->finalize(*this));
    }

    // Write end commands to file.
    file.write(this->retract_and_wipe());

    //B38 //B46
    {
        std::string gcode;
        m_writer.add_object_change_labels(gcode);
        file.write(gcode);
    }


    file.write(m_writer.set_fan(0));
    //B39
    file.write("M106 P3 S0\n");
    // adds tag for processor
    file.write_format(";%s%s\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Role).c_str(), gcode_extrusion_role_to_string(GCodeExtrusionRole::Custom).c_str());

    // Process filament-specific gcode in extruder order.
    {
        DynamicConfig config;
        config.set_key_value("layer_num", new ConfigOptionInt(m_layer_index));
        config.set_key_value("layer_z",   new ConfigOptionFloat(m_writer.get_position().z() - m_config.z_offset.value));
        config.set_key_value("max_layer_z", new ConfigOptionFloat(m_max_layer_z));
        if (print.config().single_extruder_multi_material) {
            // Process the end_filament_gcode for the active filament only.
            int extruder_id = m_writer.extruder()->id();
            config.set_key_value("filament_extruder_id", new ConfigOptionInt(extruder_id));
            file.writeln(this->placeholder_parser_process("end_filament_gcode", print.config().end_filament_gcode.get_at(extruder_id), extruder_id, &config));
        } else {
            for (const std::string &end_gcode : print.config().end_filament_gcode.values) {
                int extruder_id = (unsigned int)(&end_gcode - &print.config().end_filament_gcode.values.front());
                config.set_key_value("filament_extruder_id", new ConfigOptionInt(extruder_id));
                file.writeln(this->placeholder_parser_process("end_filament_gcode", end_gcode, extruder_id, &config));
            }
        }
        file.writeln(this->placeholder_parser_process("end_gcode", print.config().end_gcode, m_writer.extruder()->id(), &config));
    }
    file.write(m_writer.update_progress(m_layer_count, m_layer_count, true)); // 100%
    file.write(m_writer.postamble());

    // From now to the end of G-code, the G-code find / replace post-processor will be disabled.
    // Thus the QIDISlicer generated config will NOT be processed by the G-code post-processor, see GH issue #7952.
    file.find_replace_supress();

    // adds tags for time estimators
    if (print.config().remaining_times.value)
        file.write_format(";%s\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Last_Line_M73_Placeholder).c_str());

    print.throw_if_canceled();

    // Get filament stats.
    const std::string filament_stats_string_out = DoExport::update_print_stats_and_format_filament_stats(
    	// Const inputs
        has_wipe_tower, print.wipe_tower_data(),
        this->config(),
        m_writer.extruders(),
        initial_extruder_id,
        tool_ordering.toolchanges_count(),
        // Modifies
        print.m_print_statistics,
        export_to_binary_gcode,
        m_processor.get_binary_data()
    );

    if (!export_to_binary_gcode) {
        file.write_format("; objects_info = %s\n", m_label_objects.all_objects_header_singleline_json().c_str());
        file.write(filament_stats_string_out);
    }
    if (export_to_binary_gcode) {
        bgcode::binarize::BinaryData& binary_data = m_processor.get_binary_data();
        if (print.m_print_statistics.total_toolchanges > 0)
            binary_data.print_metadata.raw_data.emplace_back("total toolchanges", std::to_string(print.m_print_statistics.total_toolchanges));
        char buf[1024];
        sprintf(buf, "%.2lf", m_max_layer_z);
        binary_data.printer_metadata.raw_data.emplace_back("max_layer_z", buf);
        // Now the objects info.        
        binary_data.printer_metadata.raw_data.emplace_back("objects_info", m_label_objects.all_objects_header_singleline_json());
    }
    else {
        // if exporting gcode in ascii format, statistics export is done here
    file.write("\n");
        file.write_format(PrintStatistics::TotalFilamentUsedGValueMask.c_str(), print.m_print_statistics.total_weight);
        file.write_format(PrintStatistics::TotalFilamentCostValueMask.c_str(), print.m_print_statistics.total_cost);
        file.write_format(PrintStatistics::TotalFilamentUsedWipeTowerValueMask.c_str(), print.m_print_statistics.total_wipe_tower_filament_weight);
    if (print.m_print_statistics.total_toolchanges > 0)
    	file.write_format("; total toolchanges = %i\n", print.m_print_statistics.total_toolchanges);
    file.write_format(";%s\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Estimated_Printing_Time_Placeholder).c_str());

    //B3
    if (!export_to_binary_gcode) {
        // if exporting gcode in ascii format, generate the thumbnails here
        auto [thumbnails, errors] = GCodeThumbnails::make_and_check_thumbnail_list(print.full_print_config());
        if (errors != enum_bitmask<ThumbnailError>()) {
            std::string error_str = format("Invalid thumbnails value:");
            error_str += GCodeThumbnails::get_error_string(errors);
            throw Slic3r::ExportError(error_str);
        }
        if (!thumbnails.empty())
            GCodeThumbnails::export_qidi_thumbnails_to_file(
                thumbnail_cb, thumbnails, [&file](const char *sz) { file.write(sz); }, [&print]() { print.throw_if_canceled(); });
    }

    file.write("\n");

    // Append full config, delimited by two 'phony' configuration keys qidislicer_config = begin and qidislicer_config = end.
    // The delimiters are structured as configuration key / value pairs to be parsable by older versions of QIDISlicer G-code viewer.
      {
        file.write("\n; qidislicer_config = begin\n");
        std::string full_config;
            append_full_config(*m_print, full_config);
        if (!full_config.empty())
            file.write(full_config);
            file.write("; qidislicer_config = end\n");
        }

        if (std::optional<std::string> line_M84 = find_M84(print.config().end_gcode);
            is_mk2_or_mk3(print.config().printer_model) && line_M84.has_value()) {
            file.writeln(*line_M84);
      }
    }
    print.throw_if_canceled();
}

// Fill in cache of smooth paths for perimeters, fills and supports of the given object layers.
// Based on params, the paths are either decimated to sparser polylines, or interpolated with circular arches.
void GCodeGenerator::smooth_path_interpolate(
    const ObjectLayerToPrint                                &object_layer_to_print, 
    const GCode::SmoothPathCache::InterpolationParameters   &params, 
    GCode::SmoothPathCache                                  &out)
{
    if (const Layer *layer = object_layer_to_print.object_layer; layer) {
        for (const LayerRegion *layerm : layer->regions()) {
            out.interpolate_add(layerm->perimeters(), params);
            out.interpolate_add(layerm->fills(), params);
        }
    }
    if (const SupportLayer *layer = object_layer_to_print.support_layer; layer)
        out.interpolate_add(layer->support_fills, params);
}
// Process all layers of all objects (non-sequential mode) with a parallel pipeline:
// Generate G-code, run the filters (vase mode, cooling buffer), run the G-code analyser
// and export G-code into file.
void GCodeGenerator::process_layers(
    const Print                                                         &print,
    const ToolOrdering                                                  &tool_ordering,
    const std::vector<const PrintInstance*>                             &print_object_instances_ordering,
    const std::vector<std::pair<coordf_t, ObjectsLayerToPrint>>         &layers_to_print,
    const GCode::SmoothPathCache                                        &smooth_path_cache_global,
    GCodeOutputStream                                                   &output_stream)
{
    size_t layer_to_print_idx = 0;
    const GCode::SmoothPathCache::InterpolationParameters interpolation_params = interpolation_parameters(print.config());
    const auto smooth_path_interpolator = tbb::make_filter<void, std::pair<size_t, GCode::SmoothPathCache>>(slic3r_tbb_filtermode::serial_in_order,
        [this, &print, &layers_to_print, &layer_to_print_idx, &interpolation_params](tbb::flow_control &fc) -> std::pair<size_t, GCode::SmoothPathCache> {
            if (layer_to_print_idx >= layers_to_print.size()) {
                if (layer_to_print_idx == layers_to_print.size() + (m_pressure_equalizer ? 1 : 0)) {
                    fc.stop();
                    return {};
                } else {
                    // Pressure equalizer need insert empty input. Because it returns one layer back.
                    // Insert NOP (no operation) layer;
                    return { layer_to_print_idx ++, {} };
                }
            } else {
                print.throw_if_canceled();
                size_t idx = layer_to_print_idx ++;
                GCode::SmoothPathCache smooth_path_cache;
                for (const ObjectLayerToPrint &l : layers_to_print[idx].second)
                    GCodeGenerator::smooth_path_interpolate(l, interpolation_params, smooth_path_cache);
                return { idx, std::move(smooth_path_cache) };
            }
        });
    const auto generator = tbb::make_filter<std::pair<size_t, GCode::SmoothPathCache>, LayerResult>(slic3r_tbb_filtermode::serial_in_order,
        [this, &print, &tool_ordering, &print_object_instances_ordering, &layers_to_print, &smooth_path_cache_global](
            std::pair<size_t, GCode::SmoothPathCache> in) -> LayerResult {
            size_t layer_to_print_idx = in.first;
            if (layer_to_print_idx == layers_to_print.size()) {
                // Pressure equalizer need insert empty input. Because it returns one layer back.
                // Insert NOP (no operation) layer;
                return LayerResult::make_nop_layer_result();
            } else {
                const std::pair<coordf_t, ObjectsLayerToPrint> &layer = layers_to_print[layer_to_print_idx];
                const LayerTools& layer_tools = tool_ordering.tools_for_layer(layer.first);
                if (m_wipe_tower && layer_tools.has_wipe_tower)
                    m_wipe_tower->next_layer();
                print.throw_if_canceled();
                return this->process_layer(print, layer.second, layer_tools, 
                    GCode::SmoothPathCaches{ smooth_path_cache_global, in.second }, 
                    &layer == &layers_to_print.back(), &print_object_instances_ordering, size_t(-1));
            }
        });
    // The pipeline is variable: The vase mode filter is optional.
    const auto spiral_vase = tbb::make_filter<LayerResult, LayerResult>(slic3r_tbb_filtermode::serial_in_order,
        [spiral_vase = this->m_spiral_vase.get(), &layers_to_print](LayerResult in) -> LayerResult {
            if (in.nop_layer_result)
                return in;

            spiral_vase->enable(in.spiral_vase_enable);
            bool last_layer = in.layer_id == layers_to_print.size() - 1;
            return { spiral_vase->process_layer(std::move(in.gcode), last_layer), in.layer_id, in.spiral_vase_enable, in.cooling_buffer_flush};
        });
    const auto pressure_equalizer = tbb::make_filter<LayerResult, LayerResult>(slic3r_tbb_filtermode::serial_in_order,
        [pressure_equalizer = this->m_pressure_equalizer.get()](LayerResult in) -> LayerResult {
            return pressure_equalizer->process_layer(std::move(in));
        });
    const auto cooling = tbb::make_filter<LayerResult, std::string>(slic3r_tbb_filtermode::serial_in_order,
        [cooling_buffer = this->m_cooling_buffer.get()](LayerResult in) -> std::string {
             if (in.nop_layer_result)
                return in.gcode;

             return cooling_buffer->process_layer(std::move(in.gcode), in.layer_id, in.cooling_buffer_flush);
        });
    const auto find_replace = tbb::make_filter<std::string, std::string>(slic3r_tbb_filtermode::serial_in_order,
        [find_replace = this->m_find_replace.get()](std::string s) -> std::string {
            return find_replace->process_layer(std::move(s));
        });
    const auto output = tbb::make_filter<std::string, void>(slic3r_tbb_filtermode::serial_in_order,
        [&output_stream](std::string s) { output_stream.write(s); }
    );

    tbb::filter<void, LayerResult> pipeline_to_layerresult = smooth_path_interpolator & generator;
    if (m_spiral_vase)
        pipeline_to_layerresult = pipeline_to_layerresult & spiral_vase;
    if (m_pressure_equalizer)
        pipeline_to_layerresult = pipeline_to_layerresult & pressure_equalizer;

    tbb::filter<LayerResult, std::string> pipeline_to_string = cooling;
    if (m_find_replace)
        pipeline_to_string = pipeline_to_string & find_replace;
    // It registers a handler that sets locales to "C" before any TBB thread starts participating in tbb::parallel_pipeline.
    // Handler is unregistered when the destructor is called.
    TBBLocalesSetter locales_setter;

    // The pipeline elements are joined using const references, thus no copying is performed.
    output_stream.find_replace_supress();
    tbb::parallel_pipeline(12, pipeline_to_layerresult & pipeline_to_string & output);
    output_stream.find_replace_enable();
}

// Process all layers of a single object instance (sequential mode) with a parallel pipeline:
// Generate G-code, run the filters (vase mode, cooling buffer), run the G-code analyser
// and export G-code into file.
void GCodeGenerator::process_layers(
    const Print                             &print,
    const ToolOrdering                      &tool_ordering,
    ObjectsLayerToPrint                      layers_to_print,
    const size_t                             single_object_idx,
    const GCode::SmoothPathCache            &smooth_path_cache_global,
    GCodeOutputStream                       &output_stream)
{
    size_t layer_to_print_idx = 0;
    const GCode::SmoothPathCache::InterpolationParameters interpolation_params = interpolation_parameters(print.config());
    const auto smooth_path_interpolator = tbb::make_filter<void, std::pair<size_t, GCode::SmoothPathCache>> (slic3r_tbb_filtermode::serial_in_order,
        [this, &print, &layers_to_print, &layer_to_print_idx, interpolation_params](tbb::flow_control &fc) -> std::pair<size_t, GCode::SmoothPathCache> {
            if (layer_to_print_idx >= layers_to_print.size()) {
                if (layer_to_print_idx == layers_to_print.size() + (m_pressure_equalizer ? 1 : 0)) {
                    fc.stop();
                    return {};
                } else {
                    // Pressure equalizer need insert empty input. Because it returns one layer back.
                    // Insert NOP (no operation) layer;
                    return { layer_to_print_idx ++, {} };
                }
            } else {
                print.throw_if_canceled();
                size_t idx = layer_to_print_idx ++;
                GCode::SmoothPathCache smooth_path_cache;
                GCodeGenerator::smooth_path_interpolate(layers_to_print[idx], interpolation_params, smooth_path_cache);
                return { idx, std::move(smooth_path_cache) };
            }
        });
    const auto generator = tbb::make_filter<std::pair<size_t, GCode::SmoothPathCache>, LayerResult>(slic3r_tbb_filtermode::serial_in_order,
        [this, &print, &tool_ordering, &layers_to_print, &smooth_path_cache_global, single_object_idx](std::pair<size_t, GCode::SmoothPathCache> in) -> LayerResult {
            size_t layer_to_print_idx = in.first;
            if (layer_to_print_idx == layers_to_print.size()) {
                // Pressure equalizer need insert empty input. Because it returns one layer back.
                // Insert NOP (no operation) layer;
                return LayerResult::make_nop_layer_result();
            } else {
                ObjectLayerToPrint &layer = layers_to_print[layer_to_print_idx];
                print.throw_if_canceled();
                return this->process_layer(print, { std::move(layer) }, tool_ordering.tools_for_layer(layer.print_z()), 
                    GCode::SmoothPathCaches{ smooth_path_cache_global, in.second }, 
                    &layer == &layers_to_print.back(), nullptr, single_object_idx);
            }
        });
    // The pipeline is variable: The vase mode filter is optional.
    const auto spiral_vase = tbb::make_filter<LayerResult, LayerResult>(slic3r_tbb_filtermode::serial_in_order,
        [spiral_vase = this->m_spiral_vase.get(), &layers_to_print](LayerResult in)->LayerResult {
            if (in.nop_layer_result)
                return in;
            spiral_vase->enable(in.spiral_vase_enable);
            bool last_layer = in.layer_id == layers_to_print.size() - 1;
            return { spiral_vase->process_layer(std::move(in.gcode), last_layer), in.layer_id, in.spiral_vase_enable, in.cooling_buffer_flush };
        });
    const auto pressure_equalizer = tbb::make_filter<LayerResult, LayerResult>(slic3r_tbb_filtermode::serial_in_order,
        [pressure_equalizer = this->m_pressure_equalizer.get()](LayerResult in) -> LayerResult {
             return pressure_equalizer->process_layer(std::move(in));
        });
    const auto cooling = tbb::make_filter<LayerResult, std::string>(slic3r_tbb_filtermode::serial_in_order,
        [cooling_buffer = this->m_cooling_buffer.get()](LayerResult in)->std::string {
            if (in.nop_layer_result)
                return in.gcode;
            return cooling_buffer->process_layer(std::move(in.gcode), in.layer_id, in.cooling_buffer_flush);
        });
    const auto find_replace = tbb::make_filter<std::string, std::string>(slic3r_tbb_filtermode::serial_in_order,
        [find_replace = this->m_find_replace.get()](std::string s) -> std::string {
            return find_replace->process_layer(std::move(s));
        });
    const auto output = tbb::make_filter<std::string, void>(slic3r_tbb_filtermode::serial_in_order,
        [&output_stream](std::string s) { output_stream.write(s); }
    );

    tbb::filter<void, LayerResult> pipeline_to_layerresult = smooth_path_interpolator & generator;
    if (m_spiral_vase)
        pipeline_to_layerresult = pipeline_to_layerresult & spiral_vase;
    if (m_pressure_equalizer)
        pipeline_to_layerresult = pipeline_to_layerresult & pressure_equalizer;

    tbb::filter<LayerResult, std::string> pipeline_to_string = cooling;
    if (m_find_replace)
        pipeline_to_string = pipeline_to_string & find_replace;
    // It registers a handler that sets locales to "C" before any TBB thread starts participating in tbb::parallel_pipeline.
    // Handler is unregistered when the destructor is called.
    TBBLocalesSetter locales_setter;

    // The pipeline elements are joined using const references, thus no copying is performed.
    output_stream.find_replace_supress();
    tbb::parallel_pipeline(12, pipeline_to_layerresult & pipeline_to_string & output);
    output_stream.find_replace_enable();
}

std::string GCodeGenerator::placeholder_parser_process(
    const std::string   &name,
    const std::string   &templ,
    unsigned int         current_extruder_id,
    const DynamicConfig *config_override)
{
#ifndef NDEBUG // CHECK_CUSTOM_GCODE_PLACEHOLDERS
    if (config_override) {
        const auto& custom_gcode_placeholders = custom_gcode_specific_placeholders();

        // 1-st check: custom G-code "name" have to be present in s_CustomGcodeSpecificOptions;
        //if (custom_gcode_placeholders.count(name) > 0) {
        //    const auto& placeholders = custom_gcode_placeholders.at(name);
        if (auto it = custom_gcode_placeholders.find(name); it != custom_gcode_placeholders.end()) {
            const auto& placeholders = it->second;

            for (const std::string& key : config_override->keys()) {
                // 2-nd check: "key" have to be present in s_CustomGcodeSpecificOptions for "name" custom G-code ;
                if (std::find(placeholders.begin(), placeholders.end(), key) == placeholders.end())
                    throw Slic3r::PlaceholderParserError(format("\"%s\" placeholder for \"%s\" custom G-code \n"
                                                                "needs to be added to s_CustomGcodeSpecificOptions", key.c_str(), name.c_str()));
                // 3-rd check: "key" have to be present in CustomGcodeSpecificConfigDef for "key" placeholder;
                if (!custom_gcode_specific_config_def.has(key))
                    throw Slic3r::PlaceholderParserError(format("Definition of \"%s\" placeholder \n"
                                                                "needs to be added to CustomGcodeSpecificConfigDef", key.c_str()));
            }
        }
        else
            throw Slic3r::PlaceholderParserError(format("\"%s\" custom G-code needs to be added to s_CustomGcodeSpecificOptions", name.c_str()));
    }
#endif
    PlaceholderParserIntegration &ppi = m_placeholder_parser_integration;
    try {
        ppi.update_from_gcodewriter(m_writer, m_print->wipe_tower_data());
        std::string output = ppi.parser.process(templ, current_extruder_id, config_override, &ppi.output_config, &ppi.context);
        ppi.validate_output_vector_variables();

        if (const std::vector<double> &pos = ppi.opt_position->values; ppi.position != pos) {
            // Update G-code writer.
            m_writer.update_position({ pos[0], pos[1], pos[2] });
            this->last_position = this->gcode_to_point({ pos[0], pos[1] });
        }

        for (const Extruder &e : m_writer.extruders()) {
            unsigned int eid = e.id();
            assert(eid < ppi.num_extruders);
            if ( eid < ppi.num_extruders) {
                if (! m_writer.config.use_relative_e_distances && ! is_approx(ppi.e_position[eid], ppi.opt_e_position->values[eid]))
                    const_cast<Extruder&>(e).set_position(ppi.opt_e_position->values[eid]);
                if (! is_approx(ppi.e_retracted[eid], ppi.opt_e_retracted->values[eid]) || 
                    ! is_approx(ppi.e_restart_extra[eid], ppi.opt_e_restart_extra->values[eid]))
                    const_cast<Extruder&>(e).set_retracted(ppi.opt_e_retracted->values[eid], ppi.opt_e_restart_extra->values[eid]);
            }
        }

        return output;
    } 
    catch (std::runtime_error &err) 
    {
        // Collect the names of failed template substitutions for error reporting.
        auto it = ppi.failed_templates.find(name);
        if (it == ppi.failed_templates.end())
            // Only if there was no error reported for this template, store the first error message into the map to be reported.
            // We don't want to collect error message for each and every occurence of a single custom G-code section.
            ppi.failed_templates.insert(it, std::make_pair(name, std::string(err.what())));
        // Insert the macro error message into the G-code.
        return
            std::string("\n!!!!! Failed to process the custom G-code template ") + name + "\n" +
            err.what() +
            "!!!!! End of an error report for the custom G-code template " + name + "\n\n";
    }
}

// Parse the custom G-code, try to find mcode_set_temp_dont_wait and mcode_set_temp_and_wait or optionally G10 with temperature inside the custom G-code.
// Returns true if one of the temp commands are found, and try to parse the target temperature value into temp_out.
static bool custom_gcode_sets_temperature(const std::string &gcode, const int mcode_set_temp_dont_wait, const int mcode_set_temp_and_wait, const bool include_g10, int &temp_out)
{
    temp_out = -1;
    if (gcode.empty())
        return false;

    const char *ptr = gcode.data();
    bool temp_set_by_gcode = false;
    while (*ptr != 0) {
        // Skip whitespaces.
        for (; *ptr == ' ' || *ptr == '\t'; ++ ptr);
        if (*ptr == 'M' || // Line starts with 'M'. It is a machine command.
            (*ptr == 'G' && include_g10)) { // Only check for G10 if requested
            bool is_gcode = *ptr == 'G';
            ++ ptr;
            // Parse the M or G code value.
            char *endptr = nullptr;
            int mgcode = int(strtol(ptr, &endptr, 10));
            if (endptr != nullptr && endptr != ptr && 
                is_gcode ?
                    // G10 found
                    mgcode == 10 :
                    // M104/M109 or M140/M190 found.
                    (mgcode == mcode_set_temp_dont_wait || mgcode == mcode_set_temp_and_wait)) {
                ptr = endptr;
                if (! is_gcode)
                    // Let the caller know that the custom M-code sets the temperature.
                    temp_set_by_gcode = true;
                // Now try to parse the temperature value.
                // While not at the end of the line:
                while (strchr(";\r\n\0", *ptr) == nullptr) {
                    // Skip whitespaces.
                    for (; *ptr == ' ' || *ptr == '\t'; ++ ptr);
                    if (*ptr == 'S') {
                        // Skip whitespaces.
                        for (++ ptr; *ptr == ' ' || *ptr == '\t'; ++ ptr);
                        // Parse an int.
                        endptr = nullptr;
                        long temp_parsed = strtol(ptr, &endptr, 10);
                        if (endptr > ptr) {
                            ptr = endptr;
                            temp_out = temp_parsed;
                            // Let the caller know that the custom G-code sets the temperature
                            // Only do this after successfully parsing temperature since G10
                            // can be used for other reasons
                            temp_set_by_gcode = true;
                        }
                    } else {
                        // Skip this word.
                        for (; strchr(" \t;\r\n\0", *ptr) == nullptr; ++ ptr);
                    }
                }
            }
        }
        // Skip the rest of the line.
        for (; *ptr != 0 && *ptr != '\r' && *ptr != '\n'; ++ ptr);
        // Skip the end of line indicators.
        for (; *ptr == '\r' || *ptr == '\n'; ++ ptr);
    }
    return temp_set_by_gcode;
}

// Print the machine envelope G-code for the Marlin firmware based on the "machine_max_xxx" parameters.
// Do not process this piece of G-code by the time estimator, it already knows the values through another sources.
void GCodeGenerator::print_machine_envelope(GCodeOutputStream &file, const Print &print)
{
    const GCodeFlavor flavor = print.config().gcode_flavor.value;
    if ( (flavor == gcfMarlinLegacy || flavor == gcfMarlinFirmware || flavor == gcfRepRapFirmware)
     && print.config().machine_limits_usage.value == MachineLimitsUsage::EmitToGCode) {
        int factor = flavor == gcfRepRapFirmware ? 60 : 1; // RRF M203 and M566 are in mm/min
        file.write_format("M201 X%d Y%d Z%d E%d ; sets maximum accelerations, mm/sec^2\n",
            int(print.config().machine_max_acceleration_x.values.front() + 0.5),
            int(print.config().machine_max_acceleration_y.values.front() + 0.5),
            int(print.config().machine_max_acceleration_z.values.front() + 0.5),
            int(print.config().machine_max_acceleration_e.values.front() + 0.5));
        file.write_format("M203 X%d Y%d Z%d E%d ; sets maximum feedrates, %s\n",
            int(print.config().machine_max_feedrate_x.values.front() * factor + 0.5),
            int(print.config().machine_max_feedrate_y.values.front() * factor + 0.5),
            int(print.config().machine_max_feedrate_z.values.front() * factor + 0.5),
            int(print.config().machine_max_feedrate_e.values.front() * factor + 0.5),
            factor == 60 ? "mm / min" : "mm / sec");

        // Now M204 - acceleration. This one is quite hairy...
        if (flavor == gcfRepRapFirmware)
            // Uses M204 P[print] T[travel]
            file.write_format("M204 P%d T%d ; sets acceleration (P, T), mm/sec^2\n",
                int(print.config().machine_max_acceleration_extruding.values.front() + 0.5),
                int(print.config().machine_max_acceleration_travel.values.front() + 0.5));
        else if (flavor == gcfMarlinLegacy)
            // Legacy Marlin uses M204 S[print] T[retract]
            file.write_format("M204 S%d T%d ; sets acceleration (S) and retract acceleration (R), mm/sec^2\n",
                int(print.config().machine_max_acceleration_extruding.values.front() + 0.5),
                int(print.config().machine_max_acceleration_retracting.values.front() + 0.5));
        else if (flavor == gcfMarlinFirmware)
            // New Marlin uses M204 P[print] R[retract] T[travel]
            file.write_format("M204 P%d R%d T%d ; sets acceleration (P, T) and retract acceleration (R), mm/sec^2\n",
                int(print.config().machine_max_acceleration_extruding.values.front() + 0.5),
                int(print.config().machine_max_acceleration_retracting.values.front() + 0.5),
                int(print.config().machine_max_acceleration_travel.values.front() + 0.5));
        else
            assert(false);

        assert(is_decimal_separator_point());
        file.write_format(flavor == gcfRepRapFirmware
            ? "M566 X%.2lf Y%.2lf Z%.2lf E%.2lf ; sets the jerk limits, mm/min\n"
            : "M205 X%.2lf Y%.2lf Z%.2lf E%.2lf ; sets the jerk limits, mm/sec\n",
            print.config().machine_max_jerk_x.values.front() * factor,
            print.config().machine_max_jerk_y.values.front() * factor,
            print.config().machine_max_jerk_z.values.front() * factor,
            print.config().machine_max_jerk_e.values.front() * factor);
        if (flavor != gcfRepRapFirmware)
            file.write_format("M205 S%d T%d ; sets the minimum extruding and travel feed rate, mm/sec\n",
                int(print.config().machine_min_extruding_rate.values.front() + 0.5),
                int(print.config().machine_min_travel_rate.values.front() + 0.5));
        else {
            // M205 Sn Tn not supported in RRF. They use M203 Inn to set minimum feedrate for
            // all moves. This is currently not implemented.
        }
    }
}

// Write 1st layer bed temperatures into the G-code.
// Only do that if the start G-code does not already contain any M-code controlling an extruder temperature.
// M140 - Set Extruder Temperature
// M190 - Set Extruder Temperature and Wait
void GCodeGenerator::_print_first_layer_bed_temperature(GCodeOutputStream &file, const Print &print, const std::string &gcode, unsigned int first_printing_extruder_id, bool wait)
{
    bool autoemit = print.config().autoemit_temperature_commands;
    // Initial bed temperature based on the first extruder.
    int  temp = print.config().first_layer_bed_temperature.get_at(first_printing_extruder_id);
    // Is the bed temperature set by the provided custom G-code?
    int  temp_by_gcode     = -1;
    bool temp_set_by_gcode = custom_gcode_sets_temperature(gcode, 140, 190, false, temp_by_gcode);
    if (autoemit && temp_set_by_gcode && temp_by_gcode >= 0 && temp_by_gcode < 1000)
        temp = temp_by_gcode;
    // Always call m_writer.set_bed_temperature() so it will set the internal "current" state of the bed temp as if
    // the custom start G-code emited these.
    std::string set_temp_gcode = m_writer.set_bed_temperature(temp, wait);
    if (autoemit && ! temp_set_by_gcode)
        file.write(set_temp_gcode);
}

// Write 1st layer extruder temperatures into the G-code.
// Only do that if the start G-code does not already contain any M-code controlling an extruder temperature.
// M104 - Set Extruder Temperature
// M109 - Set Extruder Temperature and Wait
// RepRapFirmware: G10 Sxx
void GCodeGenerator::_print_first_layer_extruder_temperatures(GCodeOutputStream &file, const Print &print, const std::string &gcode, unsigned int first_printing_extruder_id, bool wait)
{
    bool autoemit = print.config().autoemit_temperature_commands;
    // Is the bed temperature set by the provided custom G-code?
    int  temp_by_gcode = -1;
    bool include_g10   = print.config().gcode_flavor == gcfRepRapFirmware;
    if (! autoemit  || custom_gcode_sets_temperature(gcode, 104, 109, include_g10, temp_by_gcode)) {
        // Set the extruder temperature at m_writer, but throw away the generated G-code as it will be written with the custom G-code.
        int temp = print.config().first_layer_temperature.get_at(first_printing_extruder_id);
        if (autoemit && temp_by_gcode >= 0 && temp_by_gcode < 1000)
            temp = temp_by_gcode;
        m_writer.set_temperature(temp, wait, first_printing_extruder_id);
    } else {
        // Custom G-code does not set the extruder temperature. Do it now.
        if (print.config().single_extruder_multi_material.value) {
            // Set temperature of the first printing extruder only.
            int temp = print.config().first_layer_temperature.get_at(first_printing_extruder_id);
            if (temp > 0)
                file.write(m_writer.set_temperature(temp, wait, first_printing_extruder_id));
        } else {
            // Set temperatures of all the printing extruders.
            for (unsigned int tool_id : print.extruders()) {
                int temp = print.config().first_layer_temperature.get_at(tool_id);

                if (print.config().ooze_prevention.value && tool_id != first_printing_extruder_id) {
                    if (print.config().idle_temperature.is_nil(tool_id))
                        temp += print.config().standby_temperature_delta.value;
                    else
                        temp = print.config().idle_temperature.get_at(tool_id);
                }

                if (temp > 0)
                    file.write(m_writer.set_temperature(temp, wait, tool_id));
            }
        }
    }
}

std::vector<GCodeGenerator::InstanceToPrint> GCodeGenerator::sort_print_object_instances(
    const std::vector<ObjectLayerToPrint>       &object_layers,
    // Ordering must be defined for normal (non-sequential print).
    const std::vector<const PrintInstance*>     *ordering,
    // For sequential print, the instance of the object to be printing has to be defined.
    const size_t                                 single_object_instance_idx)
{
    std::vector<InstanceToPrint> out;

    if (ordering == nullptr) {
        // Sequential print, single object is being printed.
        assert(object_layers.size() == 1);
        out.emplace_back(0, *object_layers.front().object(), single_object_instance_idx);
    } else {
        // Create mapping from PrintObject* to ObjectLayerToPrint ID.
        std::vector<std::pair<const PrintObject*, size_t>> sorted;
        sorted.reserve(object_layers.size());
        for (const ObjectLayerToPrint &object : object_layers)
            if (const PrintObject* print_object = object.object(); print_object)
                sorted.emplace_back(print_object, &object - object_layers.data());
        std::sort(sorted.begin(), sorted.end());

        if (! sorted.empty()) {
            out.reserve(sorted.size());
            for (const PrintInstance *instance : *ordering) {
                const PrintObject &print_object = *instance->print_object;
                std::pair<const PrintObject*, size_t> key(&print_object, 0);
                auto it = std::lower_bound(sorted.begin(), sorted.end(), key);
                if (it != sorted.end() && it->first == &print_object)
                    // ObjectLayerToPrint for this PrintObject was found.
                    out.emplace_back(it->second, print_object, instance - print_object.instances().data());
            }
        }
    }
    return out;
}

namespace ProcessLayer
{

static std::string emit_custom_color_change_gcode_per_print_z(
        GCodeGenerator                                          &gcodegen,
    const CustomGCode::Item &custom_gcode,
        unsigned int                                             current_extruder_id,
    unsigned int             first_extruder_id, // ID of the first extruder printing this layer.
    const PrintConfig       &config
) {
    const bool single_extruder_multi_material = config.single_extruder_multi_material;
    const bool single_extruder_printer        = config.nozzle_diameter.size() == 1;
    const bool color_change                   = custom_gcode.type == CustomGCode::ColorChange;

    std::string gcode;

    int color_change_extruder = -1;
    if (color_change && custom_gcode.extruder > 0)
        color_change_extruder = custom_gcode.extruder - 1;

    assert(color_change_extruder >= 0);
		        // Color Change or Tool Change as Color Change.
                // add tag for processor
    gcode += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Color_Change) + ",T" + std::to_string(color_change_extruder) + "," + custom_gcode.color + "\n";

    DynamicConfig cfg;
    cfg.set_key_value("color_change_extruder", new ConfigOptionInt(color_change_extruder));
    if (single_extruder_multi_material && !single_extruder_printer && color_change_extruder >= 0 && first_extruder_id != unsigned(color_change_extruder)) {
                    //! FIXME_in_fw show message during print pause
        // FIXME: Why is pause_print_gcode here? Why is it supplied "color_change_extruder"?
                    gcode += gcodegen.placeholder_parser_process("pause_print_gcode", config.pause_print_gcode, current_extruder_id, &cfg);
                    gcode += "\n";
        gcode += "M117 Change filament for Extruder " + std::to_string(color_change_extruder) + "\n";
    } else {
        gcode += gcodegen.placeholder_parser_process("color_change_gcode", config.color_change_gcode, current_extruder_id, &cfg);
                    gcode += "\n";
                    //FIXME Tell G-code writer that M600 filled the extruder, thus the G-code writer shall reset the extruder to unretracted state after
                    // return from M600. Thus the G-code generated by the following line is ignored.
                    // see GH issue #6362
                    gcodegen.writer().unretract();
                }
    return gcode;
	        } 
    static std::string emit_custom_gcode_per_print_z(
        GCodeGenerator                                          &gcodegen,
        const CustomGCode::Item 								&custom_gcode,
        unsigned int                                             current_extruder_id,
        // ID of the first extruder printing this layer.
        unsigned int                                             first_extruder_id,
        const PrintConfig                                       &config)
	            {
        std::string gcode;

        // Extruder switches are processed by LayerTools, they should be filtered out.
        assert(custom_gcode.type != CustomGCode::ToolChange);

        CustomGCode::Type gcode_type   = custom_gcode.type;
        const bool        color_change = gcode_type == CustomGCode::ColorChange;
        const bool        tool_change  = gcode_type == CustomGCode::ToolChange;
        // Tool Change is applied as Color Change for a single extruder printer only.
        assert(!tool_change || config.nozzle_diameter.size() == 1);

        // we should add or not colorprint_change in respect to nozzle_diameter count instead of really used extruders count
        if (color_change || tool_change) {
            gcode += emit_custom_color_change_gcode_per_print_z(gcodegen, custom_gcode, current_extruder_id, first_extruder_id, config);
        } else {
            if (gcode_type == CustomGCode::PausePrint) { // Pause print
                const std::string pause_print_msg = custom_gcode.extra;
                    // add tag for processor
                    gcode += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Pause_Print) + "\n";
                    //! FIXME_in_fw show message during print pause
	                if (!pause_print_msg.empty())
	                    gcode += "M117 " + pause_print_msg + "\n";
                DynamicConfig cfg;
                cfg.set_key_value("color_change_extruder", new ConfigOptionInt(int(current_extruder_id)));
                gcode += gcodegen.placeholder_parser_process("pause_print_gcode", config.pause_print_gcode, current_extruder_id, &cfg);
            } else {
                    // add tag for processor
                    gcode += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Custom_Code) + "\n";
                    if (gcode_type == CustomGCode::Template)    // Template Custom Gcode
                        gcode += gcodegen.placeholder_parser_process("template_custom_gcode", config.template_custom_gcode, current_extruder_id);
                    else                                        // custom Gcode
                    gcode += custom_gcode.extra;

                }
                gcode += "\n";
            }

        return gcode;
    }
} // namespace ProcessLayer

namespace Skirt {
    static void skirt_loops_per_extruder_all_printing(const Print &print, const LayerTools &layer_tools, std::map<unsigned int, std::pair<size_t, size_t>> &skirt_loops_per_extruder_out)
    {
        // Prime all extruders printing over the 1st layer over the skirt lines.
        size_t n_loops = print.skirt().entities.size();
        size_t n_tools = layer_tools.extruders.size();
        size_t lines_per_extruder = (n_loops + n_tools - 1) / n_tools;
        for (size_t i = 0; i < n_loops; i += lines_per_extruder)
            skirt_loops_per_extruder_out[layer_tools.extruders[i / lines_per_extruder]] = std::pair<size_t, size_t>(i, std::min(i + lines_per_extruder, n_loops));
    }

    static std::map<unsigned int, std::pair<size_t, size_t>> make_skirt_loops_per_extruder_1st_layer(
        const Print             				&print,
        const LayerTools                		&layer_tools,
        // Heights (print_z) at which the skirt has already been extruded.
        std::vector<coordf_t>  			    	&skirt_done)
    {
        // Extrude skirt at the print_z of the raft layers and normal object layers
        // not at the print_z of the interlaced support material layers.
        std::map<unsigned int, std::pair<size_t, size_t>> skirt_loops_per_extruder_out;
        //For sequential print, the following test may fail when extruding the 2nd and other objects.
        // assert(skirt_done.empty());
        if (skirt_done.empty() && print.has_skirt() && ! print.skirt().entities.empty() && layer_tools.has_skirt) {
            skirt_loops_per_extruder_all_printing(print, layer_tools, skirt_loops_per_extruder_out);
            skirt_done.emplace_back(layer_tools.print_z);
        }
        return skirt_loops_per_extruder_out;
    }

    static std::map<unsigned int, std::pair<size_t, size_t>> make_skirt_loops_per_extruder_other_layers(
        const Print 							&print,
        const LayerTools                		&layer_tools,
        // Heights (print_z) at which the skirt has already been extruded.
        std::vector<coordf_t>			    	&skirt_done)
    {
        // Extrude skirt at the print_z of the raft layers and normal object layers
        // not at the print_z of the interlaced support material layers.
        std::map<unsigned int, std::pair<size_t, size_t>> skirt_loops_per_extruder_out;
        if (print.has_skirt() && ! print.skirt().entities.empty() && layer_tools.has_skirt &&
            // Not enough skirt layers printed yet.
            //FIXME infinite or high skirt does not make sense for sequential print!
            (skirt_done.size() < (size_t)print.config().skirt_height.value || print.has_infinite_skirt())) {
            bool valid = ! skirt_done.empty() && skirt_done.back() < layer_tools.print_z - EPSILON;
            assert(valid);
            // This print_z has not been extruded yet (sequential print)
            // FIXME: The skirt_done should not be empty at this point. The check is a workaround
            // of https://github.com/qidi3d/QIDISlicer/issues/5652, but it deserves a real fix.
            if (valid) {
#if 0
                // Prime just the first printing extruder. This is original Slic3r's implementation.
                skirt_loops_per_extruder_out[layer_tools.extruders.front()] = std::pair<size_t, size_t>(0, print.config().skirts.value);
#else
                // Prime all extruders planned for this layer, see
                // https://github.com/qidi3d/QIDISlicer/issues/469#issuecomment-322450619
                skirt_loops_per_extruder_all_printing(print, layer_tools, skirt_loops_per_extruder_out);
#endif
                assert(!skirt_done.empty());
                skirt_done.emplace_back(layer_tools.print_z);
            }
        }
        return skirt_loops_per_extruder_out;
    }

} // namespace Skirt

bool GCodeGenerator::line_distancer_is_required(const std::vector<unsigned int>& extruder_ids) {
    for (const unsigned id : extruder_ids) {
        const double travel_slope{this->m_config.travel_slope.get_at(id)};
        if (
            this->m_config.travel_lift_before_obstacle.get_at(id)
            && this->m_config.travel_max_lift.get_at(id) > 0
            && travel_slope > 0
            && travel_slope < 90
        ) {
            return true;
        }
    }
    return false;
}
Polyline GCodeGenerator::get_layer_change_xy_path(const Vec3d &from, const Vec3d &to) {

    bool could_be_wipe_disabled{false};
    const bool needs_retraction{true};

    const Point saved_last_position{*this->last_position};
    const bool saved_use_external_mp{this->m_avoid_crossing_perimeters.use_external_mp_once};
    const Vec2d saved_origin{this->origin()};
    const Layer* saved_layer{this->layer()};

    this->m_avoid_crossing_perimeters.use_external_mp_once = m_layer_change_used_external_mp;
    if (this->m_layer_change_origin) {
        this->m_origin = *this->m_layer_change_origin;
    }
    this->m_layer = m_layer_change_layer;
    this->m_avoid_crossing_perimeters.init_layer(*this->m_layer);

    const Point start_point{this->gcode_to_point(from.head<2>())};
    const Point end_point{this->gcode_to_point(to.head<2>())};
    this->last_position = start_point;

    Polyline xy_path{
        this->generate_travel_xy_path(start_point, end_point, needs_retraction, could_be_wipe_disabled)};
    std::vector<Vec2d> gcode_xy_path;
    gcode_xy_path.reserve(xy_path.size());
    for (const Point &point : xy_path.points) {
        gcode_xy_path.push_back(this->point_to_gcode(point));
    }

    this->last_position = saved_last_position;
    this->m_avoid_crossing_perimeters.use_external_mp_once = saved_use_external_mp;
    this->m_origin = saved_origin;
    this->m_layer = saved_layer;

    Polyline result;
    for (const Vec2d& point : gcode_xy_path) {
        result.points.push_back(gcode_to_point(point));
    }

    return result;
}

GCode::Impl::Travels::ElevatedTravelParams get_ramping_layer_change_params(
    const Vec3d &from,
    const Vec3d &to,
    const Polyline &xy_path,
    const FullPrintConfig &config,
    const unsigned extruder_id,
    const GCode::TravelObstacleTracker &obstacle_tracker
) {
    using namespace GCode::Impl::Travels;

    ElevatedTravelParams elevation_params{
        get_elevated_traval_params(xy_path, config, extruder_id, obstacle_tracker)};

    const double z_change = to.z() - from.z();
    elevation_params.lift_height = std::max(z_change, elevation_params.lift_height);

    const double path_length = unscaled(xy_path.length());
    const double lift_at_travel_end = std::min(
        elevation_params.lift_height,
        elevation_params.lift_height / elevation_params.slope_end * path_length
    );
    if (lift_at_travel_end < z_change) {
        elevation_params.lift_height = z_change;
        elevation_params.slope_end = path_length;
    }

    return elevation_params;
}

std::string GCodeGenerator::get_ramping_layer_change_gcode(const Vec3d &from, const Vec3d &to, const unsigned extruder_id) {
    const Polyline xy_path{this->get_layer_change_xy_path(from, to)};

    const GCode::Impl::Travels::ElevatedTravelParams elevation_params{
        get_ramping_layer_change_params(
            from, to, xy_path, m_config, extruder_id, m_travel_obstacle_tracker
        )};
    return this->generate_ramping_layer_change_gcode(xy_path, from.z(), elevation_params);
}

std::string GCodeGenerator::generate_ramping_layer_change_gcode(
    const Polyline &xy_path,
    const double initial_elevation,
    const GCode::Impl::Travels::ElevatedTravelParams &elevation_params
) const {
    using namespace GCode::Impl::Travels;
    const std::vector<double> ensure_points_at_distances = linspace(
        elevation_params.slope_end - elevation_params.blend_width / 2.0,
        elevation_params.slope_end + elevation_params.blend_width / 2.0,
        elevation_params.parabola_points_count
    );

    Points3 travel{generate_elevated_travel(
        xy_path.points, ensure_points_at_distances, initial_elevation,
        ElevatedTravelFormula{elevation_params}
    )};

    std::string travel_gcode;
    Vec3d previous_point{this->point_to_gcode(travel.front())};
    for (const Vec3crd& point : travel) {
        const Vec3d gcode_point{this->point_to_gcode(point)};
        travel_gcode += this->m_writer
                            .get_travel_to_xyz_gcode(previous_point, gcode_point, "layer change");
        previous_point = gcode_point;
    }
    return travel_gcode;
}
// In sequential mode, process_layer is called once per each object and its copy,
// therefore layers will contain a single entry and single_object_instance_idx will point to the copy of the object.
// In non-sequential mode, process_layer is called per each print_z height with all object and support layers accumulated.
// For multi-material prints, this routine minimizes extruder switches by gathering extruder specific extrusion paths
// and performing the extruder specific extrusions together.
LayerResult GCodeGenerator::process_layer(
    const Print                    			&print,
    // Set of object & print layers of the same PrintObject and with the same print_z.
    const ObjectsLayerToPrint           	&layers,
    const LayerTools        		        &layer_tools,
    const GCode::SmoothPathCaches           &smooth_path_caches,
    const bool                               last_layer,
    // Pairs of PrintObject index and its instance index.
    const std::vector<const PrintInstance*> *ordering,
    // If set to size_t(-1), then print all copies of all objects.
    // Otherwise print a single copy of a single object.
    const size_t                     		 single_object_instance_idx)
{
    assert(! layers.empty());
    // Either printing all copies of all objects, or just a single copy of a single object.
    assert(single_object_instance_idx == size_t(-1) || layers.size() == 1);

    // First object, support and raft layer, if available.
    const Layer         *object_layer  = nullptr;
    const SupportLayer  *support_layer = nullptr;
    const SupportLayer  *raft_layer    = nullptr;
    for (const ObjectLayerToPrint &l : layers) {
        if (l.object_layer && ! object_layer)
            object_layer = l.object_layer;
        if (l.support_layer) {
            if (! support_layer)
                support_layer = l.support_layer;
            if (! raft_layer && support_layer->id() < support_layer->object()->slicing_parameters().raft_layers())
                raft_layer = support_layer;
        }
    }
    const Layer  &layer = (object_layer != nullptr) ? *object_layer : *support_layer;
    LayerResult   result { {}, layer.id(), false, last_layer, false};
    if (layer_tools.extruders.empty())
        // Nothing to extrude.
        return result;

    // Extract 1st object_layer and support_layer of this set of layers with an equal print_z.
    coordf_t             print_z       = layer.print_z + m_config.z_offset.value;
    bool                 first_layer   = layer.id() == 0;
    unsigned int         first_extruder_id = layer_tools.extruders.front();

    const std::vector<InstanceToPrint> instances_to_print{sort_print_object_instances(layers, ordering, single_object_instance_idx)};
    const PrintInstance* first_instance{instances_to_print.empty() ? nullptr : &instances_to_print.front().print_object.instances()[instances_to_print.front().instance_id]};
    m_label_objects.update(first_instance);

    //B36
    m_writer.set_is_first_layer(first_layer);

    // Initialize config with the 1st object to be printed at this layer.
    m_config.apply(layer.object()->config(), true);

    // Check whether it is possible to apply the spiral vase logic for this layer.
    // Just a reminder: A spiral vase mode is allowed for a single object, single material print only.
    m_enable_loop_clipping = true;
    if (m_spiral_vase && layers.size() == 1 && support_layer == nullptr) {
        bool enable = (layer.id() > 0 || !print.has_brim()) && (layer.id() >= (size_t)print.config().skirt_height.value && ! print.has_infinite_skirt());
        if (enable) {
            for (const LayerRegion *layer_region : layer.regions())
                if (size_t(layer_region->region().config().bottom_solid_layers.value) > layer.id() ||
                    layer_region->perimeters().items_count() > 1u ||
                    layer_region->fills().items_count() > 0) {
                    enable = false;
                    break;
                }
        }
        result.spiral_vase_enable = enable;
        // If we're going to apply spiralvase to this layer, disable loop clipping.
        m_enable_loop_clipping = !enable;
    }

    std::string gcode;
    assert(is_decimal_separator_point()); // for the sprintfs

    // add tag for processor
    gcode += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Layer_Change) + "\n";
    // export layer z
    gcode += std::string(";Z:") + float_to_string_decimal_point(print_z) + "\n";

    // export layer height
    float height = first_layer ? static_cast<float>(print_z) : static_cast<float>(print_z) - m_last_layer_z;
    gcode += std::string(";") + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height)
        + float_to_string_decimal_point(height) + "\n";

    // update caches
    const coordf_t previous_layer_z{m_last_layer_z};
    m_last_layer_z = static_cast<float>(print_z);
    m_max_layer_z  = std::max(m_max_layer_z, m_last_layer_z);
    m_last_height = height;
    m_current_layer_first_position = std::nullopt;
    m_already_unretracted = false;

    // Set new layer - this will change Z and force a retraction if retract_layer_change is enabled.
    if (!first_layer && ! print.config().before_layer_gcode.value.empty()) {
        DynamicConfig config;
        config.set_key_value("layer_num",   new ConfigOptionInt(m_layer_index + 1));
        config.set_key_value("layer_z",     new ConfigOptionFloat(print_z));
        config.set_key_value("max_layer_z", new ConfigOptionFloat(m_max_layer_z));
        gcode += this->placeholder_parser_process("before_layer_gcode",
            print.config().before_layer_gcode.value, m_writer.extruder()->id(), &config)
            + "\n";
    }
    gcode += this->change_layer(previous_layer_z, print_z, result.spiral_vase_enable);  // this will increase m_layer_index
    m_layer = &layer;
    if (this->line_distancer_is_required(layer_tools.extruders) && this->m_layer != nullptr && this->m_layer->lower_layer != nullptr)
        m_travel_obstacle_tracker.init_layer(layer, layers);
    m_object_layer_over_raft = false;
    if (!first_layer && ! print.config().layer_gcode.value.empty()) {
        DynamicConfig config;
        config.set_key_value("layer_num", new ConfigOptionInt(m_layer_index));
        config.set_key_value("layer_z",   new ConfigOptionFloat(print_z));
        config.set_key_value("max_layer_z", new ConfigOptionFloat(m_max_layer_z));
        gcode += this->placeholder_parser_process("layer_gcode",
            print.config().layer_gcode.value, m_writer.extruder()->id(), &config)
            + "\n";
    }

    if (! first_layer && ! m_second_layer_things_done) {
        // Transition from 1st to 2nd layer. Adjust nozzle temperatures as prescribed by the nozzle dependent
        // first_layer_temperature vs. temperature settings.
        for (const Extruder &extruder : m_writer.extruders()) {
            if (print.config().single_extruder_multi_material.value || m_ooze_prevention.enable) {
                // In single extruder multi material mode, set the temperature for the current extruder only.
                // The same applies when ooze prevention is enabled.
                if (extruder.id() != m_writer.extruder()->id())
                    continue;
            }
            int temperature = print.config().temperature.get_at(extruder.id());
            if (temperature > 0 && (temperature != print.config().first_layer_temperature.get_at(extruder.id())))
                gcode += m_writer.set_temperature(temperature, false, extruder.id());
        }
        gcode += m_writer.set_bed_temperature(print.config().bed_temperature.get_at(first_extruder_id));
        //B24
        gcode += m_writer.set_volume_temperature(print.config().volume_temperature.get_at(first_extruder_id));
        // Mark the temperature transition from 1st to 2nd layer to be finished.
        m_second_layer_things_done = true;
    }

    // Map from extruder ID to <begin, end> index of skirt loops to be extruded with that extruder.
    std::map<unsigned int, std::pair<size_t, size_t>> skirt_loops_per_extruder;

    // Extrude skirt at the print_z of the raft layers and normal object layers
    // not at the print_z of the interlaced support material layers.
    skirt_loops_per_extruder = first_layer ?
        Skirt::make_skirt_loops_per_extruder_1st_layer(print, layer_tools, m_skirt_done) :
        Skirt::make_skirt_loops_per_extruder_other_layers(print, layer_tools, m_skirt_done);

    if (this->config().avoid_crossing_curled_overhangs) {
        m_avoid_crossing_curled_overhangs.clear();
        for (const ObjectLayerToPrint &layer_to_print : layers) {
            if (layer_to_print.object() == nullptr)
                continue;
            for (const auto &instance : layer_to_print.object()->instances()) {
                m_avoid_crossing_curled_overhangs.add_obstacles(layer_to_print.object_layer, instance.shift);
                m_avoid_crossing_curled_overhangs.add_obstacles(layer_to_print.support_layer, instance.shift);
            }
        }
    }

    const bool has_custom_gcode_to_emit     = single_object_instance_idx == size_t(-1) && layer_tools.custom_gcode != nullptr;
    const int  extruder_id_for_custom_gcode = int(layer_tools.extruder_needed_for_color_changer) - 1;

    if (has_custom_gcode_to_emit && extruder_id_for_custom_gcode == -1) {
        // Normal (non-sequential) print with some custom code without picking a specific extruder before it.
        // If we don't need to pick a specific extruder before the color change, we can just emit a custom g-code.
        // Otherwise, we will emit the g-code after picking the specific extruder.

        std::string custom_gcode = ProcessLayer::emit_custom_gcode_per_print_z(*this, *layer_tools.custom_gcode, m_writer.extruder()->id(), first_extruder_id, print.config());
        if (layer_tools.custom_gcode->type == CustomGCode::ColorChange) {
            // We have a color change to do on this layer, but we want to do it immediately before the first extrusion instead of now, in order to fix GH #2672.
            m_pending_pre_extrusion_gcode = custom_gcode;
        } else {
            gcode += custom_gcode;
        }
    }

    // Extrude the skirt, brim, support, perimeters, infill ordered by the extruders.
    for (unsigned int extruder_id : layer_tools.extruders)
    {
        gcode += (layer_tools.has_wipe_tower && m_wipe_tower) ?
            m_wipe_tower->tool_change(*this, extruder_id, extruder_id == layer_tools.extruders.back()) :
            this->set_extruder(extruder_id, print_z);

        // let analyzer tag generator aware of a role type change
        if (layer_tools.has_wipe_tower && m_wipe_tower)
            m_last_processor_extrusion_role = GCodeExtrusionRole::WipeTower;

        if (has_custom_gcode_to_emit && extruder_id_for_custom_gcode == int(extruder_id)) {
            assert(m_writer.extruder()->id() == extruder_id_for_custom_gcode);
            assert(m_pending_pre_extrusion_gcode.empty());
            // Now we have picked the right extruder, so we can emit the custom g-code.
            gcode += ProcessLayer::emit_custom_gcode_per_print_z(*this, *layer_tools.custom_gcode, m_writer.extruder()->id(), first_extruder_id, print.config());
        }
        if (auto loops_it = skirt_loops_per_extruder.find(extruder_id); loops_it != skirt_loops_per_extruder.end()) {
            if (!this->m_config.complete_objects.value) {
                gcode += this->m_label_objects.maybe_stop_instance();
            }
            this->m_label_objects.update(nullptr);
            const std::pair<size_t, size_t> loops = loops_it->second;
            this->set_origin(0., 0.);
            m_avoid_crossing_perimeters.use_external_mp();
            Flow layer_skirt_flow = print.skirt_flow().with_height(float(m_skirt_done.back() - (m_skirt_done.size() == 1 ? 0. : m_skirt_done[m_skirt_done.size() - 2])));
            double mm3_per_mm = layer_skirt_flow.mm3_per_mm();
            for (size_t i = loops.first; i < loops.second; ++i) {
                // Adjust flow according to this layer's layer height.
                //FIXME using the support_material_speed of the 1st object printed.
                gcode += this->extrude_skirt(dynamic_cast<const ExtrusionLoop&>(*print.skirt().entities[i]),
                    // Override of skirt extrusion parameters. extrude_skirt() will fill in the extrusion width.
                    ExtrusionFlow{ mm3_per_mm, 0., layer_skirt_flow.height() },
                    smooth_path_caches.global(), "skirt"sv, m_config.support_material_speed.value);
            }
            m_avoid_crossing_perimeters.use_external_mp(false);
            // Allow a straight travel move to the first object point if this is the first layer (but don't in next layers).
            if (first_layer && loops.first == 0)
                m_avoid_crossing_perimeters.disable_once();
        }

        // Extrude brim with the extruder of the 1st region.
        if (! m_brim_done) {
            if (!this->m_config.complete_objects.value) {
                gcode += this->m_label_objects.maybe_stop_instance();
            }
            this->m_label_objects.update(nullptr);
            this->set_origin(0., 0.);
            m_avoid_crossing_perimeters.use_external_mp();
            for (const ExtrusionEntity *ee : print.brim().entities)
                gcode += this->extrude_entity({ *ee, false }, smooth_path_caches.global(), "brim"sv, m_config.support_material_speed.value);
            m_brim_done = true;
            m_avoid_crossing_perimeters.use_external_mp(false);
            // Allow a straight travel move to the first object point.
            m_avoid_crossing_perimeters.disable_once();
        }

        this->m_label_objects.update(first_instance);

        // We are almost ready to print. However, we must go through all the objects twice to print the the overridden extrusions first (infill/perimeter wiping feature):
        bool is_anything_overridden = layer_tools.wiping_extrusions().is_anything_overridden();
        if (is_anything_overridden) {
            // Extrude wipes.
            size_t gcode_size_old = gcode.size();
            for (const InstanceToPrint &instance : instances_to_print)
                this->process_layer_single_object(
                    gcode, extruder_id, instance,
                    layers[instance.object_layer_to_print_id], layer_tools, smooth_path_caches.layer_local(),
                    is_anything_overridden, true /* print_wipe_extrusions */);
            if (gcode_size_old < gcode.size())
                gcode+="; PURGING FINISHED\n";
        }
        // Extrude normal extrusions.
        for (const InstanceToPrint &instance : instances_to_print)
            this->process_layer_single_object(
                gcode, extruder_id, instance,
                layers[instance.object_layer_to_print_id], layer_tools, smooth_path_caches.layer_local(),
                is_anything_overridden, false /* print_wipe_extrusions */);
    }

    // During layer change the starting position of next layer is now known.
    // The solution is thus to emplace a temporary tag to the gcode, cache the postion and
    // replace the tag later. The tag is Layer_Change_Travel, the cached position is
    // m_current_layer_first_position and it is replaced here.
    const std::string tag = GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Layer_Change_Travel);
    std::string layer_change_gcode;
    const bool do_ramping_layer_change = (
        m_previous_layer_last_position
        && m_current_layer_first_position
        && m_layer_change_extruder_id
        && !result.spiral_vase_enable
        && print_z > previous_layer_z
        && this->m_config.travel_ramping_lift.get_at(*m_layer_change_extruder_id)
        && this->m_config.travel_slope.get_at(*m_layer_change_extruder_id) > 0
        && this->m_config.travel_slope.get_at(*m_layer_change_extruder_id) < 90
    );
    if (first_layer) {
        layer_change_gcode = ""; // Explicit for readability.
    } else if (do_ramping_layer_change) {
        const Vec3d &from{*m_previous_layer_last_position};
        const Vec3d &to{*m_current_layer_first_position};
        layer_change_gcode = this->get_ramping_layer_change_gcode(from, to, *m_layer_change_extruder_id);
    } else {
        layer_change_gcode = this->writer().get_travel_to_z_gcode(print_z, "simple layer change");
    }

    const auto keep_retraciton{[&](){
        if (!do_ramping_layer_change) {
            return true;
        }
        const double travel_length{(*m_current_layer_first_position - *m_previous_layer_last_position_before_wipe).norm()};
        if (this->m_config.retract_before_travel.get_at(*m_layer_change_extruder_id) < travel_length) {
            // Travel is long, keep retraction.
            return true;
        }
        return false;
    }};

    bool removed_retraction{false};
    if (this->m_config.travel_ramping_lift.get_at(*m_layer_change_extruder_id) && !result.spiral_vase_enable) {
        const std::string retraction_start_tag = GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Layer_Change_Retraction_Start);
        const std::string retraction_end_tag = GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Layer_Change_Retraction_End);

        if (keep_retraciton()) {
            boost::algorithm::replace_first(gcode, retraction_start_tag, "");
            boost::algorithm::replace_first(gcode, retraction_end_tag, "");
        } else {
            const std::size_t start{gcode.find(retraction_start_tag)};
            const std::size_t end_tag_start{gcode.find(retraction_end_tag)};
            const std::size_t end{end_tag_start + retraction_end_tag.size()};
            gcode.replace(start, end - start, "");

            layer_change_gcode = this->get_ramping_layer_change_gcode(*m_previous_layer_last_position_before_wipe, *m_current_layer_first_position, *m_layer_change_extruder_id);

            removed_retraction = true;
        }
    }

    if (removed_retraction) {
        const std::size_t start{gcode.find("FIRST_UNRETRACT")};
        const std::size_t end{gcode.find("\n", start)};
        gcode.replace(start, end - start, "");
    } else {
        boost::algorithm::replace_first(gcode,"FIRST_UNRETRACT", "");
    }

    boost::algorithm::replace_first(gcode, tag, layer_change_gcode);
    BOOST_LOG_TRIVIAL(trace) << "Exported layer " << layer.id() << " print_z " << print_z <<
    log_memory_info();

    result.gcode = std::move(gcode);
    result.cooling_buffer_flush = object_layer || raft_layer || last_layer;
    return result;
}

static const auto comment_perimeter = "perimeter"sv;
// Comparing string_view pointer & length for speed.
static inline bool comment_is_perimeter(const std::string_view comment) {
    return comment.data() == comment_perimeter.data() && comment.size() == comment_perimeter.size();
}

void GCodeGenerator::process_layer_single_object(
    // output
    std::string              &gcode, 
    // Index of the extruder currently active.
    const unsigned int        extruder_id,
    // What object and instance is going to be printed.
    const InstanceToPrint    &print_instance,
    // and the object & support layer of the above.
    const ObjectLayerToPrint &layer_to_print, 
    // Container for extruder overrides (when wiping into object or infill).
    const LayerTools         &layer_tools,
    // Optional smooth path interpolating extrusion polylines.
    const GCode::SmoothPathCache &smooth_path_cache,
    // Is any extrusion possibly marked as wiping extrusion?
    const bool                is_anything_overridden, 
    // Round 1 (wiping into object or infill) or round 2 (normal extrusions).
    const bool                print_wipe_extrusions)
{
    bool     first     = true;
    // Delay layer initialization as many layers may not print with all extruders.
    auto init_layer_delayed = [this, &print_instance, &layer_to_print, &first]() {
        if (first) {
            first = false;
            const PrintObject &print_object = print_instance.print_object;
            const Print       &print        = *print_object.print();
            m_config.apply(print_object.config(), true);
            m_layer = layer_to_print.layer();
            if (print.config().avoid_crossing_perimeters)
                m_avoid_crossing_perimeters.init_layer(*m_layer);
            // When starting a new object, use the external motion planner for the first travel move.
            const Point &offset = print_object.instances()[print_instance.instance_id].shift;
            GCode::PrintObjectInstance next_instance = {&print_object, int(print_instance.instance_id)};
            if (m_current_instance != next_instance) {
                m_avoid_crossing_perimeters.use_external_mp_once = true;
            }
            m_current_instance = next_instance;
            this->set_origin(unscale(offset));
            m_label_objects.update(&print_instance.print_object.instances()[print_instance.instance_id]);
        }
    };

    const PrintObject &print_object = print_instance.print_object;
    const Print       &print        = *print_object.print();


    if (! print_wipe_extrusions && layer_to_print.support_layer != nullptr)
        if (const SupportLayer &support_layer = *layer_to_print.support_layer; ! support_layer.support_fills.entities.empty()) {
            ExtrusionRole   role               = support_layer.support_fills.role();
            bool            has_support        = role.is_mixed() || role.is_support_base();
            bool            has_interface      = role.is_mixed() || role.is_support_interface();
            // Extruder ID of the support base. -1 if "don't care".
            unsigned int    support_extruder   = print_object.config().support_material_extruder.value - 1;
            // Shall the support be printed with the active extruder, preferably with non-soluble, to avoid tool changes?
            bool            support_dontcare   = support_extruder == std::numeric_limits<unsigned int>::max();
            // Extruder ID of the support interface. -1 if "don't care".
            unsigned int    interface_extruder = print_object.config().support_material_interface_extruder.value - 1;
            // Shall the support interface be printed with the active extruder, preferably with non-soluble, to avoid tool changes?
            bool            interface_dontcare = interface_extruder == std::numeric_limits<unsigned int>::max();
            if (support_dontcare || interface_dontcare) {
                // Some support will be printed with "don't care" material, preferably non-soluble.
                // Is the current extruder assigned a soluble filament?
                auto it_nonsoluble = std::find_if(layer_tools.extruders.begin(), layer_tools.extruders.end(), 
                    [&soluble = std::as_const(print.config().filament_soluble)](unsigned int extruder_id) { return ! soluble.get_at(extruder_id); });
                // There should be a non-soluble extruder available.
                assert(it_nonsoluble != layer_tools.extruders.end());
                unsigned int dontcare_extruder = it_nonsoluble == layer_tools.extruders.end() ? layer_tools.extruders.front() : *it_nonsoluble;
                if (support_dontcare)
                    support_extruder = dontcare_extruder;
                if (interface_dontcare)
                    interface_extruder = dontcare_extruder;
            }
            bool extrude_support   = has_support && support_extruder == extruder_id;
            bool extrude_interface = has_interface && interface_extruder == extruder_id;
            if (extrude_support || extrude_interface) {
                init_layer_delayed();
                m_layer = layer_to_print.support_layer;
                m_object_layer_over_raft = false;
                ExtrusionEntitiesPtr        entities_cache;
                const ExtrusionEntitiesPtr &entities = extrude_support && extrude_interface ? support_layer.support_fills.entities : entities_cache;
                if (! extrude_support || ! extrude_interface) {
                    auto role = extrude_support ? ExtrusionRole::SupportMaterial : ExtrusionRole::SupportMaterialInterface;
                    entities_cache.reserve(support_layer.support_fills.entities.size());
                    for (ExtrusionEntity *ee : support_layer.support_fills.entities)
                        if (ee->role() == role)
                            entities_cache.emplace_back(ee);
                }
                gcode += this->extrude_support(chain_extrusion_references(entities), smooth_path_cache);
            }
        }

    m_layer = layer_to_print.layer();
    // To control print speed of the 1st object layer printed over raft interface.
    m_object_layer_over_raft = layer_to_print.object_layer && layer_to_print.object_layer->id() > 0 &&
        print_object.slicing_parameters().raft_layers() == layer_to_print.object_layer->id();

    // Check whether this ExtrusionEntityCollection should be printed now with extruder_id, given print_wipe_extrusions
    // (wipe extrusions are printed before regular extrusions).
    auto shall_print_this_extrusion_collection = [extruder_id, instance_id = print_instance.instance_id, &layer_tools, is_anything_overridden, print_wipe_extrusions](const ExtrusionEntityCollection *eec, const PrintRegion &region) -> bool {
        assert(eec != nullptr);
        if (eec->entities.empty())
            // This shouldn't happen. FIXME why? but first_point() would fail.
            return false;
        // This extrusion is part of certain Region, which tells us which extruder should be used for it:
        int correct_extruder_id = layer_tools.extruder(*eec, region);
        if (! layer_tools.has_extruder(correct_extruder_id)) {
            // this entity is not overridden, but its extruder is not in layer_tools - we'll print it
            // by last extruder on this layer (could happen e.g. when a wiping object is taller than others - dontcare extruders are eradicated from layer_tools)
            correct_extruder_id = layer_tools.extruders.back();
        }
        int extruder_override_id = is_anything_overridden ? layer_tools.wiping_extrusions().get_extruder_override(eec, instance_id) : -1;
        return print_wipe_extrusions ?
            extruder_override_id == int(extruder_id) :
            extruder_override_id < 0 && int(extruder_id) == correct_extruder_id;
    };

    ExtrusionEntitiesPtr temp_fill_extrusions;
    if (const Layer *layer = layer_to_print.object_layer; layer)
        for (size_t idx : layer->lslice_indices_sorted_by_print_order) {
            const LayerSlice &lslice = layer->lslices_ex[idx];
            auto extrude_infill_range = [&](
                const LayerRegion &layerm, const ExtrusionEntityCollection &fills,
                LayerExtrusionRanges::const_iterator it_fill_ranges_begin, LayerExtrusionRanges::const_iterator it_fill_ranges_end, bool ironing) {
                // PrintObjects own the PrintRegions, thus the pointer to PrintRegion would be unique to a PrintObject, they would not
                // identify the content of PrintRegion accross the whole print uniquely. Translate to a Print specific PrintRegion.
                const PrintRegion &region = print.get_print_region(layerm.region().print_region_id());
                temp_fill_extrusions.clear();
                for (auto it_fill_range = it_fill_ranges_begin; it_fill_range != it_fill_ranges_end; ++ it_fill_range) {
                    assert(it_fill_range->region() == it_fill_ranges_begin->region());
                    for (uint32_t fill_id : *it_fill_range) {
                        assert(dynamic_cast<ExtrusionEntityCollection*>(fills.entities[fill_id]));
                        if (auto *eec = static_cast<ExtrusionEntityCollection*>(fills.entities[fill_id]);
                            (eec->role() == ExtrusionRole::Ironing) == ironing && shall_print_this_extrusion_collection(eec, region)) {
                            if (eec->can_reverse())
                                // Flatten the infill collection for better path planning.
                                for (auto *ee : eec->entities)
                                    temp_fill_extrusions.emplace_back(ee);
                            else
                                temp_fill_extrusions.emplace_back(eec);
                        }
                    }
                }
                if (! temp_fill_extrusions.empty()) {
                    init_layer_delayed();
                    m_config.apply(region.config());
                    const auto extrusion_name = ironing ? "ironing"sv : "infill"sv;
                    const Point* start_near = this->last_position ? &(*(this->last_position)) : nullptr;
                    for (const ExtrusionEntityReference &fill : chain_extrusion_references(temp_fill_extrusions, start_near))
                        if (auto *eec = dynamic_cast<const ExtrusionEntityCollection*>(&fill.extrusion_entity()); eec) {
                            for (const ExtrusionEntityReference &ee : chain_extrusion_references(*eec, start_near, fill.flipped()))
                                gcode += this->extrude_entity(ee, smooth_path_cache, extrusion_name);
                        } else
                            gcode += this->extrude_entity(fill, smooth_path_cache, extrusion_name);
                }
            };

            //FIXME order islands?
            // Sequential tool path ordering of multiple parts within the same object, aka. perimeter tracking (#5511)
            for (const LayerIsland &island : lslice.islands) {
                auto process_perimeters = [&]() {
                    const LayerRegion &layerm = *layer->get_region(island.perimeters.region());
                    // PrintObjects own the PrintRegions, thus the pointer to PrintRegion would be unique to a PrintObject, they would not
                    // identify the content of PrintRegion accross the whole print uniquely. Translate to a Print specific PrintRegion.
                    const PrintRegion &region = print.get_print_region(layerm.region().print_region_id());
                    bool first = true;
                    for (uint32_t perimeter_id : island.perimeters) {
                        // Extrusions inside islands are expected to be ordered already.
                        // Don't reorder them.
                        assert(dynamic_cast<const ExtrusionEntityCollection*>(layerm.perimeters().entities[perimeter_id]));
                        if (const auto *eec = static_cast<const ExtrusionEntityCollection*>(layerm.perimeters().entities[perimeter_id]);
                            shall_print_this_extrusion_collection(eec, region)) {
                            // This may not apply to Arachne, but maybe the Arachne gap fill should disable reverse as well?
                            // assert(! eec->can_reverse());
                            if (first) {
                                first = false;
                                init_layer_delayed();
                                m_config.apply(region.config());
                            }
                            for (const ExtrusionEntity *ee : *eec) {
                                // Don't reorder, don't flip.
                                gcode += this->extrude_entity({ *ee, false }, smooth_path_cache, comment_perimeter, -1.);
                                m_travel_obstacle_tracker.mark_extruded(ee, print_instance.object_layer_to_print_id, print_instance.instance_id);
                            }
                        }
                    }
                };
                auto process_infill = [&]() {
                    for (auto it = island.fills.begin(); it != island.fills.end();) {
                        // Gather range of fill ranges with the same region.
                        auto it_end = it;
                        for (++ it_end; it_end != island.fills.end() && it->region() == it_end->region(); ++ it_end) ;
                        const LayerRegion &layerm = *layer->get_region(it->region());
                        extrude_infill_range(layerm, layerm.fills(), it, it_end, false /* normal extrusions, not ironing */);
                        it = it_end;
                    }
                };
                if (print.config().infill_first) {
                    process_infill();
                    process_perimeters();
                } else {
                    process_perimeters();
                    process_infill();
                }
            }
            // ironing
            //FIXME move ironing into the loop above over LayerIslands?
            // First Ironing changes extrusion rate quickly, second single ironing may be done over multiple perimeter regions.
            // Ironing in a second phase is safer, but it may be less efficient.
            for (const LayerIsland &island : lslice.islands) {
                for (auto it = island.fills.begin(); it != island.fills.end();) {
                    // Gather range of fill ranges with the same region.
                    auto it_end = it;
                    for (++ it_end; it_end != island.fills.end() && it->region() == it_end->region(); ++ it_end) ;
                    const LayerRegion &layerm = *layer->get_region(it->region());
                    extrude_infill_range(layerm, layerm.fills(), it, it_end, true /* ironing, not normal extrusions */);
                    it = it_end;
                }
            }
        }

        
}


void GCodeGenerator::apply_print_config(const PrintConfig &print_config)
{
    m_writer.apply_print_config(print_config);
    m_config.apply(print_config);
    m_scaled_resolution = scaled<double>(print_config.gcode_resolution.value);
}

void GCodeGenerator::append_full_config(const Print &print, std::string &str)
{
    std::vector<std::pair<std::string, std::string>> config;
    encode_full_config(print, config);
    for (const auto& [key, value] : config) {
        str += "; " + key + " = " + value + "\n";
    }
}

void GCodeGenerator::encode_full_config(const Print& print, std::vector<std::pair<std::string, std::string>>& config)
{
    const DynamicPrintConfig &cfg = print.full_print_config();
    // Sorted list of config keys, which shall not be stored into the G-code. Initializer list.
    static constexpr auto banned_keys = {
        "compatible_printers"sv,
        "compatible_prints"sv,
        //FIXME The print host keys should not be exported to full_print_config anymore. The following keys may likely be removed.
        "print_host"sv,
        "printhost_apikey"sv,
        "printhost_cafile"sv
    };
    assert(std::is_sorted(banned_keys.begin(), banned_keys.end()));
    auto is_banned = [](const std::string &key) {
        return std::binary_search(banned_keys.begin(), banned_keys.end(), key);
    };
    config.reserve(config.size() + cfg.keys().size());
    for (const std::string& key : cfg.keys()) {
        if (! is_banned(key) && ! cfg.option(key)->is_nil())
            config.emplace_back(key, cfg.opt_serialize(key));
    }
    config.shrink_to_fit();
}

void GCodeGenerator::set_extruders(const std::vector<unsigned int> &extruder_ids)
{
    m_writer.set_extruders(extruder_ids);

    m_wipe.init(this->config(), extruder_ids);
}

void GCodeGenerator::set_origin(const Vec2d &pointf)
{
    // if origin increases (goes towards right), last_pos decreases because it goes towards left
    const auto offset = Point::new_scale(m_origin - pointf);
    if (last_position.has_value())
        *(this->last_position) += offset;
    m_wipe.offset_path(offset);
    m_origin = pointf;
}

std::string GCodeGenerator::preamble()
{
    std::string gcode = m_writer.preamble();

    /*  Perform a *silent* move to z_offset: we need this to initialize the Z
        position of our writer object so that any initial lift taking place
        before the first layer change will raise the extruder from the correct
        initial Z instead of 0.  */
    m_writer.travel_to_z(m_config.z_offset.value);

    return gcode;
}


// called by GCodeGenerator::process_layer()
std::string GCodeGenerator::change_layer(
    coordf_t previous_layer_z,
    coordf_t print_z,
    bool vase_mode
) {
    std::string gcode;
    if (m_layer_count > 0)
        // Increment a progress bar indicator.
        gcode += m_writer.update_progress(++ m_layer_index, m_layer_count);

    if (m_writer.multiple_extruders) {
        gcode += m_label_objects.maybe_change_instance(m_writer);
    }
    if (!EXTRUDER_CONFIG(travel_ramping_lift) && EXTRUDER_CONFIG(retract_layer_change)) {
        gcode += this->retract_and_wipe();
    } else if (EXTRUDER_CONFIG(travel_ramping_lift) && !vase_mode){
        m_previous_layer_last_position_before_wipe = this->last_position ?
            std::optional{to_3d(this->point_to_gcode(*this->last_position), previous_layer_z)} :
            std::nullopt;
        gcode += GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Layer_Change_Retraction_Start);
        gcode += this->retract_and_wipe(false, false);
        gcode += GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Layer_Change_Retraction_End);
        gcode += m_writer.reset_e();
    }

    Vec3d new_position = this->writer().get_position();
    new_position.z() = print_z;
    this->writer().update_position(new_position);

    //B38 //B46
    m_writer.add_object_change_labels(gcode);
    m_previous_layer_last_position = this->last_position ?
        std::optional{to_3d(this->point_to_gcode(*this->last_position), previous_layer_z)} :
        std::nullopt;

    gcode += GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Layer_Change_Travel);
    this->m_layer_change_extruder_id = m_writer.extruder()->id();

    // forget last wiping path as wiping after raising Z is pointless
    m_wipe.reset_path();
    return gcode;
}

#ifndef NDEBUG
static inline bool validate_smooth_path(const GCode::SmoothPath &smooth_path, bool loop)
{
    for (auto it = std::next(smooth_path.begin()); it != smooth_path.end(); ++ it) {
        assert(it->path.size() >= 2);
        assert(std::prev(it)->path.back().point == it->path.front().point);
    }
    assert(! loop || smooth_path.front().path.front().point == smooth_path.back().path.back().point);
    return true;
}
#endif //NDEBUG
static constexpr const double min_gcode_segment_length = 0.002;
std::string GCodeGenerator::extrude_loop(const ExtrusionLoop &loop_src, const GCode::SmoothPathCache &smooth_path_cache, const std::string_view description, double speed)
{

    // Extrude all loops CCW.
    //w38
    //bool  is_hole = loop_src.is_clockwise();
    ExtrusionLoop new_loop_src = loop_src;
    bool is_hole = (new_loop_src.loop_role() & elrHole) == elrHole;

    //w38
    if (m_config.spiral_vase && !is_hole) {
        // if spiral vase, we have to ensure that all contour are in the same orientation.
        new_loop_src.make_counter_clockwise();
    }
    Point seam_point = this->last_position.has_value() ? *this->last_position : Point::Zero();
    if (!m_config.spiral_vase && comment_is_perimeter(description)) {
        assert(m_layer != nullptr);
        //w38
        seam_point = m_seam_placer.place_seam(m_layer, new_loop_src, m_config.external_perimeters_first, seam_point);
    }
    
        // Because the G-code export has 1um resolution, don't generate segments shorter than 1.5 microns,
        // thus empty path segments will not be produced by G-code export.
    //w38
    GCode::SmoothPath smooth_path = smooth_path_cache.resolve_or_fit_split_with_seam(
        new_loop_src, is_hole, m_scaled_resolution, seam_point, scaled<double>(0.0015));

    // Clip the path to avoid the extruder to get exactly on the first point of the loop;
    // if polyline was shorter than the clipping distance we'd get a null polyline, so
    // we discard it in that case.
    if (m_enable_loop_clipping)
    //Y21
        clip_end(smooth_path, scaled<double>(EXTRUDER_CONFIG(nozzle_diameter)) * (m_config.seam_gap.value / 100), scaled<double>(min_gcode_segment_length));

    if (smooth_path.empty())
        return {};
    assert(validate_smooth_path(smooth_path, ! m_enable_loop_clipping));

    // Apply the small perimeter speed.
    //w38
    if (new_loop_src.paths.front().role().is_perimeter() && new_loop_src.length() <= SMALL_PERIMETER_LENGTH && speed == -1)
        speed = m_config.small_perimeter_speed.get_abs_value(m_config.perimeter_speed);

    // Extrude along the smooth path.
    std::string gcode;
    for (const GCode::SmoothPathElement &el : smooth_path)
        gcode += this->_extrude(el.path_attributes, el.path, description, speed);

    // reset acceleration
    gcode += m_writer.set_print_acceleration(fast_round_up<unsigned int>(m_config.default_acceleration.value));

    if (m_wipe.enabled()) {

        // Wipe will hide the seam.
        m_wipe.set_path(std::move(smooth_path));
    }//w38 
    else if (new_loop_src.paths.back().role().is_external_perimeter() && m_layer != nullptr && m_config.perimeters.value > 1) {

        // Only wipe inside if the wipe along the perimeter is disabled.
        // Make a little move inwards before leaving loop.
        if (std::optional<Point> pt = wipe_hide_seam(smooth_path, is_hole, scale_(EXTRUDER_CONFIG(nozzle_diameter))); pt) {

            // Generate the seam hiding travel move.
            gcode += m_writer.travel_to_xy(this->point_to_gcode(*pt), "move inwards before travel");
            this->last_position = *pt;
        }
    }

    return gcode;
}
std::string GCodeGenerator::extrude_skirt(
    const ExtrusionLoop &loop_src, const ExtrusionFlow &extrusion_flow_override,
    const GCode::SmoothPathCache &smooth_path_cache, const std::string_view description, double speed)
{
    assert(loop_src.is_counter_clockwise());
    Point seam_point = this->last_position.has_value() ? *this->last_position : Point::Zero();
    GCode::SmoothPath smooth_path = smooth_path_cache.resolve_or_fit_split_with_seam(
        loop_src, false, m_scaled_resolution, seam_point, scaled<double>(0.0015));
    // Clip the path to avoid the extruder to get exactly on the first point of the loop;
    // if polyline was shorter than the clipping distance we'd get a null polyline, so
    // we discard it in that case.
    if (m_enable_loop_clipping)
        clip_end(smooth_path, scale_(EXTRUDER_CONFIG(nozzle_diameter)) * LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER, scaled<double>(min_gcode_segment_length));
    if (smooth_path.empty())
        return {};
    assert(validate_smooth_path(smooth_path, ! m_enable_loop_clipping));

    // Extrude along the smooth path.
    std::string gcode;
    for (GCode::SmoothPathElement &el : smooth_path) {
        // Override extrusion parameters.
        el.path_attributes.mm3_per_mm = extrusion_flow_override.mm3_per_mm;
        el.path_attributes.height = extrusion_flow_override.height;
        gcode += this->_extrude(el.path_attributes, el.path, description, speed);
    }
    // reset acceleration
    gcode += m_writer.set_print_acceleration(fast_round_up<unsigned int>(m_config.default_acceleration.value));
    if (m_wipe.enabled())
        // Wipe will hide the seam.
        m_wipe.set_path(std::move(smooth_path));

    return gcode;
}

std::string GCodeGenerator::extrude_multi_path(const ExtrusionMultiPath &multipath, bool reverse, const GCode::SmoothPathCache &smooth_path_cache, const std::string_view description, double speed)
{
#ifndef NDEBUG
    for (auto it = std::next(multipath.paths.begin()); it != multipath.paths.end(); ++it) {
        assert(it->polyline.points.size() >= 2);
        assert(std::prev(it)->polyline.last_point() == it->polyline.first_point());
    }
#endif // NDEBUG
    GCode::SmoothPath smooth_path = smooth_path_cache.resolve_or_fit(multipath, reverse, m_scaled_resolution);
    // extrude along the path

    std::string gcode;
    for (GCode::SmoothPathElement &el : smooth_path)
        gcode += this->_extrude(el.path_attributes, el.path, description, speed);
    GCode::reverse(smooth_path);
    m_wipe.set_path(std::move(smooth_path));
    // reset acceleration
    gcode += m_writer.set_print_acceleration((unsigned int)floor(m_config.default_acceleration.value + 0.5));
    return gcode;
}

std::string GCodeGenerator::extrude_entity(const ExtrusionEntityReference &entity, const GCode::SmoothPathCache &smooth_path_cache, const std::string_view description, double speed)
{
    if (const ExtrusionPath *path = dynamic_cast<const ExtrusionPath*>(&entity.extrusion_entity()))
        return this->extrude_path(*path, entity.flipped(), smooth_path_cache, description, speed);
    else if (const ExtrusionMultiPath *multipath = dynamic_cast<const ExtrusionMultiPath*>(&entity.extrusion_entity()))
        return this->extrude_multi_path(*multipath, entity.flipped(), smooth_path_cache, description, speed);
    else if (const ExtrusionLoop *loop = dynamic_cast<const ExtrusionLoop*>(&entity.extrusion_entity()))
        return this->extrude_loop(*loop, smooth_path_cache, description, speed);
    else
        throw Slic3r::InvalidArgument("Invalid argument supplied to extrude()");
    return {};
}

std::string GCodeGenerator::extrude_path(const ExtrusionPath &path, bool reverse, const GCode::SmoothPathCache &smooth_path_cache, std::string_view description, double speed)
{
    Geometry::ArcWelder::Path smooth_path = smooth_path_cache.resolve_or_fit(path, reverse, m_scaled_resolution);
    std::string gcode = this->_extrude(path.attributes(), smooth_path, description, speed);
    Geometry::ArcWelder::reverse(smooth_path);
    m_wipe.set_path(std::move(smooth_path));
    // reset acceleration
    gcode += m_writer.set_print_acceleration((unsigned int)floor(m_config.default_acceleration.value + 0.5));
    return gcode;
}

std::string GCodeGenerator::extrude_support(const ExtrusionEntityReferences &support_fills, const GCode::SmoothPathCache &smooth_path_cache)
{
    static constexpr const auto support_label            = "support material"sv;
    static constexpr const auto support_interface_label  = "support material interface"sv;

    std::string gcode;
    if (! support_fills.empty()) {
        const double  support_speed            = m_config.support_material_speed.value;
        const double  support_interface_speed  = m_config.support_material_interface_speed.get_abs_value(support_speed);
        for (const ExtrusionEntityReference &eref : support_fills) {
            ExtrusionRole role = eref.extrusion_entity().role();
            assert(role == ExtrusionRole::SupportMaterial || role == ExtrusionRole::SupportMaterialInterface);
            const auto   label = (role == ExtrusionRole::SupportMaterial) ? support_label : support_interface_label;
            const double speed = (role == ExtrusionRole::SupportMaterial) ? support_speed : support_interface_speed;
            const ExtrusionPath *path = dynamic_cast<const ExtrusionPath*>(&eref.extrusion_entity());
            if (path)
                gcode += this->extrude_path(*path, eref.flipped(), smooth_path_cache, label, speed);
            else if (const ExtrusionMultiPath *multipath = dynamic_cast<const ExtrusionMultiPath*>(&eref.extrusion_entity()); multipath)
                gcode += this->extrude_multi_path(*multipath, eref.flipped(), smooth_path_cache, label, speed);
                else {
                const ExtrusionEntityCollection *eec = dynamic_cast<const ExtrusionEntityCollection*>(&eref.extrusion_entity());
                    assert(eec);
                if (eec) {
                    //FIXME maybe order the support here?
                    ExtrusionEntityReferences refs;
                    refs.reserve(eec->entities.size());
                    std::transform(eec->entities.begin(), eec->entities.end(), std::back_inserter(refs), 
                        [flipped = eref.flipped()](const ExtrusionEntity *ee) { return ExtrusionEntityReference{ *ee, flipped }; });
                    gcode += this->extrude_support(refs, smooth_path_cache);
                }
            }
        }
    }
    return gcode;
}

bool GCodeGenerator::GCodeOutputStream::is_error() const 
{
    return ::ferror(this->f);
}

void GCodeGenerator::GCodeOutputStream::flush()
{ 
    ::fflush(this->f);
}

void GCodeGenerator::GCodeOutputStream::close()
{ 
    if (this->f) {
        ::fclose(this->f);
        this->f = nullptr;
    }
}

void GCodeGenerator::GCodeOutputStream::write(const char *what)
{
    if (what != nullptr) {
        //FIXME don't allocate a string, maybe process a batch of lines?
        std::string gcode(m_find_replace ? m_find_replace->process_layer(what) : what);
        // writes string to file
        fwrite(gcode.c_str(), 1, gcode.size(), this->f);
        m_processor.process_buffer(gcode);
    }
}

void GCodeGenerator::GCodeOutputStream::writeln(const std::string &what)
{
    if (! what.empty())
        this->write(what.back() == '\n' ? what : what + '\n');
}

void GCodeGenerator::GCodeOutputStream::write_format(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    int buflen;
    {
        va_list args2;
        va_copy(args2, args);
        buflen =
    #ifdef _MSC_VER
            ::_vscprintf(format, args2)
    #else
            ::vsnprintf(nullptr, 0, format, args2)
    #endif
            + 1;
        va_end(args2);
    }

    char buffer[1024];
    bool buffer_dynamic = buflen > 1024;
    char *bufptr = buffer_dynamic ? (char*)malloc(buflen) : buffer;
    int res = ::vsnprintf(bufptr, buflen, format, args);
    if (res > 0)
        this->write(bufptr);

    if (buffer_dynamic)
        free(bufptr);

    va_end(args);
}

std::string GCodeGenerator::travel_to_first_position(const Vec3crd& point, const double from_z, const ExtrusionRole role, const std::function<std::string()>& insert_gcode) {
    std::string gcode;

    const Vec3d gcode_point = to_3d(this->point_to_gcode(point.head<2>()), unscaled(point.z()));

    if (!EXTRUDER_CONFIG(travel_ramping_lift) && this->last_position) {
        Vec3d writer_position{this->writer().get_position()};
        writer_position.z() = 0.0; // Endofrce z generation!
        this->writer().update_position(writer_position);
        gcode = this->travel_to(
            *this->last_position, point.head<2>(), role, "travel to first layer point", insert_gcode
        );
    } else {
        this->m_layer_change_used_external_mp = this->m_avoid_crossing_perimeters.use_external_mp_once;
        this->m_layer_change_layer = this->layer();
        this->m_layer_change_origin = this->origin();
    double lift{
        EXTRUDER_CONFIG(travel_ramping_lift) ? EXTRUDER_CONFIG(travel_max_lift) :
                                               EXTRUDER_CONFIG(retract_lift)};
    const double upper_limit = EXTRUDER_CONFIG(retract_lift_below);
    const double lower_limit = EXTRUDER_CONFIG(retract_lift_above);
    if ((lower_limit > 0 && gcode_point.z() < lower_limit) ||
        (upper_limit > 0 && gcode_point.z() > upper_limit)) {
        lift = 0.0;
    }

        if (EXTRUDER_CONFIG(retract_length) > 0 && !this->last_position) {
        if (!this->last_position || EXTRUDER_CONFIG(retract_before_travel) < (this->point_to_gcode(*this->last_position) - gcode_point.head<2>()).norm()) {
            gcode += this->writer().retract();
            gcode += this->writer().get_travel_to_z_gcode(from_z + lift, "lift");
        }
    }
        const std::string comment{"move to first layer point"};

        gcode += insert_gcode();
    gcode += this->writer().get_travel_to_xy_gcode(gcode_point.head<2>(), comment);
    gcode += this->writer().get_travel_to_z_gcode(gcode_point.z(), comment);

        this->m_avoid_crossing_perimeters.reset_once_modifiers();
        this->last_position = point.head<2>();
        this->writer().update_position(gcode_point);
    }
    m_current_layer_first_position = gcode_point;
    return gcode;
}

double cap_speed(
    double speed, const double mm3_per_mm, const FullPrintConfig &config, int extruder_id
) {
    const double general_cap{config.max_volumetric_speed.value};
    if (general_cap > 0) {
        speed = std::min(speed, general_cap / mm3_per_mm);
    }
    const double filament_cap{config.filament_max_volumetric_speed.get_at(extruder_id)};
    if (filament_cap > 0) {
        speed = std::min(speed, filament_cap / mm3_per_mm);
    }
    return speed;
}
std::string GCodeGenerator::_extrude(
    const ExtrusionAttributes       &path_attr, 
    const Geometry::ArcWelder::Path &path, 
    const std::string_view           description,
    double                           speed)
{
    std::string gcode;
    const std::string_view description_bridge = path_attr.role.is_bridge() ? " (bridge)"sv : ""sv;
    //w30
    bool                   is_first_or_bottom_layer = (path_attr.role == ExtrusionRole::TopSolidInfill) || (this->on_first_layer());
    bool                   is_first                 = this->on_first_layer();
    const bool has_active_instance{m_label_objects.has_active_instance()};
    if (m_writer.multiple_extruders && has_active_instance) {
        gcode += m_label_objects.maybe_change_instance(m_writer);
    }
    if (!m_current_layer_first_position) {
        const Vec3crd point = to_3d(path.front().point, scaled(this->m_last_layer_z));
        gcode += this->travel_to_first_position(point, unscaled(point.z()), path_attr.role, [&](){
            return m_writer.multiple_extruders ? "" : m_label_objects.maybe_change_instance(m_writer);
        });
    } else {
    // go to first point of extrusion path
        if (!this->last_position) {
            const double z = this->m_last_layer_z;
        const std::string comment{"move to print after unknown position"};
        gcode += this->retract_and_wipe();
            gcode += m_writer.multiple_extruders ? "" : m_label_objects.maybe_change_instance(m_writer);
        gcode += this->m_writer.travel_to_xy(this->point_to_gcode(path.front().point), comment);
        gcode += this->m_writer.get_travel_to_z_gcode(z, comment);
        } else if ( this->last_position != path.front().point) {
        std::string comment = "move to first ";
        comment += description;
        comment += description_bridge;
        comment += " point";
            const std::string travel_gcode{this->travel_to(*this->last_position, path.front().point, path_attr.role, comment, [&](){
                return m_writer.multiple_extruders ? "" : m_label_objects.maybe_change_instance(m_writer);
            })};
            gcode += travel_gcode;
        }
    }

    //B38 //B46
    m_writer.add_object_change_labels(gcode);

    // compensate retraction
    if (this->m_already_unretracted) {
    gcode += this->unretract();
    } else {
        this->m_already_unretracted = true;
        gcode += "FIRST_UNRETRACT" + this->unretract();
        //First unretract may or may not be removed thus we must start from E0.
        gcode += this->writer().reset_e();
    }

    if (m_writer.multiple_extruders && !has_active_instance) {
        gcode += m_label_objects.maybe_change_instance(m_writer);
    }

    if (!m_pending_pre_extrusion_gcode.empty()) {
        // There is G-Code that is due to be inserted before an extrusion starts. Insert it.
        gcode += m_pending_pre_extrusion_gcode;
        m_pending_pre_extrusion_gcode.clear();
    }

    // adjust acceleration
    if (m_config.default_acceleration.value > 0) {
        double acceleration;
        if (this->on_first_layer() && m_config.first_layer_acceleration.value > 0) {
            acceleration = m_config.first_layer_acceleration.value;
        } else if (this->object_layer_over_raft() && m_config.first_layer_acceleration_over_raft.value > 0) {
            acceleration = m_config.first_layer_acceleration_over_raft.value;
        } else if (m_config.bridge_acceleration.value > 0 && path_attr.role.is_bridge()) {
            acceleration = m_config.bridge_acceleration.value;
        } else if (m_config.top_solid_infill_acceleration > 0 && path_attr.role == ExtrusionRole::TopSolidInfill) {
            acceleration = m_config.top_solid_infill_acceleration.value;
        } else if (m_config.solid_infill_acceleration > 0 && path_attr.role.is_solid_infill()) {
            acceleration = m_config.solid_infill_acceleration.value;
        } else if (m_config.infill_acceleration.value > 0 && path_attr.role.is_infill()) {
            acceleration = m_config.infill_acceleration.value;
        } else if (m_config.external_perimeter_acceleration > 0 && path_attr.role.is_external_perimeter()) {
            acceleration = m_config.external_perimeter_acceleration.value;
        } else if (m_config.perimeter_acceleration.value > 0 && path_attr.role.is_perimeter()) {
            acceleration = m_config.perimeter_acceleration.value;
        } else {
            acceleration = m_config.default_acceleration.value;
        }
        gcode += m_writer.set_print_acceleration((unsigned int)floor(acceleration + 0.5));
    }

    // calculate extrusion length per distance unit
    double e_per_mm = m_writer.extruder()->e_per_mm3() * path_attr.mm3_per_mm;
    //w30
    if (is_first_or_bottom_layer) {
        if (is_first)
            e_per_mm *=  m_config.bottom_solid_infill_flow_ratio ;
        else
            e_per_mm *= m_config.top_solid_infill_flow_ratio ;
    }
    if (m_writer.extrusion_axis().empty())
        // gcfNoExtrusion
        e_per_mm = 0;

    // set speed
    if (speed == -1) {
        if (path_attr.role == ExtrusionRole::Perimeter) {
            speed = m_config.get_abs_value("perimeter_speed");
        } else if (path_attr.role == ExtrusionRole::ExternalPerimeter) {
            speed = m_config.get_abs_value("external_perimeter_speed");
        } else if (path_attr.role.is_bridge()) {
            assert(path_attr.role.is_perimeter() || path_attr.role == ExtrusionRole::BridgeInfill);
            speed = m_config.get_abs_value("bridge_speed");
        } else if (path_attr.role == ExtrusionRole::InternalInfill) {
            speed = m_config.get_abs_value("infill_speed");
        } else if (path_attr.role == ExtrusionRole::SolidInfill) {
            speed = m_config.get_abs_value("solid_infill_speed");
        } else if (path_attr.role == ExtrusionRole::TopSolidInfill) {
            speed = m_config.get_abs_value("top_solid_infill_speed");
        } else if (path_attr.role == ExtrusionRole::Ironing) {
            speed = m_config.get_abs_value("ironing_speed");
        } else if (path_attr.role == ExtrusionRole::GapFill) {
            speed = m_config.get_abs_value("gap_fill_speed");
        } else {
            throw Slic3r::InvalidArgument("Invalid speed");
        }
    }
    if (m_volumetric_speed != 0. && speed == 0)
        speed = m_volumetric_speed / path_attr.mm3_per_mm;
    //B37
    if (this->on_first_layer())
        speed = path_attr.role == ExtrusionRole::InternalInfill ?
            m_config.get_abs_value("first_layer_infill_speed") : 
            path_attr.role == ExtrusionRole::SolidInfill    ?
            m_config.get_abs_value("first_layer_infill_speed") :
            m_config.get_abs_value("first_layer_speed", speed);
    else if (this->object_layer_over_raft())
        speed = m_config.get_abs_value("first_layer_speed_over_raft", speed);
    //w25
    else if (m_config.slow_down_layers > 1) {
        const auto _layer = layer_id() + 1;
        if (_layer > 0 && _layer < m_config.slow_down_layers) {
            const auto first_layer_speed = (path_attr.role==ExtrusionRole::Perimeter||path_attr.role==ExtrusionRole::ExternalPerimeter) ? m_config.get_abs_value("first_layer_speed") :
                                                                       m_config.get_abs_value("first_layer_infill_speed");
            if (first_layer_speed < speed) {
                speed = std::min(speed, Slic3r::lerp(first_layer_speed, speed, (double) _layer / m_config.slow_down_layers));
            }
        }
    }
    
    

    std::pair<float, float> dynamic_speed_and_fan_speed{-1, -1};
    if (path_attr.overhang_attributes.has_value()) {

        double external_perim_reference_speed = m_config.get_abs_value("external_perimeter_speed");
        if (external_perim_reference_speed == 0)
            external_perim_reference_speed = m_volumetric_speed / path_attr.mm3_per_mm;
        external_perim_reference_speed = cap_speed(
            external_perim_reference_speed, path_attr.mm3_per_mm, m_config, m_writer.extruder()->id()
        );

        dynamic_speed_and_fan_speed = ExtrusionProcessor::calculate_overhang_speed(path_attr, this->m_config, m_writer.extruder()->id(),
                                                                                   external_perim_reference_speed, speed);
    }

    if (dynamic_speed_and_fan_speed.first > -1) {
        speed = dynamic_speed_and_fan_speed.first;
    }

    // cap speed with max_volumetric_speed anyway (even if user is not using autospeed)
    speed = cap_speed(speed, path_attr.mm3_per_mm, m_config, m_writer.extruder()->id());
    double F = speed * 60;  // convert mm/sec to mm/min

    // extrude arc or line
    if (m_enable_extrusion_role_markers)
    {
        if (GCodeExtrusionRole role = extrusion_role_to_gcode_extrusion_role(path_attr.role); role != m_last_extrusion_role)
        {
            m_last_extrusion_role = role;
            if (m_enable_extrusion_role_markers)
            {
                char buf[32];
                sprintf(buf, ";_EXTRUSION_ROLE:%d\n", int(m_last_extrusion_role));
                gcode += buf;
            }
        }
    }

    // adds processor tags and updates processor tracking data
    // QIDIMultiMaterial::Writer may generate GCodeProcessor::Height_Tag lines without updating m_last_height
    // so, if the last role was GCodeExtrusionRole::WipeTower we force export of GCodeProcessor::Height_Tag lines
    bool last_was_wipe_tower = (m_last_processor_extrusion_role == GCodeExtrusionRole::WipeTower);
    assert(is_decimal_separator_point());

    if (GCodeExtrusionRole role = extrusion_role_to_gcode_extrusion_role(path_attr.role); role != m_last_processor_extrusion_role) {
        m_last_processor_extrusion_role = role;
        char buf[64];
        sprintf(buf, ";%s%s\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Role).c_str(), gcode_extrusion_role_to_string(m_last_processor_extrusion_role).c_str());
        gcode += buf;
    }

    if (last_was_wipe_tower || m_last_width != path_attr.width) {
        m_last_width = path_attr.width;
        gcode += std::string(";") + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Width)
               + float_to_string_decimal_point(m_last_width) + "\n";
    }

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    if (last_was_wipe_tower || (m_last_mm3_per_mm != path_attr.mm3_per_mm)) {
        m_last_mm3_per_mm = path_attr.mm3_per_mm;
        gcode += std::string(";") + GCodeProcessor::Mm3_Per_Mm_Tag
            + float_to_string_decimal_point(m_last_mm3_per_mm) + "\n";
    }
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

    if (last_was_wipe_tower || std::abs(m_last_height - path_attr.height) > EPSILON) {
        m_last_height = path_attr.height;

        gcode += std::string(";") + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height)
            + float_to_string_decimal_point(m_last_height) + "\n";
    }

    std::string cooling_marker_setspeed_comments;
    if (m_enable_cooling_markers) {
        if (path_attr.role.is_bridge())
            gcode += ";_BRIDGE_FAN_START\n";
        else
            cooling_marker_setspeed_comments = ";_EXTRUDE_SET_SPEED";
        if (path_attr.role == ExtrusionRole::ExternalPerimeter)
            cooling_marker_setspeed_comments += ";_EXTERNAL_PERIMETER";
    }

        // F is mm per minute.
        gcode += m_writer.set_speed(F, "", cooling_marker_setspeed_comments);
    if (dynamic_speed_and_fan_speed.second >= 0)
        gcode += ";_SET_FAN_SPEED" + std::to_string(int(dynamic_speed_and_fan_speed.second)) + "\n";
        std::string comment;
        if (m_config.gcode_comments) {
            comment = description;
            comment += description_bridge;
        }
    Vec2d prev_exact = this->point_to_gcode(path.front().point);
    Vec2d prev = GCodeFormatter::quantize(prev_exact);
    auto  it   = path.begin();
    auto  end  = path.end();
    for (++ it; it != end; ++ it) {
        Vec2d p_exact = this->point_to_gcode(it->point);
        Vec2d p = GCodeFormatter::quantize(p_exact);
        assert(p != prev);
        if (p != prev) {
            // Center of the radius to be emitted into the G-code: Either by radius or by center offset.
            double radius = 0;
            Vec2d  ij;
            if (it->radius != 0) {
                // Extrude an arc.
                assert(m_config.arc_fitting == ArcFittingType::EmitCenter);
                radius = unscaled<double>(it->radius);
                {
                    // Calculate quantized IJ circle center offset.
                    ij = GCodeFormatter::quantize(Vec2d(
                            Geometry::ArcWelder::arc_center(prev_exact.cast<double>(), p_exact.cast<double>(), double(radius), it->ccw())
                            - prev));
                    if (ij == Vec2d::Zero())
                        // Don't extrude a degenerated circle.
                        radius = 0;
                }
            }
            if (radius == 0) {
                // Extrude line segment.
                if (const double line_length = (p - prev).norm(); line_length > 0)
                    gcode += m_writer.extrude_to_xy(p, e_per_mm * line_length, comment);
            } else {
                double angle = Geometry::ArcWelder::arc_angle(prev.cast<double>(), p.cast<double>(), double(radius));
                assert(angle > 0);
                const double line_length = angle * std::abs(radius);
                const double dE = e_per_mm * line_length;
                assert(dE > 0);
                gcode += m_writer.extrude_to_xy_G2G3IJ(p, ij, it->ccw(), dE, comment);
            }
            prev             = p;
            prev_exact = p_exact;
        }
    }
    

    if (m_enable_cooling_markers)
        gcode += path_attr.role.is_bridge() ? ";_BRIDGE_FAN_END\n" : ";_EXTRUDE_END\n";

    if (dynamic_speed_and_fan_speed.second >= 0)
        gcode += ";_RESET_FAN_SPEED\n";

    this->last_position = path.back().point;
    return gcode;
}


std::string GCodeGenerator::generate_travel_gcode(
    const Points3& travel,
    const std::string& comment,
    const std::function<std::string()>& insert_gcode
) {
    std::string gcode;
    const unsigned acceleration =(unsigned)(m_config.travel_acceleration.value + 0.5);

    if (travel.empty()) {
        return "";
    }

    // generate G-code for the travel move
    // use G1 because we rely on paths being straight (G0 may make round paths)

    gcode += this->m_writer.set_travel_acceleration(acceleration);

    Vec3d previous_point{this->point_to_gcode(travel.front())};
    bool already_inserted{false};
    for (std::size_t i{0}; i < travel.size(); ++i) {
        const Vec3crd& point{travel[i]};
        const Vec3d gcode_point{this->point_to_gcode(point)};

        if (travel.size() - i <= 2 && !already_inserted) {
            gcode += insert_gcode();
            already_inserted = true;
        }
        gcode += this->m_writer.travel_to_xyz(previous_point, gcode_point, comment);
        this->last_position = point.head<2>();
        previous_point = gcode_point;
    }

        if (! GCodeWriter::supports_separate_travel_acceleration(config().gcode_flavor)) {
            // In case that this flavor does not support separate print and travel acceleration,
            // reset acceleration to default.
        gcode += this->m_writer.set_travel_acceleration(acceleration);
        }

    return gcode;
}

bool GCodeGenerator::needs_retraction(const Polyline &travel, ExtrusionRole role)
{
    if (! m_writer.extruder() || travel.length() < scale_(EXTRUDER_CONFIG(retract_before_travel))) {
        // skip retraction if the move is shorter than the configured threshold
        return false;
    }

    if (role == ExtrusionRole::SupportMaterial)
        if (const SupportLayer *support_layer = dynamic_cast<const SupportLayer*>(m_layer);
            support_layer != nullptr && ! support_layer->support_islands_bboxes.empty()) {
            BoundingBox bbox_travel = get_extents(travel);
            Polylines   trimmed;
            bool        trimmed_initialized = false;
            for (const BoundingBox &bbox : support_layer->support_islands_bboxes)
                if (bbox.overlap(bbox_travel)) {
                    const auto &island = support_layer->support_islands[&bbox - support_layer->support_islands_bboxes.data()];
                    trimmed = trimmed_initialized ? diff_pl(trimmed, island) : diff_pl(travel, island);
                    trimmed_initialized = true;
                    if (trimmed.empty())
                        // skip retraction if this is a travel move inside a support material island
                        //FIXME not retracting over a long path may cause oozing, which in turn may result in missing material
                        // at the end of the extrusion path!
                        return false;
                    // Not sure whether updating the boudning box isn't too expensive.
                    //bbox_travel = get_extents(trimmed);
                }
        }

    if (m_config.only_retract_when_crossing_perimeters && m_layer != nullptr &&
        m_config.fill_density.value > 0 && m_retract_when_crossing_perimeters.travel_inside_internal_regions(*m_layer, travel))
        // Skip retraction if travel is contained in an internal slice *and*
        // internal infill is enabled (so that stringing is entirely not visible).
        //FIXME any_internal_region_slice_contains() is potentionally very slow, it shall test for the bounding boxes first.
        return false;

    // retract if only_retract_when_crossing_perimeters is disabled or doesn't apply
    return true;
}

Polyline GCodeGenerator::generate_travel_xy_path(
    const Point& start_point,
    const Point& end_point,
    const bool needs_retraction,
    bool& could_be_wipe_disabled
) {

    const Point scaled_origin{scaled(this->origin())};
    const bool avoid_crossing_perimeters = (
        this->m_config.avoid_crossing_perimeters
        && !this->m_avoid_crossing_perimeters.disabled_once()
    );

    Polyline xy_path{start_point, end_point};
    if (m_config.avoid_crossing_curled_overhangs) {
        if (avoid_crossing_perimeters) {
            BOOST_LOG_TRIVIAL(warning)
                << "Option >avoid crossing curled overhangs< is not compatible with avoid crossing perimeters and it will be ignored!";
        } else {
            xy_path = this->m_avoid_crossing_curled_overhangs.find_path(
                start_point + scaled_origin,
                end_point + scaled_origin
            );
            xy_path.translate(-scaled_origin);
        }
    }


    // if a retraction would be needed, try to use avoid_crossing_perimeters to plan a
    // multi-hop travel path inside the configuration space
    if (
        needs_retraction
        && avoid_crossing_perimeters
    ) {
        xy_path = this->m_avoid_crossing_perimeters.travel_to(*this, end_point, &could_be_wipe_disabled);
    }

    return xy_path;
}

// This method accepts &point in print coordinates.
std::string GCodeGenerator::travel_to(
    const Point &start_point,
    const Point &end_point,
    ExtrusionRole role,
    const std::string &comment,
    const std::function<std::string()>& insert_gcode
) {

    // check whether a straight travel move would need retraction

    bool could_be_wipe_disabled {false};
    bool needs_retraction = this->needs_retraction(Polyline{start_point, end_point}, role);

    Polyline xy_path{generate_travel_xy_path(
        start_point, end_point, needs_retraction, could_be_wipe_disabled
    )};

    needs_retraction = this->needs_retraction(xy_path, role);

    std::string wipe_retract_gcode{};
    if (needs_retraction) {
        if (could_be_wipe_disabled) {
            m_wipe.reset_path();
        }

        Point position_before_wipe{*this->last_position};
        wipe_retract_gcode = this->retract_and_wipe();

        if (*this->last_position != position_before_wipe) {
            xy_path = generate_travel_xy_path(
                *this->last_position, end_point, needs_retraction, could_be_wipe_disabled
            );
        }
    } else {
        m_wipe.reset_path();
    }

    //B38 //B46
    m_writer.add_object_change_labels(wipe_retract_gcode);
    this->m_avoid_crossing_perimeters.reset_once_modifiers();

    const unsigned extruder_id = this->m_writer.extruder()->id();
    const double retract_length = this->m_config.retract_length.get_at(extruder_id);
    bool can_be_flat{!needs_retraction || retract_length == 0};
    const double initial_elevation = this->m_last_layer_z;

    const double upper_limit = this->m_config.retract_lift_below.get_at(extruder_id);
    const double lower_limit = this->m_config.retract_lift_above.get_at(extruder_id);
    if ((lower_limit > 0 && initial_elevation < lower_limit) ||
        (upper_limit > 0 && initial_elevation > upper_limit)) {
        can_be_flat = true;
    }
    const Points3 travel = (
        can_be_flat ?
        GCode::Impl::Travels::generate_flat_travel(xy_path.points, initial_elevation) :
        GCode::Impl::Travels::generate_travel_to_extrusion(
            xy_path,
            m_config,
            extruder_id,
            initial_elevation,
            m_travel_obstacle_tracker,
            scaled(m_origin)
        )
    );

    return wipe_retract_gcode + generate_travel_gcode(travel, comment, insert_gcode);
}

std::string GCodeGenerator::retract_and_wipe(bool toolchange, bool reset_e)
{
    std::string gcode;

    if (m_writer.extruder() == nullptr)
        return gcode;

    // wipe (if it's enabled for this extruder and we have a stored wipe path)
    if (EXTRUDER_CONFIG(wipe) && m_wipe.has_path()) {
        gcode += toolchange ? m_writer.retract_for_toolchange(true) : m_writer.retract(true);
        gcode += m_wipe.wipe(*this, toolchange);
    }

    /*  The parent class will decide whether we need to perform an actual retraction
        (the extruder might be already retracted fully or partially). We call these
        methods even if we performed wipe, since this will ensure the entire retraction
        length is honored in case wipe path was too short.  */
    gcode += toolchange ? m_writer.retract_for_toolchange() : m_writer.retract();

    if (reset_e) {
    gcode += m_writer.reset_e();
    }

    return gcode;
}

std::string GCodeGenerator::set_extruder(unsigned int extruder_id, double print_z)
{
    if (!m_writer.need_toolchange(extruder_id))
        return "";

    // if we are running a single-extruder setup, just set the extruder and return nothing
    if (!m_writer.multiple_extruders) {
        this->placeholder_parser().set("current_extruder", extruder_id);

        std::string gcode;
        // Append the filament start G-code.
        const std::string &start_filament_gcode = m_config.start_filament_gcode.get_at(extruder_id);
        if (! start_filament_gcode.empty()) {
            // Process the start_filament_gcode for the filament.
            DynamicConfig config;
            config.set_key_value("layer_num", new ConfigOptionInt(m_layer_index));
            config.set_key_value("layer_z",   new ConfigOptionFloat(this->writer().get_position().z() - m_config.z_offset.value));
            config.set_key_value("max_layer_z", new ConfigOptionFloat(m_max_layer_z));
            config.set_key_value("filament_extruder_id", new ConfigOptionInt(int(extruder_id)));
            gcode += this->placeholder_parser_process("start_filament_gcode", start_filament_gcode, extruder_id, &config);
            check_add_eol(gcode);
        }
        gcode += m_writer.toolchange(extruder_id);
        return gcode;
    }

    std::string gcode{};
    if (!this->m_config.complete_objects.value) {
        gcode += this->m_label_objects.maybe_stop_instance();
    }
    // prepend retraction on the current extruder
    gcode += this->retract_and_wipe(true);

    // Always reset the extrusion path, even if the tool change retract is set to zero.
    m_wipe.reset_path();

    if (m_writer.extruder() != nullptr) {
        // Process the custom end_filament_gcode.
        unsigned int        old_extruder_id     = m_writer.extruder()->id();
        const std::string  &end_filament_gcode  = m_config.end_filament_gcode.get_at(old_extruder_id);
        if (! end_filament_gcode.empty()) {
            DynamicConfig config;
            config.set_key_value("layer_num", new ConfigOptionInt(m_layer_index));
            config.set_key_value("layer_z",   new ConfigOptionFloat(m_writer.get_position().z() - m_config.z_offset.value));
            config.set_key_value("max_layer_z", new ConfigOptionFloat(m_max_layer_z));
            config.set_key_value("filament_extruder_id", new ConfigOptionInt(int(old_extruder_id)));
            gcode += placeholder_parser_process("end_filament_gcode", end_filament_gcode, old_extruder_id, &config);
            check_add_eol(gcode);
        }
    }


    // If ooze prevention is enabled, set current extruder to the standby temperature.
    if (m_ooze_prevention.enable && m_writer.extruder() != nullptr)
        gcode += m_ooze_prevention.pre_toolchange(*this);

    const std::string& toolchange_gcode = m_config.toolchange_gcode.value;
    std::string toolchange_gcode_parsed;

    // Process the custom toolchange_gcode. If it is empty, insert just a Tn command.
    if (!toolchange_gcode.empty()) {
        DynamicConfig config;
        config.set_key_value("previous_extruder", new ConfigOptionInt((int)(m_writer.extruder() != nullptr ? m_writer.extruder()->id() : -1 )));
        config.set_key_value("next_extruder",     new ConfigOptionInt((int)extruder_id));
        config.set_key_value("layer_num",         new ConfigOptionInt(m_layer_index));
        config.set_key_value("layer_z",           new ConfigOptionFloat(print_z));
        config.set_key_value("toolchange_z",      new ConfigOptionFloat(print_z));
        config.set_key_value("max_layer_z",       new ConfigOptionFloat(m_max_layer_z));
        toolchange_gcode_parsed = placeholder_parser_process("toolchange_gcode", toolchange_gcode, extruder_id, &config);
        gcode += toolchange_gcode_parsed;
        check_add_eol(gcode);
    }

    // We inform the writer about what is happening, but we may not use the resulting gcode.
    std::string toolchange_command = m_writer.toolchange(extruder_id);
    if (! custom_gcode_changes_tool(toolchange_gcode_parsed, m_writer.toolchange_prefix(), extruder_id))
        gcode += toolchange_command;
    else {
        // user provided his own toolchange gcode, no need to do anything
    }

    // Set the temperature if the wipe tower didn't (not needed for non-single extruder MM)
    if (m_config.single_extruder_multi_material && !m_config.wipe_tower) {
        int temp = (m_layer_index <= 0 ? m_config.first_layer_temperature.get_at(extruder_id) :
                                         m_config.temperature.get_at(extruder_id));

        gcode += m_writer.set_temperature(temp, false);
    }

    this->placeholder_parser().set("current_extruder", extruder_id);

    // Append the filament start G-code.
    const std::string &start_filament_gcode = m_config.start_filament_gcode.get_at(extruder_id);
    if (! start_filament_gcode.empty()) {
        // Process the start_filament_gcode for the new filament.
        DynamicConfig config;
        config.set_key_value("layer_num", new ConfigOptionInt(m_layer_index));
        config.set_key_value("layer_z",   new ConfigOptionFloat(this->writer().get_position().z() - m_config.z_offset.value));
        config.set_key_value("max_layer_z", new ConfigOptionFloat(m_max_layer_z));
        config.set_key_value("filament_extruder_id", new ConfigOptionInt(int(extruder_id)));
        gcode += this->placeholder_parser_process("start_filament_gcode", start_filament_gcode, extruder_id, &config);
        check_add_eol(gcode);
    }
    // Set the new extruder to the operating temperature.
    if (m_ooze_prevention.enable)
        gcode += m_ooze_prevention.post_toolchange(*this);

    // The position is now known after the tool change.
    this->last_position = std::nullopt;

    return gcode;
}
//B41
std::string GCodeGenerator::set_object_range(Print &print)
{
    std::string gcode;
    std::string        object_name;

    std::map<const ModelObject *, std::vector<const PrintInstance *>> model_object_to_print_instances;
    for (const PrintObject *po : print.objects())
        for (const PrintInstance &pi : po->instances())
            model_object_to_print_instances[pi.model_instance->get_object()].emplace_back(&pi);
    int unique_id = 0;
    std::unordered_map<const PrintInstance *, LabelData> tem_m_label_data;
    for (const auto &[model_object, print_instances] : model_object_to_print_instances) {
        const ModelObjectPtrs &model_objects = model_object->get_model()->objects;
        int                    object_id = int(std::find(model_objects.begin(), model_objects.end(), model_object) - model_objects.begin());
        for (const PrintInstance *const pi : print_instances) {
            bool object_has_more_instances = print_instances.size() > 1u;
            int  instance_id = int(std::find(model_object->instances.begin(), model_object->instances.end(), pi->model_instance) -
                                  model_object->instances.begin());

            // Now compose the name of the object and define whether indexing is 0 or 1-based.
            std::string name = model_object->name;
            ++object_id;
            ++instance_id;

            if (object_has_more_instances)
                name += " (Instance " + std::to_string(instance_id) + ")";
            const std::string banned = "-. \r\n\v\t\f";
            std::replace_if(
                name.begin(), name.end(), [&banned](char c) { return banned.find(c) != std::string::npos; }, '_');
            auto shift = pi->shift;

            ExPolygons           outline;
            const ModelObject *  mo = pi->model_instance->get_object();
            const ModelInstance *mi = pi->model_instance;
            for (const ModelVolume *v : mo->volumes) {
                Polygons vol_outline;
                vol_outline = project_mesh(v->mesh().its, mi->get_matrix() * v->get_matrix(), [] {});
                switch (v->type()) {
                case ModelVolumeType::MODEL_PART: outline = union_ex(outline, vol_outline); break;
                case ModelVolumeType::NEGATIVE_VOLUME: outline = diff_ex(outline, vol_outline); break;
                default:;
                }
            }

            // The projection may contain multiple polygons, which is not supported by Klipper.
            // When that happens, calculate and use a 2d convex hull instead.
            Polygon contour;
            if (outline.size() == 1u)
                contour = outline.front().contour;
            else
                contour = pi->model_instance->get_object()->convex_hull_2d(pi->model_instance->get_matrix());
            assert(!contour.empty());
            contour.douglas_peucker(50000.f);
            Point center = contour.centroid();
            char  buffer[64];
            std::replace(name.begin(), name.end(), ' ', '_');
            std::replace(name.begin(), name.end(), '#', '_');
            std::replace(name.begin(), name.end(), '*', '_');
            std::replace(name.begin(), name.end(), ':', '_');
            std::replace(name.begin(), name.end(), ';', '_');
            std::replace(name.begin(), name.end(), '\'', '_');
            gcode += (std::string("EXCLUDE_OBJECT_DEFINE NAME=") + name);
            std::snprintf(buffer, sizeof(buffer) - 1, " CENTER=%.3f,%.3f", unscale<float>(center[0]), unscale<float>(center[1]));
            gcode += buffer + std::string(" POLYGON=[");
            for (const Point &point : contour) {
                std::snprintf(buffer, sizeof(buffer) - 1, "[%.3f,%.3f],", unscale<float>(point[0]), unscale<float>(point[1]));
                gcode += buffer;
            }
            gcode.pop_back();
            gcode += "]\n";
            tem_m_label_data.emplace(pi, LabelData{name, unique_id});
            ++unique_id;
        }
    }
    m_label_data = tem_m_label_data;
    return gcode;
}

// convert a model-space scaled point into G-code coordinates
Point GCodeGenerator::gcode_to_point(const Vec2d &point) const
{
    Vec2d pt = point - m_origin;
    if (const Extruder *extruder = m_writer.extruder(); extruder)
        // This function may be called at the very start from toolchange G-code when the extruder is not assigned yet.
        pt += m_config.extruder_offset.get_at(extruder->id());
    return scaled<coord_t>(pt);
        
}

}   // namespace Slic3r
