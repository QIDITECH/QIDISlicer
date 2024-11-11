
#ifndef slic3r_Plater_hpp_
#define slic3r_Plater_hpp_

#include <memory>
#include <vector>
#include <boost/filesystem/path.hpp>

#include <wx/panel.h>

#include "Sidebar.hpp"
#include "Selection.hpp"

#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "Jobs/Job.hpp"
#include "Jobs/Worker.hpp"

#include "libslic3r/GCode/ThumbnailData.hpp"

class wxString;

namespace Slic3r {

class BuildVolume;
class Model;
class ModelObject;
class ModelInstance;
class Print;
class SLAPrint;
enum PrintObjectStep : unsigned int;
enum SLAPrintObjectStep : unsigned int;
enum class ConversionType : int;

using ModelInstancePtrs = std::vector<ModelInstance*>;

namespace UndoRedo {
    class Stack;
    enum class SnapshotType : unsigned char;
    struct Snapshot;
}

namespace GUI {

wxDECLARE_EVENT(EVT_SCHEDULE_BACKGROUND_PROCESS, SimpleEvent);

class MainFrame;
class GLCanvas3D;
class Mouse3DController;
class NotificationManager;
struct Camera;
class GLToolbar;
class UserAccount;
class PresetArchiveDatabase;

class Plater: public wxPanel
{
public:
    using fs_path = boost::filesystem::path;

    Plater(wxWindow *parent, MainFrame *main_frame);
    Plater(Plater &&) = delete;
    Plater(const Plater &) = delete;
    Plater &operator=(Plater &&) = delete;
    Plater &operator=(const Plater &) = delete;
    ~Plater();

    bool is_project_dirty() const;
    bool is_presets_dirty() const;
    void update_project_dirty_from_presets();
    int  save_project_if_dirty(const wxString& reason);
    void reset_project_dirty_after_save();
    void reset_project_dirty_initial_presets();
#if ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
    void render_project_state_debug_window() const;
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW

    bool is_project_temp() const;

    Sidebar& sidebar();
    const Model& model() const;
    Model& model();
    const Print& fff_print() const;
    Print& fff_print();
    const SLAPrint& sla_print() const;
    SLAPrint& sla_print();

    //B34
    std::string double_to_str(const double value);
    void calib_pa_line(const double StartPA, double EndPA, double PAStep);
    void calib_pa_pattern(const double StartPA, double EndPA, double PAStep);
    void calib_pa_tower(const double StartPA, double EndPA, double PAStep);
    void calib_flowrate_coarse();
    void calib_flowrate_fine(const double target_extrusion_multiplier);
    void calib_max_volumetric_speed(const double StartVS, double EndVS, double VSStep);
    std::string move_to(const Vec2d &point, double speed, double retract_length, double retract_speed, double height, double retract_lift);
    std::string move_to(const Vec2d &point, double speed, double retract_length, double retract_speed);
    std::string move_to(const Vec2d &point, double speed, double e);
    std::string move_to(double height);
    std::string set_pressure_advance(double pa);
    std::string set_pa_acceleration(double acceleration);
    void        add_num_text(std::string num, Vec2d posotion);

    void new_project();
    void load_project();
    void load_project(const wxString& filename);
    void add_model(bool imperial_units = false);
    void import_zip_archive();
    void import_sl1_archive();
    void extract_config_from_project();
    void load_gcode();
    void load_gcode(const wxString& filename);
    void reload_gcode_from_disk();
    void convert_gcode_to_ascii();
    void convert_gcode_to_binary();
    void reload_print();

    std::vector<size_t> load_files(const std::vector<boost::filesystem::path>& input_files, bool load_model = true, bool load_config = true, bool imperial_units = false);
    // To be called when providing a list of files to the GUI slic3r on command line.
    std::vector<size_t> load_files(const std::vector<std::string>& input_files, bool load_model = true, bool load_config = true, bool imperial_units = false);
    // to be called on drag and drop
    bool load_files(const wxArrayString& filenames, bool delete_after_load = false);
    void notify_about_installed_presets();

    bool preview_zip_archive(const boost::filesystem::path& input_file);

    const wxString& get_last_loaded_gcode() const { return m_last_loaded_gcode; }

    enum class UpdateParams {
        FORCE_FULL_SCREEN_REFRESH = 1,
        FORCE_BACKGROUND_PROCESSING_UPDATE = 2,
        POSTPONE_VALIDATION_ERROR_MESSAGE = 4,
    };
    void update(unsigned int flags = 0);

