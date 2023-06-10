#include "EmbossJob.hpp"

#include <stdexcept>

#include <libslic3r/Model.hpp>
#include <libslic3r/Format/OBJ.hpp> // load_obj for default mesh
#include <libslic3r/CutSurface.hpp> // use surface cuts

#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoEmboss.hpp"
#include "slic3r/GUI/CameraUtils.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

using namespace Slic3r;
using namespace Slic3r::Emboss;
using namespace Slic3r::GUI;
using namespace Slic3r::GUI::Emboss;

// private namespace
namespace priv{
// create sure that emboss object is bigger than source object [in mm]
constexpr float safe_extension = 1.0f;

/// <summary>
/// Assert check of inputs data
/// </summary>
/// <param name="input"></param>
/// <returns></returns>
bool check(const DataBase &input, bool check_fontfile = true, bool use_surface = false);
bool check(const DataCreateVolume &input, bool is_main_thread = false);
bool check(const DataCreateObject &input);
bool check(const DataUpdate &input, bool is_main_thread = false, bool use_surface = false);
bool check(const CreateSurfaceVolumeData &input, bool is_main_thread = false);
bool check(const UpdateSurfaceVolumeData &input, bool is_main_thread = false);

template<typename Fnc> static ExPolygons create_shape(DataBase &input, Fnc was_canceled);

// <summary>
/// Try to create mesh from text
/// </summary>
/// <param name="input">Text to convert on mesh
/// + Shape of characters + Property of font</param>
/// <param name="font">Font file with cache
/// NOTE: Cache glyphs is changed</param>
/// <param name="was_canceled">To check if process was canceled</param>
/// <returns>Triangle mesh model</returns>
template<typename Fnc> static TriangleMesh try_create_mesh(DataBase &input, Fnc was_canceled);
template<typename Fnc> static TriangleMesh create_mesh(DataBase &input, Fnc was_canceled, Job::Ctl &ctl);

/// <summary>
/// Create default mesh for embossed text
/// </summary>
/// <returns>Not empty model(index trinagle set - its)</returns>
static TriangleMesh create_default_mesh();

/// <summary>
/// Must be called on main thread
/// </summary>
/// <param name="mesh">New mesh data</param>
/// <param name="data">Text configuration, ...</param>
/// <param name="mesh">Transformation of volume</param>
static void update_volume(TriangleMesh &&mesh, const DataUpdate &data, Transform3d *tr = nullptr);

/// <summary>
/// Add new volume to object
/// </summary>
/// <param name="mesh">triangles of new volume</param>
/// <param name="object_id">Object where to add volume</param>
/// <param name="type">Type of new volume</param>
/// <param name="trmat">Transformation of volume inside of object</param>
/// <param name="data">Text configuration and New VolumeName</param>
static void create_volume(TriangleMesh &&mesh, const ObjectID& object_id, 
    const ModelVolumeType type, const Transform3d trmat, const DataBase &data);

/// <summary>
/// Select Volume from objects
/// </summary>
/// <param name="objects">All objects in scene</param>
/// <param name="volume_id">Identifier of volume in object</param>
/// <returns>Pointer to volume when exist otherwise nullptr</returns>
static ModelVolume *get_volume(ModelObjectPtrs &objects, const ObjectID &volume_id);

/// <summary>
/// Create projection for cut surface from mesh
/// </summary>
/// <param name="tr">Volume transformation in object</param>
/// <param name="shape_scale">Convert shape to milimeters</param>
/// <param name="z_range">Bounding box 3d of model volume for projection ranges</param> 
/// <returns>Orthogonal cut_projection</returns>
static OrthoProject create_projection_for_cut(Transform3d tr, double shape_scale, const std::pair<float, float> &z_range);

/// <summary>
/// Create tranformation for emboss Cutted surface
/// </summary>
/// <param name="is_outside">True .. raise, False .. engrave</param>
/// <param name="emboss">Depth of embossing</param>
/// <param name="tr">Text voliume transformation inside object</param>
/// <param name="cut">Cutted surface from model</param>
/// <returns>Projection</returns>
static OrthoProject3d create_emboss_projection(bool is_outside, float emboss, Transform3d tr, SurfaceCut &cut);

/// <summary>
/// Cut surface into triangle mesh
/// </summary>
/// <param name="input1">(can't be const - cache of font)</param>
/// <param name="input2">SurfaceVolume data</param>
/// <param name="was_canceled">Check to interupt execution</param>
/// <returns>Extruded object from cuted surace</returns>
static TriangleMesh cut_surface(/*const*/ DataBase &input1, const SurfaceVolumeData &input2, std::function<bool()> was_canceled);

static void create_message(const std::string &message); // only in finalize
static bool process(std::exception_ptr &eptr);
static bool finalize(bool canceled, std::exception_ptr &eptr, const DataBase &input);

class JobException : public std::runtime_error { 
public: JobException(const char* message):runtime_error(message){}}; 

}// namespace priv

