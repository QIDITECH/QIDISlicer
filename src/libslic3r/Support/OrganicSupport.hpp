#ifndef slic3r_OrganicSupport_hpp
#define slic3r_OrganicSupport_hpp

#include <functional>
#include <vector>

#include "SupportCommon.hpp"
#include "TreeSupport.hpp"
#include "libslic3r/Support/SupportLayer.hpp"

namespace Slic3r
{

class PrintObject;

namespace FFFTreeSupport
{

class TreeModelVolumes;
class InterfacePlacer;
struct TreeSupportSettings;

// Organic specific: Smooth branches and produce one cummulative mesh to be sliced.
void organic_draw_branches(
    PrintObject                     &print_object,
    TreeModelVolumes                &volumes, 
    const TreeSupportSettings       &config,
    std::vector<SupportElements>    &move_bounds,

    // I/O:
    SupportGeneratorLayersPtr       &bottom_contacts,
    SupportGeneratorLayersPtr       &top_contacts,
    InterfacePlacer                 &interface_placer,

    // Output:
    SupportGeneratorLayersPtr       &intermediate_layers,
    SupportGeneratorLayerStorage    &layer_storage,

    std::function<void()>            throw_on_cancel);

} // namespace FFFTreeSupport

} // namespace Slic3r

#endif // slic3r_OrganicSupport_hpp