#pragma once

#include "TriangleMesh.hpp"
#include <vector>

namespace Slic3r {

class Model;
class ModelObject;
class ModelVolume;

enum class ConversionType : int {
    CONV_TO_INCH,
    CONV_FROM_INCH,
    CONV_TO_METER,
    CONV_FROM_METER,
};

namespace ModelProcessing
{
    static constexpr const double volume_threshold_inches = 9.0;    // 9 = 3*3*3;
    static constexpr const double volume_threshold_meters = 0.001;  // 0.001 = 0.1*0.1*0.1

    void    convert_to_multipart_object(Model& model, unsigned int max_extruders);

    void    convert_from_imperial_units(Model& model, bool only_small_volumes);
    void    convert_from_imperial_units(ModelVolume* volume);

    void    convert_from_meters(Model& model, bool only_small_volumes);
    void    convert_from_meters(ModelVolume* volume);

    void    convert_units(Model& model_to, ModelObject* object_from, ConversionType conv_type, std::vector<int> volume_idxs);

    // Get full stl statistics for all object's meshes
    TriangleMeshStats   get_object_mesh_stats(const ModelObject* object);
    // Get count of errors in the mesh
    int     get_repaired_errors_count(const ModelVolume* volume);
    // Get count of errors in the mesh( or all object's meshes, if volume index isn't defined)
    int     get_repaired_errors_count(const ModelObject* object, const int vol_idx = -1);

    // Split this volume, append the result to the object owning this volume.
    // Return the number of volumes created from this one.
    // This is useful to assign different materials to different volumes of an object.
    size_t  split(ModelVolume* volume, unsigned int max_extruders);

    void    split(ModelObject* object, std::vector<ModelObject*>* new_objects);
    void    merge(ModelObject* object);
}

} // namespace Slic3r::ModelProcessing