/////////////////
/// Create Volume
CreateVolumeJob::CreateVolumeJob(DataCreateVolume &&input)
    : m_input(std::move(input))
{
    assert(priv::check(m_input, true));
}

void CreateVolumeJob::process(Ctl &ctl) {
    if (!priv::check(m_input)) throw std::runtime_error("Bad input data for EmbossCreateVolumeJob.");
    auto was_canceled = [&ctl]()->bool { return ctl.was_canceled(); };
    m_result = priv::create_mesh(m_input, was_canceled, ctl);
    // center result
    Vec3f c = m_result.bounding_box().center().cast<float>();
    if (!c.isApprox(Vec3f::Zero())) m_result.translate(-c);
}

void CreateVolumeJob::finalize(bool canceled, std::exception_ptr &eptr) {
    if (!priv::finalize(canceled, eptr, m_input))
        return;
    if (m_result.its.empty()) 
        return priv::create_message("Can't create empty volume.");

    priv::create_volume(std::move(m_result), m_input.object_id, m_input.volume_type, m_input.trmat, m_input);
}


/////////////////
/// Create Object
CreateObjectJob::CreateObjectJob(DataCreateObject &&input)
    : m_input(std::move(input))
{
    assert(priv::check(m_input));
}

void CreateObjectJob::process(Ctl &ctl) 
{
    if (!priv::check(m_input))
        throw std::runtime_error("Bad input data for EmbossCreateObjectJob.");

    auto was_canceled = [&ctl]()->bool { return ctl.was_canceled(); };
    m_result = priv::create_mesh(m_input, was_canceled, ctl);
    if (was_canceled()) return;

    // Create new object
    // calculate X,Y offset position for lay on platter in place of
    // mouse click
    Vec2d bed_coor = CameraUtils::get_z0_position(
        m_input.camera, m_input.screen_coor);

    // check point is on build plate:
    Points bed_shape_;
    bed_shape_.reserve(m_input.bed_shape.size());
    for (const Vec2d &p : m_input.bed_shape)
        bed_shape_.emplace_back(p.cast<int>());
    Polygon bed(bed_shape_);
    if (!bed.contains(bed_coor.cast<int>()))
        // mouse pose is out of build plate so create object in center of plate
        bed_coor = bed.centroid().cast<double>();

    double z = m_input.text_configuration.style.prop.emboss / 2;
    Vec3d  offset(bed_coor.x(), bed_coor.y(), z);
    offset -= m_result.center();
    Transform3d::TranslationType tt(offset.x(), offset.y(), offset.z());
    m_transformation = Transform3d(tt);
}

