#ifndef slic3r_FillConcentricInternal_hpp_
#define slic3r_FillConcentricInternal_hpp_

#include "FillBase.hpp"

namespace Slic3r {

class FillConcentricInternal : public Fill
{
public:
    ~FillConcentricInternal() override = default;
    void fill_surface_extrusion(const Surface *   surface,
                                const FillParams &params,
                                Polylines &       polylines,
                                ThickPolylines &  thick_polylines) override;
    void variable_width(const ThickPolylines &polylines, ExtrusionRole role, const Flow &flow, std::vector<ExtrusionEntity *> &out)
    {
        const float tolerance = float(scale_(0.05));
        for (const ThickPolyline &p : polylines) {
            ExtrusionPaths paths = thick_polyline_to_extrusion_paths_2(p, role, flow, tolerance);
            // Append paths to collection.
            if (!paths.empty()) {
                if (paths.front().first_point() == paths.back().last_point())
                    out.emplace_back(new ExtrusionLoop(std::move(paths)));
                else {
                    for (ExtrusionPath &path : paths)
                        out.emplace_back(new ExtrusionPath(std::move(path)));
                }
            }
        }
    }
    ExtrusionPaths thick_polyline_to_extrusion_paths_2(const ThickPolyline &thick_polyline,
                                                       ExtrusionRole        role,
                                                       const Flow &         flow,
                                                       const float          tolerance)
    {
        ExtrusionPaths paths;
        ExtrusionPath  path(role);
        ThickLines     lines = thick_polyline.thicklines();

        size_t start_index = 0;
        double max_width, min_width;

        for (int i = 0; i < (int) lines.size(); ++i) {
            const ThickLine &line = lines[i];

            if (i == 0) {
                max_width = line.a_width;
                min_width = line.a_width;
            }

            const coordf_t line_len = line.length();
            if (line_len < SCALED_EPSILON)
                continue;

            double thickness_delta = std::max(fabs(max_width - line.b_width), fabs(min_width - line.b_width));
            if (thickness_delta > tolerance) {
                if (start_index != i) {
                    path          = ExtrusionPath(role);
                    double length = lines[start_index].length();
                    double sum    = lines[start_index].length() * 0.5 * (lines[start_index].a_width + lines[start_index].b_width);
                    path.polyline.append(lines[start_index].a);
                    for (int idx = start_index + 1; idx < i; idx++) {
                        length += lines[idx].length();
                        sum += lines[idx].length() * 0.5 * (lines[idx].a_width + lines[idx].b_width);
                        path.polyline.append(lines[idx].a);
                    }
                    path.polyline.append(lines[i].a);
                    if (length > SCALED_EPSILON) {
                        double w        = sum / length;
                        Flow   new_flow = flow.with_width(unscale<float>(w) + flow.height() * float(1. - 0.25 * PI));

                        // path.mm3_per_mm = new_flow.mm3_per_mm();
                        path.set_mm3_per_mm(new_flow.mm3_per_mm());
                        // path.width      = new_flow.width();
                        path.set_width(new_flow.width());
                        // path.height     = new_flow.height();
                        path.set_height(new_flow.height());
                        paths.emplace_back(std::move(path));
                    }
                }

                start_index     = i;
                max_width       = line.a_width;
                min_width       = line.a_width;
                thickness_delta = fabs(line.a_width - line.b_width);
                if (thickness_delta > tolerance) {
                    const unsigned int    segments = (unsigned int) ceil(thickness_delta / tolerance);
                    const coordf_t        seg_len  = line_len / segments;
                    Points                pp;
                    std::vector<coordf_t> width;
                    {
                        pp.push_back(line.a);
                        width.push_back(line.a_width);
                        for (size_t j = 1; j < segments; ++j) {
                            pp.push_back(
                                (line.a.cast<double>() + (line.b - line.a).cast<double>().normalized() * (j * seg_len)).cast<coord_t>());

                            coordf_t w = line.a_width + (j * seg_len) * (line.b_width - line.a_width) / line_len;
                            width.push_back(w);
                            width.push_back(w);
                        }
                        pp.push_back(line.b);
                        width.push_back(line.b_width);

                        assert(pp.size() == segments + 1u);
                        assert(width.size() == segments * 2);
                    }

                    lines.erase(lines.begin() + i);
                    for (size_t j = 0; j < segments; ++j) {
                        ThickLine new_line(pp[j], pp[j + 1]);
                        new_line.a_width = width[2 * j];
                        new_line.b_width = width[2 * j + 1];
                        lines.insert(lines.begin() + i + j, new_line);
                    }
                    --i;
                    continue;
                }
            } else {
                max_width = std::max(max_width, std::max(line.a_width, line.b_width));
                min_width = std::min(min_width, std::min(line.a_width, line.b_width));
            }
        }
        size_t final_size = lines.size();
        if (start_index < final_size) {
            path          = ExtrusionPath(role);
            double length = lines[start_index].length();
            double sum    = lines[start_index].length() * lines[start_index].a_width;
            path.polyline.append(lines[start_index].a);
            for (int idx = start_index + 1; idx < final_size; idx++) {
                length += lines[idx].length();
                sum += lines[idx].length() * lines[idx].a_width;
                path.polyline.append(lines[idx].a);
            }
            path.polyline.append(lines[final_size - 1].b);
            if (length > SCALED_EPSILON) {
                double w        = sum / length;
                Flow   new_flow = flow.with_width(unscale<float>(w) + flow.height() * float(1. - 0.25 * PI));
                // path.mm3_per_mm = new_flow.mm3_per_mm();
                path.set_mm3_per_mm(new_flow.mm3_per_mm());
                // path.width      = new_flow.width();
                path.set_width(new_flow.width());
                // path.height     = new_flow.height();
                path.set_height(new_flow.height());
                paths.emplace_back(std::move(path));
            }
        }

        return paths;
    }
    bool is_self_crossing() override { return false; }

protected:
    Fill *clone() const override { return new FillConcentricInternal(*this); };
    bool  no_sort() const override { return true; }

    const PrintConfig *      print_config        = nullptr;
    const PrintObjectConfig *print_object_config = nullptr;

    friend class Layer;
};

} // namespace Slic3r

#endif // slic3r_FillConcentricInternal_hpp_
