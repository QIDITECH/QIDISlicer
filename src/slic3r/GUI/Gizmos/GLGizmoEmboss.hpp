#ifndef slic3r_GLGizmoEmboss_hpp_
#define slic3r_GLGizmoEmboss_hpp_

#include "GLGizmoBase.hpp"
#include "GLGizmoRotate.hpp"
#include "slic3r/GUI/IconManager.hpp"
#include "slic3r/GUI/SurfaceDrag.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/Utils/RaycastManager.hpp"
#include "slic3r/Utils/EmbossStyleManager.hpp"

#include <optional>
#include <memory>
#include <atomic>

#include "libslic3r/Emboss.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TextConfiguration.hpp"

#include <imgui/imgui.h>
#include <GL/glew.h>

class wxFont;
namespace Slic3r{
    class AppConfig;
    class GLVolume;
    enum class ModelVolumeType : int;
}

namespace Slic3r::GUI {
class GLGizmoEmboss : public GLGizmoBase
{
public:
    GLGizmoEmboss(GLCanvas3D& parent);

    /// <summary>
    /// Create new embossed text volume by type on position of mouse
    /// </summary>
    /// <param name="volume_type">Object part / Negative volume / Modifier</param>
    /// <param name="mouse_pos">Define position of new volume</param>
    void create_volume(ModelVolumeType volume_type, const Vec2d &mouse_pos);

    /// <summary>
    /// Create new text without given position
    /// </summary>
    /// <param name="volume_type">Object part / Negative volume / Modifier</param>
    void create_volume(ModelVolumeType volume_type);

    /// <summary>
    /// Handle pressing of shortcut
    /// </summary>
    void on_shortcut_key();
protected:
    bool on_init() override;
    std::string on_get_name() const override;
    void on_render() override;
    void on_register_raycasters_for_picking() override;
    void on_unregister_raycasters_for_picking() override;
    void on_render_input_window(float x, float y, float bottom_limit) override;
    bool on_is_selectable() const override { return false; }
    bool on_is_activable() const override { return true; };
    void on_set_state() override;
    void data_changed(bool is_serializing) override; // selection changed
    void on_set_hover_id() override{ m_rotate_gizmo.set_hover_id(m_hover_id); }
    void on_enable_grabber(unsigned int id) override { m_rotate_gizmo.enable_grabber(); }
    void on_disable_grabber(unsigned int id) override { m_rotate_gizmo.disable_grabber(); }
    void on_start_dragging() override;
    void on_stop_dragging() override;
    void on_dragging(const UpdateData &data) override;    

    /// <summary>
    /// Rotate by text on dragging rotate grabers
    /// </summary>
    /// <param name="mouse_event">Information about mouse</param>
    /// <returns>Propagete normaly return false.</returns>
    bool on_mouse(const wxMouseEvent &mouse_event) override;