void CreateObjectJob::finalize(bool canceled, std::exception_ptr &eptr)
{
    if (!priv::finalize(canceled, eptr, m_input))
        return;

    // only for sure
    if (m_result.empty()) 
        return priv::create_message("Can't create empty object.");

    GUI_App    &app      = wxGetApp();
    Plater     *plater   = app.plater();
    ObjectList *obj_list = app.obj_list();
    GLCanvas3D *canvas   = plater->canvas3D();

    plater->take_snapshot(_L("Add Emboss text object"));

    // Create new object and change selection
    bool center = false;
    obj_list->load_mesh_object(std::move(m_result), m_input.volume_name,
                                center, &m_input.text_configuration,
                                &m_transformation);

    // When add new object selection is empty.
    // When cursor move and no one object is selected than
    // Manager::reset_all() So Gizmo could be closed before end of creation object
    GLGizmosManager &manager = canvas->get_gizmos_manager();
    if (manager.get_current_type() != GLGizmosManager::Emboss)
        manager.open_gizmo(GLGizmosManager::Emboss);   

    // redraw scene
    canvas->reload_scene(true);
}

/////////////////
/// Update Volume
UpdateJob::UpdateJob(DataUpdate&& input)
    : m_input(std::move(input))
{
    assert(priv::check(m_input, true));
}

void UpdateJob::process(Ctl &ctl)
{
    if (!priv::check(m_input))
        throw std::runtime_error("Bad input data for EmbossUpdateJob.");

    auto was_canceled = [&ctl, &cancel = m_input.cancel]()->bool {
        if (cancel->load()) return true;
        return ctl.was_canceled();
    };
    m_result = priv::try_create_mesh(m_input, was_canceled);
    if (was_canceled()) return;
    if (m_result.its.empty())
        throw priv::JobException("Created text volume is empty. Change text or font.");

    // center triangle mesh
    Vec3d shift = m_result.bounding_box().center();
    m_result.translate(-shift.cast<float>());    
}

void UpdateJob::finalize(bool canceled, std::exception_ptr &eptr)
{
    if (!priv::finalize(canceled, eptr, m_input))
        return;
    priv::update_volume(std::move(m_result), m_input);    
}

namespace Slic3r::GUI::Emboss {

SurfaceVolumeData::ModelSources create_sources(const ModelVolumePtrs &volumes, std::optional<size_t> text_volume_id)
{
    SurfaceVolumeData::ModelSources result;
    result.reserve(volumes.size() - 1);
    for (const ModelVolume *v : volumes) {
        if (text_volume_id.has_value() && v->id().id == *text_volume_id) continue;
        // skip modifiers and negative volumes, ...
        if (!v->is_model_part()) continue;
        const TriangleMesh &tm = v->mesh();
        if (tm.empty()) continue;
        if (tm.its.empty()) continue;
        result.push_back({v->get_mesh_shared_ptr(), v->get_matrix()});
    }
    return result;
}

SurfaceVolumeData::ModelSources create_volume_sources(const ModelVolume *text_volume)
{
    if (text_volume == nullptr) return {};
    if (!text_volume->text_configuration.has_value()) return {};
    const ModelVolumePtrs &volumes = text_volume->get_object()->volumes;
    // no other volume in object
    if (volumes.size() <= 1) return {};
    return create_sources(volumes, text_volume->id().id);
}

} // namespace Slic3r::GUI::Emboss

/////////////////
/// Create Surface volume
CreateSurfaceVolumeJob::CreateSurfaceVolumeJob(CreateSurfaceVolumeData &&input) 
    : m_input(std::move(input))
{
    assert(priv::check(m_input, true));
}

void CreateSurfaceVolumeJob::process(Ctl &ctl) {
    if (!priv::check(m_input)) 
        throw std::runtime_error("Bad input data for CreateSurfaceVolumeJob.");
    // check cancelation of process
    auto was_canceled = [&ctl]() -> bool { return ctl.was_canceled(); };
    m_result = priv::cut_surface(m_input, m_input, was_canceled);
}

void CreateSurfaceVolumeJob::finalize(bool canceled, std::exception_ptr &eptr) {
    if (!priv::finalize(canceled, eptr, m_input))
        return; 
    priv::create_volume(std::move(m_result), m_input.object_id,
        m_input.volume_type, m_input.text_tr, m_input);
}

/////////////////
/// Cut Surface
UpdateSurfaceVolumeJob::UpdateSurfaceVolumeJob(UpdateSurfaceVolumeData &&input)
    : m_input(std::move(input))
{
    assert(priv::check(m_input, true));
}