    // Get the worker handling the UI jobs (arrange, fill bed, etc...)
    // Here is an example of starting up an ad-hoc job:
    //    queue_job(
    //        get_ui_job_worker(),
    //        [](Job::Ctl &ctl) {
    //            // Executed in the worker thread
    //
    //            CursorSetterRAII cursor_setter{ctl};
    //            std::string msg = "Running";
    //
    //            ctl.update_status(0, msg);
    //            for (int i = 0; i < 100; i++) {
    //                usleep(100000);
    //                if (ctl.was_canceled()) break;
    //                ctl.update_status(i + 1, msg);
    //            }
    //            ctl.update_status(100, msg);
    //        },
    //        [](bool, std::exception_ptr &e) {
    //            // Executed in UI thread after the work is done
    //
    //            try {
    //                if (e) std::rethrow_exception(e);
    //            } catch (std::exception &e) {
    //                BOOST_LOG_TRIVIAL(error) << e.what();
    //            }
    //            e = nullptr;
    //        });
    // This would result in quick run of the progress indicator notification
    // from 0 to 100. Use replace_job() instead of queue_job() to cancel all
    // pending jobs.
    Worker& get_ui_job_worker();
    const Worker & get_ui_job_worker() const;

    void select_view(const std::string& direction);
    void select_view_3D(const std::string& name);

    bool is_preview_shown() const;
    bool is_preview_loaded() const;
    bool is_view3D_shown() const;

    bool are_view3D_labels_shown() const;
    void show_view3D_labels(bool show);

    bool is_legend_shown() const;
    void show_legend(bool show);

    bool is_sidebar_collapsed() const;
    void collapse_sidebar(bool show);

    bool is_view3D_layers_editing_enabled() const;

    // Called after the Preferences dialog is closed and the program settings are saved.
    // Update the UI based on the current preferences.
    void update_ui_from_settings();

    void select_all();
    void deselect_all();
    void remove(size_t obj_idx);
    void reset();
    void reset_with_confirm();
    bool delete_object_from_model(size_t obj_idx);
    void remove_selected();
    void increase_instances(size_t num = 1, int obj_idx = -1, int inst_idx = -1);
    void decrease_instances(size_t num = 1, int obj_idx = -1);
    void set_number_of_copies();
    void fill_bed_with_instances();
    bool is_selection_empty() const;
    void scale_selection_to_fit_print_volume();
    void convert_unit(ConversionType conv_type);
    void toggle_layers_editing(bool enable);

    void apply_cut_object_to_model(size_t init_obj_idx, const ModelObjectPtrs& cut_objects);

    //B61
    ThumbnailData get_thumbnailldate();
    //B64
    ThumbnailData get_thumbnailldate_send();
    void export_gcode(bool prefer_removable);
    void export_stl_obj(bool extended = false, bool selection_only = false);
    bool export_3mf(const boost::filesystem::path& output_path = boost::filesystem::path());
    void reload_from_disk();
    void replace_with_stl();
    void reload_all_from_disk();
    bool has_toolpaths_to_export() const;
    void export_toolpaths_to_obj() const;
    void reslice();
    void reslice_FFF_until_step(PrintObjectStep step, const ModelObject &object, bool postpone_error_messages = false);
    void reslice_SLA_until_step(SLAPrintObjectStep step, const ModelObject &object, bool postpone_error_messages = false);

    void clear_before_change_volume(ModelVolume &mv, const std::string &notification_msg);
    void clear_before_change_mesh(int obj_idx, const std::string &notification_msg);
    void changed_mesh(int obj_idx);

    void changed_object(ModelObject &object);
    void changed_object(int obj_idx);
    void changed_objects(const std::vector<size_t>& object_idxs);
    void schedule_background_process(bool schedule = true);
    bool is_background_process_update_scheduled() const;
    void suppress_background_process(const bool stop_background_process) ;
    void send_gcode();
    // void send_gcode_inner(DynamicPrintConfig* physical_printer_config);
	void eject_drive();
    void connect_gcode();
    std::string get_upload_filename();

    void take_snapshot(const std::string &snapshot_name);
    void take_snapshot(const wxString &snapshot_name);
    void take_snapshot(const std::string &snapshot_name, UndoRedo::SnapshotType snapshot_type);
    void take_snapshot(const wxString &snapshot_name, UndoRedo::SnapshotType snapshot_type);

