#ifndef slic3r_GUI_ObjectManipulation_hpp_
#define slic3r_GUI_ObjectManipulation_hpp_

#include <memory>

#include "GUI_ObjectSettings.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_Geometry.hpp"
#include "libslic3r/Point.hpp"
#include <float.h>

#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"

#ifdef __WXOSX__
class wxBitmapComboBox;
#else
class wxComboBox;
#endif // __WXOSX__
class wxStaticText;
class LockButton;
class wxStaticBitmap;

namespace Slic3r {
    namespace GUI {

#ifdef _WIN32
        class BitmapComboBox;
#endif

#if 1
        using choice_ctrl = ::ComboBox;
#else
#ifdef __WXOSX__
    static_assert(wxMAJOR_VERSION >= 3, "Use of wxBitmapComboBox on Manipulation panel requires wxWidgets 3.0 and newer");
    using choice_ctrl = wxBitmapComboBox;
#else
#ifdef _WIN32
    using choice_ctrl = BitmapComboBox;
#else
    using choice_ctrl = wxComboBox;
#endif
#endif // __WXOSX__
#endif

class Selection;

class ObjectManipulation;
class ManipulationEditor : public ::TextInput
{
    std::string         m_opt_key;
    int                 m_axis;
    bool                m_enter_pressed { false };
    wxString            m_valid_value {wxEmptyString};

    std::string         m_full_opt_name;

public:
    ManipulationEditor(ObjectManipulation* parent, const std::string& opt_key, int axis);
    ~ManipulationEditor() {}

    void                msw_rescale();
    void                sys_color_changed(ObjectManipulation* parent);
    void                set_value(const wxString& new_value);
    void                kill_focus(ObjectManipulation *parent);

    const std::string&  get_full_opt_name() const { return m_full_opt_name; }

    bool                has_opt_key(const std::string& key) { return m_opt_key == key; }

private:
    double              get_value();
};


class ObjectManipulation : public OG_Settings
{
public:
    static const double in_to_mm;
    static const double mm_to_in;

private:
    struct Cache
    {
        Vec3d position;
        Vec3d position_rounded;
        Vec3d rotation;
        Vec3d rotation_rounded;
        Vec3d scale;
        Vec3d scale_rounded;
        Vec3d size;
        Vec3d size_inches;
        Vec3d size_rounded;

        wxString move_label_string;
        wxString rotate_label_string;
        wxString scale_label_string;

        Cache() { reset(); }
        void reset()
        {
            position = position_rounded = Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
            rotation = rotation_rounded = Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
            scale = scale_rounded = Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
            size = size_rounded = Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
            move_label_string = wxString();
            rotate_label_string = wxString();
            scale_label_string = wxString();
        }
        bool is_valid() const { return position != Vec3d(DBL_MAX, DBL_MAX, DBL_MAX); }
    };

    Cache m_cache;

    wxStaticText*   m_move_Label = nullptr;
    wxStaticText*   m_scale_Label = nullptr;
    wxStaticText*   m_rotate_Label = nullptr;

    bool            m_imperial_units { false };
    bool            m_use_colors     { false };
    wxStaticText*   m_position_unit  { nullptr };
    wxStaticText*   m_size_unit      { nullptr };

    wxStaticText*   m_item_name = nullptr;
    wxStaticText*   m_empty_str = nullptr;

    // Non-owning pointers to the reset buttons, so we can hide and show them.
    ScalableButton* m_reset_scale_button{ nullptr };
    ScalableButton* m_reset_rotation_button{ nullptr };
    ScalableButton* m_reset_skew_button{ nullptr };
    ScalableButton* m_drop_to_bed_button{ nullptr };

    wxWindow*       m_check_inch {nullptr};

    std::array<ScalableButton*, 3> m_mirror_buttons;

    // Bitmaps for the mirroring buttons.
    ScalableBitmap m_mirror_bitmap_on;

    // Needs to be updated from OnIdle?
    bool            m_dirty = false;
    // Cached labels for the delayed update, not localized!
    std::string     m_new_move_label_string;
    std::string     m_new_rotate_label_string;
    std::string     m_new_scale_label_string;
    Vec3d           m_new_position;
    Vec3d           m_new_rotation;
    Vec3d           m_new_scale;
    Vec3d           m_new_size;
    bool            m_new_enabled {true};
    bool            m_uniform_scale {true};
    ECoordinatesType m_coordinates_type{ ECoordinatesType::World };
    LockButton*     m_lock_bnt{ nullptr };
    choice_ctrl*    m_word_local_combo { nullptr };