void UpdateSurfaceVolumeJob::process(Ctl &ctl)
{
    if (!priv::check(m_input)) 
        throw std::runtime_error("Bad input data for UseSurfaceJob.");
    
    // check cancelation of process
    auto was_canceled = [&ctl, &cancel = m_input.cancel]()->bool {
        if (cancel->load()) return true;
        return ctl.was_canceled();
    };
    m_result = priv::cut_surface(m_input, m_input, was_canceled);
}

void UpdateSurfaceVolumeJob::finalize(bool canceled, std::exception_ptr &eptr)
{
    if (!priv::finalize(canceled, eptr, m_input))
        return;

    // when start using surface it is wanted to move text origin on surface of model
    // also when repeteadly move above surface result position should match
    Transform3d *tr = &m_input.text_tr;
    priv::update_volume(std::move(m_result), m_input, tr);
}

////////////////////////////
/// private namespace implementation
bool priv::check(const DataBase &input, bool check_fontfile, bool use_surface)
{
    bool res = true;
    if (check_fontfile) {
        assert(input.font_file.has_value());
        res &= input.font_file.has_value();
    }
    assert(!input.text_configuration.fix_3mf_tr.has_value());
    res &= !input.text_configuration.fix_3mf_tr.has_value();
    assert(!input.text_configuration.text.empty());
    res &= !input.text_configuration.text.empty();
    assert(!input.volume_name.empty());
    res &= !input.volume_name.empty();
    assert(input.text_configuration.style.prop.use_surface == use_surface);
    res &= input.text_configuration.style.prop.use_surface == use_surface;
    return res; 
}
bool priv::check(const DataCreateVolume &input, bool is_main_thread) {
    bool check_fontfile = false;
    bool res = check((DataBase) input, check_fontfile);
    assert(input.volume_type != ModelVolumeType::INVALID);
    res &= input.volume_type != ModelVolumeType::INVALID;
    assert(input.object_id.id >= 0);
    res &= input.object_id.id >= 0;
    return res; 
}
bool priv::check(const DataCreateObject &input) {
    bool check_fontfile = false;
    bool res = check((DataBase) input, check_fontfile);
    assert(input.screen_coor.x() >= 0.);
    res &= input.screen_coor.x() >= 0.;
    assert(input.screen_coor.y() >= 0.);
    res &= input.screen_coor.y() >= 0.;
    assert(input.bed_shape.size() >= 3); // at least triangle
    res &= input.bed_shape.size() >= 3;
    return res;
}
bool priv::check(const DataUpdate &input, bool is_main_thread, bool use_surface){
    bool check_fontfile = true;
    bool res = check((DataBase) input, check_fontfile, use_surface);
    assert(input.volume_id.id >= 0);
    res &= input.volume_id.id >= 0;
    if (is_main_thread)
        assert(get_volume(wxGetApp().model().objects, input.volume_id) != nullptr);
    assert(input.cancel != nullptr);
    res &= input.cancel != nullptr;
    if (is_main_thread)
        assert(!input.cancel->load());
    return res;
}
bool priv::check(const CreateSurfaceVolumeData &input, bool is_main_thread)
{
    bool use_surface = true;
    bool res = check((DataBase)input, is_main_thread, use_surface);
    assert(!input.sources.empty());
    res &= !input.sources.empty();
    return res;
}
bool priv::check(const UpdateSurfaceVolumeData &input, bool is_main_thread){
    bool use_surface = true;
    bool res = check((DataUpdate)input, is_main_thread, use_surface);
    assert(!input.sources.empty());
    res &= !input.sources.empty();
    return res;
}

template<typename Fnc> 
ExPolygons priv::create_shape(DataBase &input, Fnc was_canceled) {
    FontFileWithCache       &font = input.font_file;
    const TextConfiguration &tc   = input.text_configuration;
    const char              *text = tc.text.c_str();
    const FontProp          &prop = tc.style.prop;

    assert(font.has_value());
    if (!font.has_value())
        return {};

    return text2shapes(font, text, prop, was_canceled);
}