    bool wants_enter_leave_snapshots() const override { return true; }
    std::string get_gizmo_entering_text() const override { return _u8L("Enter emboss gizmo"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leave emboss gizmo"); }
    std::string get_action_snapshot_name() const override { return _u8L("Embossing actions"); }
private:
    static EmbossStyles create_default_styles();
    // localized default text
    bool init_create(ModelVolumeType volume_type);

    void set_volume_by_selection();
    void reset_volume();

    // create volume from text - main functionality
    bool process();
    void close();
    void draw_window();
    void draw_text_input();
    void draw_model_type();
    void fix_transformation(const FontProp &from, const FontProp &to);
    void draw_style_list();
    void draw_delete_style_button();
    void draw_style_rename_popup();
    void draw_style_rename_button();
    void draw_style_save_button(bool is_modified);
    void draw_style_save_as_popup();
    void draw_style_add_button();
    void init_font_name_texture();
    struct FaceName;
    void draw_font_preview(FaceName &face, bool is_visible);
    void draw_font_list_line();
    void draw_font_list();
    void draw_height(bool use_inch);
    void draw_depth(bool use_inch);

    // call after set m_style_manager.get_style().prop.size_in_mm
    bool set_height();
    // call after set m_style_manager.get_style().prop.emboss
    bool set_depth();

    bool draw_italic_button();
    bool draw_bold_button();
    void draw_advanced();

    bool select_facename(const wxString& facename);

    void do_translate(const Vec3d& relative_move);
    void do_rotate(float relative_z_angle);

    bool rev_input_mm(const std::string &name, float &value, const float *default_value,
        const std::string &undo_tooltip, float step, float step_fast, const char *format,
        bool use_inch, const std::optional<float>& scale);

    /// <summary>
    /// Reversible input float with option to restor default value
    /// TODO: make more general, static and move to ImGuiWrapper 
    /// </summary>
    /// <returns>True when value changed otherwise FALSE.</returns>
    bool rev_input(const std::string &name, float &value, const float *default_value, 
        const std::string &undo_tooltip, float step, float step_fast, const char *format, 
        ImGuiInputTextFlags flags = 0);
    bool rev_checkbox(const std::string &name, bool &value, const bool* default_value, const std::string  &undo_tooltip);
    bool rev_slider(const std::string &name, std::optional<int>& value, const std::optional<int> *default_value,
        const std::string &undo_tooltip, int v_min, int v_max, const std::string &format, const wxString &tooltip);
    bool rev_slider(const std::string &name, std::optional<float>& value, const std::optional<float> *default_value,
        const std::string &undo_tooltip, float v_min, float v_max, const std::string &format, const wxString &tooltip);
    bool rev_slider(const std::string &name, float &value, const float *default_value, 
        const std::string &undo_tooltip, float v_min, float v_max, const std::string &format, const wxString &tooltip);
    template<typename T, typename Draw>
    bool revertible(const std::string &name, T &value, const T *default_value, const std::string &undo_tooltip, float undo_offset, Draw draw);

    bool m_should_set_minimal_windows_size = false;
    void set_minimal_window_size(bool is_advance_edit_style);
    ImVec2 get_minimal_window_size() const;

    // process mouse event
    bool on_mouse_for_rotation(const wxMouseEvent &mouse_event);
    bool on_mouse_for_translate(const wxMouseEvent &mouse_event);
    void on_mouse_change_selection(const wxMouseEvent &mouse_event);

    // When open text loaded from .3mf it could be written with unknown font
    bool m_is_unknown_font;
    void create_notification_not_valid_font(const TextConfiguration& tc);
    void create_notification_not_valid_font(const std::string& text);
    void remove_notification_not_valid_font();
    
    // This configs holds GUI layout size given by translated texts.
    // etc. When language changes, GUI is recreated and this class constructed again,
    // so the change takes effect. (info by GLGizmoFdmSupports.hpp)
    struct GuiCfg
    {
        // Detect invalid config values when change monitor DPI
        double screen_scale;
        float  main_toolbar_height;

        // Zero means it is calculated in init function
        ImVec2       minimal_window_size                  = ImVec2(0, 0);
        ImVec2       minimal_window_size_with_advance     = ImVec2(0, 0);
        ImVec2       minimal_window_size_with_collections = ImVec2(0, 0);
        float        height_of_volume_type_selector       = 0.f;
        float        input_width                          = 0.f;
        float        delete_pos_x                         = 0.f;
        float        max_style_name_width                 = 0.f;
        unsigned int icon_width                           = 0;

        // maximal width and height of style image
        Vec2i max_style_image_size = Vec2i(0, 0);

        float indent                = 0.f;
        float input_offset          = 0.f;
        float advanced_input_offset = 0.f;

        float lock_offset = 0.f;

        ImVec2 text_size;

        // maximal size of face name image
        Vec2i face_name_size             = Vec2i(100, 0);
        float face_name_max_width        = 100.f;
        float face_name_texture_offset_x = 105.f;

        // maximal texture generate jobs running at once
        unsigned int max_count_opened_font_files = 10;

        // Only translations needed for calc GUI size
        struct Translations
        {
            std::string font;
            std::string height;
            std::string depth;
            std::string use_surface;

            // advanced
            std::string char_gap;
            std::string line_gap;
            std::string boldness;
            std::string skew_ration;
            std::string from_surface;
            std::string rotation;
            std::string keep_up;
            std::string collection;
        };
        Translations translations;
    };
    std::optional<const GuiCfg> m_gui_cfg; 
    static GuiCfg create_gui_configuration();

    // Is open tree with advanced options
    bool m_is_advanced_edit_style = false;

    // when true window will appear near to text volume when open
    // When false it opens on last position
    bool m_allow_open_near_volume = false;
    // setted only when wanted to use - not all the time
    std::optional<ImVec2> m_set_window_offset;

    // Keep information about stored styles and loaded actual style to compare with
    Emboss::StyleManager m_style_manager;

    struct FaceName{
        wxString wx_name;
        std::string name_truncated = "";
        size_t texture_index = 0;
        // State for generation of texture
        // when start generate create share pointers
        std::shared_ptr<std::atomic<bool>> cancel = nullptr;
        // R/W only on main thread - finalize of job
        std::shared_ptr<bool> is_created = nullptr;
    };

    // Keep sorted list of loadable face names
    struct Facenames
    {
        // flag to keep need of enumeration fonts from OS
        // false .. wants new enumeration check by Hash
        // true  .. already enumerated(During opened combo box)
        bool is_init = false;

        bool has_truncated_names = false;

        // data of can_load() faces
        std::vector<FaceName> faces = {};
        // Sorter set of Non valid face names in OS
        std::vector<wxString> bad   = {};

        // Configuration of font encoding
        static constexpr wxFontEncoding encoding = wxFontEncoding::wxFONTENCODING_SYSTEM;

        // Identify if preview texture exists
        GLuint texture_id = 0;
                
        // protection for open too much font files together
        // Gtk:ERROR:../../../../gtk/gtkiconhelper.c:494:ensure_surface_for_gicon: assertion failed (error == NULL): Failed to load /usr/share/icons/Yaru/48x48/status/image-missing.png: Error opening file /usr/share/icons/Yaru/48x48/status/image-missing.png: Too many open files (g-io-error-quark, 31)
        // This variable must exist until no CreateFontImageJob is running
        unsigned int count_opened_font_files = 0; 

        // Configuration for texture height
        const int count_cached_textures = 32;

        // index for new generated texture index(must be lower than count_cached_textures)
        size_t texture_index = 0;

        // hash created from enumerated font from OS
        // check when new font was installed
        size_t hash = 0;

        // filtration pattern
        std::string search = "";
        std::vector<bool> hide; // result of filtration
    } m_face_names;
    static bool store(const Facenames &facenames);
    static bool load(Facenames &facenames);

    static void init_face_names(Facenames &facenames);
    static void init_truncated_names(Facenames &face_names, float max_width);

    // Text to emboss
    std::string m_text; // Sequence of Unicode UTF8 symbols

    // When true keep up vector otherwise relative rotation
    bool m_keep_up = true;

    // current selected volume 
    // NOTE: Be carefull could be uninitialized (removed from Model)
    ModelVolume *m_volume;

    // When work with undo redo stack there could be situation that 
    // m_volume point to unexisting volume so One need also objectID
    ObjectID m_volume_id;

    // True when m_text contain character unknown by selected font
    bool m_text_contain_unknown_glyph = false;

    // cancel for previous update of volume to cancel finalize part
    std::shared_ptr<std::atomic<bool>> m_job_cancel;

    // Rotation gizmo
    GLGizmoRotate m_rotate_gizmo;
    // Value is set only when dragging rotation to calculate actual angle
    std::optional<float> m_rotate_start_angle;

    // Keep data about dragging only during drag&drop
    std::optional<SurfaceDrag> m_surface_drag;

    // TODO: it should be accessible by other gizmo too.
    // May be move to plater?
    RaycastManager m_raycast_manager;

    // For text on scaled objects
    std::optional<float> m_scale_height;
    std::optional<float> m_scale_depth;
    void calculate_scale();

    // drawing icons
    IconManager m_icon_manager;
    IconManager::VIcons m_icons;
    void init_icons();

    // only temporary solution
    static const std::string M_ICON_FILENAME;
};

} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoEmboss_hpp_