    void undo();
    void redo();
    void undo_to(int selection);
    void redo_to(int selection);
    bool undo_redo_string_getter(const bool is_undo, int idx, const char** out_text);
    void undo_redo_topmost_string_getter(const bool is_undo, std::string& out_text);
    // For the memory statistics. 
    const Slic3r::UndoRedo::Stack& undo_redo_stack_main() const;
    void clear_undo_redo_stack_main();
    // Enter / leave the Gizmos specific Undo / Redo stack. To be used by the SLA support point editing gizmo.
    void enter_gizmos_stack();
    void leave_gizmos_stack();

    bool update_filament_colors_in_full_config();
    void on_config_change(const DynamicPrintConfig &config);
    void force_filament_colors_update();
    void force_filament_cb_update();
    void force_print_bed_update();
    // On activating the parent window.
    void on_activate(bool active);

    std::vector<std::string> get_extruder_color_strings_from_plater_config(const GCodeProcessorResult* const result = nullptr) const;
    std::vector<std::string> get_color_strings_for_color_print(const GCodeProcessorResult* const result = nullptr) const;
    std::vector<ColorRGBA> get_extruder_colors_from_plater_config() const;
    std::vector<ColorRGBA> get_colors_for_color_print() const;

    void update_menus();
    void show_action_buttons(const bool is_ready_to_slice) const;
    void show_action_buttons() const;

    wxString get_project_filename(const wxString& extension = wxEmptyString) const;
    void set_project_filename(const wxString& filename);

    bool is_export_gcode_scheduled() const;
    
    const Selection& get_selection() const;
    int get_selected_object_idx();
    bool is_single_full_object_selection() const;
    GLCanvas3D* canvas3D();
    const GLCanvas3D * canvas3D() const;
    GLCanvas3D* get_current_canvas3D();

    void render_sliders(GLCanvas3D& canvas);
    
    void arrange();
    void arrange(Worker &w, bool selected);

    void set_current_canvas_as_dirty();
    void unbind_canvas_event_handlers();
    void reset_canvas_volumes();

    PrinterTechnology   printer_technology() const;
    const DynamicPrintConfig * config() const;
    bool                set_printer_technology(PrinterTechnology printer_technology);

    void copy_selection_to_clipboard();
    void paste_from_clipboard();
    void mirror(Axis axis);
    void split_object();
    void split_volume();

    bool can_delete() const;
    bool can_delete_all() const;
    bool can_increase_instances() const;
    bool can_decrease_instances(int obj_idx = -1) const;
    bool can_set_instance_to_object() const;
    bool can_fix_through_winsdk() const;
    bool can_simplify() const;
    bool can_split_to_objects() const;
    bool can_split_to_volumes() const;
    bool can_arrange() const;
    bool can_layers_editing() const;
    bool can_paste_from_clipboard() const;
    bool can_copy_to_clipboard() const;
    bool can_undo() const;
    bool can_redo() const;
    bool can_reload_from_disk() const;
    bool can_replace_with_stl() const;
    bool can_mirror() const;
    bool can_split(bool to_objects) const;
    bool can_scale_to_print_volume() const;

    void msw_rescale();
    void sys_color_changed();

    bool init_view_toolbar();
    void enable_view_toolbar(bool enable);
    bool init_collapse_toolbar();
    void enable_collapse_toolbar(bool enable);

    const Camera& get_camera() const;
    Camera& get_camera();

#if ENABLE_ENVIRONMENT_MAP
    void init_environment_texture();
    unsigned int get_environment_texture_id() const;
#endif // ENABLE_ENVIRONMENT_MAP

    const BuildVolume& build_volume() const;

    const GLToolbar& get_view_toolbar() const;
    GLToolbar& get_view_toolbar();

    const GLToolbar& get_collapse_toolbar() const;
    GLToolbar& get_collapse_toolbar();

    void set_preview_layers_slider_values_range(int bottom, int top);

    void update_preview_moves_slider(std::optional<int> visible_range_min = std::nullopt, std::optional<int> visible_range_max = std::nullopt);
    void enable_preview_moves_slider(bool enable);

    void reset_gcode_toolpaths();
    void reset_last_loaded_gcode() { m_last_loaded_gcode = ""; }

    const Mouse3DController& get_mouse3d_controller() const;
    Mouse3DController& get_mouse3d_controller();

