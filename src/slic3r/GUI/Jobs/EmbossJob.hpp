#ifndef slic3r_EmbossJob_hpp_
#define slic3r_EmbossJob_hpp_

#include <atomic>
#include <memory>
#include <string>
#include <libslic3r/Emboss.hpp>
#include "slic3r/Utils/RaycastManager.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "Job.hpp"

namespace Slic3r {
class ModelVolume;
class TriangleMesh;
}

namespace Slic3r::GUI::Emboss {

/// <summary>
/// Base data holder for embossing
/// </summary>
struct DataBase
{
    // Keep pointer on Data of font (glyph shapes)
    Slic3r::Emboss::FontFileWithCache font_file;
    // font item is not used for create object
    TextConfiguration text_configuration;
    // new volume name created from text
    std::string volume_name;

    // flag that job is canceled
    // for time after process.
    std::shared_ptr<std::atomic<bool>> cancel;
};

/// <summary>
/// Hold neccessary data to create ModelVolume in job
/// Volume is created on the surface of existing volume in object.
/// NOTE: EmbossDataBase::font_file doesn't have to be valid !!!
/// </summary>
struct DataCreateVolume : public DataBase
{
    // define embossed volume type
    ModelVolumeType volume_type;

    // parent ModelObject index where to create volume
    ObjectID object_id;

    // new created volume transformation
    Transform3d trmat;
};

/// <summary>
/// Create new TextVolume on the surface of ModelObject
/// Should not be stopped
/// NOTE: EmbossDataBase::font_file doesn't have to be valid !!!
/// </summary>
class CreateVolumeJob : public Job
{
    DataCreateVolume m_input;
    TriangleMesh     m_result;

public:
    CreateVolumeJob(DataCreateVolume&& input);
    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &eptr) override;
};

/// <summary>
/// Hold neccessary data to create ModelObject in job
/// Object is placed on bed under screen coor
/// OR to center of scene when it is out of bed shape
/// </summary>
struct DataCreateObject : public DataBase
{
    // define position on screen where to create object
    Vec2d screen_coor;

    // projection property
    Camera camera;

    // shape of bed in case of create volume on bed
    std::vector<Vec2d> bed_shape;
};

/// <summary>
/// Create new TextObject on the platter
/// Should not be stopped
/// </summary>
class CreateObjectJob : public Job
{
    DataCreateObject m_input;
    TriangleMesh     m_result;
    Transform3d      m_transformation;
public:
    CreateObjectJob(DataCreateObject&& input);
    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &eptr) override;
};

/// <summary>
/// Hold neccessary data to update embossed text object in job
/// </summary>
struct DataUpdate : public DataBase
{
    // unique identifier of volume to change
    ObjectID volume_id;
};

/// <summary>
/// Update text shape in existing text volume
/// Predict that there is only one runnig(not canceled) instance of it
/// </summary>
class UpdateJob : public Job
{
    DataUpdate   m_input;
    TriangleMesh m_result;

public:
    // move params to private variable
    UpdateJob(DataUpdate &&input);

    /// <summary>
    /// Create new embossed volume by m_input data and store to m_result
    /// </summary>
    /// <param name="ctl">Control containing cancel flag</param>
    void process(Ctl &ctl) override;

    /// <summary>
    /// Update volume - change object_id
    /// </summary>
    /// <param name="canceled">Was process canceled.
    /// NOTE: Be carefull it doesn't care about
    /// time between finished process and started finalize part.</param>
    /// <param name="">unused</param>
    void finalize(bool canceled, std::exception_ptr &eptr) override;

    /// <summary>
    /// Update text volume
    /// </summary>
    /// <param name="volume">Volume to be updated</param>
    /// <param name="mesh">New Triangle mesh for volume</param>
    /// <param name="text_configuration">Parametric description of volume</param>
    /// <param name="volume_name">Name of volume</param>
    static void update_volume(ModelVolume             *volume,
                              TriangleMesh           &&mesh,
                              const TextConfiguration &text_configuration,
                              std::string_view        volume_name);
};

struct SurfaceVolumeData
{
    // Transformation of text volume inside of object
    Transform3d text_tr;

    // Define projection move
    // True (raised) .. move outside from surface
    // False (engraved).. move into object
    bool is_outside;

    struct ModelSource
    {
        // source volumes
        std::shared_ptr<const TriangleMesh> mesh;
        // Transformation of volume inside of object
        Transform3d tr;
    };
    using ModelSources = std::vector<ModelSource>;
    ModelSources sources;
};

/// <summary>
/// Hold neccessary data to create(cut) volume from surface object in job
/// </summary>
struct CreateSurfaceVolumeData : public DataBase, public SurfaceVolumeData{    
    // define embossed volume type
    ModelVolumeType volume_type;

    // parent ModelObject index where to create volume
    ObjectID object_id;
};

/// <summary>
/// Cut surface from object and create cutted volume
/// Should not be stopped
/// </summary>
class CreateSurfaceVolumeJob : public Job
{
    CreateSurfaceVolumeData m_input;
    TriangleMesh           m_result;

public:
    CreateSurfaceVolumeJob(CreateSurfaceVolumeData &&input);
    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &eptr) override;
};

/// <summary>
/// Hold neccessary data to update embossed text object in job
/// </summary>
struct UpdateSurfaceVolumeData : public DataUpdate, public SurfaceVolumeData{};

/// <summary>
/// Copied triangles from object to be able create mesh for cut surface from
/// </summary>
/// <param name="volumes">Source object volumes for cut surface from</param>
/// <param name="text_volume_id">Source volume id</param>
/// <returns>Source data for cut surface from</returns>
SurfaceVolumeData::ModelSources create_sources(const ModelVolumePtrs &volumes, std::optional<size_t> text_volume_id = {});

/// <summary>
/// Copied triangles from object to be able create mesh for cut surface from
/// </summary>
/// <param name="text_volume">Define text in object</param>
/// <returns>Source data for cut surface from</returns>
SurfaceVolumeData::ModelSources create_volume_sources(const ModelVolume *text_volume);


/// <summary>
/// Update text volume to use surface from object
/// </summary>
class UpdateSurfaceVolumeJob : public Job
{
    UpdateSurfaceVolumeData m_input;
    TriangleMesh   m_result;

public:
    // move params to private variable
    UpdateSurfaceVolumeJob(UpdateSurfaceVolumeData &&input);
    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &eptr) override;
};

} // namespace Slic3r::GUI

#endif // slic3r_EmbossJob_hpp_
