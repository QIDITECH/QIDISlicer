#ifndef MODELARRANGE_HPP
#define MODELARRANGE_HPP

#include <libslic3r/Arrange/Scene.hpp>

namespace Slic3r {

class Model;
class ModelInstance;
using ModelInstancePtrs = std::vector<ModelInstance*>;

//void duplicate(Model &model, ArrangePolygons &copies, VirtualBedFn);
void duplicate_objects(Model &model, size_t copies_num);

bool arrange_objects(Model &model,
                     const arr2::ArrangeBed &bed,
                     const arr2::ArrangeSettingsView &settings);

void duplicate_objects(Model &              model,
                       size_t               copies_num,
                       const arr2::ArrangeBed &bed,
                       const arr2::ArrangeSettingsView &settings);

void duplicate(Model &              model,
               size_t               copies_num,
               const arr2::ArrangeBed &bed,
               const arr2::ArrangeSettingsView &settings);

} // namespace Slic3r

#endif // MODELARRANGE_HPP
