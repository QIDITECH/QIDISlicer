#ifndef slic3r_SurfaceDrag_hpp_
#define slic3r_SurfaceDrag_hpp_

#include <optional>
#include "libslic3r/Point.hpp" // Vec2d, Transform3d
#include "slic3r/Utils/RaycastManager.hpp"
#include "wx/event.h" // wxMouseEvent

namespace Slic3r {
class GLVolume;
} // namespace Slic3r

namespace Slic3r::GUI {
class GLCanvas3D;
class Selection;
struct Camera;

// Data for drag&drop over surface with mouse
struct SurfaceDrag
{
    // hold screen coor offset of cursor from object center
    Vec2d mouse_offset;

    // Start dragging text transformations to world
    Transform3d world;

    // Invers transformation of text volume instance
    // Help convert world transformation to instance space
    Transform3d instance_inv;

    // Dragged gl volume
    GLVolume *gl_volume;

    // condition for raycaster
    RaycastManager::AllowVolumes condition;

    // initial rotation in Z axis of volume
    std::optional<float> start_angle;

    // Flag whether coordinate hit some volume
    bool exist_hit = true;

    //  hold screen coor offset of cursor from object center without SLA shift
    Vec2d mouse_offset_without_sla_shift;
};

/// <summary>
/// Mouse event handler, when move(drag&drop) volume over model surface
/// NOTE: Dragged volume has to be selected. And also has to be hovered on start of dragging.
/// </summary>
/// <param name="mouse_event">Contain type of event and mouse position</param>
/// <param name="camera">Actual viewport of camera</param>
/// <param name="surface_drag">Structure which keep information about dragging</param>
/// <param name="canvas">Contain gl_volumes and selection</param>
/// <param name="raycast_manager">AABB trees for raycast in object
/// Refresh state inside of function </param>
/// <param name="up_limit">When set than use correction of up vector</param>
/// <returns>True when event is processed otherwise false</returns>
bool on_mouse_surface_drag(const wxMouseEvent         &mouse_event,
                           const Camera               &camera,
                           std::optional<SurfaceDrag> &surface_drag,
                           GLCanvas3D                 &canvas,
                           RaycastManager             &raycast_manager,
                           std::optional<double>       up_limit = {});

/// <summary>
/// Calculate translation of volume onto surface of model
/// </summary>
/// <param name="selection">Must contain only one selected volume, Transformation of current instance</param>
/// <param name="raycast_manager">AABB trees of object. Actualize object</param>
/// <returns>Offset of volume in volume coordinate</returns>
std::optional<Vec3d> calc_surface_offset(const Selection &selection, RaycastManager &raycast_manager);

/// <summary>
/// Get transformation to world
/// - use fix after store to 3mf when exists
/// </summary>
/// <param name="gl_volume">Scene volume</param>
/// <param name="objects">To identify Model volume with fix transformation</param>
/// <returns>Fixed Transformation of gl_volume</returns>
Transform3d world_matrix_fixed(const GLVolume &gl_volume, const ModelObjectPtrs& objects);

/// <summary>
/// Get transformation to world
/// - use fix after store to 3mf when exists
/// NOTE: when not one volume selected return identity
/// </summary>
/// <param name="selection">Selected volume</param>
/// <returns>Fixed Transformation of selected volume in selection</returns>
Transform3d world_matrix_fixed(const Selection &selection);

} // namespace Slic3r::GUI
#endif // slic3r_SurfaceDrag_hpp_