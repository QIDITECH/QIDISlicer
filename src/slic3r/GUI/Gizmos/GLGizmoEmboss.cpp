#include "GLGizmoEmboss.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/GUI_ObjectManipulation.hpp"
#include "slic3r/GUI/MainFrame.hpp" // to update title when add text
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/CameraUtils.hpp"
#include "slic3r/GUI/Jobs/EmbossJob.hpp"
#include "slic3r/GUI/Jobs/CreateFontNameImageJob.hpp"
#include "slic3r/GUI/Jobs/NotificationProgressIndicator.hpp"
#include "slic3r/Utils/WxFontUtils.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

// TODO: remove include
#include "libslic3r/SVG.hpp"      // debug store
#include "libslic3r/Geometry.hpp" // covex hull 2d
#include "libslic3r/Timer.hpp" 

#include "libslic3r/NSVGUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/ClipperUtils.hpp" // union_ex
#include "libslic3r/AppConfig.hpp"    // store/load font list
#include "libslic3r/Format/OBJ.hpp" // load obj file for default object
#include "libslic3r/BuildVolume.hpp"

#include "imgui/imgui_stdlib.h" // using std::string for inputs
#include "nanosvg/nanosvg.h"    // load SVG file

#include <wx/font.h>
#include <wx/fontutil.h>
#include <wx/fontdlg.h>
#include <wx/fontenum.h>
#include <wx/display.h> // detection of change DPI

#include <boost/log/trivial.hpp>

#include <GL/glew.h>
#include <chrono> // measure enumeration of fonts

// uncomment for easier debug
//#define ALLOW_DEBUG_MODE
#ifdef ALLOW_DEBUG_MODE
#define ALLOW_ADD_FONT_BY_FILE
#define ALLOW_ADD_FONT_BY_OS_SELECTOR
#define SHOW_WX_FONT_DESCRIPTOR // OS specific descriptor | file path --> in edit style <tree header>
#define SHOW_FONT_FILE_PROPERTY // ascent, descent, line gap, cache --> in advanced <tree header>
#define SHOW_CONTAIN_3MF_FIX // when contain fix matrix --> show gray '3mf' next to close button
#define SHOW_OFFSET_DURING_DRAGGING // when drag with text over surface visualize used center
#define SHOW_IMGUI_ATLAS
#define SHOW_ICONS_TEXTURE
#define SHOW_FINE_POSITION // draw convex hull around volume
#define DRAW_PLACE_TO_ADD_TEXT // Interactive draw of window position 
#define ALLOW_OPEN_NEAR_VOLUME
#define EXECUTE_PROCESS_ON_MAIN_THREAD // debug execution on main thread
#endif // ALLOW_DEBUG_MODE

//#define USE_PIXEL_SIZE_IN_WX_FONT

using namespace Slic3r;
using namespace Slic3r::Emboss;
using namespace Slic3r::GUI;
using namespace Slic3r::GUI::Emboss;

namespace priv {
template<typename T> struct MinMax { T min; T max;};
template<typename T> struct Limit {
    // Limitation for view slider range in GUI
    MinMax<T> gui;
    // Real limits for setting exacts values
    MinMax<T> values;
};

// Variable keep limits for variables
static const struct Limits
{
    MinMax<float> emboss{0.01f, 1e4f}; // in mm
    MinMax<float> size_in_mm{0.1f, 1000.f}; // in mm
    Limit<float> boldness{{-200.f, 200.f}, {-2e4f, 2e4f}}; // in font points
    Limit<float> skew{{-1.f, 1.f}, {-100.f, 100.f}}; // ration without unit
    MinMax<int>  char_gap{-20000, 20000}; // in font points
    MinMax<int>  line_gap{-20000, 20000}; // in font points
    // distance text object from surface
    MinMax<float> angle{-180.f, 180.f}; // in degrees

    template<typename T>
    static bool apply(std::optional<T> &val, const MinMax<T> &limit) {
        if (val.has_value())
            return apply<T>(*val, limit);
        return false;
    }
    template<typename T>
    static bool apply(T &val, const MinMax<T> &limit)
    {
        if (val > limit.max) {
            val = limit.max;
            return true;
        }
        if (val < limit.min) {
            val = limit.min;
            return true;
        }
        return false;
    }

} limits;

// Define where is up vector on model
constexpr double up_limit = 0.9;

// Normalize radian angle from -PI to PI
template<typename T> void to_range_pi_pi(T& angle)
{
    if (angle > PI || angle < -PI) {
        int count = static_cast<int>(std::round(angle / (2 * PI)));
        angle -= static_cast<T>(count * 2 * PI);
    }
}
} // namespace priv
using namespace priv;

GLGizmoEmboss::GLGizmoEmboss(GLCanvas3D &parent)
    : GLGizmoBase(parent, M_ICON_FILENAME, -2)
    , m_volume(nullptr)
    , m_is_unknown_font(false)
    , m_rotate_gizmo(parent, GLGizmoRotate::Axis::Z) // grab id = 2 (Z axis)
    , m_style_manager(m_imgui->get_glyph_ranges(), create_default_styles)
    , m_job_cancel(nullptr)
{
    m_rotate_gizmo.set_group_id(0);
    m_rotate_gizmo.set_force_local_coordinate(true);
    // TODO: add suggestion to use https://fontawesome.com/
    // (copy & paste) unicode symbols from web    
    // paste HEX unicode into notepad move cursor after unicode press [alt] + [x]
}

// Private namespace with helper function for create volume
namespace priv {

/// <summary>
/// Prepare data for emboss
/// </summary>
/// <param name="text">Text to emboss</param>
/// <param name="style_manager">Keep actual selected style</param>
/// <param name="text_lines">Needed when transform per glyph</param>
/// <param name="selection">Needed for transform per glyph</param>
/// <param name="type">Define type of volume - side of surface(in / out)</param>
/// <param name="cancel">Cancel for previous job</param>
/// <returns>Base data for emboss text</returns>
static DataBase create_emboss_data_base(const std::string                  &text,
                                        StyleManager                       &style_manager,
                                        TextLinesModel                     &text_lines,
                                        const Selection                    &selection,
                                        ModelVolumeType type,
                                        std::shared_ptr<std::atomic<bool>> &cancel);

/// <summary>
/// Start job for add new volume to object with given transformation
/// </summary>
/// <param name="object">Define where to add</param>
/// <param name="volume_trmat">Volume transformation</param>
/// <param name="emboss_data">Define text</param>
/// <param name="volume_type">Type of volume</param>
static void start_create_volume_job(const ModelObject *object,
                                    const Transform3d  volume_trmat,
                                    DataBase          &emboss_data,
                                    ModelVolumeType    volume_type);

/// <summary>
/// Start job for add new volume on surface of object defined by screen coor
/// </summary>
/// <param name="emboss_data">Define params of text</param>
/// <param name="volume_type">Emboss / engrave</param>
/// <param name="screen_coor">Mouse position which define position</param>
/// <param name="gl_volume">Volume to find surface for create</param>
/// <param name="raycaster">Ability to ray cast to model</param>
/// <param name="text_lines">Per glyph transformation</param>
/// <param name="style_manager">Line height need font file/param>
/// <param name="canvas">Contain already used scene RayCasters</param>
/// <returns>True when start creation, False when there is no hit surface by screen coor</returns>
static bool start_create_volume_on_surface_job(DataBase         &emboss_data,
                                               ModelVolumeType   volume_type,
                                               const Vec2d      &screen_coor,
                                               const GLVolume   *gl_volume,
                                               RaycastManager   &raycaster,
                                               TextLinesModel &text_lines,
                                               /*const */ StyleManager &style_manager,
                                               GLCanvas3D       &canvas);

/// <summary>
/// Find volume in selected object with closest convex hull to screen center.
/// Return 
/// </summary>
/// <param name="selection">Define where to search for closest</param>
/// <param name="screen_center">Canvas center(dependent on camera settings)</param>
/// <param name="objects">Actual objects</param>
/// <param name="closest_center">OUT: coordinate of controid of closest volume</param>
/// <param name="closest_volume">OUT: closest volume</param>
static void find_closest_volume(const Selection       &selection,
                                const Vec2d           &screen_center,
                                const Camera          &camera,
                                const ModelObjectPtrs &objects,
                                Vec2d                 *closest_center,
                                const GLVolume       **closest_volume);

/// <summary>
/// Start job for add object with text into scene
/// </summary>
/// <param name="emboss_data">Define params of text</param>
/// <param name="coor">Screen coordinat, where to create new object laying on bed</param>
static void start_create_object_job(DataBase &emboss_data, const Vec2d &coor);

// Loaded icons enum
// Have to match order of files in function GLGizmoEmboss::init_icons()
enum class IconType : unsigned {
    rename = 0,
    erase,
    add,
    save,
    undo,
    italic,
    unitalic,
    bold,
    unbold,
    system_selector,
    open_file,
    exclamation,
    lock,
    lock_bold,
    unlock,
    unlock_bold,
    align_horizontal_left,
    align_horizontal_center,
    align_horizontal_right,
    align_vertical_top,
    align_vertical_center,
    align_vertical_bottom,
    // automatic calc of icon's count
    _count
};
// Define rendered version of icon
enum class IconState : unsigned { activable = 0, hovered /*1*/, disabled /*2*/ };
// selector for icon by enum
const IconManager::Icon &get_icon(const IconManager::VIcons& icons, IconType type, IconState state);
// short call of Slic3r::GUI::button
static bool draw_button(const IconManager::VIcons& icons, IconType type, bool disable = false);

/// <summary>
/// Apply camera direction for emboss direction
/// </summary>
/// <param name="camera">Define view vector</param>
/// <param name="canvas">Containe Selected Model to modify</param>
/// <param name="keep_up">Keep same up vector</param>
/// <returns>True when apply change otherwise false</returns>
static bool apply_camera_dir(const Camera &camera, GLCanvas3D &canvas, bool keep_up);

} // namespace priv

namespace {
// for existing volume which is selected(could init different(to volume text) lines count when edit text)
void init_text_lines(TextLinesModel &text_lines, const Selection& selection, /* const*/ StyleManager &style_manager, unsigned count_lines=0);
// before text volume is created
void init_new_text_line(TextLinesModel &text_lines, const Transform3d& new_text_tr, const ModelObject& mo, /* const*/ StyleManager &style_manager);
}


//B34
void GLGizmoEmboss::create_volume(ModelVolumeType volume_type, const Vec2d &mouse_pos, std::string str)
{
    if (!init_create(volume_type))
        return;

    // select position by camera position and view direction
    const Selection &selection  = m_parent.get_selection();
    int              object_idx = selection.get_object_idx();

    Size s = m_parent.get_canvas_size();

    Vec2d                  screen_center(s.get_width() / 2., s.get_height() / 2.);
    DataBase emboss_data = priv::create_emboss_data_base(str, m_style_manager, m_text_lines, m_parent.get_selection(), volume_type,
                                                         m_job_cancel);

    const ModelObjectPtrs &objects        = selection.get_model()->objects;
    bool                   is_simple_mode = wxGetApp().get_mode() == comSimple;
    // No selected object so create new object
    if (selection.is_empty() || object_idx < 0 || static_cast<size_t>(object_idx) >= objects.size() || is_simple_mode) {
        // create Object on center of screen
        // when ray throw center of screen not hit bed it create object on center of bed
        priv::start_create_object_job(emboss_data, screen_center);
        return;
    }

    // create volume inside of selected object
    Vec2d           coor;
    const GLVolume *vol    = nullptr;
    const Camera &  camera = wxGetApp().plater()->get_camera();
    priv::find_closest_volume(selection, screen_center, camera, objects, &coor, &vol);

    // there is no point on surface so no use of surface will be applied
    FontProp &prop = emboss_data.text_configuration.style.prop;
    if (prop.use_surface)
        prop.use_surface = false;


    Plater *plater = wxGetApp().plater();
    // Transformation is inspired add generic volumes in ObjectList::load_generic_subobject
    const ModelObject *obj         = objects[vol->object_idx()];
    BoundingBoxf3      instance_bb = obj->instance_bounding_box(vol->instance_idx());

    size_t vol_id = obj->volumes[vol->volume_idx()]->id().id;

    auto cond = RaycastManager::AllowVolumes({vol_id});

    RaycastManager::Meshes meshes = create_meshes(m_parent, cond);
    m_raycast_manager.actualize(*obj, &cond, &meshes);
    std::optional<RaycastManager::Hit> hit    = ray_from_camera(m_raycast_manager, coor, camera, &cond);

    DynamicPrintConfig *print_config    = &wxGetApp().preset_bundle->prints.get_edited_preset().config;
    double pa_first_layer_height = print_config->get_abs_value("first_layer_height");
    double pa_layer_height       = print_config->get_abs_value("layer_height");
    Transform3d surface_trmat = create_transformation_onto_surface(Vec3d(mouse_pos.x(), mouse_pos.y(), pa_first_layer_height),
                                                                   hit->normal,
                                                                    priv::up_limit);
    emboss_data.text_configuration.style.prop.emboss     = pa_layer_height;
    emboss_data.text_configuration.style.prop.size_in_mm = 7;

    const FontProp &font_prop     = emboss_data.text_configuration.style.prop;
    apply_transformation(font_prop, surface_trmat);
    Transform3d instance     = vol->get_instance_transformation().get_matrix();
    Transform3d volume_trmat = instance.inverse() * surface_trmat;
    start_create_volume_job(obj, volume_trmat, emboss_data, volume_type);

}


void GLGizmoEmboss::create_volume(ModelVolumeType volume_type, const Vec2d& mouse_pos)
{
    if (!init_create(volume_type))
        return;

    const GLVolume *gl_volume = get_first_hovered_gl_volume(m_parent);
    DataBase emboss_data    = priv::create_emboss_data_base(m_text, m_style_manager, m_text_lines, m_parent.get_selection(), volume_type, m_job_cancel);
    bool is_simple_mode = wxGetApp().get_mode() == comSimple;
    if (gl_volume != nullptr && !is_simple_mode) {
        // Try to cast ray into scene and find object for add volume
        if (!priv::start_create_volume_on_surface_job(emboss_data, volume_type, mouse_pos, gl_volume, m_raycast_manager, m_text_lines, m_style_manager, m_parent)) {
            // When model is broken. It could appear that hit miss the object.
            // So add part near by in simmilar manner as right panel do
            create_volume(volume_type);
        }
    } else {
        // object is not under mouse position soo create object on plater
        priv::start_create_object_job(emboss_data, mouse_pos);    
    }
}

// Designed for create volume without information of mouse in scene
void GLGizmoEmboss::create_volume(ModelVolumeType volume_type)
{
    if (!init_create(volume_type))
        return;

    // select position by camera position and view direction
    const Selection &selection = m_parent.get_selection();
    int object_idx = selection.get_object_idx();

    Size s = m_parent.get_canvas_size();
    Vec2d screen_center(s.get_width() / 2., s.get_height() / 2.);
    DataBase emboss_data = priv::create_emboss_data_base(m_text, m_style_manager, m_text_lines, m_parent.get_selection(), volume_type, m_job_cancel);
    const ModelObjectPtrs &objects = selection.get_model()->objects;
    bool is_simple_mode = wxGetApp().get_mode() == comSimple;
    // No selected object so create new object
    if (selection.is_empty() || object_idx < 0 || static_cast<size_t>(object_idx) >= objects.size() || is_simple_mode) {
        // create Object on center of screen
        // when ray throw center of screen not hit bed it create object on center of bed
        priv::start_create_object_job(emboss_data, screen_center);
        return;
    }

    // create volume inside of selected object
    Vec2d coor;
    const GLVolume *vol = nullptr;
    const Camera &camera = wxGetApp().plater()->get_camera();
    priv::find_closest_volume(selection, screen_center, camera, objects, &coor, &vol);
    if (vol == nullptr) {
        priv::start_create_object_job(emboss_data, screen_center);
    } else if (!priv::start_create_volume_on_surface_job(emboss_data, volume_type, coor, vol, m_raycast_manager, m_text_lines, m_style_manager, m_parent)) {
        // in centroid of convex hull is not hit with object
        // soo create transfomation on border of object
        
        // there is no point on surface so no use of surface will be applied
        FontProp &prop = emboss_data.text_configuration.style.prop;
        if (prop.use_surface)
            prop.use_surface = false;
        
        // Transformation is inspired add generic volumes in ObjectList::load_generic_subobject
        const ModelObject *obj = objects[vol->object_idx()];
        BoundingBoxf3 instance_bb = obj->instance_bounding_box(vol->instance_idx());
        // Translate the new modifier to be pickable: move to the left front corner of the instance's bounding box, lift to print bed.
        Transform3d tr = vol->get_instance_transformation().get_matrix_no_offset().inverse();
        Vec3d offset_tr(0, // center of instance - Can't suggest width of text before it will be created
            - instance_bb.size().y() / 2 - prop.size_in_mm / 2, // under
            prop.emboss / 2 - instance_bb.size().z() / 2 // lay on bed
        );
        Transform3d volume_trmat = tr * Eigen::Translation3d(offset_tr);                
        if (prop.per_glyph) {
            init_new_text_line(m_text_lines, volume_trmat, *obj, m_style_manager);
            emboss_data.text_lines = m_text_lines.get_lines();
        }
        priv::start_create_volume_job(obj, volume_trmat, emboss_data, volume_type);
    }
}

void GLGizmoEmboss::on_shortcut_key() {
    set_volume_by_selection();
    if (m_volume == nullptr) {
        // No volume to select from selection so create volume.
        // NOTE: After finish job for creation emboss Text volume,
        // GLGizmoEmboss will be opened
        create_volume(ModelVolumeType::MODEL_PART);
    } else {
        // shortcut is pressed when text is selected soo start edit it.
        auto &mng = m_parent.get_gizmos_manager();
        if (mng.get_current_type() != GLGizmosManager::Emboss)
            mng.open_gizmo(GLGizmosManager::Emboss);
    }
}

namespace{
ModelVolumePtrs prepare_volumes_to_slice(const ModelVolume &mv)
{
    const ModelVolumePtrs &volumes = mv.get_object()->volumes;
    ModelVolumePtrs        result;
    result.reserve(volumes.size());
    for (ModelVolume *volume : volumes) {
        // only part could be surface for volumes
        if (!volume->is_model_part())
            continue;

        // is selected volume
        if (mv.id() == volume->id())
            continue;

        result.push_back(volume);
    }
    return result;
}
}

bool GLGizmoEmboss::do_mirror(size_t axis)
{ 
    // is valid input
    assert(axis < 3);
    if (axis >= 3)
        return false;

    // is gizmo opened and initialized?
    assert(m_parent.get_gizmos_manager().get_current_type() == GLGizmosManager::Emboss);
    if (m_parent.get_gizmos_manager().get_current_type() != GLGizmosManager::Emboss)
        return false;

    const TextConfiguration &tc= *m_volume->text_configuration;
    if(tc.style.prop.per_glyph){
        // init textlines before mirroring on mirrored text volume transformation
        Transform3d tr = m_volume->get_matrix();
        const std::optional<Transform3d> &fix_tr = tc.fix_3mf_tr;
        if (fix_tr.has_value())
            tr = tr * (fix_tr->inverse());

        // mirror
        Vec3d scale = Vec3d::Ones();
        scale[axis] = -1.;
        tr = tr * Eigen::Scaling(scale);

        // collect volumes in object
        ModelVolumePtrs volumes = prepare_volumes_to_slice(*m_volume);
        m_text_lines.init(tr, volumes, m_style_manager, m_text_lines.get_lines().size());
    }

    // mirror
    Transform3d tr = m_volume->get_matrix();
    Vec3d scale = Vec3d::Ones();
    scale[axis] = -1.;
    tr = tr * Eigen::Scaling(scale);
    m_volume->set_transformation(tr); 
    // NOTE: Staff around volume transformation change is done in job finish
    return process();
}

namespace{
// verify correct volume type for creation of text
bool check(ModelVolumeType volume_type) {
    return volume_type == ModelVolumeType::MODEL_PART ||
           volume_type == ModelVolumeType::NEGATIVE_VOLUME ||
           volume_type == ModelVolumeType::PARAMETER_MODIFIER;
}
}

