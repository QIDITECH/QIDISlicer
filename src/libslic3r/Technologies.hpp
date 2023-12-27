#ifndef _qidislicer_technologies_h_
#define _qidislicer_technologies_h_

//=============
// debug techs
//=============
// Shows camera target in the 3D scene
#define ENABLE_SHOW_CAMERA_TARGET 0
// Log debug messages to console when changing selection
#define ENABLE_SELECTION_DEBUG_OUTPUT 0
// Renders a small sphere in the center of the bounding box of the current selection when no gizmo is active
#define ENABLE_RENDER_SELECTION_CENTER 0
// Shows an imgui dialog with camera related data
#define ENABLE_CAMERA_STATISTICS 0
// Enable extracting thumbnails from selected gcode and save them as png files
#define ENABLE_THUMBNAIL_GENERATOR_DEBUG 0
// Disable synchronization of unselected instances
#define DISABLE_INSTANCES_SYNCH 0
// Use wxDataViewRender instead of wxDataViewCustomRenderer
#define ENABLE_NONCUSTOM_DATA_VIEW_RENDERING 0
// Enable G-Code viewer statistics imgui dialog
#define ENABLE_GCODE_VIEWER_STATISTICS 0
// Enable G-Code viewer comparison between toolpaths height and width detected from gcode and calculated at gcode generation 
#define ENABLE_GCODE_VIEWER_DATA_CHECKING 0
// Enable project dirty state manager debug window
#define ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW 0
// Disable using instanced models to render options in gcode preview
#define DISABLE_GCODEVIEWER_INSTANCED_MODELS 1
// Enable Measure Gizmo debug window
#define ENABLE_MEASURE_GIZMO_DEBUG 0
// Enable scene raycast picking debug window
#define ENABLE_RAYCAST_PICKING_DEBUG 0
// Shows an imgui dialog with GLModel statistics data
#define ENABLE_GLMODEL_STATISTICS 0
// Shows an imgui dialog containing the matrices of the selected volumes
#define ENABLE_MATRICES_DEBUG 0
// Shows an imgui dialog containing data from class ObjectManipulation
#define ENABLE_OBJECT_MANIPULATION_DEBUG 0
// Shows an imgui dialog containing data for class GLCanvas3D::SLAView
#define ENABLE_SLA_VIEW_DEBUG_WINDOW 0


// Enable rendering of objects using environment map
#define ENABLE_ENVIRONMENT_MAP 0
// Enable smoothing of objects normals
#define ENABLE_SMOOTH_NORMALS 0

// Enable OpenGL ES
#define ENABLE_OPENGL_ES 0
// Enable OpenGL core profile context (tested against Mesa 20.1.8 on Windows)
#define ENABLE_GL_CORE_PROFILE (1 && !ENABLE_OPENGL_ES)
// Enable OpenGL debug messages using debug context
#define ENABLE_OPENGL_DEBUG_OPTION (1 && ENABLE_GL_CORE_PROFILE)

// Enable imgui dialog which allows to set the parameters used to export binarized gcode
#define ENABLE_BINARIZED_GCODE_DEBUG_WINDOW 0


#endif // _prusaslicer_technologies_h_