template<typename Fnc>
TriangleMesh priv::try_create_mesh(DataBase &input, Fnc was_canceled)
{
    ExPolygons shapes = priv::create_shape(input, was_canceled);
    if (shapes.empty()) return {};
    if (was_canceled()) return {}; 
    
    const FontProp &prop = input.text_configuration.style.prop;
    const FontFile &ff = *input.font_file.font_file;
    // NOTE: SHAPE_SCALE is applied in ProjectZ
    double scale = get_shape_scale(prop, ff) / SHAPE_SCALE;
    double depth = prop.emboss / scale;    
    auto  projectZ = std::make_unique<ProjectZ>(depth);
    ProjectScale project(std::move(projectZ), scale);
    if (was_canceled()) return {};
    return TriangleMesh(polygons2model(shapes, project));
}

template<typename Fnc>
TriangleMesh priv::create_mesh(DataBase &input, Fnc was_canceled, Job::Ctl& ctl)
{
    // It is neccessary to create some shape
    // Emboss text window is opened by creation new emboss text object
    TriangleMesh result;
    if (input.font_file.has_value()) {
        result = try_create_mesh(input, was_canceled);
        if (was_canceled()) return {};
    }

    if (result.its.empty()) {
        result = priv::create_default_mesh();
        if (was_canceled()) return {};
        // only info
        ctl.call_on_main_thread([]() {
            create_message("It is used default volume for embossed "
                                "text, try to change text or font to fix it.");
        });
    }

    assert(!result.its.empty());
    return result;
}

TriangleMesh priv::create_default_mesh()
{
    // When cant load any font use default object loaded from file
    std::string  path = Slic3r::resources_dir() + "/data/embossed_text.obj";
    TriangleMesh triangle_mesh;
    if (!load_obj(path.c_str(), &triangle_mesh)) {
        // when can't load mesh use cube
        return TriangleMesh(its_make_cube(36., 4., 2.5));
    }
    return triangle_mesh;
}

namespace{
void update_volume_name(const ModelVolume &volume, const ObjectList *obj_list)
{
    if (obj_list == nullptr)
        return;

    const std::vector<ModelObject *>* objects = obj_list->objects();
    if (objects == nullptr)
        return;

    int object_idx = -1;
    int volume_idx = -1;
    for (size_t oi = 0; oi < objects->size(); ++oi) {
        const ModelObject *mo = objects->at(oi);
        if (mo == nullptr)
            continue;
        if (volume.get_object()->id() != mo->id())
            continue;
        const ModelVolumePtrs& volumes = mo->volumes;
        for (size_t vi = 0; vi < volumes.size(); ++vi) {
            const ModelVolume *mv = volumes[vi];
            if (mv == nullptr)
                continue;
            if (mv->id() == volume.id()){
                object_idx = static_cast<int>(oi);
                volume_idx = static_cast<int>(vi);
                break;
            }
        }
        if (volume_idx > 0)
            break;
    }
    obj_list->update_name_in_list(object_idx, volume_idx);
}
}

void UpdateJob::update_volume(ModelVolume             *volume,
                              TriangleMesh           &&mesh,
                              const TextConfiguration &text_configuration,
                              std::string_view       volume_name)
{
    // check inputs
    bool is_valid_input = 
        volume != nullptr &&
        !mesh.empty() && 
        !volume_name.empty();
    assert(is_valid_input);
    if (!is_valid_input) return;

    // update volume
    volume->set_mesh(std::move(mesh));
    volume->set_new_unique_id();
    volume->calculate_convex_hull();
    volume->get_object()->invalidate_bounding_box();
    volume->text_configuration = text_configuration;

    // discard information about rotation, should not be stored in volume
    volume->text_configuration->style.prop.angle.reset();
        
    GUI_App &app = wxGetApp(); // may be move ObjectList and Plater to input?

    // update volume name in right panel( volume / object name)
    if (volume->name != volume_name) {
        volume->name = volume_name;
        update_volume_name(*volume, app.obj_list());
    }

    // When text is object.
    // When text positive volume is lowest part of object than modification of text 
    // have to move object on bed.
    if (volume->type() == ModelVolumeType::MODEL_PART)
        volume->get_object()->ensure_on_bed();

    // redraw scene
    Plater *plater = app.plater();
    if (plater == nullptr)
        return;

    // Update Model and redraw scene
    plater->update();
}