bool GLGizmoEmboss::init_create(ModelVolumeType volume_type)
{
    // check valid volume type
    if (!check(volume_type)){    
        BOOST_LOG_TRIVIAL(error) << "Can't create embossed volume with this type: " << (int) volume_type;
        return false;
    }

    if (!is_activable()) {
        BOOST_LOG_TRIVIAL(error) << "Can't create text. Gizmo is not activabled.";
        return false;
    }
    
    // Check can't be inside is_activable() cause crash
    // steps to reproduce: start App -> key 't' -> key 'delete'
    if (wxGetApp().obj_list()->has_selected_cut_object()) {
        BOOST_LOG_TRIVIAL(error) << "Can't create text on cut object";
        return false;    
    }

    m_style_manager.discard_style_changes();

    // set default text
    m_text = _u8L("Embossed text");
    return true;
}

namespace {
TransformationType get_transformation_type(const Selection &selection)
{
    assert(selection.is_single_full_object() || selection.is_single_volume());
    return selection.is_single_volume() ? 
        TransformationType::Local_Relative_Joint :
        TransformationType::Instance_Relative_Joint; // object
}
} // namespace

bool GLGizmoEmboss::on_mouse_for_rotation(const wxMouseEvent &mouse_event)
{
    if (mouse_event.Moving()) return false;

    bool used = use_grabbers(mouse_event);
    if (!m_dragging) return used;

    if (mouse_event.Dragging()) {
        if (!m_rotate_start_angle.has_value()) {
            // when m_rotate_start_angle is not set mean it is not Dragging
            // when angle_opt is not set mean angle is Zero
            const std::optional<float> &angle_opt = m_style_manager.get_font_prop().angle;
            m_rotate_start_angle = angle_opt.has_value() ? *angle_opt : 0.f;
        }

        double angle = m_rotate_gizmo.get_angle();
        angle -= PI / 2; // Grabber is upward

        // temporary rotation
        Selection& selection = m_parent.get_selection();
        selection.rotate(Vec3d(0., 0., angle), get_transformation_type(selection));

        angle += *m_rotate_start_angle;
        // move to range <-M_PI, M_PI>
        priv::to_range_pi_pi(angle);

        // set into activ style
        assert(m_style_manager.is_active_font());
        if (m_style_manager.is_active_font()) {
            std::optional<float> angle_opt;
            if (!is_approx(angle, 0.))
                angle_opt = angle;
            m_style_manager.get_font_prop().angle = angle_opt;
        }

        volume_transformation_changing();
    }
    return used;
}

bool GLGizmoEmboss::on_mouse_for_translate(const wxMouseEvent &mouse_event)
{
    // exist selected volume?
    if (m_volume == nullptr)
        return false;
    
    std::optional<double> up_limit;
    if (m_keep_up)        up_limit = priv::up_limit;
    const Camera &camera = wxGetApp().plater()->get_camera();
    bool was_dragging = m_surface_drag.has_value();
    bool res = on_mouse_surface_drag(mouse_event, camera, m_surface_drag, m_parent, m_raycast_manager, up_limit);
    bool is_dragging = m_surface_drag.has_value();

    // End with surface dragging?
    if (was_dragging && !is_dragging) 
        volume_transformation_changed();
    
    // Start with dragging
    else if (!was_dragging && is_dragging) {
        // Cancel job to prevent interuption of dragging (duplicit result)
        if (m_job_cancel != nullptr)
            m_job_cancel->store(true);
    }

    // during drag
    else if (was_dragging && is_dragging) {
        // update scale of selected volume --> should be approx the same
        calculate_scale();

        // Recalculate angle for GUI
        if (!m_keep_up) { 
            const GLVolume *gl_volume = get_selected_gl_volume(m_parent.get_selection());
            assert(gl_volume != nullptr);
            assert(m_style_manager.is_active_font());
            if (gl_volume == nullptr || !m_style_manager.is_active_font())
                return res;

            m_style_manager.get_font_prop().angle = calc_up(gl_volume->world_matrix(), priv::up_limit);
        }

        volume_transformation_changing();
    }
    return res;
}

void GLGizmoEmboss::on_mouse_change_selection(const wxMouseEvent &mouse_event)
{
    static bool was_dragging = true;  
    if ((mouse_event.LeftUp() || mouse_event.RightUp()) && !was_dragging) {
        // is hovered volume closest hovered?
        int hovered_idx = m_parent.get_first_hover_volume_idx();
        if (hovered_idx < 0) 
            // unselect object
            return close();

        const GLVolumePtrs &gl_volumes = m_parent.get_volumes().volumes;
        auto hovered_idx_ = static_cast<size_t>(hovered_idx);
        if (hovered_idx_ >= gl_volumes.size())
            return close();
        
        const GLVolume *gl_volume = gl_volumes[hovered_idx_];
        if (gl_volume == nullptr)
            return close();

        const ModelVolume *volume = get_model_volume(*gl_volume, m_parent.get_model()->objects);
        if (volume == nullptr || !volume->text_configuration.has_value())
            // select volume without text configuration
            return close();

        // Reselection of text to another text
    }
    was_dragging = mouse_event.Dragging();

    // Hook When click on object for reselection must be on event left down not up
    if (mouse_event.LeftDown()) {
        // is hovered volume closest hovered?
        int hovered_idx = m_parent.get_first_hover_volume_idx();
        if (hovered_idx < 0)
            // Potentionaly move with camera (drag)
            return;

        const GLVolumePtrs &gl_volumes = m_parent.get_volumes().volumes;
        auto hovered_idx_ = static_cast<size_t>(hovered_idx);
        if (hovered_idx_ >= gl_volumes.size())
            return;
        const GLVolume *gl_volume = gl_volumes[hovered_idx_];
        if (gl_volume == nullptr)
            return;
        const ModelVolume *volume = get_model_volume(*gl_volume, m_parent.get_model()->objects);
        if (volume == nullptr)
            return;

        if (volume->text_configuration.has_value())        
            return; // Reselection of text to another text

        // select volume without text configuration
        return close();
    }

    // Hook When drag with scene by right mouse button
    // object it is selected after drag scene !!
    if (mouse_event.RightDown()) {
        // is hovered volume closest hovered?
        int hovered_idx = m_parent.get_first_hover_volume_idx();
        if (hovered_idx < 0)
            // Potentionaly move with camera (drag)
            return;

        const GLVolumePtrs &gl_volumes = m_parent.get_volumes().volumes;
        auto hovered_idx_ = static_cast<size_t>(hovered_idx);
        if (hovered_idx_ >= gl_volumes.size())
            return;
        const GLVolume *gl_volume = gl_volumes[hovered_idx_];
        if (gl_volume == nullptr)
            return;
        const ModelVolume *volume = get_model_volume(*gl_volume, m_parent.get_model()->objects);
        if (volume == nullptr)
            return;

        // is actual selected?
        if (m_volume->id() == volume->id())
            return;

        // select volume without text configuration
        return close();
    }
}

bool GLGizmoEmboss::on_mouse(const wxMouseEvent &mouse_event)
{
    // not selected volume
    if (m_volume == nullptr ||
        get_model_volume(m_volume_id, m_parent.get_selection().get_model()->objects) == nullptr ||
        !m_volume->text_configuration.has_value()) return false;

    if (on_mouse_for_rotation(mouse_event)) return true;
    if (on_mouse_for_translate(mouse_event)) return true;
    on_mouse_change_selection(mouse_event);
    return false;
}

void GLGizmoEmboss::volume_transformation_changing()
{
    if (m_volume == nullptr || !m_volume->text_configuration.has_value()) {
        assert(false);
        return;
    }
    const FontProp &prop = m_volume->text_configuration->style.prop;
    if (prop.per_glyph)
        init_text_lines(m_text_lines, m_parent.get_selection(), m_style_manager, m_text_lines.get_lines().size());
}

void GLGizmoEmboss::volume_transformation_changed()
{
    if (m_volume == nullptr || !m_volume->text_configuration.has_value()) {
        assert(false);
        return;
    }

    const FontProp &prop = m_volume->text_configuration->style.prop;
    if (prop.per_glyph)
        init_text_lines(m_text_lines, m_parent.get_selection(), m_style_manager, m_text_lines.get_lines().size());

    // Update surface by new position
    if (prop.use_surface || prop.per_glyph)
        process();

    // Show correct value of height & depth inside of inputs
    calculate_scale();
}

bool GLGizmoEmboss::on_init()
{
    m_rotate_gizmo.init();
    ColorRGBA gray_color(.6f, .6f, .6f, .3f);
    m_rotate_gizmo.set_highlight_color(gray_color);

    // NOTE: It has special handling in GLGizmosManager::handle_shortcut
    m_shortcut_key = WXK_CONTROL_T;

    // initialize text styles
    m_style_manager.init(wxGetApp().app_config);

    // Set rotation gizmo upwardrotate
    m_rotate_gizmo.set_angle(PI / 2);
    return true;
}

std::string GLGizmoEmboss::on_get_name() const { return _u8L("Emboss"); }

void GLGizmoEmboss::on_render() {
    // no volume selected
    if (m_volume == nullptr ||
        get_model_volume(m_volume_id, m_parent.get_selection().get_model()->objects) == nullptr)
        return;
    const Selection &selection = m_parent.get_selection();
    if (selection.is_empty()) return;

    // prevent get local coordinate system on multi volumes
    if (!selection.is_single_volume_or_modifier() && 
        !selection.is_single_volume_instance()) return;
    
    const GLVolume *gl_volume_ptr = m_parent.get_selection().get_first_volume();
    if (gl_volume_ptr == nullptr) return;

    if (m_text_lines.is_init()) {
        const Transform3d& tr = gl_volume_ptr->world_matrix();
        const auto &fix = m_volume->text_configuration->fix_3mf_tr;
        if (fix.has_value()) 
            m_text_lines.render(tr * fix->inverse());
        else 
            m_text_lines.render(tr);
    }

    bool is_surface_dragging = m_surface_drag.has_value();
    bool is_parent_dragging = m_parent.is_mouse_dragging();
    // Do NOT render rotation grabbers when dragging object
    bool is_rotate_by_grabbers = m_dragging;
    if (is_rotate_by_grabbers || 
        (!is_surface_dragging && !is_parent_dragging)) {
        glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
        m_rotate_gizmo.render();
    }
}

void GLGizmoEmboss::on_register_raycasters_for_picking(){
    m_rotate_gizmo.register_raycasters_for_picking();
}
void GLGizmoEmboss::on_unregister_raycasters_for_picking(){
    m_rotate_gizmo.unregister_raycasters_for_picking();
}

#ifdef SHOW_FINE_POSITION
// draw suggested position of window
static void draw_fine_position(const Selection &selection,
                               const Size      &canvas,
                               const ImVec2    &windows_size)
{
    const Selection::IndicesList& indices = selection.get_volume_idxs();
    // no selected volume
    if (indices.empty()) return;
    const GLVolume *volume = selection.get_volume(*indices.begin());
    // bad volume selected (e.g. deleted one)
    if (volume == nullptr) return;

    const Camera   &camera = wxGetApp().plater()->get_camera();
    Slic3r::Polygon hull   = CameraUtils::create_hull2d(camera, *volume);
    ImVec2          canvas_size(canvas.get_width(), canvas.get_height());
    ImVec2 offset = ImGuiWrapper::suggest_location(windows_size, hull,
                                                   canvas_size);
    Slic3r::Polygon rect(
        {Point(offset.x, offset.y), Point(offset.x + windows_size.x, offset.y),
         Point(offset.x + windows_size.x, offset.y + windows_size.y),
         Point(offset.x, offset.y + windows_size.y)});
    ImGuiWrapper::draw(hull);
    ImGuiWrapper::draw(rect);
}
#endif // SHOW_FINE_POSITION

#ifdef DRAW_PLACE_TO_ADD_TEXT
static void draw_place_to_add_text()
{
    ImVec2             mp = ImGui::GetMousePos();
    Vec2d              mouse_pos(mp.x, mp.y);
    const Camera      &camera = wxGetApp().plater()->get_camera();
    Vec3d              p1 = CameraUtils::get_z0_position(camera, mouse_pos);
    std::vector<Vec3d> rect3d{p1 + Vec3d(5, 5, 0), p1 + Vec3d(-5, 5, 0),
                              p1 + Vec3d(-5, -5, 0), p1 + Vec3d(5, -5, 0)};
    Points             rect2d = CameraUtils::project(camera, rect3d);
    ImGuiWrapper::draw(Slic3r::Polygon(rect2d));
}
#endif // DRAW_PLACE_TO_ADD_TEXT

#ifdef SHOW_OFFSET_DURING_DRAGGING
static void draw_mouse_offset(const std::optional<Vec2d> &offset)
{
    if (!offset.has_value()) return;
    // debug draw
    auto   draw_list = ImGui::GetOverlayDrawList();
    ImVec2 p1        = ImGui::GetMousePos();
    ImVec2 p2(p1.x + offset->x(), p1.y + offset->y());
    //B18
    ImU32  color     = ImGui::GetColorU32(ImGuiWrapper::COL_BLUE_LIGHT);
    float  thickness = 3.f;
    draw_list->AddLine(p1, p2, color, thickness);
}
#endif // SHOW_OFFSET_DURING_DRAGGING

void GLGizmoEmboss::on_render_input_window(float x, float y, float bottom_limit)
{
    assert(m_volume != nullptr);
    // Do not render window for not selected text volume
    if (m_volume == nullptr ||
        get_model_volume(m_volume_id, m_parent.get_selection().get_model()->objects) == nullptr ||
        !m_volume->text_configuration.has_value()) {
        // This closing could lead to bad behavior of undo/redo stack when unselection create snapshot before close
        close();
        return;
    }

    // Not known situation when could happend this is only for sure
    if (!m_is_unknown_font && !m_style_manager.is_active_font())
        create_notification_not_valid_font("No active font in style. Select correct one.");
    else if (!m_is_unknown_font && !m_style_manager.get_wx_font().IsOk())
        create_notification_not_valid_font("WxFont is not loaded properly.");

    // Configuration creation
    double screen_scale = wxDisplay(wxGetApp().plater()).GetScaleFactor();
    float  main_toolbar_height = m_parent.get_main_toolbar_height();
    if (!m_gui_cfg.has_value() ||                   // Exist configuration - first run
        m_gui_cfg->screen_scale != screen_scale || // change of DPI
        m_gui_cfg->main_toolbar_height != main_toolbar_height // change size of view port
        ) {
        // Create cache for gui offsets
        GuiCfg cfg = create_gui_configuration();
        cfg.screen_scale = screen_scale;
        cfg.main_toolbar_height = main_toolbar_height;
        m_gui_cfg.emplace(std::move(cfg));
        // set position near toolbar
        m_set_window_offset = ImVec2(-1.f, -1.f);

        // change resolution regenerate icons
        init_icons();
        m_style_manager.clear_imgui_font();
    }

    const ImVec2 &min_window_size = get_minimal_window_size();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, min_window_size);

    // Draw origin position of text during dragging
    if (m_surface_drag.has_value()) {
        ImVec2 mouse_pos = ImGui::GetMousePos();
        ImVec2 center(
            mouse_pos.x + m_surface_drag->mouse_offset.x(),
            mouse_pos.y + m_surface_drag->mouse_offset.y());
        ImU32 color = ImGui::GetColorU32(
            m_surface_drag->exist_hit ? 
                ImVec4(1.f, 1.f, 1.f, .75f) : // transparent white
                ImVec4(1.f, .3f, .3f, .75f)
        ); // Warning color
        const float radius = 16.f;
        ImGuiWrapper::draw_cross_hair(center, radius, color);
    }

#ifdef SHOW_FINE_POSITION
    draw_fine_position(m_parent.get_selection(), m_parent.get_canvas_size(), min_window_size);
#endif // SHOW_FINE_POSITION
#ifdef DRAW_PLACE_TO_ADD_TEXT
    draw_place_to_add_text();
#endif // DRAW_PLACE_TO_ADD_TEXT
#ifdef SHOW_OFFSET_DURING_DRAGGING
    draw_mouse_offset(m_dragging_mouse_offset);
#endif // SHOW_OFFSET_DURING_DRAGGING

    // check if is set window offset
    if (m_set_window_offset.has_value()) {
        if (m_set_window_offset->y < 0)
            // position near toolbar
            m_set_window_offset = ImVec2(x, std::min(y, bottom_limit - min_window_size.y));
        
        ImGui::SetNextWindowPos(*m_set_window_offset, ImGuiCond_Always);
        m_set_window_offset.reset();
    } else if (!m_allow_open_near_volume) {
        y = std::min(y, bottom_limit - min_window_size.y);
        // position near toolbar
        ImVec2 pos(x, y);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Once);
    }

    bool is_opened = true;
    ImGuiWindowFlags flag = ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin(on_get_name().c_str(), &is_opened, flag)) {
        // Need to pop var before draw window
        ImGui::PopStyleVar(); // WindowMinSize
        draw_window();
    } else {
        ImGui::PopStyleVar(); // WindowMinSize
    }

    // after change volume from object to volume it is necessary to recalculate
    // minimal windows size because of set type
    if (m_should_set_minimal_windows_size) {
        m_should_set_minimal_windows_size = false;
        ImGui::SetWindowSize(ImVec2(0.f, min_window_size.y), ImGuiCond_Always);
    }

    ImGui::End();
    if (!is_opened)
        close();
}

namespace priv {
/// <summary>
/// Move window for edit emboss text near to embossed object
/// NOTE: embossed object must be selected
/// </summary>
static ImVec2 calc_fine_position(const Selection &selection, const ImVec2 &windows_size, const Size &canvas_size);

/// <summary>
/// Change position of emboss window
/// </summary>
/// <param name="output_window_offset"></param>
/// <param name="try_to_fix">When True Only move to be full visible otherwise reset position</param>
static void change_window_position(std::optional<ImVec2> &output_window_offset, bool try_to_fix);
} // namespace priv

void GLGizmoEmboss::on_set_state()
{
    // enable / disable bed from picking
    // Rotation gizmo must work through bed
    m_parent.set_raycaster_gizmos_on_top(m_state == GLGizmoBase::On);

    m_rotate_gizmo.set_state(m_state);

    // Closing gizmo. e.g. selecting another one
    if (m_state == GLGizmoBase::Off) {
        // refuse outgoing during text preview
        reset_volume();
        // Store order and last activ index into app.ini
        // TODO: what to do when can't store into file?
        m_style_manager.store_styles_to_app_config(false);
        remove_notification_not_valid_font();
    } else if (m_state == GLGizmoBase::On) {
        // to reload fonts from system, when install new one
        wxFontEnumerator::InvalidateCache();

        // Immediately after set state On is called function data_changed(), 
        // where one could distiguish undo/redo serialization from opening by letter 'T'
        // set_volume_by_selection();

        // change position of just opened emboss window
        if (m_allow_open_near_volume) {
            m_set_window_offset = priv::calc_fine_position(m_parent.get_selection(), get_minimal_window_size(), m_parent.get_canvas_size());
        } else {
            if (m_gui_cfg.has_value())
                priv::change_window_position(m_set_window_offset, false);
            else
                m_set_window_offset = ImVec2(-1, -1);
        }

        // when open by hyperlink it needs to show up
        // or after key 'T' windows doesn't appear
        m_parent.set_as_dirty();
    }
}

void GLGizmoEmboss::data_changed(bool is_serializing) {
    set_volume_by_selection();
    if (!is_serializing && m_volume == nullptr)
        close();
}

void GLGizmoEmboss::on_start_dragging() { m_rotate_gizmo.start_dragging(); }
void GLGizmoEmboss::on_stop_dragging()
{
    m_rotate_gizmo.stop_dragging();

    // TODO: when start second rotatiton previous rotation rotate draggers
    // This is fast fix for second try to rotate
    // When fixing, move grabber above text (not on side)
    m_rotate_gizmo.set_angle(PI/2);

    // apply rotation
    m_parent.do_rotate(L("Text-Rotate"));

    // Re-Calculate current angle of up vector
    const GLVolume *gl_volume = get_selected_gl_volume(m_parent.get_selection());
    assert(m_style_manager.is_active_font());
    assert(gl_volume != nullptr);
    if (m_style_manager.is_active_font() && gl_volume != nullptr) 
        m_style_manager.get_font_prop().angle = calc_up(gl_volume->world_matrix(), priv::up_limit);

    m_rotate_start_angle.reset();

    volume_transformation_changed();
}
void GLGizmoEmboss::on_dragging(const UpdateData &data) { m_rotate_gizmo.dragging(data); }