    void set_bed_shape() const;
    //B52
    void set_bed_shape(const Pointfs& shape,
        const double       max_print_height,
        const std::string& custom_texture,
        const std::string& custom_model,
        const Pointfs& exclude_bed_shape,
        bool               force_as_custom = false) const;
    void set_default_bed_shape() const;

    NotificationManager* get_notification_manager();
    const NotificationManager* get_notification_manager() const;

    PresetArchiveDatabase* get_preset_archive_database();
    const PresetArchiveDatabase* get_preset_archive_database() const;

    UserAccount* get_user_account();
    const UserAccount* get_user_account() const;

    void toggle_remember_user_account_session();
    void act_with_user_account();

    void init_notification_manager();

    void bring_instance_forward();
    
    // ROII wrapper for suppressing the Undo / Redo snapshot to be taken.
	class SuppressSnapshots
	{
	public:
		SuppressSnapshots(Plater *plater) : m_plater(plater)
		{
			m_plater->suppress_snapshots();
		}
		~SuppressSnapshots()
		{
			m_plater->allow_snapshots();
		}
	private:
		Plater *m_plater;
	};

    // RAII wrapper for taking an Undo / Redo snapshot while disabling the snapshot taking by the methods called from inside this snapshot.
	class TakeSnapshot
	{
	public:
        TakeSnapshot(Plater *plater, const std::string &snapshot_name);
		TakeSnapshot(Plater *plater, const wxString &snapshot_name) : m_plater(plater)
		{
			m_plater->take_snapshot(snapshot_name);
			m_plater->suppress_snapshots();
		}
        TakeSnapshot(Plater* plater, const std::string& snapshot_name, UndoRedo::SnapshotType snapshot_type);
        TakeSnapshot(Plater *plater, const wxString &snapshot_name, UndoRedo::SnapshotType snapshot_type) : m_plater(plater)
        {
            m_plater->take_snapshot(snapshot_name, snapshot_type);
            m_plater->suppress_snapshots();
        }

		~TakeSnapshot()
		{
			m_plater->allow_snapshots();
		}
	private:
		Plater *m_plater;
	};

    bool inside_snapshot_capture();

    void toggle_render_statistic_dialog();
    bool is_render_statistic_dialog_visible() const;

    void set_keep_current_preview_type(bool value);

	// Wrapper around wxWindow::PopupMenu to suppress error messages popping out while tracking the popup menu.
	bool PopupMenu(wxMenu *menu, const wxPoint& pos = wxDefaultPosition);
    bool PopupMenu(wxMenu *menu, int x, int y) { return this->PopupMenu(menu, wxPoint(x, y)); }

    // get same Plater/ObjectList menus
    wxMenu* object_menu();
    wxMenu* part_menu();
    wxMenu* text_part_menu();
    wxMenu* svg_part_menu();
    wxMenu* sla_object_menu();
    wxMenu* default_menu();
    wxMenu* instance_menu();
    wxMenu* layer_menu();
    wxMenu* multi_selection_menu();

    void        resetUploadCount()
    {
        UploadCount = 0;
        m_sending_interval = 0;
    };

private:
    void reslice_until_step_inner(int step, const ModelObject &object, bool postpone_error_messages);

    struct priv;
    std::unique_ptr<priv> p;

    // Set true during PopupMenu() tracking to suppress immediate error message boxes.
    // The error messages are collected to m_tracking_popup_menu_error_message instead and these error messages
    // are shown after the pop-up dialog closes.
    bool 	 m_tracking_popup_menu = false;
    wxString m_tracking_popup_menu_error_message;

    wxString m_last_loaded_gcode;

    void suppress_snapshots();
    void allow_snapshots();

    friend class SuppressBackgroundProcessingUpdate;
    //B64
    std::thread m_sendThread;
    std::chrono::system_clock::time_point m_time_p;
    int                                   UploadCount = 0;
    int                                   max_send_number = 1;
    int                                   m_sending_interval = 0;
};

class SuppressBackgroundProcessingUpdate
{
public:
    SuppressBackgroundProcessingUpdate();
    ~SuppressBackgroundProcessingUpdate();
private:
    bool m_was_scheduled;
};

class PlaterAfterLoadAutoArrange
{
    bool m_enabled{ false };

public:
    PlaterAfterLoadAutoArrange();
    ~PlaterAfterLoadAutoArrange();
    void disable() { m_enabled = false; }
};

} // namespace GUI
} // namespace Slic3r

#endif