void priv::update_volume(TriangleMesh &&mesh, const DataUpdate &data, Transform3d* tr)
{
    // for sure that some object will be created
    if (mesh.its.empty()) 
        return create_message("Empty mesh can't be created.");

    Plater     *plater = wxGetApp().plater();
    GLCanvas3D *canvas = plater->canvas3D();

    // Check emboss gizmo is still open
    GLGizmosManager &manager  = canvas->get_gizmos_manager();
    if (manager.get_current_type() != GLGizmosManager::Emboss) 
        return;

    std::string snap_name = GUI::format(_L("Text: %1%"), data.text_configuration.text);
    Plater::TakeSnapshot snapshot(plater, snap_name, UndoRedo::SnapshotType::GizmoAction);
    ModelVolume *volume = get_volume(plater->model().objects, data.volume_id);

    // could appear when user delete edited volume
    if (volume == nullptr)
        return;

    if (tr) {
        volume->set_transformation(*tr);
    } else {
        // apply fix matrix made by store to .3mf
        const auto &tc = volume->text_configuration;
        assert(tc.has_value());
        if (tc.has_value() && tc->fix_3mf_tr.has_value())
            volume->set_transformation(volume->get_matrix() * tc->fix_3mf_tr->inverse());
    }

    UpdateJob::update_volume(volume, std::move(mesh), data.text_configuration, data.volume_name);
}

void priv::create_volume(
    TriangleMesh &&mesh, const ObjectID& object_id, 
    const ModelVolumeType type, const Transform3d trmat, const DataBase &data)
{
    GUI_App         &app      = wxGetApp();
    Plater          *plater   = app.plater();
    ObjectList      *obj_list = app.obj_list();
    GLCanvas3D      *canvas   = plater->canvas3D();
    ModelObjectPtrs &objects  = plater->model().objects;

    ModelObject *obj = nullptr;
    size_t object_idx = 0;
    for (; object_idx < objects.size(); ++object_idx) {
        ModelObject *o = objects[object_idx];
        if (o->id() == object_id) { 
            obj = o;
            break;
        }   
    }

    // Parent object for text volume was propably removed.
    // Assumption: User know what he does, so text volume is no more needed.
    if (obj == nullptr) 
        return priv::create_message("Bad object to create volume.");

    if (mesh.its.empty()) 
        return priv::create_message("Can't create empty volume.");

    plater->take_snapshot(_L("Add Emboss text Volume"));

    // NOTE: be carefull add volume also center mesh !!!
    // So first add simple shape(convex hull is also calculated)
    ModelVolume *volume = obj->add_volume(make_cube(1., 1., 1.), type);

    // TODO: Refactor to create better way to not set cube at begining
    // Revert mesh centering by set mesh after add cube
    volume->set_mesh(std::move(mesh));
    volume->calculate_convex_hull();


    // set a default extruder value, since user can't add it manually
    volume->config.set_key_value("extruder", new ConfigOptionInt(0));

    // do not allow model reload from disk
    volume->source.is_from_builtin_objects = true;

    volume->name               = data.volume_name; // copy
    volume->text_configuration = data.text_configuration; // copy

    // discard information about rotation, should not be stored in volume
    volume->text_configuration->style.prop.angle.reset();

    volume->set_transformation(trmat);

    // update printable state on canvas
    if (type == ModelVolumeType::MODEL_PART) {
        volume->get_object()->ensure_on_bed();
        canvas->update_instance_printable_state_for_object(object_idx);
    }

    // update volume name in object list
    // updata selection after new volume added
    // change name of volume in right panel
    // select only actual volume
    // when new volume is created change selection to this volume
    auto                add_to_selection = [volume](const ModelVolume *vol) { return vol == volume; };
    wxDataViewItemArray sel = obj_list->reorder_volumes_and_get_selection(object_idx, add_to_selection);
    if (!sel.IsEmpty()) obj_list->select_item(sel.front());

    obj_list->selection_changed();

    // Now is valid text volume selected open emboss gizmo
    GLGizmosManager &manager = canvas->get_gizmos_manager();
    if (manager.get_current_type() != GLGizmosManager::Emboss) 
        manager.open_gizmo(GLGizmosManager::Emboss);

    // update model and redraw scene
    //canvas->reload_scene(true);
    plater->update();
}