GLGizmoEmboss::GuiCfg GLGizmoEmboss::create_gui_configuration()
{
    GuiCfg cfg; // initialize by default values;

    float line_height = ImGui::GetTextLineHeight();
    float line_height_with_spacing = ImGui::GetTextLineHeightWithSpacing();
    float space = line_height_with_spacing - line_height;
    const ImGuiStyle &style  = ImGui::GetStyle();

    cfg.max_style_name_width = ImGui::CalcTextSize("Maximal font name, extended").x;

    cfg.icon_width = static_cast<unsigned int>(std::ceil(line_height));
    // make size pair number
    if (cfg.icon_width % 2 != 0) ++cfg.icon_width;

    cfg.delete_pos_x = cfg.max_style_name_width + space;
    int count_line_of_text = 3;
    cfg.text_size = ImVec2(-FLT_MIN, line_height_with_spacing * count_line_of_text);
    ImVec2 letter_m_size = ImGui::CalcTextSize("M");
    int count_letter_M_in_input = 12;
    cfg.input_width = letter_m_size.x * count_letter_M_in_input;
    GuiCfg::Translations &tr = cfg.translations;

    tr.font   = _u8L("Font");
    tr.height = _u8L("Height");
    tr.depth  = _u8L("Depth");

    float max_text_width = std::max({
        ImGui::CalcTextSize(tr.font.c_str()).x,
        ImGui::CalcTextSize(tr.height.c_str()).x,
        ImGui::CalcTextSize(tr.depth.c_str()).x});
    cfg.indent       = static_cast<float>(cfg.icon_width);
    cfg.input_offset = style.WindowPadding.x + cfg.indent + max_text_width + space;

    tr.use_surface  = _u8L("Use surface");
    tr.per_glyph    = _u8L("Per glyph orientation");
    tr.alignment    = _u8L("Alignment");
    tr.char_gap     = _u8L("Char gap");
    tr.line_gap     = _u8L("Line gap");
    tr.boldness     = _u8L("Boldness");
    tr.skew_ration  = _u8L("Skew ratio");
    tr.from_surface = _u8L("From surface");
    tr.rotation     = _u8L("Rotation");
    tr.collection   = _u8L("Collection");

    float max_advanced_text_width = std::max({
        ImGui::CalcTextSize(tr.use_surface.c_str()).x,
        ImGui::CalcTextSize(tr.per_glyph.c_str()).x,
        ImGui::CalcTextSize(tr.alignment.c_str()).x,
        ImGui::CalcTextSize(tr.char_gap.c_str()).x,
        ImGui::CalcTextSize(tr.line_gap.c_str()).x,
        ImGui::CalcTextSize(tr.boldness.c_str()).x,
        ImGui::CalcTextSize(tr.skew_ration.c_str()).x,
        ImGui::CalcTextSize(tr.from_surface.c_str()).x,
        ImGui::CalcTextSize(tr.rotation.c_str()).x + cfg.icon_width + 2*space,
        ImGui::CalcTextSize(tr.collection.c_str()).x });
    cfg.advanced_input_offset = max_advanced_text_width
        + 3 * space + cfg.indent;
    cfg.lock_offset = cfg.advanced_input_offset - (cfg.icon_width + space);
    // calculate window size
    float window_title = line_height + 2*style.FramePadding.y + 2 * style.WindowTitleAlign.y;
    float input_height = line_height_with_spacing + 2*style.FramePadding.y;
    float tree_header  = line_height_with_spacing;
    float separator_height = 1 + style.FramePadding.y;

    // "Text is to object" + radio buttons
    cfg.height_of_volume_type_selector = separator_height + line_height_with_spacing + input_height;

    float window_height = 
        window_title + // window title
        cfg.text_size.y +  // text field
        input_height * 4 + // font name + height + depth + style selector 
        tree_header +      // advance tree
        separator_height + // presets separator line
        line_height_with_spacing + // "Presets"
        2 * style.WindowPadding.y;
    float window_width = cfg.input_offset + cfg.input_width + 2*style.WindowPadding.x 
        + 2 * (cfg.icon_width + space);
    cfg.minimal_window_size = ImVec2(window_width, window_height);

    // 8 = useSurface, per glyph, charGap, lineGap, bold, italic, surfDist, rotation, textFaceToCamera
    // 4 = 1px for fix each edit image of drag float 
    float advance_height = input_height * 10 + 9;
    cfg.minimal_window_size_with_advance =
        ImVec2(cfg.minimal_window_size.x,
               cfg.minimal_window_size.y + advance_height);

    cfg.minimal_window_size_with_collections = 
        ImVec2(cfg.minimal_window_size_with_advance.x,
            cfg.minimal_window_size_with_advance.y + input_height);

    int max_style_image_width = cfg.max_style_name_width /2 -
                                2 * style.FramePadding.x;
    int max_style_image_height = 1.5 * input_height;
    cfg.max_style_image_size = Vec2i(max_style_image_width, max_style_image_height);
    cfg.face_name_size.y() = line_height_with_spacing;
    cfg.face_name_size.x() = cfg.input_width;
    cfg.face_name_texture_offset_x = cfg.input_width + space;
    return cfg;
}

EmbossStyles GLGizmoEmboss::create_default_styles()
{
    wxFontEnumerator::InvalidateCache();
    wxArrayString facenames = wxFontEnumerator::GetFacenames(Facenames::encoding);

    wxFont wx_font_normal = *wxNORMAL_FONT;
#ifdef __APPLE__
    // Set normal font to helvetica when possible
    for (const wxString &facename : facenames) {
        if (facename.IsSameAs("Helvetica")) {
            wx_font_normal = wxFont(wxFontInfo().FaceName(facename).Encoding(Facenames::encoding));
            break;
        }
    }
#endif // __APPLE__

    // https://docs.wxwidgets.org/3.0/classwx_font.html
    // Predefined objects/pointers: wxNullFont, wxNORMAL_FONT, wxSMALL_FONT, wxITALIC_FONT, wxSWISS_FONT
    EmbossStyles styles = {
        WxFontUtils::create_emboss_style(wx_font_normal, _u8L("NORMAL")), // wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT)
        WxFontUtils::create_emboss_style(*wxSMALL_FONT, _u8L("SMALL")),  // A font using the wxFONTFAMILY_SWISS family and 2 points smaller than wxNORMAL_FONT.
        WxFontUtils::create_emboss_style(*wxITALIC_FONT, _u8L("ITALIC")), // A font using the wxFONTFAMILY_ROMAN family and wxFONTSTYLE_ITALIC style and of the same size of wxNORMAL_FONT.
        WxFontUtils::create_emboss_style(*wxSWISS_FONT, _u8L("SWISS")),  // A font identic to wxNORMAL_FONT except for the family used which is wxFONTFAMILY_SWISS.
        WxFontUtils::create_emboss_style(wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD), _u8L("MODERN")),        
    };

    // Not all predefined font for wx must be valid TTF, but at least one style must be loadable
    styles.erase(std::remove_if(styles.begin(), styles.end(), [](const EmbossStyle& style) {
        wxFont wx_font = WxFontUtils::create_wxFont(style);

        // check that face name is setabled
        if (style.prop.face_name.has_value()) {
            wxString face_name(style.prop.face_name->c_str());
            wxFont wx_font_temp;
            if (!wx_font_temp.SetFaceName(face_name))
                return true;        
        }

        // Check that exsit valid TrueType Font for wx font
        return WxFontUtils::create_font_file(wx_font) == nullptr;
        }),styles.end()
    );

    // exist some valid style?
    if (!styles.empty())
        return styles;

    // No valid style in defult list
    // at least one style must contain loadable font
    wxFont wx_font;
    for (const wxString &face : facenames) {
        wx_font = wxFont(face);
        if (WxFontUtils::create_font_file(wx_font) != nullptr)
            break;
        wx_font = wxFont(); // NotOk
    }

    if (wx_font.IsOk()) {
        // use first alphabetic sorted installed font
        styles.push_back(WxFontUtils::create_emboss_style(wx_font, _u8L("First font")));
    } else {
        // On current OS is not installed any correct TTF font
        // use font packed with Slic3r
        std::string font_path = Slic3r::resources_dir() + "/fonts/NotoSans-Regular.ttf";
        styles.push_back(EmbossStyle{_u8L("Default font"), font_path, EmbossStyle::Type::file_path});
    }
    return styles;
}

namespace{
void init_text_lines(TextLinesModel &text_lines, const Selection& selection, /* const*/ StyleManager &style_manager, unsigned count_lines)
{    
    const GLVolume *gl_volume_ptr = selection.get_first_volume();
    if (gl_volume_ptr == nullptr)
        return;
    const GLVolume        &gl_volume = *gl_volume_ptr;
    const ModelObjectPtrs &objects   = selection.get_model()->objects;
    const ModelVolume *mv_ptr = get_model_volume(gl_volume, objects);
    if (mv_ptr == nullptr)
        return;
    const ModelVolume &mv = *mv_ptr;
    if (mv.is_the_only_one_part())
        return;

    const std::optional<TextConfiguration> &tc_opt = mv.text_configuration;
    if (!tc_opt.has_value())
        return;
    const TextConfiguration &tc = *tc_opt;

    // calculate count lines when not set
    if (count_lines == 0) {
        count_lines = get_count_lines(tc.text);
        if (count_lines == 0)
            return;
    }

    // prepare volumes to slice
    ModelVolumePtrs volumes = prepare_volumes_to_slice(mv);

    // For interactivity during drag over surface it must be from gl_volume not volume.
    Transform3d mv_trafo = gl_volume.get_volume_transformation().get_matrix();
    if (tc.fix_3mf_tr.has_value())
        mv_trafo = mv_trafo * (tc.fix_3mf_tr->inverse());
    text_lines.init(mv_trafo, volumes, style_manager, count_lines);
}

void init_new_text_line(TextLinesModel &text_lines, const Transform3d& new_text_tr, const ModelObject& mo, /* const*/ StyleManager &style_manager)
{
    // prepare volumes to slice
    ModelVolumePtrs volumes;
    volumes.reserve(mo.volumes.size());
    for (ModelVolume *volume : mo.volumes) {
        // only part could be surface for volumes
        if (!volume->is_model_part())
            continue;
        volumes.push_back(volume);
    }
    unsigned count_lines = 1;
    text_lines.init(new_text_tr, volumes, style_manager, count_lines);
}

}

void GLGizmoEmboss::reinit_text_lines(unsigned count_lines) {    
    init_text_lines(m_text_lines, m_parent.get_selection(), m_style_manager, count_lines);
}

void GLGizmoEmboss::set_volume_by_selection()
{
    const Selection &selection = m_parent.get_selection();
    const GLVolume *gl_volume = get_selected_gl_volume(selection);
    if (gl_volume == nullptr)
        return reset_volume();

    const ModelObjectPtrs &objects = m_parent.get_model()->objects;
    ModelVolume *volume = get_model_volume(*gl_volume, objects);
    if (volume == nullptr)
        return reset_volume();

    // is same volume as actual selected?
    if (volume->id() == m_volume_id && 
        m_volume != nullptr && 
        volume->text_configuration->style == m_volume->text_configuration->style)
        return;

    // for changed volume notification is NOT valid
    remove_notification_not_valid_font();

    // Do not use focused input value when switch volume(it must swith value)
    if (m_volume != nullptr && 
        m_volume != volume) // when update volume it changed id BUT not pointer
        ImGuiWrapper::left_inputs();

    // Is selected volume text volume?
    const std::optional<TextConfiguration>& tc_opt = volume->text_configuration;
    if (!tc_opt.has_value()) 
        return reset_volume();

    const TextConfiguration &tc = *tc_opt;
    const EmbossStyle    &style = tc.style;

    // Could exist OS without getter on face_name,
    // but it is able to restore font from descriptor
    // Soo default value must be TRUE
    bool is_font_installed = true; 
    wxString face_name;
    std::optional<std::string> face_name_opt = style.prop.face_name;
    if (face_name_opt.has_value()) {
        face_name = wxString(face_name_opt->c_str());

        // search in enumerated fonts
        // refresh list of installed font in the OS.
        init_face_names(m_face_names);
        m_face_names.is_init = false;

        auto cmp = [](const FaceName &fn, const wxString& face_name)->bool { return fn.wx_name < face_name; };
        const std::vector<FaceName> &faces = m_face_names.faces;
        auto it = std::lower_bound(faces.begin(), faces.end(), face_name, cmp);
        is_font_installed = it != faces.end() && it->wx_name == face_name;

        if (!is_font_installed) {
            const std::vector<wxString> &bad = m_face_names.bad;
            auto it_bad = std::lower_bound(bad.begin(), bad.end(), face_name);
            if (it_bad == bad.end() || *it_bad != face_name){
                // check if wx allowed to set it up - another encoding of name
                wxFontEnumerator::InvalidateCache();
                wxFont wx_font_; // temporary structure
                if (wx_font_.SetFaceName(face_name) && 
                    WxFontUtils::create_font_file(wx_font_) != nullptr // can load TTF file?
                    ) {
                    is_font_installed = true;
                    // QUESTION: add this name to allowed faces?
                    // Could create twin of font face name
                    // When not add it will be hard to select it again when change font
                }
            }
        }
    }

    wxFont wx_font;
    // load wxFont from same OS when font name is installed
    if (style.type == WxFontUtils::get_actual_type() && is_font_installed) 
        wx_font = WxFontUtils::load_wxFont(style.path);    

    // Flag that is selected same font
    bool is_exact_font = true;
    // Different OS or try found on same OS
    if (!wx_font.IsOk()) {
        is_exact_font = false;
        // Try create similar wx font by FontFamily
        wx_font = WxFontUtils::create_wxFont(style);
        if (is_font_installed)
            is_exact_font = wx_font.SetFaceName(face_name);        

        // Have to use some wxFont
        if (!wx_font.IsOk())
            wx_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    }
    assert(wx_font.IsOk());

    // Load style to style manager
    const auto& styles = m_style_manager.get_styles();
    auto has_same_name = [&style](const StyleManager::Item &style_item) -> bool {
        const EmbossStyle &es = style_item.style;
        return es.name == style.name;
    };
    auto it = std::find_if(styles.begin(), styles.end(), has_same_name);
    if (it == styles.end()) {
        // style was not found
        m_style_manager.load_style(style, wx_font);
    } else {
        // style name is in styles list
        size_t style_index = it - styles.begin();
        if (!m_style_manager.load_style(style_index)) {
            // can`t load stored style
            m_style_manager.erase(style_index);
            m_style_manager.load_style(style, wx_font);
        } else {
            // stored style is loaded, now set modification of style
            m_style_manager.get_style() = style;
            m_style_manager.set_wx_font(wx_font);
        }
    }
    
    if (!is_exact_font)
        create_notification_not_valid_font(tc);
        
    // The change of volume could show or hide part with setter on volume type
    if (m_volume == nullptr || 
        get_model_volume(m_volume_id, objects) == nullptr ||
        (m_volume->get_object()->volumes.size() == 1) != 
        (volume->get_object()->volumes.size() == 1)){
        m_should_set_minimal_windows_size = true;
    }

    // cancel previous job
    if (m_job_cancel != nullptr) {
        m_job_cancel->store(true);
        m_job_cancel = nullptr;
    }

    m_text   = tc.text;
    m_volume = volume;
    m_volume_id = volume->id();
        
    if (tc.style.prop.per_glyph)
        reinit_text_lines();

    // Calculate current angle of up vector
    assert(m_style_manager.is_active_font());
    if (m_style_manager.is_active_font()) 
        m_style_manager.get_font_prop().angle = calc_up(gl_volume->world_matrix(), priv::up_limit);    

    // calculate scale for height and depth inside of scaled object instance
    calculate_scale();    
}

void GLGizmoEmboss::reset_volume()
{
    if (m_volume == nullptr)
        return; // already reseted

    m_volume = nullptr;
    m_volume_id.id = 0;

    // No more need of current notification
    remove_notification_not_valid_font();
}

void GLGizmoEmboss::calculate_scale() {
    Transform3d to_world = m_parent.get_selection().get_first_volume()->world_matrix();
    auto to_world_linear = to_world.linear();
    auto calc = [&to_world_linear](const Vec3d &axe, std::optional<float>& scale)->bool {
        Vec3d  axe_world = to_world_linear * axe;
        double norm_sq   = axe_world.squaredNorm();
        if (is_approx(norm_sq, 1.)) {
            if (scale.has_value())
                scale.reset();
            else
                return false;
        } else {
            scale = sqrt(norm_sq);
        }
        return true;
    };

    bool exist_change = calc(Vec3d::UnitY(), m_scale_height);
    exist_change |= calc(Vec3d::UnitZ(), m_scale_depth);

    // Change of scale has to change font imgui font size
    if (exist_change)
        m_style_manager.clear_imgui_font();
}

#ifdef EXECUTE_PROCESS_ON_MAIN_THREAD
namespace priv {
// Run Job on main thread (blocking) - ONLY DEBUG
static inline void execute_job(std::shared_ptr<Job> j)
{
    struct MyCtl : public Job::Ctl
    {
        void update_status(int st, const std::string &msg = "") override{};
        bool was_canceled() const override { return false; }
        std::future<void> call_on_main_thread(std::function<void()> fn) override
        {
            return std::future<void>{};
        }
    } ctl;
    j->process(ctl);
    wxGetApp().plater()->CallAfter([j]() {
        std::exception_ptr e_ptr = nullptr;
        j->finalize(false, e_ptr);
    });
}
} // namespace priv
#endif

namespace priv {
static bool is_text_empty(const std::string &text) { return text.empty() || text.find_first_not_of(" \n\t\r") == std::string::npos; }
} // namespace priv

bool GLGizmoEmboss::process()
{
    // no volume is selected -> selection from right panel
    assert(m_volume != nullptr);
    if (m_volume == nullptr) return false;

    // without text there is nothing to emboss
    if (priv::is_text_empty(m_text)) return false;

    // exist loaded font file?
    if (!m_style_manager.is_active_font()) return false;
    
    DataUpdate data{priv::create_emboss_data_base(m_text, m_style_manager, m_text_lines, m_parent.get_selection(), m_volume->type(), m_job_cancel),
                    m_volume->id()};
    std::unique_ptr<Job> job = nullptr;

    // check cutting from source mesh
    bool &use_surface = data.text_configuration.style.prop.use_surface;
    bool  is_object   = m_volume->get_object()->volumes.size() == 1;
    if (use_surface && is_object) 
        use_surface = false;
    
    assert(!data.text_configuration.style.prop.per_glyph ||
        get_count_lines(m_text) == m_text_lines.get_lines().size());

    if (use_surface) {
        // Model to cut surface from.
        SurfaceVolumeData::ModelSources sources = create_volume_sources(m_volume);
        if (sources.empty()) 
            return false;

        Transform3d text_tr = m_volume->get_matrix();
        auto& fix_3mf = m_volume->text_configuration->fix_3mf_tr;
        if (fix_3mf.has_value())
            text_tr = text_tr * fix_3mf->inverse();

        // when it is new applying of use surface than move origin onto surfaca
        if (!m_volume->text_configuration->style.prop.use_surface) {
            auto offset = calc_surface_offset(m_parent.get_selection(), m_raycast_manager);
            if (offset.has_value())
                text_tr *= Eigen::Translation<double, 3>(*offset);
        }

        // check that there is not unexpected volume type
        bool is_valid_type = check(m_volume->type());
        assert(is_valid_type);
        if (!is_valid_type)
            return false;

        UpdateSurfaceVolumeData surface_data{std::move(data), {text_tr, std::move(sources)}};
        job = std::make_unique<UpdateSurfaceVolumeJob>(std::move(surface_data));                  
    } else {
        job = std::make_unique<UpdateJob>(std::move(data));
    }

#ifndef EXECUTE_PROCESS_ON_MAIN_THREAD
    auto &worker = wxGetApp().plater()->get_ui_job_worker();
    queue_job(worker, std::move(job));
#else 
    // Run Job on main thread (blocking) - ONLY DEBUG
    priv::execute_job(std::move(job));
#endif // EXECUTE_PROCESS_ON_MAIN_THREAD

    // notification is removed befor object is changed by job
    remove_notification_not_valid_font();
    return true;
}

