#include <fast_float.h>
#include <boost/algorithm/string/predicate.hpp>
#include <cstring>
#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <cctype>
#include <cstdlib>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/GCode.hpp"
#include "GCodeWriter.hpp"
#include "libslic3r/GCode/PressureEqualizer.hpp"
#include "libslic3r/Exception.hpp"

namespace Slic3r {

static const std::string EXTRUSION_ROLE_TAG = ";_EXTRUSION_ROLE:";
static const std::string EXTRUDE_END_TAG = ";_EXTRUDE_END";
static const std::string EXTRUDE_SET_SPEED_TAG = ";_EXTRUDE_SET_SPEED";
static const std::string EXTERNAL_PERIMETER_TAG = ";_EXTERNAL_PERIMETER";

// Maximum segment length to split a long segment if the initial and the final flow rate differ.
// Smaller value means a smoother transition between two different flow rates.
static constexpr float max_segment_length = 5.f;

// For how many GCode lines back will adjust a flow rate from the latest line.
// Bigger values affect the GCode export speed a lot, and smaller values could
// affect how distant will be propagated a flow rate adjustment.
static constexpr int max_look_back_limit = 128;

// Max non-extruding XY distance (travel move) in mm between two continuous extrusions where we pretend
// it's all one continuous extrusion line. Above this distance, we assume extruder pressure hits 0
// This exists because often there are tiny travel moves between stuff like infill.
// Lines where some extruder pressure will remain (so we should equalize between these small travels).
static constexpr double max_ignored_gap_between_extruding_segments = 3.;

// Minimum feedrate change that will be emitted into the G-code.
// Changes below this value will not be emitted into the G-code to filter out tiny changes
// of feedrate and reduce the size of the G-code.
static constexpr float min_emitted_feedrate_change = 0.20f * 60.f;

PressureEqualizer::PressureEqualizer(const Slic3r::GCodeConfig &config) : m_use_relative_e_distances(config.use_relative_e_distances.value)
{
    // Preallocate some data, so that output_buffer.data() will return an empty string.
    output_buffer.assign(32, 0);
    output_buffer_length      = 0;
    output_buffer_prev_length = 0;

    m_current_extruder = 0;
    // Zero the position of the XYZE axes + the current feed
    memset(m_current_pos, 0, sizeof(float) * 5);
    m_current_extrusion_role = GCodeExtrusionRole::None;
    // Expect the first command to fill the nozzle (deretract).
    m_retracted = true;

    // Calculate filamet crossections for the multiple extruders.
    m_filament_crossections.clear();
    for (double r : config.filament_diameter.values) {
        double a = 0.25f * M_PI * r * r;
        m_filament_crossections.push_back(float(a));
    }

    // Volumetric rate of a 0.45mm x 0.2mm extrusion at 60mm/s XY movement: 0.45*0.2*60*60=5.4*60 = 324 mm^3/min
    // Volumetric rate of a 0.45mm x 0.2mm extrusion at 20mm/s XY movement: 0.45*0.2*20*60=1.8*60 = 108 mm^3/min
    // Slope of the volumetric rate, changing from 20mm/s to 60mm/s over 2 seconds: (5.4-1.8)*60*60/2=60*60*1.8 = 6480 mm^3/min^2 = 1.8 mm^3/s^2
    m_max_volumetric_extrusion_rate_slope_positive = float(config.max_volumetric_extrusion_rate_slope_positive.value) * 60.f * 60.f;
    m_max_volumetric_extrusion_rate_slope_negative = float(config.max_volumetric_extrusion_rate_slope_negative.value) * 60.f * 60.f;

    for (ExtrusionRateSlope &extrusion_rate_slope : m_max_volumetric_extrusion_rate_slopes) {
        extrusion_rate_slope.negative = m_max_volumetric_extrusion_rate_slope_negative;
        extrusion_rate_slope.positive = m_max_volumetric_extrusion_rate_slope_positive;
    }

    // Don't regulate the pressure before and after ironing.
    for (const GCodeExtrusionRole er : {GCodeExtrusionRole::Ironing}) {
        m_max_volumetric_extrusion_rate_slopes[size_t(er)].negative = 0;
        m_max_volumetric_extrusion_rate_slopes[size_t(er)].positive = 0;
    }

    opened_extrude_set_speed_block = false;

#ifdef PRESSURE_EQUALIZER_STATISTIC
    m_stat.reset();
#endif

#ifdef PRESSURE_EQUALIZER_DEBUG
    line_idx = 0;
#endif
}

void PressureEqualizer::process_layer(const std::string &gcode)
{
    if (!gcode.empty()) {
        const char *gcode_begin = gcode.c_str();
        while (*gcode_begin != 0) {
            // Find end of the line.
            const char *gcode_end = gcode_begin;
            // Slic3r always generates end of lines in a Unix style.
            for (; *gcode_end != 0 && *gcode_end != '\n'; ++gcode_end);

            m_gcode_lines.emplace_back();
            if (!this->process_line(gcode_begin, gcode_end, m_gcode_lines.back())) {
                // The line has to be forgotten. It contains comment marks, which shall be filtered out of the target g-code.
                m_gcode_lines.pop_back();
            }
            gcode_begin = gcode_end;
            if (*gcode_begin == '\n')
                ++gcode_begin;
        }
        assert(!this->opened_extrude_set_speed_block);
    }

    // At this point, we have an entire layer of gcode lines loaded into m_gcode_lines.
    // Now, we will split the mix of travels and extrusions into segments of continuous extrusions and process them.
    // We skip over large travels, and pretend that small ones are part of a continuous extrusion segment.
    for (auto current_extrusion_end_it = m_gcode_lines.cbegin(); current_extrusion_end_it != m_gcode_lines.cend();) {
        // Find beginning of next extrusion segment from current position.
        const auto current_extrusion_begin_it = std::find_if(current_extrusion_end_it, m_gcode_lines.cend(), [](const GCodeLine &line) {
                                                    return line.extruding();
                                                });

        // We start with extrusion length of zero.
        current_extrusion_end_it = current_extrusion_begin_it;

        // Inner loop extends the extrusion segment over small travel moves.
        while (current_extrusion_end_it != m_gcode_lines.cend()) {
            // Find the end of the current extrusion segment.
            const auto travel_begin_it = std::find_if(std::next(current_extrusion_end_it), m_gcode_lines.cend(), [](const GCodeLine &line) {
                                             return !line.extruding();
                                         });

            current_extrusion_end_it = std::prev(travel_begin_it);

            const auto next_extrusion_segment_it = advance_segment_beyond_small_gap(current_extrusion_end_it);
            if (std::distance(current_extrusion_end_it, next_extrusion_segment_it) > 0) {
                // Extend the continuous line over the small gap.
                current_extrusion_end_it = next_extrusion_segment_it;
                continue; // Keep going, loop again to find the new end of extrusion segment.
            } else {
                break; // Gap to next extrude is too big, stop looking forward. We've found the end of this segment.
            }
        }

        // Now, run the pressure equalizer across the segment like a streamroller.
        // It operates on a sliding window that moves forward across gcode line by line.
        const std::ptrdiff_t current_extrusion_begin_idx = std::distance(m_gcode_lines.cbegin(), current_extrusion_begin_it);
        for (auto current_line_it = current_extrusion_begin_it; current_line_it != current_extrusion_end_it; ++current_line_it) {
            const std::ptrdiff_t current_line_idx = std::distance(m_gcode_lines.cbegin(), current_line_it);

            // Feed pressure equalizer past lines, going back to max_look_back_limit (or start of segment).
            const size_t start_idx = size_t(std::max<std::ptrdiff_t>(current_extrusion_begin_idx, current_line_idx - max_look_back_limit));
            adjust_volumetric_rate(start_idx, size_t(current_line_idx));
        }

        // Current extrusion is all done processing so advance beyond it for the next loop.
        if (current_extrusion_end_it != m_gcode_lines.cend())
            ++current_extrusion_end_it;
    }
}

PressureEqualizer::GCodeLinesConstIt PressureEqualizer::advance_segment_beyond_small_gap(const GCodeLinesConstIt &last_extruding_line_it) const {
    // This should only be run on the last extruding line before a gap.
    assert(last_extruding_line_it != m_gcode_lines.cend() && last_extruding_line_it->extruding());
    double travel_distance = 0.;
    // Start at the beginning of a gap, advance till extrusion found or gap too big.
    for (auto current_line_it = std::next(last_extruding_line_it); current_line_it != m_gcode_lines.cend(); ++current_line_it) {
        // Started extruding again! Return segment extension.
        if (current_line_it->extruding())
            return current_line_it;

        travel_distance += current_line_it->dist_xy();
        // Gap too big, don't extend segment.
        if (travel_distance > max_ignored_gap_between_extruding_segments)
            return last_extruding_line_it;
    }

    // Looped until the end of the layer and couldn't extend extrusion.
    return last_extruding_line_it;
}

LayerResult PressureEqualizer::process_layer(LayerResult &&input)
{
    const bool   is_first_layer       = m_layer_results.empty();
    const size_t next_layer_first_idx = m_gcode_lines.size();

    if (!input.nop_layer_result) {
        this->process_layer(input.gcode);
        input.gcode.clear(); // GCode is already processed, so it isn't needed to store it.
        m_layer_results.emplace(new LayerResult(input));
    }

    if (is_first_layer) // Buffer previous input result and output NOP.
        return LayerResult::make_nop_layer_result();

    // Export previous layer.
    LayerResult *prev_layer_result = m_layer_results.front();
    m_layer_results.pop();

    output_buffer_length      = 0;
    output_buffer_prev_length = 0;
    for (size_t line_idx = 0; line_idx < next_layer_first_idx; ++line_idx)
        output_gcode_line(line_idx);
    m_gcode_lines.erase(m_gcode_lines.begin(), m_gcode_lines.begin() + int(next_layer_first_idx));

    if (output_buffer_length > 0)
        prev_layer_result->gcode = std::string(output_buffer.data());

    assert(!input.nop_layer_result || m_layer_results.empty());
    LayerResult out = *prev_layer_result;
    delete prev_layer_result;
    return out;
}

// Is a white space?
static inline bool is_ws(const char c) { return c == ' ' || c == '\t'; }
// Is it an end of line? Consider a comment to be an end of line as well.
static inline bool is_eol(const char c) { return c == 0 || c == '\r' || c == '\n' || c == ';'; }
// Is it a white space or end of line?
static inline bool is_ws_or_eol(const char c) { return is_ws(c) || is_eol(c); }

// Eat whitespaces.
static void eatws(const char *&line)
{
    while (is_ws(*line))
        ++line;
}

// Parse an int starting at the current position of a line.
// If succeeded, the line pointer is advanced.
static inline int parse_int(const char *&line)
{
    char *endptr = nullptr;
    long result = strtol(line, &endptr, 10);
    if (endptr == nullptr || !is_ws_or_eol(*endptr))
        throw Slic3r::InvalidArgument("PressureEqualizer: Error parsing an int");
    line = endptr;
    return int(result);
}

float string_to_float_decimal_point(const char *line, const size_t str_len, size_t* pos)
{
    float out;
    size_t p = fast_float::from_chars(line, line + str_len, out).ptr - line;
    if (pos)
        *pos = p;
    return out;
}

// Parse an int starting at the current position of a line.
// If succeeded, the line pointer is advanced.
static inline float parse_float(const char *&line, const size_t line_length)
{
    size_t endptr = 0;
    auto   result = string_to_float_decimal_point(line, line_length, &endptr);
    if (endptr == 0 || !is_ws_or_eol(*(line + endptr)))
        throw Slic3r::RuntimeError("PressureEqualizer: Error parsing a float");
    line = line + endptr;
    return result;
}

bool PressureEqualizer::process_line(const char *line, const char *line_end, GCodeLine &buf)
{
    const size_t len = line_end - line;
    if (strncmp(line, EXTRUSION_ROLE_TAG.data(), EXTRUSION_ROLE_TAG.length()) == 0) {
        line += EXTRUSION_ROLE_TAG.length();
        int role = atoi(line);
        m_current_extrusion_role = GCodeExtrusionRole(role);
#ifdef PRESSURE_EQUALIZER_DEBUG
        ++line_idx;
#endif
        return false;
    }

    // Set the type, copy the line to the buffer.
    buf.type = GCODELINETYPE_OTHER;
    buf.modified = false;
    if (buf.raw.size() < len + 1)
        buf.raw.assign(line, line + len + 1);
    else
        memcpy(buf.raw.data(), line, len);
    buf.raw[len] = 0;
    buf.raw_length = len;

    memcpy(buf.pos_start, m_current_pos, sizeof(float)*5);
    memcpy(buf.pos_end, m_current_pos, sizeof(float)*5);
    memset(buf.pos_provided, 0, 5);

    buf.volumetric_extrusion_rate = 0.f;
    buf.volumetric_extrusion_rate_start = 0.f;
    buf.volumetric_extrusion_rate_end = 0.f;
    buf.max_volumetric_extrusion_rate_slope_positive = 0.f;
    buf.max_volumetric_extrusion_rate_slope_negative = 0.f;
    buf.extrusion_role = m_current_extrusion_role;

    std::string str_line(line, line_end);
    const bool found_extrude_set_speed_tag = boost::contains(str_line, EXTRUDE_SET_SPEED_TAG);
    const bool found_extrude_end_tag = boost::contains(str_line, EXTRUDE_END_TAG);
    assert(!found_extrude_set_speed_tag || !found_extrude_end_tag);

    if (found_extrude_set_speed_tag)
        this->opened_extrude_set_speed_block = true;
    else if (found_extrude_end_tag)
        this->opened_extrude_set_speed_block = false;

    // Parse the G-code line, store the result into the buf.
    switch (toupper(*line ++)) {
    case 'G': {
        int gcode = -1;
        try {
            gcode = parse_int(line);
        } catch (Slic3r::InvalidArgument &) {
            // Ignore invalid GCodes.
            eatws(line);
            break;
        }

        assert(gcode != -1);
        eatws(line);
        switch (gcode) {
        case 0:
        case 1:
        {
            // G0, G1: A FFF 3D printer does not make a difference between the two.
            buf.adjustable_flow = this->opened_extrude_set_speed_block;
            float new_pos[5];
            memcpy(new_pos, m_current_pos, sizeof(float)*5);
            bool  changed[5] = { false, false, false, false, false };
            while (!is_eol(*line)) {
                const char axis = toupper(*line++);
                int  i = -1;
                switch (axis) {
                case 'X':
                case 'Y':
                case 'Z':
                    i = axis - 'X';
                    break;
                case 'E':
                    i = 3;
                    break;
                case 'F':
                    i = 4;
                    break;
                default:
                    break;
                }
                if (i != -1) {
                    buf.pos_provided[i] = true;
                    new_pos[i] = parse_float(line, line_end - line);
                    if (i == 3 && m_use_relative_e_distances)
                        new_pos[i] += m_current_pos[i];
                    changed[i] = new_pos[i] != m_current_pos[i];
                    eatws(line);
                }
            }
            if (changed[3]) {
                // Extrusion, retract or unretract.
                float diff = new_pos[3] - m_current_pos[3];
                if (diff < 0) {
                    buf.type = GCODELINETYPE_RETRACT;
                    m_retracted = true;
                } else if (! changed[0] && ! changed[1] && ! changed[2]) {
                    // assert(m_retracted);
                    buf.type = GCODELINETYPE_UNRETRACT;
                    m_retracted = false;
                } else {
                    assert(changed[0] || changed[1]);
                    // Moving in XY plane.
                    buf.type = GCODELINETYPE_EXTRUDE;
                    // Calculate the volumetric extrusion rate.
                    float diff[4];
                    for (size_t i = 0; i < 4; ++ i)
                        diff[i] = new_pos[i] - m_current_pos[i];
                    // volumetric extrusion rate = A_filament * F_xyz * L_e / L_xyz [mm^3/min]
                    float len2 = diff[0]*diff[0]+diff[1]*diff[1]+diff[2]*diff[2];
                    float rate = m_filament_crossections[m_current_extruder] * new_pos[4] * sqrt((diff[3]*diff[3])/len2);
                    buf.volumetric_extrusion_rate       = rate;
                    buf.volumetric_extrusion_rate_start = rate;
                    buf.volumetric_extrusion_rate_end   = rate;

#ifdef PRESSURE_EQUALIZER_STATISTIC
                    m_stat.update(rate, sqrt(len2));
#endif
#ifdef PRESSURE_EQUALIZER_DEBUG
                    if (rate < 40.f) {
                        printf("Extremely low flow rate: %f. Line %d, Length: %f, extrusion: %f Old position: (%f, %f, %f), new position: (%f, %f, %f)\n",
                               rate, int(line_idx), sqrt(len2), sqrt((diff[3] * diff[3]) / len2), m_current_pos[0], m_current_pos[1], m_current_pos[2],
                               new_pos[0], new_pos[1], new_pos[2]);
                    }
#endif
                }
            } else if (changed[0] || changed[1] || changed[2]) {
                // Moving without extrusion.
                buf.type = GCODELINETYPE_MOVE;
            }
            memcpy(m_current_pos, new_pos, sizeof(float) * 5);
            break;
        }
        case 92:
        {
            // G92 : Set Position
            // Set a logical coordinate position to a new value without actually moving the machine motors.
            // Which axes to set?
            while (!is_eol(*line)) {
                const char axis = toupper(*line++);
                switch (axis) {
                case 'X':
                case 'Y':
                case 'Z':
                    m_current_pos[axis - 'X'] = (!is_ws_or_eol(*line)) ? parse_float(line, line_end - line) : 0.f;
                    break;
                case 'E':
                    m_current_pos[3] = (!is_ws_or_eol(*line)) ? parse_float(line, line_end - line) : 0.f;
                    break;
                default:
                    break;
                }
                eatws(line);
            }
            break;
        }
        case 10:
        case 22:
            // Firmware retract.
            buf.type = GCODELINETYPE_RETRACT;
            m_retracted = true;
            break;
        case 11:
        case 23:
            // Firmware unretract.
            buf.type = GCODELINETYPE_UNRETRACT;
            m_retracted = false;
            break;
        default:
            // Ignore the rest.
            break;
        }
        break;
    }
    case 'M': {
        eatws(line);
        // Ignore the rest of the M-codes.
        break;
    }
    case 'T':
    {
        // Activate an extruder head.
        int new_extruder = -1;
        try {
            new_extruder = parse_int(line);
        } catch (Slic3r::InvalidArgument &) {
            // Ignore invalid GCodes starting with T.
            eatws(line);
            break;
        }
        assert(new_extruder != -1);

        if (new_extruder != int(m_current_extruder)) {
            m_current_extruder = new_extruder;
            m_retracted = true;
            buf.type = GCODELINETYPE_TOOL_CHANGE;
        } else {
            buf.type = GCODELINETYPE_NOOP;
        }
        break;
    }
    }

    buf.extruder_id = m_current_extruder;
    memcpy(buf.pos_end, m_current_pos, sizeof(float)*5);
#ifdef PRESSURE_EQUALIZER_DEBUG
    ++line_idx;
#endif
    return true;
}

void PressureEqualizer::GCodeLine::update_end_position(const float *position_end, const bool *position_provided_original)
{
    assert(position_end != nullptr);
    if (position_end == nullptr)
        return;

    for (int i = 0; i < 4; ++i) {
        this->pos_end[i]      = position_end[i];
        this->pos_provided[i] = position_provided_original[i] || (this->pos_end[i] != this->pos_start[i]);
    }
}

void PressureEqualizer::GCodeLine::update_end_position(const float *position_start, const float *position_end, const float t, const bool *position_provided_original)
{
    assert(position_start != nullptr && position_end != nullptr);
    if (position_start == nullptr || position_end == nullptr)
        return;

    for (size_t i = 0; i < 4; ++i) {
        this->pos_end[i]      = position_start[i] + (position_end[i] - position_start[i]) * t;
        this->pos_provided[i] = position_provided_original[i] || (this->pos_end[i] != this->pos_start[i]);
    }
}

void PressureEqualizer::output_gcode_line(const size_t line_idx)
{
    GCodeLine &line = m_gcode_lines[line_idx];
    if (!line.modified) {
        push_to_output(line.raw.data(), line.raw_length, true);
        return;
    }

    // The line was modified.
    // Find the comment.
    const char *comment = line.raw.data();
    while (*comment != ';' && *comment != 0) ++comment;
    if (*comment != ';')
        comment = nullptr;

    // Emit the line with lowered extrusion rates.
    const float l              = line.dist_xyz();
    const float feedrate_start = line.volumetric_extrusion_rate_start * line.feedrate() / line.volumetric_extrusion_rate;
    const float feedrate_end   = line.volumetric_extrusion_rate_end   * line.feedrate() / line.volumetric_extrusion_rate;
    const float feedrate_avg   = 0.5f * (feedrate_start + feedrate_end);
    if (std::abs(feedrate_avg - line.pos_end[4]) <= min_emitted_feedrate_change) {
        // The average feedrate is close to the original feedrate, so we emit the line with the original feedrate.
        push_line_to_output(line_idx, line.pos_end[4], comment);
    } else if (auto nSegments = size_t(ceil(l / max_segment_length)); nSegments == 1) { // Just update this segment.
        push_line_to_output(line_idx, line.feedrate() * line.volumetric_correction_avg(), comment);
    } else {
        bool accelerating = line.volumetric_extrusion_rate_start < line.volumetric_extrusion_rate_end;
        // Update the initial and final feed rate values.
        line.pos_start[4] = feedrate_start;
        line.pos_end  [4] = feedrate_end;

        // Limiting volumetric extrusion rate slope for this segment.
        float max_volumetric_extrusion_rate_slope = accelerating ? line.max_volumetric_extrusion_rate_slope_positive :
                                                                   line.max_volumetric_extrusion_rate_slope_negative;
        // Total time for the segment, corrected for the possibly lowered volumetric feed rate,
        // if accelerating / decelerating over the complete segment.
        float t_total = line.dist_xyz() / feedrate_avg;
        // Time of the acceleration / deceleration part of the segment, if accelerating / decelerating
        // with the maximum volumetric extrusion rate slope.
        float t_acc    = 0.5f * (line.volumetric_extrusion_rate_start + line.volumetric_extrusion_rate_end) / max_volumetric_extrusion_rate_slope;
        float l_acc    = l;
        float l_steady = 0.f;
        if (t_acc < t_total) {
            // One may achieve higher print speeds if part of the segment is not speed limited.
            l_acc    = t_acc * feedrate_avg;
            l_steady = l - l_acc;
            if (l_steady < 0.5f * max_segment_length) {
                l_acc    = l;
                l_steady = 0.f;
            } else
                nSegments = size_t(ceil(l_acc / max_segment_length));
        }

        float pos_start[5];
        float pos_end[5];
        float pos_end2[4];
        memcpy(pos_start, line.pos_start, sizeof(float) * 5);
        memcpy(pos_end, line.pos_end, sizeof(float) * 5);

        bool pos_provided_original[5];
        memcpy(pos_provided_original, line.pos_provided, sizeof(bool) * 5);
        if (l_steady > 0.f) {
            // There will be a steady feed segment emitted.
            if (accelerating) {
                // Prepare the final steady feed rate segment.
                memcpy(pos_end2, pos_end, sizeof(float)*4);
                float t = l_acc / l;
                for (int i = 0; i < 4; ++ i) {
                    pos_end[i] = pos_start[i] + (pos_end[i] - pos_start[i]) * t;
                }
            } else {
                // Emit the steady feed rate segment.
                const float t = l_steady / l;
                line.update_end_position(pos_start, pos_end, t, pos_provided_original);
                push_line_to_output(line_idx, pos_start[4], comment);
                comment = nullptr;

                float new_pos_start_feedrate = pos_start[4];

                memcpy(line.pos_start, line.pos_end, sizeof(float)*5);
                memcpy(pos_start, line.pos_end, sizeof(float)*5);

                line.pos_start[4] = new_pos_start_feedrate;
                pos_start[4] = new_pos_start_feedrate;
            }
        }

        // Split the segment into pieces.
        for (size_t i = 1; i < nSegments; ++ i) {
            const float t = float(i) / float(nSegments);
            line.update_end_position(pos_start, pos_end, t, pos_provided_original);

            // Interpolate the feed rate at the center of the segment.
            push_line_to_output(line_idx, pos_start[4] + (pos_end[4] - pos_start[4]) * (float(i) - 0.5f) / float(nSegments), comment);
            comment = nullptr;
            memcpy(line.pos_start, line.pos_end, sizeof(float)*5);
        }

        if (l_steady > 0.f && accelerating) {
            line.update_end_position(pos_end2, pos_provided_original);
            push_line_to_output(line_idx, pos_end[4], comment);
        } else {
            line.update_end_position(pos_end, pos_provided_original);
            push_line_to_output(line_idx, pos_end[4], comment);
        }
    }
}

void PressureEqualizer::adjust_volumetric_rate(const size_t first_line_idx, const size_t last_line_idx)
{
    // Don't bother adjusting volumetric rate if there's no gcode to adjust.
    if (last_line_idx <= first_line_idx || last_line_idx - first_line_idx < 2)
        return;

    size_t line_idx = last_line_idx;
    if (line_idx == first_line_idx || !m_gcode_lines[line_idx].extruding())
        // Nothing to do, the last move is not extruding.
        return;

    std::array<float, size_t(GCodeExtrusionRole::Count)> feedrate_per_extrusion_role{};
    feedrate_per_extrusion_role.fill(std::numeric_limits<float>::max());
    feedrate_per_extrusion_role[int(m_gcode_lines[line_idx].extrusion_role)] = m_gcode_lines[line_idx].volumetric_extrusion_rate_start;

    while (line_idx != first_line_idx) {
        size_t idx_prev = line_idx - 1;
        for (; !m_gcode_lines[idx_prev].extruding() && idx_prev != first_line_idx; --idx_prev);
        if (!m_gcode_lines[idx_prev].extruding())
            break;
        // Don't decelerate before ironing.
        if (m_gcode_lines[line_idx].extrusion_role == GCodeExtrusionRole::Ironing) {
            line_idx = idx_prev;
            continue;
        }
        // Volumetric extrusion rate at the start of the succeding segment.
        float rate_succ = m_gcode_lines[line_idx].volumetric_extrusion_rate_start;
        // What is the gradient of the extrusion rate between idx_prev and idx?
        line_idx        = idx_prev;
        GCodeLine &line = m_gcode_lines[line_idx];

        for (size_t iRole = 1; iRole < size_t(GCodeExtrusionRole::Count); ++ iRole) {
            const float &rate_slope = m_max_volumetric_extrusion_rate_slopes[iRole].negative;
            if (rate_slope == 0 || feedrate_per_extrusion_role[iRole] == std::numeric_limits<float>::max())
                continue; // The negative rate is unlimited or the rate for GCodeExtrusionRole iRole is unlimited.

            float rate_end = feedrate_per_extrusion_role[iRole];
            if (iRole == size_t(line.extrusion_role) && rate_succ < rate_end)
                // Limit by the succeeding volumetric flow rate.
                rate_end = rate_succ;

            // Don't alter the flow rate for these extrusion types.
            if (!line.adjustable_flow || line.extrusion_role == GCodeExtrusionRole::BridgeInfill || line.extrusion_role == GCodeExtrusionRole::Ironing) {
                rate_end = line.volumetric_extrusion_rate_end;
            } else if (line.volumetric_extrusion_rate_end > rate_end) {
                line.volumetric_extrusion_rate_end = rate_end;
                line.max_volumetric_extrusion_rate_slope_negative = rate_slope;
                line.modified = true;
            } else if (iRole == size_t(line.extrusion_role)) {
                rate_end = line.volumetric_extrusion_rate_end;
            } else {
                // Use the original, 'floating' extrusion rate as a starting point for the limiter.
            }

            if (line.adjustable_flow) {
                float rate_start = sqrt(rate_end * rate_end + 2 * line.volumetric_extrusion_rate * line.dist_xyz() * rate_slope / line.feedrate());
                if (rate_start < line.volumetric_extrusion_rate_start) {
                    // Limit the volumetric extrusion rate at the start of this segment due to a segment
                    // of ExtrusionType iRole, which will be extruded in the future.
                    line.volumetric_extrusion_rate_start = rate_start;
                    line.max_volumetric_extrusion_rate_slope_negative = rate_slope;
                    line.modified = true;
                }
            }

            // Don't store feed rate for ironing.
            if (line.extrusion_role != GCodeExtrusionRole::Ironing)
                feedrate_per_extrusion_role[iRole] = line.volumetric_extrusion_rate_start;
        }
    }

    feedrate_per_extrusion_role.fill(std::numeric_limits<float>::max());
    feedrate_per_extrusion_role[size_t(m_gcode_lines[line_idx].extrusion_role)] = m_gcode_lines[line_idx].volumetric_extrusion_rate_end;

    assert(m_gcode_lines[line_idx].extruding());
    while (line_idx != last_line_idx) {
        size_t idx_next = line_idx + 1;
        for (; !m_gcode_lines[idx_next].extruding() && idx_next != last_line_idx; ++idx_next);
        if (!m_gcode_lines[idx_next].extruding())
            break;
        // Don't accelerate after ironing.
        if (m_gcode_lines[line_idx].extrusion_role == GCodeExtrusionRole::Ironing) {
            line_idx = idx_next;
            continue;
        }
        float rate_prec = m_gcode_lines[line_idx].volumetric_extrusion_rate_end;
        // What is the gradient of the extrusion rate between idx_prev and idx?
        line_idx = idx_next;
        GCodeLine &line = m_gcode_lines[line_idx];

        for (size_t iRole = 1; iRole < size_t(GCodeExtrusionRole::Count); ++ iRole) {
            const float &rate_slope = m_max_volumetric_extrusion_rate_slopes[iRole].positive;
            if (rate_slope == 0 || feedrate_per_extrusion_role[iRole] == std::numeric_limits<float>::max())
                continue; // The positive rate is unlimited or the rate for GCodeExtrusionRole iRole is unlimited.

            float rate_start = feedrate_per_extrusion_role[iRole];
            // Don't alter the flow rate for these extrusion types.
            if (!line.adjustable_flow || line.extrusion_role == GCodeExtrusionRole::BridgeInfill || line.extrusion_role == GCodeExtrusionRole::Ironing) {
                rate_start = line.volumetric_extrusion_rate_start;
            } else if (iRole == size_t(line.extrusion_role) && rate_prec < rate_start)
                rate_start = rate_prec;
            if (line.volumetric_extrusion_rate_start > rate_start) {
                line.volumetric_extrusion_rate_start = rate_start;
                line.max_volumetric_extrusion_rate_slope_positive = rate_slope;
                line.modified = true;
            } else if (iRole == size_t(line.extrusion_role)) {
                rate_start = line.volumetric_extrusion_rate_start;
            } else {
                // Use the original, 'floating' extrusion rate as a starting point for the limiter.
            }

            if (line.adjustable_flow) {
                float rate_end = sqrt(rate_start * rate_start + 2 * line.volumetric_extrusion_rate * line.dist_xyz() * rate_slope / line.feedrate());
                if (rate_end < line.volumetric_extrusion_rate_end) {
                    // Limit the volumetric extrusion rate at the start of this segment due to a segment
                    // of ExtrusionType iRole, which was extruded before.
                    line.volumetric_extrusion_rate_end                = rate_end;
                    line.max_volumetric_extrusion_rate_slope_positive = rate_slope;
                    line.modified                                     = true;
                }
            }

            // Don't store feed rate for ironing
            if (line.extrusion_role != GCodeExtrusionRole::Ironing)
                feedrate_per_extrusion_role[iRole] = line.volumetric_extrusion_rate_end;
        }
    }
}

inline void PressureEqualizer::push_to_output(GCodeG1Formatter &formatter)
{
    return this->push_to_output(formatter.string(), false);
}

inline void PressureEqualizer::push_to_output(const std::string &text, bool add_eol)
{
    return this->push_to_output(text.data(), text.size(), add_eol);
}

inline void PressureEqualizer::push_to_output(const char *text, const size_t len, bool add_eol)
{
    // New length of the output buffer content.
    size_t len_new = output_buffer_length + len + 1;
    if (add_eol)
        ++len_new;

    // Resize the output buffer to a power of 2 higher than the required memory.
    if (output_buffer.size() < len_new) {
        size_t v = len_new;
        // Compute the next highest power of 2 of 32-bit v
        // http://graphics.stanford.edu/~seander/bithacks.html
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;
        output_buffer.resize(v);
    }

    // Copy the text to the output.
    if (len != 0) {
        memcpy(output_buffer.data() + output_buffer_length, text, len);
        this->output_buffer_prev_length = this->output_buffer_length;
        output_buffer_length += len;
    }
    if (add_eol)
        output_buffer[output_buffer_length++] = '\n';
    output_buffer[output_buffer_length] = 0;
}

inline bool is_just_line_with_extrude_set_speed_tag(const std::string &line)
{
    if (line.empty() && !boost::starts_with(line, "G1 ") && !boost::ends_with(line, EXTRUDE_SET_SPEED_TAG))
        return false;

    const char       *p_line   = line.data() + 3;
    const char *const line_end = line.data() + line.length() - 1;
    while (!is_eol(*p_line)) {
        if (toupper(*p_line++) == 'F')
            break;
        else
            return false;
    }
    parse_float(p_line, line_end - p_line);
    eatws(p_line);
    p_line += EXTRUDE_SET_SPEED_TAG.length();
    return p_line <= line_end && is_eol(*p_line);
}

void PressureEqualizer::push_line_to_output(const size_t line_idx, float new_feedrate, const char *comment) {
    // Ensure the minimum feedrate will not be below 1 mm/s.
    new_feedrate = std::max(60.f, new_feedrate);

    const GCodeLine &line = m_gcode_lines[line_idx];
    if (line_idx > 0 && output_buffer_length > 0) {
        const std::string prev_line_str = std::string(output_buffer.begin() + int(this->output_buffer_prev_length),
                                                      output_buffer.begin() + int(this->output_buffer_length) + 1);
        if (is_just_line_with_extrude_set_speed_tag(prev_line_str))
            this->output_buffer_length = this->output_buffer_prev_length; // Remove the last line because it only sets the speed for an empty block of g-code lines, so it is useless.
        else
            push_to_output(EXTRUDE_END_TAG.data(), EXTRUDE_END_TAG.length(), true);
    } else
        push_to_output(EXTRUDE_END_TAG.data(), EXTRUDE_END_TAG.length(), true);

    GCodeG1Formatter feedrate_formatter;
    feedrate_formatter.emit_f(new_feedrate);
    feedrate_formatter.emit_string(std::string(EXTRUDE_SET_SPEED_TAG.data(), EXTRUDE_SET_SPEED_TAG.length()));
    if (line.extrusion_role == GCodeExtrusionRole::ExternalPerimeter)
        feedrate_formatter.emit_string(std::string(EXTERNAL_PERIMETER_TAG.data(), EXTERNAL_PERIMETER_TAG.length()));
    push_to_output(feedrate_formatter);

    GCodeG1Formatter extrusion_formatter;
    for (size_t axis_idx = 0; axis_idx < 3; ++axis_idx)
        if (line.pos_provided[axis_idx])
            extrusion_formatter.emit_axis(char('X' + axis_idx), line.pos_end[axis_idx], GCodeFormatter::XYZF_EXPORT_DIGITS);
    extrusion_formatter.emit_axis('E', m_use_relative_e_distances ? (line.pos_end[3] - line.pos_start[3]) : line.pos_end[3], GCodeFormatter::E_EXPORT_DIGITS);

    if (comment != nullptr)
        extrusion_formatter.emit_string(std::string(comment));

    push_to_output(extrusion_formatter);
}

} // namespace Slic3r
