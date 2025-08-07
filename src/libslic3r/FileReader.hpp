#pragma once

#include "PrintConfig.hpp"
#include "enum_bitmask.hpp"
#include <utility>
#include <optional>

namespace Slic3r {

class Model;
class TriangleMesh;

namespace FileReader
{
    enum class LoadAttribute : int {
        AddDefaultInstances,
        CheckVersion
    };
    using LoadAttributes = enum_bitmask<LoadAttribute>;

    struct LoadStats {
        int     deleted_objects_cnt         { 0 };
        bool    looks_like_saved_in_meters  { false };
        bool    looks_like_imperial_units   { false };
        bool    looks_like_multipart_object { false };
    };

    bool            is_project_file(const std::string& input_file);

    // Load model from input file and return the its mesh. 
    // Throw RuntimeError if some problem was detected during model loading
    TriangleMesh    load_mesh(const std::string& input_file);

    // Load model from input file and fill statistics if it's required.
    // In respect to the params will be applied needed convertions over the model.
    // Exceptions don't catched inside
    Model           load_model(const std::string& input_file,
                               LoadAttributes options = LoadAttribute::AddDefaultInstances, 
                               LoadStats* statistics = nullptr,
                               std::optional<std::pair<double, double>> step_deflections = std::nullopt);

    // Load model, config and config substitutions from input file and fill statistics if it's required.
    // Exceptions don't catched inside
    Model           load_model_with_config(const std::string& input_file,
                                           DynamicPrintConfig* config,
                                           ConfigSubstitutionContext* config_substitutions,
                                           boost::optional<Semver> &qidislicer_generator_version,
                                           LoadAttributes options,
                                           LoadStats* statistics = nullptr);
}
    
ENABLE_ENUM_BITMASK_OPERATORS(FileReader::LoadAttribute)

} // namespace Slic3r::ModelProcessing