void GLGizmoEmboss::close()
{
    // remove volume when text is empty
    if (m_volume != nullptr && 
        m_volume->text_configuration.has_value() &&
        priv::is_text_empty(m_text)) {
        Plater &p = *wxGetApp().plater();
        // is the text object?
        if (m_volume->is_the_only_one_part()) {
            // delete whole object
            p.remove(m_parent.get_selection().get_object_idx());
        } else {
            // delete text volume
            p.remove_selected();
        }
    }

    // close gizmo == open it again
    auto& mng = m_parent.get_gizmos_manager();
    if (mng.get_current_type() == GLGizmosManager::Emboss)
        mng.open_gizmo(GLGizmosManager::Emboss);
}

void GLGizmoEmboss::draw_window()
{
#ifdef ALLOW_DEBUG_MODE
    if (ImGui::Button("re-process")) process();
    if (ImGui::Button("add svg")) choose_svg_file();
#endif //  ALLOW_DEBUG_MODE

    // Setter of indent must be befor disable !!!
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, m_gui_cfg->indent);
    ScopeGuard indent_sc([](){ ImGui::PopStyleVar(/*ImGuiStyleVar_IndentSpacing*/); });

    // Disable all except selection of font, when open text from 3mf with unknown font
    m_imgui->disabled_begin(m_is_unknown_font);
    ScopeGuard unknown_font_sc([imgui = m_imgui]() { imgui->disabled_end(/*m_is_unknown_font*/); });

    draw_text_input();

    ImGui::Indent();
        // When unknown font is inside .3mf only font selection is allowed
        m_imgui->disabled_end(/*m_is_unknown_font*/);
        draw_font_list_line();
        m_imgui->disabled_begin(m_is_unknown_font);
        bool use_inch = wxGetApp().app_config->get_bool("use_inches");
        draw_height(use_inch);
        draw_depth(use_inch);
    ImGui::Unindent();

    // close advanced style property when unknown font is selected
    if (m_is_unknown_font && m_is_advanced_edit_style) 
        ImGui::SetNextTreeNodeOpen(false);

    if (ImGui::TreeNode(_u8L("Advanced").c_str())) {
        if (!m_is_advanced_edit_style) {
            set_minimal_window_size(true);
        } else {
            draw_advanced();
        }
        ImGui::TreePop();
    } else if (m_is_advanced_edit_style) 
        set_minimal_window_size(false);

    ImGui::Separator();

    draw_style_list();

    // Do not select volume type, when it is text object
    if (m_volume->get_object()->volumes.size() != 1) {
        ImGui::Separator();
        draw_model_type();
    }
       
#ifdef SHOW_WX_FONT_DESCRIPTOR
    if (is_selected_style)
        m_imgui->text_colored(ImGuiWrapper::COL_GREY_DARK, m_style_manager.get_style().path);
#endif // SHOW_WX_FONT_DESCRIPTOR

#ifdef SHOW_CONTAIN_3MF_FIX
    if (m_volume!=nullptr &&
        m_volume->text_configuration.has_value() &&
        m_volume->text_configuration->fix_3mf_tr.has_value()) {
        ImGui::SameLine();
        m_imgui->text_colored(ImGuiWrapper::COL_GREY_DARK, ".3mf");
        if (ImGui::IsItemHovered()) {
            Transform3d &fix = *m_volume->text_configuration->fix_3mf_tr;
            std::stringstream ss;
            ss << fix.matrix();            
            std::string filename = (m_volume->source.input_file.empty())? "unknown.3mf" :
                                   m_volume->source.input_file + ".3mf";
            ImGui::SetTooltip("Text configuation contain \n"
                              "Fix Transformation Matrix \n"
                              "%s\n"
                              "loaded from \"%s\" file.",
                              ss.str().c_str(), filename.c_str()
                );
        }
    }
#endif // SHOW_CONTAIN_3MF_FIX
#ifdef SHOW_ICONS_TEXTURE    
    auto &t = m_icons_texture;
    ImGui::Image((void *) t.get_id(), ImVec2(t.get_width(), t.get_height()));
#endif //SHOW_ICONS_TEXTURE
#ifdef SHOW_IMGUI_ATLAS
    const auto &atlas = m_style_manager.get_atlas();
    ImGui::Image(atlas.TexID, ImVec2(atlas.TexWidth, atlas.TexHeight));
#endif // SHOW_IMGUI_ATLAS

#ifdef ALLOW_OPEN_NEAR_VOLUME
    ImGui::SameLine();
    if (ImGui::Checkbox("##ALLOW_OPEN_NEAR_VOLUME", &m_allow_open_near_volume)) {
        if (m_allow_open_near_volume)
            m_set_window_offset = priv::calc_fine_position(m_parent.get_selection(), get_minimal_window_size(), m_parent.get_canvas_size());
    } else if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", ((m_allow_open_near_volume) ? 
            "Fix settings position":
            "Allow floating window near text").c_str());
    }
#endif // ALLOW_FLOAT_WINDOW
 }

#include "imgui/imgui_internal.h" // scroll bar existence

void GLGizmoEmboss::draw_text_input()
{
    auto create_range_text_prep = [&mng = m_style_manager, &text = m_text, &exist_unknown = m_text_contain_unknown_glyph]() {
        auto& ff = mng.get_font_file_with_cache();
        assert(ff.has_value());
        const auto &cn = mng.get_font_prop().collection_number;
        unsigned int font_index = (cn.has_value()) ? *cn : 0;
        return create_range_text(text, *ff.font_file, font_index, &exist_unknown);
    };
    
    double scale = m_scale_height.has_value() ? *m_scale_height : 1.;
    ImFont *imgui_font = m_style_manager.get_imgui_font();
    if (imgui_font == nullptr) {
        // try create new imgui font
        double screen_scale = wxDisplay(wxGetApp().plater()).GetScaleFactor();
        double imgui_scale = scale * screen_scale;
        m_style_manager.create_imgui_font(create_range_text_prep(), imgui_scale);
        imgui_font = m_style_manager.get_imgui_font();
    }
    bool exist_font = 
        imgui_font != nullptr &&
        imgui_font->IsLoaded() &&
        imgui_font->Scale > 0.f &&
        imgui_font->ContainerAtlas != nullptr;
    // NOTE: Symbol fonts doesn't have atlas 
    // when their glyph range is out of language character range
    if (exist_font) ImGui::PushFont(imgui_font);

    // show warning about incorrectness view of font
    std::string warning_tool_tip;
    if (!exist_font) {
        warning_tool_tip = _u8L("The text cannot be written using the selected font. Please try choosing a different font.");
    } else {
        auto append_warning = [&warning_tool_tip](std::string t) {
            if (!warning_tool_tip.empty()) 
                warning_tool_tip += "\n";
            warning_tool_tip += t;
        };

        if (priv::is_text_empty(m_text))  append_warning(_u8L("Embossed text cannot contain only white spaces."));
        if (m_text_contain_unknown_glyph) append_warning(_u8L("Text contains character glyph (represented by '?') unknown by font."));

        const FontProp &prop = m_style_manager.get_font_prop();
        if (prop.skew.has_value())     append_warning(_u8L("Text input doesn't show font skew."));
        if (prop.boldness.has_value()) append_warning(_u8L("Text input doesn't show font boldness."));
        if (prop.line_gap.has_value()) append_warning(_u8L("Text input doesn't show gap between lines."));
        auto &ff         = m_style_manager.get_font_file_with_cache();
        float imgui_size = StyleManager::get_imgui_font_size(prop, *ff.font_file, scale);
        if (imgui_size > StyleManager::max_imgui_font_size)
            append_warning(_u8L("Too tall, diminished font height inside text input."));
        if (imgui_size < StyleManager::min_imgui_font_size)
            append_warning(_u8L("Too small, enlarged font height inside text input."));
        bool is_multiline = m_text_lines.get_lines().size() > 1;
        if (is_multiline && (prop.align.first == FontProp::HorizontalAlign::center || prop.align.first == FontProp::HorizontalAlign::right))
            append_warning(_u8L("Text doesn't show current horizontal alignment."));
    }
    
    // flag for extend font ranges if neccessary
    // ranges can't be extend during font is activ(pushed)
    std::string range_text;
    float  window_height  = ImGui::GetWindowHeight();
    float  minimal_height = get_minimal_window_size().y;
    float  extra_height   = window_height - minimal_height;
    ImVec2 input_size(m_gui_cfg->text_size.x, m_gui_cfg->text_size.y + extra_height);
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_AutoSelectAll;
    if (ImGui::InputTextMultiline("##Text", &m_text, input_size, flags)) {
        if (m_style_manager.get_font_prop().per_glyph) {
            unsigned count_lines = get_count_lines(m_text);
            if (count_lines != m_text_lines.get_lines().size()) 
                // Necesarry to initialize count by given number (differ from stored in volume at the moment)
                reinit_text_lines(count_lines);         
        }
        process();
        range_text = create_range_text_prep();
    }

    if (exist_font) ImGui::PopFont();

    // warning tooltip has to be with default font
    if (!warning_tool_tip.empty()) {
        // Multiline input has hidden window for scrolling
        ImGuiWindow *input = ImGui::GetCurrentWindow()->DC.ChildWindows.front();

        const ImGuiStyle &style = ImGui::GetStyle();
        float scrollbar_width = (input->ScrollbarY) ? style.ScrollbarSize : 0.f;
        float scrollbar_height = (input->ScrollbarX) ? style.ScrollbarSize : 0.f;

        bool hovered = ImGui::IsItemHovered();
        if (hovered)
            ImGui::SetTooltip("%s", warning_tool_tip.c_str());

        ImVec2 cursor = ImGui::GetCursorPos();
        float width = ImGui::GetContentRegionAvailWidth();
        const ImVec2& padding = style.FramePadding;
        ImVec2 icon_pos(width - m_gui_cfg->icon_width - scrollbar_width + padding.x, 
                        cursor.y - m_gui_cfg->icon_width - scrollbar_height - 2*padding.y);
        
        ImGui::SetCursorPos(icon_pos);
        draw(get_icon(m_icons, IconType::exclamation, IconState::hovered));
        ImGui::SetCursorPos(cursor);
    }

    // NOTE: must be after ImGui::font_pop() 
    //          -> imgui_font has to be unused
    // IMPROVE: only extend not clear
    // Extend font ranges
    if (!range_text.empty() &&
        !m_imgui->contain_all_glyphs(imgui_font, range_text) )
        m_style_manager.clear_imgui_font();    
}

#include <boost/functional/hash.hpp>
#include "wx/hashmap.h"
std::size_t hash_value(wxString const &s)
{
    boost::hash<std::string> hasher;
    return hasher(s.ToStdString());
}

static std::string concat(std::vector<wxString> data) {
    std::stringstream ss;
    for (const auto &d : data) 
        ss << d.c_str() << ", ";
    return ss.str();
}

#include <boost/filesystem.hpp>
static boost::filesystem::path get_fontlist_cache_path()
{
    return boost::filesystem::path(data_dir()) / "cache" / "fonts.cereal";
}

// cache font list by cereal
#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/string.hpp>
#include <cereal/archives/binary.hpp>

// increase number when change struct FacenamesSerializer
#define FACENAMES_VERSION 1
struct FacenamesSerializer
{
    // hash number for unsorted vector of installed font into system
    size_t hash = 0;
    // assumption that is loadable
    std::vector<wxString> good;
    // Can't load for some reason
    std::vector<wxString> bad;
};

template<class Archive> void save(Archive &archive, wxString const &d)
{ std::string s(d.ToUTF8().data()); archive(s);}
template<class Archive> void load(Archive &archive, wxString &d)
{ std::string s; archive(s); d = s;} 

template<class Archive> void serialize(Archive &ar, FacenamesSerializer &t, const std::uint32_t version)
{
    // When performing a load, the version associated with the class
    // is whatever it was when that data was originally serialized
    // When we save, we'll use the version that is defined in the macro
    if (version != FACENAMES_VERSION) return;
    ar(t.hash, t.good, t.bad);
}
CEREAL_CLASS_VERSION(FacenamesSerializer, FACENAMES_VERSION); // register class version

#include <boost/nowide/fstream.hpp>
bool GLGizmoEmboss::store(const Facenames &facenames) {
    std::string cache_path = get_fontlist_cache_path().string();
    boost::nowide::ofstream file(cache_path, std::ios::binary);
    cereal::BinaryOutputArchive archive(file);
    std::vector<wxString> good;
    good.reserve(facenames.faces.size());
    for (const FaceName &face : facenames.faces) good.push_back(face.wx_name);
    FacenamesSerializer data = {facenames.hash, good, facenames.bad};

    assert(std::is_sorted(data.bad.begin(), data.bad.end()));
    assert(std::is_sorted(data.good.begin(), data.good.end()));

    try {
        archive(data);
    } catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << "Failed to write fontlist cache - " << cache_path << ex.what();
        return false;
    }
    return true;
}

bool GLGizmoEmboss::load(Facenames &facenames) {
    boost::filesystem::path path = get_fontlist_cache_path();
    std::string             path_str = path.string();
    if (!boost::filesystem::exists(path)) {
        BOOST_LOG_TRIVIAL(warning) << "Fontlist cache - '" << path_str << "' does not exists.";
        return false;
    }
    boost::nowide::ifstream file(path_str, std::ios::binary);
    cereal::BinaryInputArchive archive(file);
    
    FacenamesSerializer data;
    try {
        archive(data);
    } catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << "Failed to load fontlist cache - '" << path_str << "'. Exception: " << ex.what();
        return false;
    }

    assert(std::is_sorted(data.bad.begin(), data.bad.end()));
    assert(std::is_sorted(data.good.begin(), data.good.end()));

    facenames.hash = data.hash;
    facenames.faces.reserve(data.good.size());
    for (const wxString &face : data.good)
        facenames.faces.push_back({face});
    facenames.bad = data.bad;
    return true;
}

void GLGizmoEmboss::init_truncated_names(Facenames &face_names, float max_width)
{
    for (FaceName &face : face_names.faces) {
        std::string name_str(face.wx_name.ToUTF8().data());
        face.name_truncated = ImGuiWrapper::trunc(name_str, max_width);
    }
    face_names.has_truncated_names = true;
}

void GLGizmoEmboss::init_face_names(Facenames &face_names)
{
    Timer t("enumerate_fonts");
    if (face_names.is_init) return;
    face_names.is_init = true;

    // to reload fonts from system, when install new one
    wxFontEnumerator::InvalidateCache();

    // try load cache
    // Only not OS enumerated face has hash value 0
    if (face_names.hash == 0) {
        load(face_names);
        face_names.has_truncated_names = false;
    }

    using namespace std::chrono;
    steady_clock::time_point enumerate_start = steady_clock::now();
    ScopeGuard sg([&enumerate_start, &face_names = face_names]() {
        steady_clock::time_point enumerate_end = steady_clock::now();
        long long enumerate_duration = duration_cast<milliseconds>(enumerate_end - enumerate_start).count();
        BOOST_LOG_TRIVIAL(info) << "OS enumerate " << face_names.faces.size() << " fonts "
                                << "(+ " << face_names.bad.size() << " can't load "
                                << "= " << face_names.faces.size() + face_names.bad.size() << " fonts) "
                                << "in " << enumerate_duration << " ms\n" << concat(face_names.bad);
    });
    wxArrayString facenames = wxFontEnumerator::GetFacenames(face_names.encoding);
    size_t hash = boost::hash_range(facenames.begin(), facenames.end());
    // Zero value is used as uninitialized hash
    if (hash == 0) hash = 1;
    // check if it is same as last time
    if (face_names.hash == hash) { 
        // no new installed font
        BOOST_LOG_TRIVIAL(info) << "Same FontNames hash, cache is used. " 
            << "For clear cache delete file: " << get_fontlist_cache_path().string();
        return;
    }

    BOOST_LOG_TRIVIAL(info) << ((face_names.hash == 0) ?
        "FontName list is generate from scratch." :
        "Hash are different. Only previous bad fonts are used and set again as bad");
    face_names.hash = hash;
    
    // validation lambda
    auto is_valid_font = [encoding = face_names.encoding, bad = face_names.bad /*copy*/](const wxString &name) {
        if (name.empty()) return false;

        // vertical font start with @, we will filter it out
        // Not sure if it is only in Windows so filtering is on all platforms
        if (name[0] == '@') return false;        

        // previously detected bad font
        auto it = std::lower_bound(bad.begin(), bad.end(), name);
        if (it != bad.end() && *it == name) return false;

        wxFont wx_font(wxFontInfo().FaceName(name).Encoding(encoding));
        //*
        // Faster chech if wx_font is loadable but not 100%
        // names could contain not loadable font
        if (!WxFontUtils::can_load(wx_font)) return false;

        /*/
        // Slow copy of font files to try load font
        // After this all files are loadable
        auto font_file = WxFontUtils::create_font_file(wx_font);
        if (font_file == nullptr) 
            return false; // can't create font file
        // */
        return true;
    };

    face_names.faces.clear();
    face_names.bad.clear();
    face_names.faces.reserve(facenames.size());
    std::sort(facenames.begin(), facenames.end());
    for (const wxString &name : facenames) {
        if (is_valid_font(name)) {
            face_names.faces.push_back({name});
        }else{
            face_names.bad.push_back(name);
        }
    }
    assert(std::is_sorted(face_names.bad.begin(), face_names.bad.end()));
    face_names.has_truncated_names = false;
    store(face_names);
}