ModelVolume *priv::get_volume(ModelObjectPtrs &objects,
                               const ObjectID  &volume_id)
{
    for (ModelObject *obj : objects)
        for (ModelVolume *vol : obj->volumes)
            if (vol->id() == volume_id) return vol;
    return nullptr;
};

OrthoProject priv::create_projection_for_cut(
    Transform3d                    tr,
    double                         shape_scale,
    const std::pair<float, float> &z_range)
{
    double min_z = z_range.first - priv::safe_extension;
    double max_z = z_range.second + priv::safe_extension;
    assert(min_z < max_z);
    // range between min and max value
    double projection_size = max_z - min_z; 
    Matrix3d transformation_for_vector = tr.linear();
    // Projection must be negative value.
    // System of text coordinate
    // X .. from left to right
    // Y .. from bottom to top
    // Z .. from text to eye
    Vec3d untransformed_direction(0., 0., projection_size);
    Vec3d project_direction = transformation_for_vector * untransformed_direction;

    // Projection is in direction from far plane
    tr.translate(Vec3d(0., 0., min_z));
    tr.scale(shape_scale);
    return OrthoProject(tr, project_direction);
}

OrthoProject3d priv::create_emboss_projection(
    bool is_outside, float emboss, Transform3d tr, SurfaceCut &cut)
{
    // Offset of clossed side to model
    const float surface_offset = 0.015f; // [in mm]
    float 
        front_move = (is_outside) ? emboss : surface_offset,
        back_move  = -((is_outside) ? surface_offset : emboss);    
    its_transform(cut, tr.pretranslate(Vec3d(0., 0., front_move)));    
    Vec3d from_front_to_back(0., 0., back_move - front_move);
    return OrthoProject3d(from_front_to_back);
}

