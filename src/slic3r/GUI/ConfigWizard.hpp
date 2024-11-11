#ifndef slic3r_ConfigWizard_hpp_
#define slic3r_ConfigWizard_hpp_

#include <memory>

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>

#include "GUI_Utils.hpp"

namespace Slic3r {

class PresetBundle;
class PresetUpdater;

namespace GUI {

class ConfigWizardLoadingDialog : public wxDialog
{
public:
    ConfigWizardLoadingDialog(wxWindow* parent, const wxString& message);
};

namespace DownloaderUtils {
    class Worker : public wxBoxSizer
    {
        wxWindow*   m_parent{ nullptr };
        wxTextCtrl* m_input_path{ nullptr };
        bool        downloader_checked{ false };

        void deregister();

    public:
        Worker(wxWindow* parent);
        ~Worker() {}

        void allow(bool allow_) { downloader_checked = allow_; }
        bool is_checked() const { return downloader_checked; }
        wxString path_name() const { return m_input_path ? m_input_path->GetValue() : wxString(); }

        void set_path_name(wxString name);
        void set_path_name(const std::string& name);

        bool on_finish();
        static bool perform_download_register(const std::string& path);
        static bool perform_url_register();
#ifdef __linux__
        static bool perform_registration_linux;
#endif // __linux__
    };
}

class ConfigWizard: public DPIDialog
{
public:
    // Why is the Wizard run
    enum RunReason {
        RR_DATA_EMPTY,                  // No or empty datadir
        RR_DATA_LEGACY,                 // Pre-updating datadir
        RR_DATA_INCOMPAT,               // Incompatible datadir - Slic3r downgrade situation
        RR_USER,                        // User requested the Wizard from the menus
    };

    // What page should wizard start on
    enum StartPage {
        SP_WELCOME,
        SP_PRINTERS,
        SP_FILAMENTS,
        SP_MATERIALS,
    };

    ConfigWizard(wxWindow *parent);
    ConfigWizard(ConfigWizard &&) = delete;
    ConfigWizard(const ConfigWizard &) = delete;
    ConfigWizard &operator=(ConfigWizard &&) = delete;
    ConfigWizard &operator=(const ConfigWizard &) = delete;
    ~ConfigWizard();

    // Run the Wizard. Return whether it was completed.
    bool run(RunReason reason, StartPage start_page = SP_WELCOME);
    void update_login();

    static const wxString& name(const bool from_menu = false);
protected:
    void on_dpi_changed(const wxRect &suggested_rect) override ;
    void on_sys_color_changed() override;

private:
    struct priv;
    std::unique_ptr<priv> p;

    friend struct ConfigWizardPage;
};



}
}

#endif
