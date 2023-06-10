#ifndef slic3r_BugWizard_hpp_
#define slic3r_BugWizard_hpp_

#include <memory>

#include <wx/dialog.h>

#include "GUI_Utils.hpp"

namespace Slic3r {

class BugPresetBundle;
class PresetUpdater;

namespace GUI {


class BugWizard: public DPIDialog
{
public:
    // Why is the Wizard run
    enum BugRunReason {
        RR_DATA_EMPTY,                  // No or empty datadir
        RR_DATA_LEGACY,                 // Pre-updating datadir
        RR_DATA_INCOMPAT,               // Incompatible datadir - Slic3r downgrade situation
        RR_USER,                        // User requested the Wizard from the menus
    };

    // What page should wizard start on
    enum BugStartPage {
        SP_WELCOME,
        SP_PRINTERS,
        SP_FILAMENTS,
        SP_MATERIALS,
    };

    BugWizard(wxWindow *parent);
    BugWizard(BugWizard &&) = delete;
    BugWizard(const BugWizard &) = delete;
    BugWizard &operator=(BugWizard &&) = delete;
    BugWizard &operator=(const BugWizard &) = delete;
    ~BugWizard();

    // Run the Wizard. Return whether it was completed.
    bool run(BugRunReason reason, BugStartPage start_page = SP_WELCOME);

    static const wxString& name(const bool from_menu = false);
protected:
    void on_dpi_changed(const wxRect &suggested_rect) override ;
    void on_sys_color_changed() override;

private:
    struct priv;
    std::unique_ptr<priv> p;

    friend struct BugWizardPage;
};



}
}

#endif