// input can't be const - cache of font
TriangleMesh priv::cut_surface(DataBase& input1, const SurfaceVolumeData& input2, std::function<bool()> was_canceled)
{
    ExPolygons shapes = create_shape(input1, was_canceled);
    if (shapes.empty())
        throw JobException(_u8L("Font doesn't have any shape for given text.").c_str());

    if (was_canceled()) return {};

    // Define alignment of text - left, right, center, top bottom, ....
    BoundingBox bb                = get_extents(shapes);
    Point       projection_center = bb.center();
    for (ExPolygon &shape : shapes) shape.translate(-projection_center);
    bb.translate(-projection_center);

    const FontFile &ff = *input1.font_file.font_file;
    const FontProp &fp = input1.text_configuration.style.prop;
    double shape_scale = get_shape_scale(fp, ff);

    const SurfaceVolumeData::ModelSources &sources = input2.sources;
    const SurfaceVolumeData::ModelSource  *biggest = nullptr;

    size_t biggest_count = 0;
    // convert index from (s)ources to (i)ndexed (t)riangle (s)ets
    std::vector<size_t> s_to_itss(sources.size(), std::numeric_limits<size_t>::max());
    std::vector<indexed_triangle_set>  itss;
    itss.reserve(sources.size());
    for (const SurfaceVolumeData::ModelSource &s : sources) {
        Transform3d mesh_tr_inv       = s.tr.inverse();
        Transform3d cut_projection_tr = mesh_tr_inv * input2.text_tr;
        std::pair<float, float> z_range{0., 1.};
        OrthoProject    cut_projection = create_projection_for_cut(cut_projection_tr, shape_scale, z_range);
        // copy only part of source model
        indexed_triangle_set its = its_cut_AoI(s.mesh->its, bb, cut_projection);
        if (its.indices.empty()) continue;
        if (biggest_count < its.vertices.size()) {
            biggest_count = its.vertices.size();
            biggest       = &s;
        }
        size_t source_index = &s - &sources.front();
        size_t its_index = itss.size();
        s_to_itss[source_index] = its_index;
        itss.emplace_back(std::move(its));
    }
    if (itss.empty())
        throw JobException(_u8L("There is no volume in projection direction.").c_str());

    Transform3d tr_inv = biggest->tr.inverse();
    Transform3d cut_projection_tr = tr_inv * input2.text_tr;

    size_t        itss_index = s_to_itss[biggest - &sources.front()];
    BoundingBoxf3 mesh_bb    = bounding_box(itss[itss_index]);
    for (const SurfaceVolumeData::ModelSource &s : sources) {
        size_t itss_index = s_to_itss[&s - &sources.front()];
        if (itss_index == std::numeric_limits<size_t>::max()) continue;
        if (&s == biggest) 
            continue;

        Transform3d tr = s.tr * tr_inv;
        bool fix_reflected = true;
        indexed_triangle_set &its = itss[itss_index];
        its_transform(its, tr, fix_reflected);
        BoundingBoxf3 bb = bounding_box(its);
        mesh_bb.merge(bb);
    }

    // tr_inv = transformation of mesh inverted
    Transform3d   emboss_tr  = cut_projection_tr.inverse();
    BoundingBoxf3 mesh_bb_tr = mesh_bb.transformed(emboss_tr);
    std::pair<float, float> z_range{mesh_bb_tr.min.z(), mesh_bb_tr.max.z()};
    OrthoProject cut_projection = create_projection_for_cut(cut_projection_tr, shape_scale, z_range);
    float projection_ratio = (-z_range.first + safe_extension) / (z_range.second - z_range.first + 2 * safe_extension);

    bool is_text_reflected = Slic3r::has_reflection(input2.text_tr);
    if (is_text_reflected) {
        // revert order of points in expolygons
        // CW --> CCW
        for (ExPolygon &shape : shapes) {
            shape.contour.reverse();
            for (Slic3r::Polygon &hole : shape.holes)
                hole.reverse();
        }
    }

    // Use CGAL to cut surface from triangle mesh
    SurfaceCut cut = cut_surface(shapes, itss, cut_projection, projection_ratio);

    if (is_text_reflected) {
        for (SurfaceCut::Contour &c : cut.contours)
            std::reverse(c.begin(), c.end());
        for (Vec3i &t : cut.indices)
            std::swap(t[0], t[1]);
    }

    if (cut.empty()) throw JobException(_u8L("There is no valid surface for text projection.").c_str());
    if (was_canceled()) return {};

    // !! Projection needs to transform cut
    OrthoProject3d projection = create_emboss_projection(input2.is_outside, fp.emboss, emboss_tr, cut);
    indexed_triangle_set new_its = cut2model(cut, projection);
    assert(!new_its.empty());
    if (was_canceled()) return {};
    return TriangleMesh(std::move(new_its));
}

bool priv::process(std::exception_ptr &eptr) { 
    if (!eptr) return false;
    try {
        std::rethrow_exception(eptr);
    } catch (priv::JobException &e) {
        create_message(e.what());
        eptr = nullptr;
    }
    return true;
}

bool priv::finalize(bool canceled, std::exception_ptr &eptr, const DataBase &input)
{
    // doesn't care about exception when process was canceled by user
    if (canceled || input.cancel->load()) {
        eptr = nullptr;
        return false;
    }
    return !process(eptr);
}

void priv::create_message(const std::string &message) {
    show_error(nullptr, message.c_str());
}