// create texture for visualization font face
void GLGizmoEmboss::init_font_name_texture() {
    Timer t("init_font_name_texture");
    // check if already exists
    GLuint &id = m_face_names.texture_id; 
    if (id != 0) return;
    // create texture for font
    GLenum target = GL_TEXTURE_2D;
    glsafe(::glGenTextures(1, &id));
    glsafe(::glBindTexture(target, id));
    glsafe(::glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    glsafe(::glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    const Vec2i &size = m_gui_cfg->face_name_size;
    GLint w = size.x(), h = m_face_names.count_cached_textures * size.y();
    std::vector<unsigned char> data(4*w * h, {0});
    const GLenum format = GL_RGBA, type = GL_UNSIGNED_BYTE;
    const GLint level = 0, internal_format = GL_RGBA, border = 0;
    glsafe(::glTexImage2D(target, level, internal_format, w, h, border, format, type, data.data()));

    // bind default texture
    GLuint no_texture_id = 0;
    glsafe(::glBindTexture(target, no_texture_id));

    // clear info about creation of texture - no one is initialized yet
    for (FaceName &face : m_face_names.faces) { 
        face.cancel = nullptr;
        face.is_created = nullptr;
    }

    // Prepare filtration cache
    m_face_names.hide = std::vector<bool>(m_face_names.faces.size(), {false});
}

void GLGizmoEmboss::draw_font_preview(FaceName& face, bool is_visible)
{
    // Limit for opened font files at one moment
    unsigned int &count_opened_fonts = m_face_names.count_opened_font_files; 
    // Size of texture
    ImVec2 size(m_gui_cfg->face_name_size.x(), m_gui_cfg->face_name_size.y());
    float  count_cached_textures_f = static_cast<float>(m_face_names.count_cached_textures);
    std::string state_text;
    // uv0 and uv1 set to pixel 0,0 in texture
    ImVec2 uv0(0.f, 0.f), uv1(1.f / size.x, 1.f / size.y / count_cached_textures_f);
    if (face.is_created != nullptr) {
        // not created preview 
        if (*face.is_created) {
            // Already created preview
            size_t texture_index = face.texture_index;
            uv0 = ImVec2(0.f, texture_index / count_cached_textures_f);
            uv1 = ImVec2(1.f, (texture_index + 1) / count_cached_textures_f);
        } else {
            // Not finished preview
            if (is_visible) {
                // when not canceled still loading
                state_text = std::string(" ") + (face.cancel->load() ?
                    _u8L("No symbol") :
                    (dots.ToStdString() + _u8L("Loading")));
            } else {
                // not finished and not visible cancel job
                face.is_created = nullptr;
                face.cancel->store(true);
            }
        }
    } else if (is_visible && count_opened_fonts < m_gui_cfg->max_count_opened_font_files) {
        ++count_opened_fonts;
        face.cancel     = std::make_shared<std::atomic_bool>(false);
        face.is_created = std::make_shared<bool>(false);

        const unsigned char gray_level = 5;
        // format type and level must match to texture data
        const GLenum format = GL_RGBA, type = GL_UNSIGNED_BYTE;
        const GLint  level = 0;
        // select next texture index
        size_t texture_index = (m_face_names.texture_index + 1) % m_face_names.count_cached_textures;

        // set previous cach as deleted
        for (FaceName &f : m_face_names.faces)
            if (f.texture_index == texture_index) {
                if (f.cancel != nullptr) f.cancel->store(true);
                f.is_created = nullptr;
            }

        m_face_names.texture_index = texture_index;
        face.texture_index         = texture_index;

        // render text to texture
        FontImageData data{
            m_text,
            face.wx_name,
            m_face_names.encoding,
            m_face_names.texture_id,
            m_face_names.texture_index,
            m_gui_cfg->face_name_size,
            gray_level,
            format,
            type,
            level,
            &count_opened_fonts,
            face.cancel,    // copy
            face.is_created // copy
        };
        auto  job    = std::make_unique<CreateFontImageJob>(std::move(data));
        auto &worker = wxGetApp().plater()->get_ui_job_worker();
        queue_job(worker, std::move(job));
    } else {
        // cant start new thread at this moment so wait in queue
        state_text = " " + dots.ToStdString() +  " " + _u8L("Queue");
    }

    if (!state_text.empty()) {
        ImGui::SameLine(m_gui_cfg->face_name_texture_offset_x);
        m_imgui->text(state_text);
    }

    ImGui::SameLine(m_gui_cfg->face_name_texture_offset_x);
    ImTextureID tex_id = (void *) (intptr_t) m_face_names.texture_id;
    ImGui::Image(tex_id, size, uv0, uv1);
}

bool GLGizmoEmboss::select_facename(const wxString &facename)
{
    if (!wxFontEnumerator::IsValidFacename(facename)) return false;
    // Select font
    const wxFontEncoding &encoding = m_face_names.encoding;
    wxFont wx_font(wxFontInfo().FaceName(facename).Encoding(encoding));
    if (!wx_font.IsOk()) return false;
#ifdef USE_PIXEL_SIZE_IN_WX_FONT
    // wx font could change source file by size of font
    int point_size = static_cast<int>(m_style_manager.get_font_prop().size_in_mm);
    wx_font.SetPointSize(point_size);
#endif // USE_PIXEL_SIZE_IN_WX_FONT
    if (!m_style_manager.set_wx_font(wx_font)) return false;
    process();
    return true;
}

void GLGizmoEmboss::draw_font_list_line()
{    
    bool exist_stored_style   = m_style_manager.exist_stored_style();
    bool exist_change_in_font = m_style_manager.is_font_changed();
    const std::string& font_text = m_gui_cfg->translations.font;
    if (exist_change_in_font || !exist_stored_style)
    //B18
        ImGuiWrapper::text_colored(ImGuiWrapper::COL_BLUE_LIGHT, font_text);
    else
        ImGuiWrapper::text(font_text);

    ImGui::SameLine(m_gui_cfg->input_offset);

    draw_font_list();

    bool exist_change = false;
    if (!m_is_unknown_font) {
        ImGui::SameLine();
        if (draw_italic_button())
            exist_change = true;
        ImGui::SameLine();
        if (draw_bold_button())
            exist_change = true;
    } else {
        // when exist unknown font add confirmation button
        ImGui::SameLine();
        // Apply for actual selected font
        if (ImGui::Button(_u8L("Apply").c_str()))
            exist_change = true;
    }

    EmbossStyle &style = m_style_manager.get_style();
    if (exist_change_in_font) {
        ImGui::SameLine(ImGui::GetStyle().FramePadding.x);
        if (draw_button(m_icons, IconType::undo)) {
            const EmbossStyle *stored_style = m_style_manager.get_stored_style();

            style.path          = stored_style->path;
            style.prop.boldness = stored_style->prop.boldness;
            style.prop.skew     = stored_style->prop.skew;

            wxFont new_wx_font = WxFontUtils::load_wxFont(style.path);
            if (new_wx_font.IsOk() && m_style_manager.set_wx_font(new_wx_font))
                exist_change = true;
        } else if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", _u8L("Revert font changes.").c_str());
    }

    if (exist_change) {
        m_style_manager.clear_glyphs_cache();
        if (m_style_manager.get_font_prop().per_glyph)
            reinit_text_lines(m_text_lines.get_lines().size());
        process();
    }
}

void GLGizmoEmboss::draw_font_list()
{
    // Set partial
    wxString actual_face_name;
    if (m_style_manager.is_active_font()) {
        const wxFont &wx_font = m_style_manager.get_wx_font();
        if (wx_font.IsOk())
            actual_face_name = wx_font.GetFaceName();
    }
    // name of actual selected font
    const char * selected = (!actual_face_name.empty()) ?
        actual_face_name.ToUTF8().data() : " --- ";

    // Do not remove font face during enumeration
    // When deletation of font appear this variable is set
    std::optional<size_t> del_index;

    // Code
    const char *popup_id = "##font_list_popup";
    const char *input_id = "##font_list_input";
    ImGui::SetNextItemWidth(m_gui_cfg->input_width);

    // change color of hint to normal text
    bool is_popup_open = ImGui::IsPopupOpen(popup_id);
    if (!is_popup_open) {
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::GetStyleColorVec4(ImGuiCol_Text));

        // Fix clearance of search input,
        // Sometime happens that search text not disapear after font select
        m_face_names.search.clear();
    }

    if (ImGui::InputTextWithHint(input_id, selected, &m_face_names.search)) {
        // update filtration result        
        m_face_names.hide = std::vector<bool>(m_face_names.faces.size(), {false});

        // search to uppercase
        std::string search = m_face_names.search; // copy
        std::transform(search.begin(), search.end(), search.begin(), ::toupper);

        for (FaceName &face : m_face_names.faces) {
            size_t index = &face - &m_face_names.faces.front();

            // font name to uppercase
            std::string name(face.wx_name.ToUTF8().data());
            std::transform(name.begin(), name.end(), name.begin(), ::toupper);

            // It should use C++ 20 feature https://en.cppreference.com/w/cpp/string/basic_string/starts_with
            bool start_with = boost::starts_with(name, search);
            m_face_names.hide[index] = !start_with; 
        }
    }
    if (!is_popup_open) 
        ImGui::PopStyleColor(); // revert changes for hint color

    const bool is_input_text_active = ImGui::IsItemActive();
    
    // is_input_text_activated
    if (ImGui::IsItemActivated())
        ImGui::OpenPopup(popup_id);
    
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
    ImGui::SetNextWindowSize({2*m_gui_cfg->input_width, ImGui::GetTextLineHeight()*10});
    ImGuiWindowFlags popup_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | 
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_ChildWindow;
    if (ImGui::BeginPopup(popup_id, popup_flags))
    {
        bool set_selection_focus = false;
        if (!m_face_names.is_init) {
            init_face_names(m_face_names);
            set_selection_focus = true;
        }

        if (!m_face_names.has_truncated_names)
            init_truncated_names(m_face_names, m_gui_cfg->input_width);
        
        if (m_face_names.texture_id == 0) 
            init_font_name_texture();

        for (FaceName &face : m_face_names.faces) {
            const wxString &wx_face_name = face.wx_name;
            size_t index = &face - &m_face_names.faces.front();

            // Filter for face names
            if (m_face_names.hide[index])
                continue;

            ImGui::PushID(index);
            ScopeGuard sg([]() { ImGui::PopID(); });
            bool is_selected = (actual_face_name == wx_face_name);
            ImVec2 selectable_size(0, m_gui_cfg->face_name_size.y());
            ImGuiSelectableFlags flags = 0;
            if (ImGui::Selectable(face.name_truncated.c_str(), is_selected, flags, selectable_size)) {
                if (!select_facename(wx_face_name)) {
                    del_index = index;
                    MessageDialog(wxGetApp().plater(), GUI::format_wxstr(_L("Font \"%1%\" can't be selected."), wx_face_name));
                }
            }
            // tooltip as full name of font face
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", wx_face_name.ToUTF8().data());

            // on first draw set focus on selected font
            if (set_selection_focus && is_selected)
                ImGui::SetScrollHereY();
            draw_font_preview(face, ImGui::IsItemVisible());
        }

        if (!ImGui::IsWindowFocused() || 
            (!is_input_text_active && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)))) {
            // closing of popup
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (m_face_names.is_init) {
        // Just one after close combo box
        // free texture and set id to zero
        m_face_names.is_init = false;
        m_face_names.hide.clear();
        // cancel all process for generation of texture
        for (FaceName &face : m_face_names.faces)
            if (face.cancel != nullptr)
                face.cancel->store(true);
        glsafe(::glDeleteTextures(1, &m_face_names.texture_id));
        m_face_names.texture_id = 0;

        // Remove value from search input
        ImGuiWrapper::left_inputs();
        m_face_names.search.clear();
    }

    // delete unloadable face name when try to use
    if (del_index.has_value()) {
        auto face = m_face_names.faces.begin() + (*del_index);
        std::vector<wxString>& bad = m_face_names.bad;
        // sorted insert into bad fonts
        auto it = std::upper_bound(bad.begin(), bad.end(), face->wx_name);
        bad.insert(it, face->wx_name);
        m_face_names.faces.erase(face);
        // update cached file
        store(m_face_names);
    }

#ifdef ALLOW_ADD_FONT_BY_FILE
    ImGui::SameLine();
    // select font file by file browser
    if (draw_button(IconType::open_file)) {
        if (choose_true_type_file()) { 
            process();
        }
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add file with font(.ttf, .ttc)");
#endif //  ALLOW_ADD_FONT_BY_FILE

#ifdef ALLOW_ADD_FONT_BY_OS_SELECTOR
    ImGui::SameLine();
    if (draw_button(IconType::system_selector)) {
        if (choose_font_by_wxdialog()) {
            process();
        }
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open dialog for choose from fonts.");
#endif //  ALLOW_ADD_FONT_BY_OS_SELECTOR

}

void GLGizmoEmboss::draw_model_type()
{
    bool is_last_solid_part = m_volume->is_the_only_one_part();
    std::string title = _u8L("Operation");
    if (is_last_solid_part) {
        ImVec4 color{.5f, .5f, .5f, 1.f};
        m_imgui->text_colored(color, title.c_str());
    } else {
        ImGui::Text("%s", title.c_str());
    }

    std::optional<ModelVolumeType> new_type;
    ModelVolumeType modifier = ModelVolumeType::PARAMETER_MODIFIER;
    ModelVolumeType negative = ModelVolumeType::NEGATIVE_VOLUME;
    ModelVolumeType part = ModelVolumeType::MODEL_PART;
    ModelVolumeType type = m_volume->type();

    //TRN EmbossOperation
    if (ImGui::RadioButton(_u8L("Join").c_str(), type == part))
        new_type = part;
    else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", _u8L("Click to change text into object part.").c_str());
    ImGui::SameLine();

    std::string last_solid_part_hint = _u8L("You can't change a type of the last solid part of the object.");
    if (ImGui::RadioButton(_CTX_utf8(L_CONTEXT("Cut", "EmbossOperation"), "EmbossOperation").c_str(), type == negative))
        new_type = negative;
    else if (ImGui::IsItemHovered()) {
        if (is_last_solid_part)
            ImGui::SetTooltip("%s", last_solid_part_hint.c_str());
        else if (type != negative)
            ImGui::SetTooltip("%s", _u8L("Click to change part type into negative volume.").c_str());
    }

    // In simple mode are not modifiers
    if (wxGetApp().plater()->printer_technology() != ptSLA && wxGetApp().get_mode() != ConfigOptionMode::comSimple) {
        ImGui::SameLine();
        if (ImGui::RadioButton(_u8L("Modifier").c_str(), type == modifier))
            new_type = modifier;
        else if (ImGui::IsItemHovered()) {
            if (is_last_solid_part)
                ImGui::SetTooltip("%s", last_solid_part_hint.c_str());
            else if (type != modifier)
                ImGui::SetTooltip("%s", _u8L("Click to change part type into modifier.").c_str());
        }
    }

    if (m_volume != nullptr && new_type.has_value() && !is_last_solid_part) {
        GUI_App &app    = wxGetApp();
        Plater * plater = app.plater();
        Plater::TakeSnapshot snapshot(plater, _L("Change Text Type"), UndoRedo::SnapshotType::GizmoAction);
        m_volume->set_type(*new_type);

        // move inside
        bool is_volume_move_inside  = (type == part);
        bool is_volume_move_outside = (*new_type == part);
         // Update volume position when switch (from part) or (into part)
        if ((is_volume_move_inside || is_volume_move_outside))
            process();

        // inspiration in ObjectList::change_part_type()
        // how to view correct side panel with objects
        ObjectList *obj_list = app.obj_list();
        wxDataViewItemArray sel = obj_list->reorder_volumes_and_get_selection(
            obj_list->get_selected_obj_idx(),
            [volume = m_volume](const ModelVolume *vol) { return vol == volume; });
        if (!sel.IsEmpty()) obj_list->select_item(sel.front());       

        // NOTE: on linux, function reorder_volumes_and_get_selection call GLCanvas3D::reload_scene(refresh_immediately = false)
        // which discard m_volume pointer and set it to nullptr also selection is cleared so gizmo is automaticaly closed
        auto &mng = m_parent.get_gizmos_manager();
        if (mng.get_current_type() != GLGizmosManager::Emboss)
            mng.open_gizmo(GLGizmosManager::Emboss);
        // TODO: select volume back - Ask @Sasa
    }
}

void GLGizmoEmboss::draw_style_rename_popup() {
    std::string& new_name = m_style_manager.get_style().name;
    const std::string &old_name = m_style_manager.get_stored_style()->name;
    std::string text_in_popup = GUI::format(_L("Rename style(%1%) for embossing text"), old_name) + ": ";
    ImGui::Text("%s", text_in_popup.c_str());
    
    bool is_unique = true;
    for (const auto &item : m_style_manager.get_styles()) {
        const EmbossStyle &style = item.style;
        if (&style == &m_style_manager.get_style())
            continue; // could be same as original name
        if (style.name == new_name) is_unique = false;
    }
    bool allow_change = false;
    //B18
    if (new_name.empty()) {
        m_imgui->text_colored(ImGuiWrapper::COL_BLUE_LIGHT, _u8L("Name can't be empty."));
    }else if (!is_unique) { 
        m_imgui->text_colored(ImGuiWrapper::COL_BLUE_LIGHT, _u8L("Name has to be unique."));
    } else {
        ImGui::NewLine();
        allow_change = true;
    }

    bool store = false;
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
    if (ImGui::InputText("##rename style", &new_name, flags) && allow_change) store = true;
    if (m_imgui->button(_L("OK"), ImVec2(0.f, 0.f), allow_change)) store = true;
    ImGui::SameLine();
    if (ImGui::Button(_u8L("Cancel").c_str())) {
        new_name = old_name;
        ImGui::CloseCurrentPopup();
    }

    if (store) {
        // rename style in all objects and volumes
        for (ModelObject *mo :wxGetApp().plater()->model().objects) {
            for (ModelVolume *mv : mo->volumes) { 
                if (!mv->text_configuration.has_value()) continue;
                std::string& name = mv->text_configuration->style.name;
                if (name != old_name) continue;
                name = new_name;
            }
        }
        
        m_style_manager.rename(new_name);
        m_style_manager.store_styles_to_app_config();
        ImGui::CloseCurrentPopup();
    }
}

void GLGizmoEmboss::draw_style_rename_button()
{
    bool can_rename = m_style_manager.exist_stored_style();
    std::string title = _u8L("Rename style");
    const char * popup_id = title.c_str();
    if (priv::draw_button(m_icons, IconType::rename, !can_rename)) {
        assert(m_style_manager.get_stored_style());
        ImGui::OpenPopup(popup_id);
    }
    else if (ImGui::IsItemHovered()) {
        if (can_rename) ImGui::SetTooltip("%s", _u8L("Rename current style.").c_str());
        else            ImGui::SetTooltip("%s", _u8L("Can't rename temporary style.").c_str());
    }
    if (ImGui::BeginPopupModal(popup_id, 0, ImGuiWindowFlags_AlwaysAutoResize)) {
        m_imgui->disable_background_fadeout_animation();
        draw_style_rename_popup();
        ImGui::EndPopup();
    }
}

void GLGizmoEmboss::draw_style_save_button(bool is_modified)
{
    if (draw_button(m_icons, IconType::save, !is_modified)) {
        // save styles to app config
        m_style_manager.store_styles_to_app_config();
    }else if (ImGui::IsItemHovered()) {
        std::string tooltip;
        if (!m_style_manager.exist_stored_style()) {
            tooltip = _u8L("First Add style to list.");
        } else if (is_modified) {
            tooltip = GUI::format(_L("Save %1% style"), m_style_manager.get_style().name);
        } else {
            tooltip = _u8L("No changes to save.");
        }
        ImGui::SetTooltip("%s", tooltip.c_str());
    }
}

void GLGizmoEmboss::draw_style_save_as_popup() {
    ImGui::Text("%s", (_u8L("New name of style") +": ").c_str());

    // use name inside of volume configuration as temporary new name
    std::string &new_name = m_volume->text_configuration->style.name;

    bool is_unique = true;
    for (const auto &item : m_style_manager.get_styles())
        if (item.style.name == new_name) is_unique = false;
        
    bool allow_change = false;
    //B18
    if (new_name.empty()) {
        m_imgui->text_colored(ImGuiWrapper::COL_BLUE_LIGHT, _u8L("Name can't be empty."));
    }else if (!is_unique) { 
        m_imgui->text_colored(ImGuiWrapper::COL_BLUE_LIGHT, _u8L("Name has to be unique."));
    } else {
        ImGui::NewLine();
        allow_change = true;
    }

    bool save_style = false;
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
    if (ImGui::InputText("##save as style", &new_name, flags))
        save_style = true;
        
    if (m_imgui->button(_L("OK"), ImVec2(0.f, 0.f), allow_change))
        save_style = true;

    ImGui::SameLine();
    if (ImGui::Button(_u8L("Cancel").c_str())){
        // write original name to volume TextConfiguration
        new_name = m_style_manager.get_style().name;
        ImGui::CloseCurrentPopup();
    }

    if (save_style && allow_change) {
        m_style_manager.add_style(new_name);
        m_style_manager.store_styles_to_app_config();
        ImGui::CloseCurrentPopup();
    }
}

void GLGizmoEmboss::draw_style_add_button()
{
    bool only_add_style = !m_style_manager.exist_stored_style();
    bool can_add        = true;
    if (only_add_style &&
        m_volume->text_configuration->style.type != WxFontUtils::get_actual_type())
        can_add = false;

    std::string title    = _u8L("Save as new style");
    const char *popup_id = title.c_str();
    // save as new style
    ImGui::SameLine();
    if (draw_button(m_icons, IconType::add, !can_add)) {
        if (!m_style_manager.exist_stored_style()) {
            m_style_manager.store_styles_to_app_config(wxGetApp().app_config);
        } else {
            ImGui::OpenPopup(popup_id);
        }
    } else if (ImGui::IsItemHovered()) {
        if (!can_add) {
            ImGui::SetTooltip("%s", _u8L("Only valid font can be added to style.").c_str());
        }else if (only_add_style) {
            ImGui::SetTooltip("%s", _u8L("Add style to my list.").c_str());
        } else {
            ImGui::SetTooltip("%s", _u8L("Save as new style.").c_str());
        }
    }

    if (ImGui::BeginPopupModal(popup_id, 0, ImGuiWindowFlags_AlwaysAutoResize)) {
        m_imgui->disable_background_fadeout_animation();
        draw_style_save_as_popup();
        ImGui::EndPopup();
    }
}

void GLGizmoEmboss::draw_delete_style_button() {
    bool is_stored  = m_style_manager.exist_stored_style();
    bool is_last    = m_style_manager.get_styles().size() == 1;
    bool can_delete = is_stored && !is_last;

    if (draw_button(m_icons, IconType::erase, !can_delete)) {
        std::string style_name = m_style_manager.get_style().name; // copy
        wxString dialog_title = _L("Remove style");
        size_t next_style_index = std::numeric_limits<size_t>::max();
        Plater *plater = wxGetApp().plater();
        bool exist_change = false;
        while (true) {
            // NOTE: can't use previous loaded activ index -> erase could change index
            size_t active_index = m_style_manager.get_style_index();
            next_style_index = (active_index > 0) ? active_index - 1 :
                                                   active_index + 1;
            
            if (next_style_index >= m_style_manager.get_styles().size()) {
                MessageDialog msg(plater, _L("Can't remove the last existing style."), dialog_title, wxICON_ERROR | wxOK);
                msg.ShowModal();
                break;
            }

            // IMPROVE: add function can_load?
            // clean unactivable styles
            if (!m_style_manager.load_style(next_style_index)) {
                m_style_manager.erase(next_style_index);
                exist_change = true;
                continue;
            }

            wxString message = GUI::format_wxstr(_L("Are you sure you want to permanently remove the \"%1%\" style?"), style_name);
            MessageDialog msg(plater, message, dialog_title, wxICON_WARNING | wxYES | wxNO);
            if (msg.ShowModal() == wxID_YES) {
                // delete style
                m_style_manager.erase(active_index);
                exist_change = true;
                process();
            } else {
                // load back style
                m_style_manager.load_style(active_index);
            }
            break;
        }
        if (exist_change)
            m_style_manager.store_styles_to_app_config(wxGetApp().app_config);
    }

    if (ImGui::IsItemHovered()) {
        const std::string &style_name = m_style_manager.get_style().name;
        std::string tooltip;
        if (can_delete)        tooltip = GUI::format(_L("Delete \"%1%\" style."), style_name);
        else if (is_last)      tooltip = GUI::format(_L("Can't delete \"%1%\". It is last style."), style_name);
        else/*if(!is_stored)*/ tooltip = GUI::format(_L("Can't delete temporary style \"%1%\"."), style_name);        
        ImGui::SetTooltip("%s", tooltip.c_str());
    }
}

// FIX IT: it should not change volume position before successfull change
void GLGizmoEmboss::fix_transformation(const FontProp &from,
                                       const FontProp &to)
{
    // fix Z rotation when exists difference in styles
    const std::optional<float> &f_angle_opt = from.angle;
    const std::optional<float> &t_angle_opt = to.angle;
    if (!is_approx(f_angle_opt, t_angle_opt)) {
        // fix rotation
        float f_angle = f_angle_opt.has_value() ? *f_angle_opt : .0f;
        float t_angle = t_angle_opt.has_value() ? *t_angle_opt : .0f;
        do_rotate(t_angle - f_angle);
    }

    // fix distance (Z move) when exists difference in styles
    const std::optional<float> &f_move_opt = from.distance;
    const std::optional<float> &t_move_opt = to.distance;
    if (!is_approx(f_move_opt, t_move_opt)) {
        float f_move = f_move_opt.has_value() ? *f_move_opt : .0f;
        float t_move = t_move_opt.has_value() ? *t_move_opt : .0f;
        do_translate(Vec3d::UnitZ() * (t_move - f_move));
    }
}

void GLGizmoEmboss::draw_style_list() {
    if (!m_style_manager.is_active_font()) return;

    const EmbossStyle *stored_style = nullptr;
    bool is_stored = m_style_manager.exist_stored_style();
    if (is_stored)
        stored_style = m_style_manager.get_stored_style();
    const EmbossStyle &actual_style = m_style_manager.get_style();
    bool is_changed = (stored_style)? !(*stored_style == actual_style) : true;    
    bool is_modified = is_stored && is_changed;

    const float &max_style_name_width = m_gui_cfg->max_style_name_width;
    std::string &trunc_name = m_style_manager.get_truncated_name();
    if (trunc_name.empty()) {
        // generate trunc name
        std::string current_name = actual_style.name;
        ImGuiWrapper::escape_double_hash(current_name);
        trunc_name = ImGuiWrapper::trunc(current_name, max_style_name_width);
    }

    std::string title = _u8L("Style");
    if (m_style_manager.exist_stored_style())
        ImGui::Text("%s", title.c_str());
    else
        //B18
        ImGui::TextColored(ImGuiWrapper::COL_BLUE_LIGHT, "%s", title.c_str());
        
    ImGui::SetNextItemWidth(m_gui_cfg->input_width);
    auto add_text_modify = [&is_modified](const std::string& name) {
        if (!is_modified) return name;
        return name + Preset::suffix_modified();
    };
    std::optional<size_t> selected_style_index;
    if (ImGui::BeginCombo("##style_selector", add_text_modify(trunc_name).c_str())) {
        m_style_manager.init_style_images(m_gui_cfg->max_style_image_size, m_text);
        m_style_manager.init_trunc_names(max_style_name_width);
        std::optional<std::pair<size_t,size_t>> swap_indexes;
        const std::vector<StyleManager::Item> &styles = m_style_manager.get_styles();
        for (const auto &item : styles) {
            size_t index = &item - &styles.front();
            const EmbossStyle &style = item.style;
            const std::string &actual_style_name = style.name;
            ImGui::PushID(actual_style_name.c_str());
            bool is_selected = (index == m_style_manager.get_style_index());

            ImVec2 select_size(0,m_gui_cfg->max_style_image_size.y()); // 0,0 --> calculate in draw
            const std::optional<StyleManager::StyleImage> &img = item.image;            
            // allow click delete button
            ImGuiSelectableFlags_ flags = ImGuiSelectableFlags_AllowItemOverlap; 
            if (ImGui::Selectable(item.truncated_name.c_str(), is_selected, flags, select_size)) {
                selected_style_index = index;
            } else if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", actual_style_name.c_str());

            // reorder items
            if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
                if (ImGui::GetMouseDragDelta(0).y < 0.f) {
                    if (index > 0) 
                        swap_indexes = {index, index - 1};
                } else if ((index + 1) < styles.size())
                    swap_indexes = {index, index + 1};
                if (swap_indexes.has_value()) 
                    ImGui::ResetMouseDragDelta();
            }

            // draw style name
            if (img.has_value()) {
                ImGui::SameLine(max_style_name_width);
                ImGui::Image(img->texture_id, img->tex_size, img->uv0, img->uv1);
            }

            ImGui::PopID();
        }
        if (swap_indexes.has_value()) 
            m_style_manager.swap(swap_indexes->first,
                                swap_indexes->second);
        ImGui::EndCombo();
    } else {
        // do not keep in memory style images when no combo box open
        m_style_manager.free_style_images();
        if (ImGui::IsItemHovered()) {            
            std::string style_name = add_text_modify(actual_style.name);
            std::string tooltip = is_modified?
                GUI::format(_L("Modified style \"%1%\""), actual_style.name):
                GUI::format(_L("Current style is \"%1%\""), actual_style.name);
            ImGui::SetTooltip(" %s", tooltip.c_str());
        }
    }
        
    // Check whether user wants lose actual style modification
    if (selected_style_index.has_value() && is_modified) { 
        const EmbossStyle &style = m_style_manager.get_styles()[*selected_style_index].style;        
        wxString message = GUI::format_wxstr(_L("Changing style to \"%1%\" will discard current style modification.\n\nWould you like to continue anyway?"), style.name);
        MessageDialog not_loaded_style_message(nullptr, message, _L("Warning"), wxICON_WARNING | wxYES | wxNO);
        if (not_loaded_style_message.ShowModal() != wxID_YES) 
            selected_style_index.reset();
    }

    // selected style from combo box
    if (selected_style_index.has_value()) {
        const EmbossStyle &style = m_style_manager.get_styles()[*selected_style_index].style;
        // create copy to be able do fix transformation only when successfully load style
        FontProp act_prop = actual_style.prop;  // copy
        FontProp new_prop = style.prop;         // copy
        if (m_style_manager.load_style(*selected_style_index)) {
            fix_transformation(act_prop, new_prop);
            process();
        } else {
            wxString title   = _L("Not valid style.");
            wxString message = GUI::format_wxstr(_L("Style \"%1%\" can't be used and will be removed from a list."), style.name);
            MessageDialog not_loaded_style_message(nullptr, message, title, wxOK);
            not_loaded_style_message.ShowModal();
            m_style_manager.erase(*selected_style_index);
        }
    }

    ImGui::SameLine();
    draw_style_rename_button();
        
    ImGui::SameLine();
    draw_style_save_button(is_modified);

    ImGui::SameLine();
    draw_style_add_button();

    // delete button
    ImGui::SameLine();
    draw_delete_style_button();
}

bool GLGizmoEmboss::draw_italic_button()
{
    const wxFont &wx_font = m_style_manager.get_wx_font(); 
    const auto& ff = m_style_manager.get_font_file_with_cache();
    if (!wx_font.IsOk() || !ff.has_value()) { 
        draw(get_icon(m_icons, IconType::italic, IconState::disabled));
        return false;
    }

    std::optional<float> &skew = m_style_manager.get_font_prop().skew;
    bool is_font_italic = skew.has_value() || WxFontUtils::is_italic(wx_font);
    if (is_font_italic) {
        // unset italic
        if (clickable(get_icon(m_icons, IconType::italic, IconState::hovered),
                      get_icon(m_icons, IconType::unitalic, IconState::hovered))) {
            skew.reset();
            if (wx_font.GetStyle() != wxFontStyle::wxFONTSTYLE_NORMAL) {
                wxFont new_wx_font = wx_font; // copy
                new_wx_font.SetStyle(wxFontStyle::wxFONTSTYLE_NORMAL);
                if(!m_style_manager.set_wx_font(new_wx_font))
                    return false;
            }
            return true;
        }
        if (ImGui::IsItemHovered()) 
            ImGui::SetTooltip("%s", _u8L("Unset italic").c_str());
    } else {
        // set italic
        if (draw_button(m_icons, IconType::italic)) {
            wxFont new_wx_font = wx_font; // copy
            auto new_ff = WxFontUtils::set_italic(new_wx_font, *ff.font_file);
            if (new_ff != nullptr) {
                if(!m_style_manager.set_wx_font(new_wx_font, std::move(new_ff)))
                    return false;
            } else {
                // italic font doesn't exist 
                // add skew when wxFont can't set it
                skew = 0.2f;
            }            
            return true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", _u8L("Set italic").c_str());
    }
    return false;
}

bool GLGizmoEmboss::draw_bold_button() {
    const wxFont &wx_font = m_style_manager.get_wx_font();
    const auto& ff = m_style_manager.get_font_file_with_cache();
    if (!wx_font.IsOk() || !ff.has_value()) {
        draw(get_icon(m_icons, IconType::bold, IconState::disabled));
        return false;
    }
    
    std::optional<float> &boldness = m_style_manager.get_font_prop().boldness;
    bool is_font_bold = boldness.has_value() || WxFontUtils::is_bold(wx_font);
    if (is_font_bold) {
        // unset bold
        if (clickable(get_icon(m_icons, IconType::bold, IconState::hovered),
                      get_icon(m_icons, IconType::unbold, IconState::hovered))) {
            boldness.reset();
            if (wx_font.GetWeight() != wxFontWeight::wxFONTWEIGHT_NORMAL) {
                wxFont new_wx_font = wx_font; // copy
                new_wx_font.SetWeight(wxFontWeight::wxFONTWEIGHT_NORMAL);
                if(!m_style_manager.set_wx_font(new_wx_font))
                    return false;
            }
            return true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", _u8L("Unset bold").c_str());
    } else {
        // set bold
        if (draw_button(m_icons, IconType::bold)) {
            wxFont new_wx_font = wx_font; // copy
            auto new_ff = WxFontUtils::set_bold(new_wx_font, *ff.font_file);
            if (new_ff != nullptr) {
                if(!m_style_manager.set_wx_font(new_wx_font, std::move(new_ff)))
                    return false;
            } else {
                // bold font can't be loaded
                // set up boldness
                boldness = 20.f;
                //font_file->cache.empty();
            }
            return true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", _u8L("Set bold").c_str());
    }
    return false;
}

template<typename T> bool exist_change(const T &value, const T *default_value){
    if (default_value == nullptr) return false;
    return (value != *default_value);
}

template<> bool exist_change(const std::optional<float> &value, const std::optional<float> *default_value){
    if (default_value == nullptr) return false;
    return !is_approx(value, *default_value);
}

template<> bool exist_change(const float &value, const float *default_value){
    if (default_value == nullptr) return false;
    return !is_approx(value, *default_value);
}

template<typename T, typename Draw>
bool GLGizmoEmboss::revertible(const std::string &name,
                               T                 &value,
                               const T           *default_value,
                               const std::string &undo_tooltip,
                               float              undo_offset,
                               Draw               draw)
{
    bool changed = exist_change(value, default_value);
    if (changed || default_value == nullptr)
    //B18
        ImGuiWrapper::text_colored(ImGuiWrapper::COL_BLUE_LIGHT, name);
    else
        ImGuiWrapper::text(name);

    // render revert changes button
    if (changed) {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        float prev_x = window->DC.CursorPosPrevLine.x;
        ImGui::SameLine(undo_offset); // change cursor postion
        if (draw_button(m_icons, IconType::undo)) {
            value = *default_value;
            return true;
        } else if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", undo_tooltip.c_str());
        window->DC.CursorPosPrevLine.x = prev_x; // set back previous position
    }
    return draw();
}


bool GLGizmoEmboss::rev_input(const std::string  &name,
                              float              &value,
                              const float       *default_value,
                              const std::string  &undo_tooltip,
                              float               step,
                              float               step_fast,
                              const char         *format,
                              ImGuiInputTextFlags flags)
{
    // draw offseted input
    auto draw_offseted_input = [&]()->bool{
        float input_offset = m_gui_cfg->input_offset;
        float input_width  = m_gui_cfg->input_width;
        ImGui::SameLine(input_offset);
        ImGui::SetNextItemWidth(input_width);
        return ImGui::InputFloat(("##" + name).c_str(),
            &value, step, step_fast, format, flags);
    };
    float undo_offset = ImGui::GetStyle().FramePadding.x;
    return revertible(name, value, default_value, undo_tooltip, undo_offset, draw_offseted_input);
}

bool GLGizmoEmboss::rev_input_mm(const std::string   &name,
                                 float               &value,
                                 const float         *default_value_ptr,
                                 const std::string   &undo_tooltip,
                                 float                step,
                                 float                step_fast,
                                 const char          *format,
                                 bool                 use_inch,
                                 const std::optional<float>& scale)
{
    // _variable which temporary keep value
    float  value_ = value;
    float  default_value_;
    if (use_inch) {
        // calc value in inch
        value_ *= ObjectManipulation::mm_to_in;
        if (default_value_ptr) {
            default_value_    = ObjectManipulation::mm_to_in * (*default_value_ptr);
            default_value_ptr = &default_value_;
        }
    }
    if (scale.has_value())        
        value_ *= *scale;
    bool use_correction = use_inch || scale.has_value();
    if (rev_input(name, use_correction ? value_ : value, default_value_ptr, undo_tooltip, step, step_fast, format)) {
        if (use_correction) {
            value = value_;
            if (use_inch)
                value *= ObjectManipulation::in_to_mm;
            if (scale.has_value())
                value /= *scale;
        }
        return true;
    }
    return false;
}

bool GLGizmoEmboss::rev_checkbox(const std::string &name,
                                 bool              &value,
                                 const bool        *default_value,
                                 const std::string &undo_tooltip)
{
    // draw offseted input
    auto draw_offseted_input = [&]() -> bool {
        ImGui::SameLine(m_gui_cfg->advanced_input_offset);
        return ImGui::Checkbox(("##" + name).c_str(), &value);
    };
    float undo_offset  = ImGui::GetStyle().FramePadding.x;
    return revertible(name, value, default_value, undo_tooltip,
                      undo_offset, draw_offseted_input);
}

bool GLGizmoEmboss::set_height() {
    float &value = m_style_manager.get_font_prop().size_in_mm;

    // size can't be zero or negative
    priv::Limits::apply(value, priv::limits.size_in_mm);

    if (m_volume == nullptr || !m_volume->text_configuration.has_value()) {
        assert(false);
        return false;
    }
    
    // only different value need process
    if (is_approx(value, m_volume->text_configuration->style.prop.size_in_mm))
        return false;
    
    if (m_style_manager.get_font_prop().per_glyph)
        reinit_text_lines(m_text_lines.get_lines().size());

#ifdef USE_PIXEL_SIZE_IN_WX_FONT
    // store font size into path serialization
    const wxFont &wx_font = m_style_manager.get_wx_font();
    if (wx_font.IsOk()) {
        wxFont wx_font_new = wx_font; // copy
        wx_font_new.SetPointSize(static_cast<int>(value));
        m_style_manager.set_wx_font(wx_font_new);
    }
#endif
    return true;
}

void GLGizmoEmboss::draw_height(bool use_inch)
{
    float &value = m_style_manager.get_font_prop().size_in_mm;
    const EmbossStyle* stored_style = m_style_manager.get_stored_style();
    const float *stored = (stored_style != nullptr)? &stored_style->prop.size_in_mm : nullptr;
    const char *size_format = use_inch ? "%.2f in" : "%.1f mm";
    const std::string revert_text_size = _u8L("Revert text size.");
    const std::string& name = m_gui_cfg->translations.height;
    if (rev_input_mm(name, value, stored, revert_text_size, 0.1f, 1.f, size_format, use_inch, m_scale_height))
        if (set_height())
            process();
}

bool GLGizmoEmboss::set_depth()
{
    float &value = m_style_manager.get_font_prop().emboss;

    // size can't be zero or negative
    priv::Limits::apply(value, priv::limits.emboss);

    // only different value need process
    return !is_approx(value, m_volume->text_configuration->style.prop.emboss);
}

void GLGizmoEmboss::draw_depth(bool use_inch)
{
    float &value = m_style_manager.get_font_prop().emboss;
    const EmbossStyle* stored_style = m_style_manager.get_stored_style();
    const float *stored = ((stored_style)? &stored_style->prop.emboss : nullptr);
    const std::string  revert_emboss_depth = _u8L("Revert embossed depth.");
    const char *size_format = ((use_inch) ? "%.3f in" : "%.2f mm");
    const std::string  name = m_gui_cfg->translations.depth;
    if (rev_input_mm(name, value, stored, revert_emboss_depth, 0.1f, 1.f, size_format, use_inch, m_scale_depth))
        if (set_depth())
            process();    
}

bool GLGizmoEmboss::rev_slider(const std::string &name,
                               std::optional<int>& value,
                               const std::optional<int> *default_value,
                               const std::string &undo_tooltip,
                               int                v_min,
                               int                v_max,
                               const std::string& format,
                               const wxString    &tooltip)
{    
    auto draw_slider_optional_int = [&]() -> bool {
        float slider_offset = m_gui_cfg->advanced_input_offset;
        float slider_width  = m_gui_cfg->input_width;
        ImGui::SameLine(slider_offset);
        ImGui::SetNextItemWidth(slider_width);
        return m_imgui->slider_optional_int( ("##" + name).c_str(), value, 
            v_min, v_max, format.c_str(), 1.f, false, tooltip);
    };
    float undo_offset = ImGui::GetStyle().FramePadding.x;
    return revertible(name, value, default_value,
        undo_tooltip, undo_offset, draw_slider_optional_int);
}

bool GLGizmoEmboss::rev_slider(const std::string &name,
                               std::optional<float>& value,
                               const std::optional<float> *default_value,
                               const std::string &undo_tooltip,
                               float                v_min,
                               float                v_max,
                               const std::string& format,
                               const wxString    &tooltip)
{    
    auto draw_slider_optional_float = [&]() -> bool {
        float slider_offset = m_gui_cfg->advanced_input_offset;
        float slider_width  = m_gui_cfg->input_width;
        ImGui::SameLine(slider_offset);
        ImGui::SetNextItemWidth(slider_width);
        return m_imgui->slider_optional_float(("##" + name).c_str(), value,
            v_min, v_max, format.c_str(), 1.f, false, tooltip);
    };
    float undo_offset = ImGui::GetStyle().FramePadding.x;
    return revertible(name, value, default_value,
        undo_tooltip, undo_offset, draw_slider_optional_float);
}

bool GLGizmoEmboss::rev_slider(const std::string &name,
                               float             &value,
                               const float       *default_value,
                               const std::string &undo_tooltip,
                               float              v_min,
                               float              v_max,
                               const std::string &format,
                               const wxString    &tooltip)
{    
    auto draw_slider_float = [&]() -> bool {
        float slider_offset = m_gui_cfg->advanced_input_offset;
        float slider_width  = m_gui_cfg->input_width;
        ImGui::SameLine(slider_offset);
        ImGui::SetNextItemWidth(slider_width);
        return m_imgui->slider_float("##" + name, &value, v_min, v_max,
            format.c_str(), 1.f, false, tooltip);
    };
    float undo_offset = ImGui::GetStyle().FramePadding.x;
    return revertible(name, value, default_value,
        undo_tooltip, undo_offset, draw_slider_float);
}

void GLGizmoEmboss::do_translate(const Vec3d &relative_move)
{
    assert(m_volume != nullptr);
    assert(m_volume->text_configuration.has_value());
    Selection &selection = m_parent.get_selection();
    assert(!selection.is_empty());
    selection.setup_cache();
    selection.translate(relative_move, TransformationType::Local);

    std::string snapshot_name; // empty mean no store undo / redo
    // NOTE: it use L instead of _L macro because prefix _ is appended inside
    // function do_move
    // snapshot_name = L("Set surface distance");
    m_parent.do_move(snapshot_name);
}

void GLGizmoEmboss::do_rotate(float relative_z_angle)
{
    assert(m_volume != nullptr);
    assert(m_volume->text_configuration.has_value());
    Selection &selection = m_parent.get_selection();
    assert(!selection.is_empty());
    selection.setup_cache();
    selection.rotate(Vec3d(0., 0., relative_z_angle), get_transformation_type(selection));

    std::string snapshot_name; // empty meand no store undo / redo
    // NOTE: it use L instead of _L macro because prefix _ is appended
    // inside function do_move
    // snapshot_name = L("Set text rotation");
    m_parent.do_rotate(snapshot_name);
}

void GLGizmoEmboss::draw_advanced()
{
    const auto &ff = m_style_manager.get_font_file_with_cache();
    if (!ff.has_value()) { 
        ImGui::Text("%s", _u8L("Advanced options cannot be changed for the selected font.\n"
                                   "Select another font.").c_str());
        return;
    }

    FontProp &font_prop = m_style_manager.get_font_prop();
    const FontFile::Info &font_info = get_font_info(*ff.font_file, font_prop);
#ifdef SHOW_FONT_FILE_PROPERTY
    ImGui::SameLine();
    int cache_size = ff.has_value()? (int)ff.cache->size() : 0;
    std::string ff_property = 
        "ascent=" + std::to_string(font_info.ascent) +
        ", descent=" + std::to_string(font_info.descent) +
        ", lineGap=" + std::to_string(font_info.linegap) +
        ", unitPerEm=" + std::to_string(font_info.unit_per_em) + 
        ", cache(" + std::to_string(cache_size) + " glyphs)";
    if (font_file->infos.size() > 1) { 
        unsigned int collection = font_prop.collection_number.has_value() ?
            *font_prop.collection_number : 0;
        ff_property += ", collect=" + std::to_string(collection+1) + "/" + std::to_string(font_file->infos.size());
    }
    m_imgui->text_colored(ImGuiWrapper::COL_GREY_DARK, ff_property);
#endif // SHOW_FONT_FILE_PROPERTY

    bool exist_change = false;
    auto &tr = m_gui_cfg->translations;

    const EmbossStyle *stored_style = nullptr;
    if (m_style_manager.exist_stored_style())
        stored_style = m_style_manager.get_stored_style();
    
    bool is_the_only_one_part = m_volume->is_the_only_one_part();
    bool can_use_surface = (font_prop.use_surface)? true : // already used surface must have option to uncheck
                            !is_the_only_one_part;
    m_imgui->disabled_begin(!can_use_surface);
    const bool *def_use_surface = stored_style ?
        &stored_style->prop.use_surface : nullptr;
    if (rev_checkbox(tr.use_surface, font_prop.use_surface, def_use_surface,
                     _u8L("Revert using of model surface."))) {
        if (font_prop.use_surface) {
            // when using surface distance is not used
            font_prop.distance.reset();

            // there should be minimal embossing depth
            if (font_prop.emboss < 0.1)
                font_prop.emboss = 1;
        }
        process();
    }
    m_imgui->disabled_end(); // !can_use_surface

    bool &per_glyph = font_prop.per_glyph;
    bool can_use_per_glyph = (per_glyph) ? true : // already used surface must have option to uncheck
                            !is_the_only_one_part;
    m_imgui->disabled_begin(!can_use_per_glyph);
    const bool *def_per_glyph = stored_style ? &stored_style->prop.per_glyph : nullptr;
    if (rev_checkbox(tr.per_glyph, per_glyph, def_per_glyph,
        _u8L("Revert Transformation per glyph."))) {
        if (per_glyph && !m_text_lines.is_init())
            reinit_text_lines();
        process();
    } else if (ImGui::IsItemHovered()) {
        if (per_glyph) {
            ImGui::SetTooltip("%s", _u8L("Set global orientation for whole text.").c_str());
        } else {
            ImGui::SetTooltip("%s", _u8L("Set position and orientation per glyph.").c_str());
            if (!m_text_lines.is_init())
                reinit_text_lines();
        }
    } else if (!per_glyph && m_text_lines.is_init())
        m_text_lines.reset();
    m_imgui->disabled_end(); // !can_use_per_glyph
        
    auto draw_align = [&align = font_prop.align, gui_cfg = m_gui_cfg, &icons = m_icons]() {
        bool is_change = false;
        ImGui::SameLine(gui_cfg->advanced_input_offset);
        if (align.first==FontProp::HorizontalAlign::left) draw(get_icon(icons, IconType::align_horizontal_left, IconState::hovered));
        else if (draw_button(icons, IconType::align_horizontal_left)) { align.first=FontProp::HorizontalAlign::left; is_change = true; }
        else if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", _CTX_utf8(L_CONTEXT("Left", "Alignment"), "Alignment").c_str());
        ImGui::SameLine();
        if (align.first==FontProp::HorizontalAlign::center) draw(get_icon(icons, IconType::align_horizontal_center, IconState::hovered));
        else if (draw_button(icons, IconType::align_horizontal_center)) { align.first=FontProp::HorizontalAlign::center; is_change = true; }
        else if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", _CTX_utf8(L_CONTEXT("Center", "Alignment"), "Alignment").c_str());
        ImGui::SameLine();
        if (align.first==FontProp::HorizontalAlign::right) draw(get_icon(icons, IconType::align_horizontal_right, IconState::hovered));
        else if (draw_button(icons, IconType::align_horizontal_right)) { align.first=FontProp::HorizontalAlign::right; is_change = true; }
        else if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", _CTX_utf8(L_CONTEXT("Right", "Alignment"), "Alignment").c_str());

        ImGui::SameLine();
        if (align.second==FontProp::VerticalAlign::top) draw(get_icon(icons, IconType::align_vertical_top, IconState::hovered));
        else if (draw_button(icons, IconType::align_vertical_top)) { align.second=FontProp::VerticalAlign::top; is_change = true; }
        else if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", _CTX_utf8(L_CONTEXT("Top", "Alignment"), "Alignment").c_str());
        ImGui::SameLine();
        if (align.second==FontProp::VerticalAlign::center) draw(get_icon(icons, IconType::align_vertical_center, IconState::hovered));
        else if (draw_button(icons, IconType::align_vertical_center)) { align.second=FontProp::VerticalAlign::center; is_change = true; }
        else if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", _CTX_utf8(L_CONTEXT("Middle", "Alignment"), "Alignment").c_str());
        ImGui::SameLine();
        if (align.second==FontProp::VerticalAlign::bottom) draw(get_icon(icons, IconType::align_vertical_bottom, IconState::hovered));
        else if (draw_button(icons, IconType::align_vertical_bottom)) { align.second=FontProp::VerticalAlign::bottom; is_change = true; }
        else if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", _CTX_utf8(L_CONTEXT("Bottom", "Alignment"), "Alignment").c_str());
        return is_change;
    };
    const FontProp::Align * def_align = stored_style ? &stored_style->prop.align : nullptr;
    float undo_offset = ImGui::GetStyle().FramePadding.x;
    if (revertible(tr.alignment, font_prop.align, def_align, _u8L("Revert alignment."), undo_offset, draw_align)) {
        if (font_prop.per_glyph)
            reinit_text_lines(m_text_lines.get_lines().size());
        // TODO: move with text in finalize to not change position
        process();
    }
    
    // TRN EmbossGizmo: font units
    std::string units = _u8L("points");
    std::string units_fmt = "%.0f " + units;
    
    // input gap between characters
    auto def_char_gap = stored_style ?
        &stored_style->prop.char_gap : nullptr;

    int half_ascent = font_info.ascent / 2;
    int min_char_gap = -half_ascent, max_char_gap = half_ascent;
    if (rev_slider(tr.char_gap, font_prop.char_gap, def_char_gap, _u8L("Revert gap between characters"), 
        min_char_gap, max_char_gap, units_fmt, _L("Distance between characters"))){
        // Condition prevent recalculation when insertint out of limits value by imgui input
        if (!priv::Limits::apply(font_prop.char_gap, priv::limits.char_gap) ||
            !m_volume->text_configuration->style.prop.char_gap.has_value() ||
            m_volume->text_configuration->style.prop.char_gap != font_prop.char_gap) {        
            // char gap is stored inside of imgui font atlas
            m_style_manager.clear_imgui_font();
            exist_change = true;
        }
    }

    // input gap between lines
    bool is_multiline = m_text_lines.get_lines().size() > 1;
    m_imgui->disabled_begin(!is_multiline);
    auto def_line_gap = stored_style ?
        &stored_style->prop.line_gap : nullptr;
    int  min_line_gap = -half_ascent, max_line_gap = half_ascent;
    if (rev_slider(tr.line_gap, font_prop.line_gap, def_line_gap, _u8L("Revert gap between lines"), 
        min_line_gap, max_line_gap, units_fmt, _L("Distance between lines"))){
        // Condition prevent recalculation when insertint out of limits value by imgui input
        if (!priv::Limits::apply(font_prop.line_gap, priv::limits.line_gap) ||
            !m_volume->text_configuration->style.prop.line_gap.has_value() ||
            m_volume->text_configuration->style.prop.line_gap != font_prop.line_gap) {        
            // line gap is planed to be stored inside of imgui font atlas
            m_style_manager.clear_imgui_font();
            if (font_prop.per_glyph)
                reinit_text_lines(m_text_lines.get_lines().size());
            exist_change = true;
        }
    }
    m_imgui->disabled_end(); // !is_multiline

    // input boldness
    auto def_boldness = stored_style ?
        &stored_style->prop.boldness : nullptr;
    if (rev_slider(tr.boldness, font_prop.boldness, def_boldness, _u8L("Undo boldness"), 
        priv::limits.boldness.gui.min, priv::limits.boldness.gui.max, units_fmt, _L("Tiny / Wide glyphs"))){
        if (!priv::Limits::apply(font_prop.boldness, priv::limits.boldness.values) ||
            !m_volume->text_configuration->style.prop.boldness.has_value() ||
            m_volume->text_configuration->style.prop.boldness != font_prop.boldness)
            exist_change = true;
    }

    // input italic
    auto def_skew = stored_style ?
        &stored_style->prop.skew : nullptr;
    if (rev_slider(tr.skew_ration, font_prop.skew, def_skew, _u8L("Undo letter's skew"),
        priv::limits.skew.gui.min, priv::limits.skew.gui.max, "%.2f", _L("Italic strength ratio"))){
        if (!priv::Limits::apply(font_prop.skew, priv::limits.skew.values) ||
            !m_volume->text_configuration->style.prop.skew.has_value() ||
            m_volume->text_configuration->style.prop.skew != font_prop.skew)
            exist_change = true;
    }
    
    // input surface distance
    bool allowe_surface_distance = 
        !m_volume->text_configuration->style.prop.use_surface &&
        !m_volume->is_the_only_one_part();
    std::optional<float> &distance = font_prop.distance;
    float prev_distance = distance.has_value() ? *distance : .0f,
          min_distance = -2 * font_prop.emboss,
          max_distance = 2 * font_prop.emboss;
    auto def_distance = stored_style ?
        &stored_style->prop.distance : nullptr;    
    m_imgui->disabled_begin(!allowe_surface_distance);
    
    const std::string undo_move_tooltip = _u8L("Undo translation");
    const wxString move_tooltip = _L("Distance of the center of the text to the model surface.");
    bool is_moved = false;
    bool use_inch = wxGetApp().app_config->get_bool("use_inches");
    if (use_inch) {
        std::optional<float> distance_inch;
        if (distance.has_value()) distance_inch = (*distance * ObjectManipulation::mm_to_in);
        std::optional<float> def_distance_inch;
        if (def_distance != nullptr) {
            if (def_distance->has_value()) def_distance_inch = ObjectManipulation::mm_to_in * (*(*def_distance));
            def_distance = &def_distance_inch;
        }
        min_distance *= ObjectManipulation::mm_to_in;
        max_distance *= ObjectManipulation::mm_to_in;
        if (rev_slider(tr.from_surface, distance_inch, def_distance, undo_move_tooltip, min_distance, max_distance, "%.3f in", move_tooltip)) {
            if (distance_inch.has_value()) {
                font_prop.distance = *distance_inch * ObjectManipulation::in_to_mm;
            } else {
                font_prop.distance.reset();
            }
            is_moved = true;
        }
    } else {
        if (rev_slider(tr.from_surface, distance, def_distance, undo_move_tooltip, 
        min_distance, max_distance, "%.2f mm", move_tooltip)) is_moved = true;
    }

    if (is_moved){
        if (font_prop.per_glyph){
            process();
        } else {
            m_volume->text_configuration->style.prop.distance = font_prop.distance;        
            float act_distance = font_prop.distance.has_value() ? *font_prop.distance : .0f;
            do_translate(Vec3d::UnitZ() * (act_distance - prev_distance));
        }
    }
    m_imgui->disabled_end();

    // slider for Clock-wise angle in degress
    // stored angle is optional CCW and in radians
    // Convert stored value to degress
    // minus create clock-wise roation from CCW
    const std::optional<float> &angle_opt = m_style_manager.get_font_prop().angle;
    float angle = angle_opt.has_value() ? *angle_opt: 0.f;
    float angle_deg = static_cast<float>(-angle * 180 / M_PI);
    float def_angle_deg_val = 
        (!stored_style || !stored_style->prop.angle.has_value()) ?
        0.f : (*stored_style->prop.angle * -180 / M_PI);
    float* def_angle_deg = stored_style ?
        &def_angle_deg_val : nullptr;
    if (rev_slider(tr.rotation, angle_deg, def_angle_deg, _u8L("Undo rotation"), 
        priv::limits.angle.min, priv::limits.angle.max, u8"%.2f °",
                   _L("Rotate text Clock-wise."))) {
        // convert back to radians and CCW
        float angle_rad = static_cast<float>(-angle_deg * M_PI / 180.0);
        priv::to_range_pi_pi(angle_rad);
                

        float diff_angle = angle_rad - angle;
        do_rotate(diff_angle);
        
        // calc angle after rotation
        const GLVolume *gl_volume = get_selected_gl_volume(m_parent.get_selection());
        assert(gl_volume != nullptr);
        assert(m_style_manager.is_active_font());
        if (m_style_manager.is_active_font() && gl_volume != nullptr) 
            m_style_manager.get_font_prop().angle = calc_up(gl_volume->world_matrix(), priv::up_limit);
        
        if (font_prop.per_glyph)
            reinit_text_lines(m_text_lines.get_lines().size());

        // recalculate for surface cut
        if (font_prop.use_surface || font_prop.per_glyph) 
            process();
    }

    // Keep up - lock button icon
    ImGui::SameLine(m_gui_cfg->lock_offset);
    const IconManager::Icon &icon = get_icon(m_icons, m_keep_up ? IconType::lock : IconType::unlock, IconState::activable);
    const IconManager::Icon &icon_hover = get_icon(m_icons, m_keep_up ? IconType::lock_bold : IconType::unlock_bold, IconState::activable);
    const IconManager::Icon &icon_disable = get_icon(m_icons, m_keep_up ? IconType::lock : IconType::unlock, IconState::disabled);
    if (button(icon, icon_hover, icon_disable)) {
        m_keep_up = !m_keep_up;
        if (m_keep_up) {
            // copy angle to volume
            m_volume->text_configuration->style.prop.angle = font_prop.angle;
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", (m_keep_up?
            _u8L("Unlock the text's rotation when moving text along the object's surface."):
            _u8L("Lock the text's rotation when moving text along the object's surface.")
        ).c_str());

    // when more collection add selector
    if (ff.font_file->infos.size() > 1) {
        ImGui::Text("%s", tr.collection.c_str());
        ImGui::SameLine(m_gui_cfg->advanced_input_offset);
        ImGui::SetNextItemWidth(m_gui_cfg->input_width);
        unsigned int selected = font_prop.collection_number.has_value() ?
                               *font_prop.collection_number : 0;
        if (ImGui::BeginCombo("## Font collection", std::to_string(selected).c_str())) {
            for (unsigned int i = 0; i < ff.font_file->infos.size(); ++i) {
                ImGui::PushID(1 << (10 + i));
                bool is_selected = (i == selected);
                if (ImGui::Selectable(std::to_string(i).c_str(), is_selected)) {
                    if (i == 0) font_prop.collection_number.reset();
                    else font_prop.collection_number = i;
                    exist_change = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        } else if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", _u8L("Select from True Type Collection.").c_str());
        }
    }

    if (exist_change) {
        m_style_manager.clear_glyphs_cache();
        if (m_style_manager.get_font_prop().per_glyph)
            reinit_text_lines();
        else
            m_text_lines.reset();
        process();
    }

    if (ImGui::Button(_u8L("Set text to face camera").c_str())) {
        assert(get_selected_volume(m_parent.get_selection()) == m_volume);
        const Camera &cam  = wxGetApp().plater()->get_camera();
        const FontProp &prop = m_style_manager.get_font_prop();
        if (priv::apply_camera_dir(cam, m_parent, m_keep_up) && 
            (prop.use_surface || prop.per_glyph)){
            if (prop.per_glyph)
                reinit_text_lines();
            process();
        }
    } else if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", _u8L("Orient the text towards the camera.").c_str());
    }

#ifdef ALLOW_DEBUG_MODE
    ImGui::Text("family = %s", (font_prop.family.has_value() ?
                                    font_prop.family->c_str() :
                                    " --- "));
    ImGui::Text("face name = %s", (font_prop.face_name.has_value() ?
                                       font_prop.face_name->c_str() :
                                       " --- "));
    ImGui::Text("style = %s",
                (font_prop.style.has_value() ? font_prop.style->c_str() :
                                                 " --- "));
    ImGui::Text("weight = %s", (font_prop.weight.has_value() ?
                                    font_prop.weight->c_str() :
                                    " --- "));

    std::string descriptor = style.path;
    ImGui::Text("descriptor = %s", descriptor.c_str());
#endif // ALLOW_DEBUG_MODE
}

void GLGizmoEmboss::set_minimal_window_size(bool is_advance_edit_style)
{
    ImVec2 window_size = ImGui::GetWindowSize();
    const ImVec2& min_win_size_prev = get_minimal_window_size();
    //ImVec2 diff(window_size.x - min_win_size_prev.x,
    //            window_size.y - min_win_size_prev.y);
    float diff_y = window_size.y - min_win_size_prev.y;
    m_is_advanced_edit_style = is_advance_edit_style;
    const ImVec2 &min_win_size = get_minimal_window_size();
    ImGui::SetWindowSize(ImVec2(0.f, min_win_size.y + diff_y),
                         ImGuiCond_Always);
    priv::change_window_position(m_set_window_offset, true);
}

ImVec2 GLGizmoEmboss::get_minimal_window_size() const
{
    ImVec2 res;
    if (!m_is_advanced_edit_style)
        res = m_gui_cfg->minimal_window_size;
    else if (!m_style_manager.has_collections())
        res = m_gui_cfg->minimal_window_size_with_advance;
    else
        res = m_gui_cfg->minimal_window_size_with_collections;

    bool is_object = m_volume->get_object()->volumes.size() == 1;
    if (!is_object)
        res.y += m_gui_cfg->height_of_volume_type_selector;
    return res;
}

#ifdef ALLOW_ADD_FONT_BY_OS_SELECTOR
bool GLGizmoEmboss::choose_font_by_wxdialog()
{
    wxFontData data;
    data.EnableEffects(false);
    data.RestrictSelection(wxFONTRESTRICT_SCALABLE);
    // set previous selected font
    EmbossStyle &selected_style = m_style_manager.get_style();
    if (selected_style.type == WxFontUtils::get_actual_type()) {
        std::optional<wxFont> selected_font = WxFontUtils::load_wxFont(
            selected_style.path);
        if (selected_font.has_value()) data.SetInitialFont(*selected_font);
    }

    wxFontDialog font_dialog(wxGetApp().mainframe, data);
    if (font_dialog.ShowModal() != wxID_OK) return false;

    data                = font_dialog.GetFontData();
    wxFont   wx_font       = data.GetChosenFont();
    size_t   font_index = m_style_manager.get_fonts().size();
    EmbossStyle emboss_style  = WxFontUtils::create_emboss_style(wx_font);

    // Check that deserialization NOT influence font
    // false - use direct selected wxFont in dialog
    // true - use font item (serialize and deserialize wxFont)
    bool use_deserialized_font = false;

    // Try load and use new added font
    if ((use_deserialized_font && !m_style_manager.load_style(font_index)) ||
        (!use_deserialized_font && !m_style_manager.load_style(emboss_style, wx_font))) {
        m_style_manager.erase(font_index);
        wxString message = GUI::format_wxstr(
            "Font \"%1%\" can't be used. Please select another.",
            emboss_style.name);
        wxString      title = "Selected font is NOT True-type.";
        MessageDialog not_loaded_font_message(nullptr, message, title, wxOK);
        not_loaded_font_message.ShowModal();
        return choose_font_by_wxdialog();
    }

    // fix dynamic creation of italic font
    const auto& cn = m_style_manager.get_font_prop().collection_number;
    unsigned int font_collection = cn.has_value() ? *cn : 0;
    const auto&ff = m_style_manager.get_font_file_with_cache();
    if (WxFontUtils::is_italic(wx_font) &&
        !Emboss::is_italic(*ff.font_file, font_collection)) {
        m_style_manager.get_font_prop().skew = 0.2;
    }
    return true;
}
#endif // ALLOW_ADD_FONT_BY_OS_SELECTOR

#if defined ALLOW_ADD_FONT_BY_FILE or defined ALLOW_DEBUG_MODE
namespace priv {
static std::string get_file_name(const std::string &file_path)
{
    size_t pos_last_delimiter = file_path.find_last_of("/\\");
    size_t pos_point          = file_path.find_last_of('.');
    size_t offset             = pos_last_delimiter + 1;
    size_t count              = pos_point - pos_last_delimiter - 1;
    return file_path.substr(offset, count);
}
} // namespace priv
#endif // ALLOW_ADD_FONT_BY_FILE || ALLOW_DEBUG_MODE

#ifdef ALLOW_ADD_FONT_BY_FILE
bool GLGizmoEmboss::choose_true_type_file()
{
    wxArrayString input_files;
    wxString      fontDir      = wxEmptyString;
    wxString      selectedFile = wxEmptyString;
    wxFileDialog  dialog(nullptr, "Choose one or more files (TTF, TTC):",
                        fontDir, selectedFile, file_wildcards(FT_FONTS),
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) dialog.GetPaths(input_files);
    if (input_files.IsEmpty()) return false;
    size_t index = m_style_manager.get_fonts().size();
    // use first valid font
    for (auto &input_file : input_files) {
        std::string path = std::string(input_file.c_str());
        std::string name = priv::get_file_name(path);
        //make_unique_name(name, m_font_list);
        const FontProp& prop = m_style_manager.get_font_prop();
        EmbossStyle style{ name, path, EmbossStyle::Type::file_path, prop };
        m_style_manager.add_font(style);
        // set first valid added font as active
        if (m_style_manager.load_style(index)) return true;
        m_style_manager.erase(index);       
    }
    return false;
}
#endif // ALLOW_ADD_FONT_BY_FILE

#ifdef ALLOW_DEBUG_MODE
bool GLGizmoEmboss::choose_svg_file()
{
    wxArrayString input_files;
    wxString      fontDir      = wxEmptyString;
    wxString      selectedFile = wxEmptyString;
    wxFileDialog  dialog(nullptr, _L("Choose SVG file")+":", fontDir,
                        selectedFile, file_wildcards(FT_SVG),
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) dialog.GetPaths(input_files);
    if (input_files.IsEmpty()) return false;
    if (input_files.size() != 1) return false;
    auto &      input_file = input_files.front();
    std::string path       = std::string(input_file.c_str());
    std::string name       = priv::get_file_name(path);

    NSVGimage *image = nsvgParseFromFile(path.c_str(), "mm", 96.0f);
    ExPolygons polys = NSVGUtils::to_ExPolygons(image);
    nsvgDelete(image);

    BoundingBox bb;
    for (const auto &p : polys) bb.merge(p.contour.points);
    const FontProp &fp = m_style_manager.get_font_prop();
    float scale   = fp.size_in_mm / std::max(bb.max.x(), bb.max.y());
    auto  project = std::make_unique<ProjectScale>(
        std::make_unique<ProjectZ>(fp.emboss / scale), scale);
    indexed_triangle_set its = polygons2model(polys, *project);
    return false;
    // test store:
    // for (auto &poly : polys) poly.scale(1e5);
    // SVG svg("converted.svg", BoundingBox(polys.front().contour.points));
    // svg.draw(polys);
    //return add_volume(name, its);
}
#endif // ALLOW_DEBUG_MODE

void GLGizmoEmboss::create_notification_not_valid_font(
    const TextConfiguration &tc)
{
    const EmbossStyle &es = m_style_manager.get_style();
    const auto &face_name_opt = es.prop.face_name;
    const std::string &face_name_3mf = tc.style.prop.face_name.value_or(tc.style.path);

    std::optional<std::string> face_name_by_wx;
    if (!face_name_opt.has_value()) {
        const wxFont& wx_font = m_style_manager.get_wx_font();
        if (wx_font.IsOk()) {
            wxString wx_face_name = wx_font.GetFaceName();
            if (!wx_face_name.empty())
                face_name_by_wx = std::string(wx_face_name.ToUTF8().data());
        }
    }
    const std::string &face_name = face_name_opt.value_or(face_name_by_wx.value_or(es.path));
    std::string text =
        GUI::format(_L("Can't load exactly same font(\"%1%\"). "
                       "Aplication selected a similar one(\"%2%\"). "
                       "You have to specify font for enable edit text."),
                    face_name_3mf, face_name);
    create_notification_not_valid_font(text);
}

void GLGizmoEmboss::create_notification_not_valid_font(const std::string &text) {
    // not neccessary, but for sure that old notification doesnt exist
    if (m_is_unknown_font)
        remove_notification_not_valid_font();
    m_is_unknown_font = true;

    auto type  = NotificationType::UnknownFont;
    auto level = NotificationManager::NotificationLevel::WarningNotificationLevel;
    auto notification_manager = wxGetApp().plater()->get_notification_manager();
    notification_manager->push_notification(type, level, text);
}

void GLGizmoEmboss::remove_notification_not_valid_font()
{
    if (!m_is_unknown_font) return;
    m_is_unknown_font      = false;
    auto type                 = NotificationType::UnknownFont;
    auto notification_manager = wxGetApp().plater()->get_notification_manager();
    notification_manager->close_notification_of_type(type);
}

void GLGizmoEmboss::init_icons()
{
    // icon order has to match the enum IconType
    std::vector<std::string> filenames{
        "edit_button.svg",
        "delete.svg",
        "add_copies.svg", 
        "save.svg", 
        "undo.svg",    
        "make_italic.svg",
        "make_unitalic.svg",
        "make_bold.svg",
        "make_unbold.svg",   
        "search.svg",
        "open.svg", 
        "exclamation.svg",   
        "lock_closed.svg",  // lock,
        "lock_closed_f.svg",// lock_bold,
        "lock_open.svg",    // unlock,
        "lock_open_f.svg",  // unlock_bold,
        "align_horizontal_left.svg", 
        "align_horizontal_center.svg",
        "align_horizontal_right.svg",
        "align_vertical_top.svg",
        "align_vertical_center.svg",
        "align_vertical_bottom.svg"
    };
    assert(filenames.size() == static_cast<size_t>(IconType::_count));
    std::string path = resources_dir() + "/icons/";
    for (std::string &filename : filenames) filename = path + filename;

    ImVec2 size(m_gui_cfg->icon_width, m_gui_cfg->icon_width);
    auto type = IconManager::RasterType::color_wite_gray;
    m_icons = m_icon_manager.init(filenames, size, type);
}

const IconManager::Icon &priv::get_icon(const IconManager::VIcons& icons, IconType type, IconState state) { return *icons[(unsigned) type][(unsigned) state]; }
bool priv::draw_button(const IconManager::VIcons &icons, IconType type, bool disable)
{
    return Slic3r::GUI::button(
        get_icon(icons, type, IconState::activable),
        get_icon(icons, type, IconState::hovered),
        get_icon(icons, type, IconState::disabled),
        disable
    );
}

/////////////
// priv namespace implementation
///////////////

DataBase priv::create_emboss_data_base(const std::string                  &text,
                                       StyleManager                       &style_manager,
                                       TextLinesModel                     &text_lines,
                                       const Selection                    &selection,
                                       ModelVolumeType                     type,
                                       std::shared_ptr<std::atomic<bool>> &cancel)
{
    // create volume_name
    std::string volume_name = text; // copy
    // contain_enter?
    if (volume_name.find('\n') != std::string::npos)
        // change enters to space
        std::replace(volume_name.begin(), volume_name.end(), '\n', ' ');

    if (!style_manager.is_active_font()) {
        style_manager.load_valid_style();
        assert(style_manager.is_active_font());
        if (!style_manager.is_active_font())
            return {}; // no active font in style, should never happend !!!
    }

    const EmbossStyle &es = style_manager.get_style();
    // actualize font path - during changes in gui it could be corrupted
    // volume must store valid path
    assert(style_manager.get_wx_font().IsOk());
    assert(es.path.compare(WxFontUtils::store_wxFont(style_manager.get_wx_font())) == 0);
    TextConfiguration tc{es, text};

    if (es.prop.per_glyph) {
        if (!text_lines.is_init())
            init_text_lines(text_lines, selection, style_manager);
    } else
        text_lines.reset();
    
    bool is_outside = (type == ModelVolumeType::MODEL_PART);

    // Cancel previous Job, when it is in process
    // worker.cancel(); --> Use less in this case I want cancel only previous EmbossJob no other jobs
    // Cancel only EmbossUpdateJob no others
    if (cancel != nullptr)
        cancel->store(true);
    // create new shared ptr to cancel new job
    cancel = std::make_shared<std::atomic<bool>>(false);
    return Slic3r::GUI::Emboss::DataBase{style_manager.get_font_file_with_cache(), tc, volume_name, is_outside, cancel, text_lines.get_lines()};
}

void priv::start_create_object_job(DataBase &emboss_data, const Vec2d &coor)
{
    // start creation of new object
    Plater        *plater    = wxGetApp().plater();
    const Camera  &camera    = plater->get_camera();
    const Pointfs &bed_shape = plater->build_volume().bed_shape();

    // can't create new object with distance from surface
    FontProp &prop = emboss_data.text_configuration.style.prop;
    if (prop.distance.has_value()) prop.distance.reset();

    // can't create new object with using surface
    if (prop.use_surface)
        prop.use_surface = false;

    //    Transform3d volume_tr = priv::create_transformation_on_bed(mouse_pos, camera, bed_shape, prop.emboss / 2);
    DataCreateObject data{std::move(emboss_data), coor, camera, bed_shape};
    auto             job    = std::make_unique<CreateObjectJob>(std::move(data));
    Worker          &worker = plater->get_ui_job_worker();
    queue_job(worker, std::move(job));
}

void priv::start_create_volume_job(const ModelObject *object,
                                   const Transform3d  volume_trmat,
                                   DataBase          &emboss_data,
                                   ModelVolumeType    volume_type)
{
    bool &use_surface = emboss_data.text_configuration.style.prop.use_surface;
    std::unique_ptr<GUI::Job> job;
    if (use_surface) {
        // Model to cut surface from.
        SurfaceVolumeData::ModelSources sources = create_sources(object->volumes);
        if (sources.empty()) {
            use_surface = false;
        } else {
            SurfaceVolumeData sfvd{volume_trmat, std::move(sources)};
            CreateSurfaceVolumeData surface_data{std::move(emboss_data), std::move(sfvd), volume_type, object->id()};
            job = std::make_unique<CreateSurfaceVolumeJob>(std::move(surface_data));
        }
    }
    if (!use_surface) {
        // create volume
        DataCreateVolume data{std::move(emboss_data), volume_type, object->id(), volume_trmat};
        job = std::make_unique<CreateVolumeJob>(std::move(data));
    }

    Plater *plater = wxGetApp().plater();
    Worker &worker = plater->get_ui_job_worker();
    queue_job(worker, std::move(job));
}

bool priv::start_create_volume_on_surface_job(DataBase       &emboss_data,
                                              ModelVolumeType volume_type,
                                              const Vec2d    &screen_coor,
                                              const GLVolume *gl_volume,
                                              RaycastManager &raycaster,
                                              TextLinesModel &text_lines,
                                              StyleManager   &style_manager,
                                              GLCanvas3D     &canvas)
{
    assert(gl_volume != nullptr);
    if (gl_volume == nullptr) return false;
    if (gl_volume->volume_idx() < 0) return false;

    Plater *plater = wxGetApp().plater();
    const ModelObjectPtrs &objects = plater->model().objects;

    int object_idx = gl_volume->object_idx();
    if (object_idx < 0 || static_cast<size_t>(object_idx) >= objects.size()) return false;
    const ModelObject *obj_ptr = objects[object_idx];
    if (obj_ptr == nullptr) return false;
    const ModelObject &obj = *obj_ptr;
    size_t vol_id = obj.volumes[gl_volume->volume_idx()]->id().id;
    auto cond = RaycastManager::AllowVolumes({vol_id});

    RaycastManager::Meshes meshes = create_meshes(canvas, cond);
    raycaster.actualize(obj, &cond, &meshes);

    const Camera &camera = plater->get_camera();
    std::optional<RaycastManager::Hit> hit = ray_from_camera(raycaster, screen_coor, camera, &cond);

    // context menu for add text could be open only by right click on an
    // object. After right click, object is selected and object_idx is set
    // also hit must exist. But there is options to add text by object list
    if (!hit.has_value())
        return false;

    // Create result volume transformation
    Transform3d surface_trmat = create_transformation_onto_surface(hit->position, hit->normal, priv::up_limit);
    const FontProp &font_prop = emboss_data.text_configuration.style.prop;
    apply_transformation(font_prop, surface_trmat);
    Transform3d instance = gl_volume->get_instance_transformation().get_matrix();
    Transform3d volume_trmat = instance.inverse() * surface_trmat; 

    if (font_prop.per_glyph){
        init_new_text_line(text_lines, volume_trmat, obj, style_manager);
        emboss_data.text_lines = text_lines.get_lines();
    }
    start_create_volume_job(obj_ptr, volume_trmat, emboss_data, volume_type);
    return true;
}

void priv::find_closest_volume(const Selection       &selection,
                               const Vec2d           &screen_center,
                               const Camera          &camera,
                               const ModelObjectPtrs &objects,
                               Vec2d                 *closest_center,
                               const GLVolume       **closest_volume)
{
    assert(closest_center != nullptr);
    assert(closest_volume != nullptr);
    assert(*closest_volume == nullptr);
    const Selection::IndicesList &indices = selection.get_volume_idxs();
    assert(!indices.empty()); // no selected volume
    if (indices.empty()) return;

    double center_sq_distance = std::numeric_limits<double>::max();
    for (unsigned int id : indices) {
        const GLVolume *gl_volume = selection.get_volume(id);
        const ModelVolume *volume = get_model_volume(*gl_volume, objects);
        if (volume == nullptr || !volume->is_model_part()) continue;
        Slic3r::Polygon hull = CameraUtils::create_hull2d(camera, *gl_volume);
        Vec2d c = hull.centroid().cast<double>();
        Vec2d d = c - screen_center;
        bool is_bigger_x = std::fabs(d.x()) > std::fabs(d.y());
        if ((is_bigger_x && d.x() * d.x() > center_sq_distance) ||
           (!is_bigger_x && d.y() * d.y() > center_sq_distance)) continue;

        double distance = d.squaredNorm();
        if (center_sq_distance < distance) continue;
        center_sq_distance = distance;
        *closest_center = c;
        *closest_volume = gl_volume;
    }
}

ImVec2 priv::calc_fine_position(const Selection &selection, const ImVec2 &windows_size, const Size &canvas_size)
{
    const Selection::IndicesList indices = selection.get_volume_idxs();
    // no selected volume
    if (indices.empty())
        return {};
    const GLVolume *volume = selection.get_volume(*indices.begin());
    // bad volume selected (e.g. deleted one)
    if (volume == nullptr)
        return {};

    const Camera   &camera = wxGetApp().plater()->get_camera();
    Slic3r::Polygon hull   = CameraUtils::create_hull2d(camera, *volume);

    ImVec2 c_size(canvas_size.get_width(), canvas_size.get_height());
    ImVec2 offset = ImGuiWrapper::suggest_location(windows_size, hull, c_size);
    return offset;
}

// Need internals to get window
#include "imgui/imgui_internal.h"
void priv::change_window_position(std::optional<ImVec2>& output_window_offset, bool try_to_fix) {
    const char* name = "Emboss";
    ImGuiWindow *window = ImGui::FindWindowByName(name);
    // is window just created 
    if (window == NULL)
        return;

    // position of window on screen
    ImVec2 position = window->Pos;
    ImVec2 size     = window->SizeFull;

    // screen size
    ImVec2 screen = ImGui::GetMainViewport()->Size;

    if (position.x < 0) {
        if (position.y < 0)
            output_window_offset = ImVec2(0, 0);
        else
            output_window_offset = ImVec2(0, position.y);
    } else if (position.y < 0) {
        output_window_offset = ImVec2(position.x, 0);
    } else if (screen.x < (position.x + size.x)) {
        if (screen.y < (position.y + size.y))
            output_window_offset = ImVec2(screen.x - size.x, screen.y - size.y);
        else
            output_window_offset = ImVec2(screen.x - size.x, position.y);
    } else if (screen.y < (position.y + size.y)) {
        output_window_offset = ImVec2(position.x, screen.y - size.y);
    }

    if (!try_to_fix && output_window_offset.has_value())
        output_window_offset = ImVec2(-1, -1); // Cannot 
}

bool priv::apply_camera_dir(const Camera &camera, GLCanvas3D &canvas, bool keep_up) {
    const Vec3d &cam_dir = camera.get_dir_forward();

    Selection &sel = canvas.get_selection();
    if (sel.is_empty()) return false;
    
    // camera direction transformed into volume coordinate system    
    Transform3d to_world = world_matrix_fixed(sel);
    Vec3d cam_dir_tr = to_world.inverse().linear() * cam_dir;
    cam_dir_tr.normalize();

    Vec3d emboss_dir(0., 0., -1.);

    // check wether cam_dir is already used
    if (is_approx(cam_dir_tr, emboss_dir)) return false;

    assert(sel.get_volume_idxs().size() == 1);
    GLVolume *gl_volume = sel.get_volume(*sel.get_volume_idxs().begin());

    Transform3d vol_rot;
    Transform3d vol_tr = gl_volume->get_volume_transformation().get_matrix();
    // check whether cam_dir is opposit to emboss dir
    if (is_approx(cam_dir_tr, -emboss_dir)) {
        // rotate 180 DEG by y
        vol_rot = Eigen::AngleAxis(M_PI_2, Vec3d(0., 1., 0.));
    } else {
        // calc params for rotation
        Vec3d axe = emboss_dir.cross(cam_dir_tr);
        axe.normalize();
        double angle = std::acos(emboss_dir.dot(cam_dir_tr));
        vol_rot = Eigen::AngleAxis(angle, axe);
    }

    Vec3d offset = vol_tr * Vec3d::Zero();
    Vec3d offset_inv = vol_rot.inverse() * offset;
    Transform3d res = vol_tr * 
        Eigen::Translation<double, 3>(-offset) * 
        vol_rot * 
        Eigen::Translation<double, 3>(offset_inv);
    //Transform3d res = vol_tr * vol_rot;
    gl_volume->set_volume_transformation(Geometry::Transformation(res));
    get_model_volume(*gl_volume, sel.get_model()->objects)->set_transformation(res);
    return true;
}

// any existing icon filename to not influence GUI
const std::string GLGizmoEmboss::M_ICON_FILENAME = "cut.svg";