    ScalableBitmap  m_manifold_warning_bmp;
    wxStaticBitmap* m_fix_by_winsdk_bitmap{ nullptr };
    wxStaticBitmap* m_mirror_warning_bitmap{ nullptr };

    // Currently focused editor (nullptr if none)
    ManipulationEditor* m_focused_editor{ nullptr };

    wxFlexGridSizer* m_main_grid_sizer;
    wxFlexGridSizer* m_labels_grid_sizer;

    wxStaticText* m_skew_label{ nullptr };

    // sizers, used for msw_rescale
    wxBoxSizer*     m_word_local_combo_sizer;
    std::vector<wxBoxSizer*>            m_rescalable_sizers;

    std::vector<ManipulationEditor*>    m_editors;

    // parameters for enabling/disabling of editors
    bool m_is_enabled                   { true };
    bool m_is_enabled_size_and_scale    { true };

    bool m_show_skew                    { false };

public:
    ObjectManipulation(wxWindow* parent);
    ~ObjectManipulation() {}

    void        Show(const bool show) override;
    bool        IsShown() override;
    void        UpdateAndShow(const bool show) override;
    void        Enable(const bool enadle = true);
    void        Disable() { Enable(false); }
    void        DisableScale();
    void        DisableUnuniformScale();
    void        update_ui_from_settings();
    bool        use_colors() { return m_use_colors; }

    void        set_dirty() { m_dirty = true; }
	// Called from the App to update the UI if dirty.
	void		update_if_dirty();

    void        set_uniform_scaling(const bool use_uniform_scale);
    bool        get_uniform_scaling() const { return m_uniform_scale; }

    void             set_coordinates_type(ECoordinatesType type);
    ECoordinatesType get_coordinates_type() const;
    bool             is_world_coordinates() const { return m_coordinates_type == ECoordinatesType::World; }
    bool             is_instance_coordinates() const { return m_coordinates_type == ECoordinatesType::Instance; }
    bool             is_local_coordinates() const { return m_coordinates_type == ECoordinatesType::Local; }

    void reset_cache() { m_cache.reset(); }
#ifndef __APPLE__
    // On Windows and Linux, emulates a kill focus event on the currently focused option (if any)
    // Used only in ObjectList wxEVT_DATAVIEW_SELECTION_CHANGED handler which is called before the regular kill focus event
    // bound to this class when changing selection in the objects list
    void emulate_kill_focus();
#endif // __APPLE__

    void update_item_name(const wxString &item_name);
    void update_warning_icon_state(const MeshErrorsInfo& warning);
    void msw_rescale();
    void sys_color_changed();
    void on_change(const std::string& opt_key, int axis, double new_value);
    void set_focused_editor(ManipulationEditor* focused_editor) {
        m_focused_editor = focused_editor;
    }

    ManipulationEditor* get_focused_editor() { return m_focused_editor; }

    static wxString coordinate_type_str(ECoordinatesType type);

    bool is_enabled()               const { return m_is_enabled; }
    bool is_enabled_size_and_scale()const { return m_is_enabled_size_and_scale; }

#if ENABLE_OBJECT_MANIPULATION_DEBUG
    void render_debug_window();
#endif // ENABLE_OBJECT_MANIPULATION_DEBUG

private:
    void reset_settings_value();
    void update_settings_value(const Selection& selection);

    // Show or hide scale/rotation reset buttons if needed
    void update_reset_buttons_visibility();
    //Show or hide mirror buttons
    void update_mirror_buttons_visibility();

    // change values 
    void change_position_value(int axis, double value);
    void change_rotation_value(int axis, double value);
    void change_scale_value(int axis, double value);
    void change_size_value(int axis, double value);
    void do_scale(int axis, const Vec3d &scale) const;
    void do_size(int axis, const Vec3d& scale) const;

    void set_coordinates_type(const wxString& type_string);
};

}}

#endif // slic3r_GUI_ObjectManipulation_hpp_
