#ifndef slic3r_calib_dlg_hpp_
#define slic3r_calib_dlg_hpp_

#include "wxExtensions.hpp"
#include "GUI_Utils.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
// #include "Widgets/RadioBox.hpp"
#include "Widgets/RoundedRectangle.hpp"
// #include "Widgets/CheckBoxInWT.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"
#include "GUI_App.hpp"
#include "wx/hyperlink.h"
#include <wx/radiobox.h>
#include "Plater.hpp"

namespace Slic3r { namespace GUI {

class FRF_Calibration_Dlg : public DPIDialog
{
public:
    FRF_Calibration_Dlg(wxWindow *parent, wxWindowID id, Plater *plater);
    ~FRF_Calibration_Dlg();
    void on_dpi_changed(const wxRect &suggested_rect) override;

protected:
    virtual void on_start(wxCommandEvent &event);

    wxTextCtrl* m_tc_extrusion_multiplier;
    wxButton*   m_btnStart;
    Plater*     m_plater;
};

class PA_Calibration_Dlg : public DPIDialog
{
public:
    PA_Calibration_Dlg(wxWindow* parent, wxWindowID id, Plater* plater);
    ~PA_Calibration_Dlg();
    void on_dpi_changed(const wxRect& suggested_rect) override;

protected:
    virtual void on_start(wxCommandEvent& event);
    virtual void on_method_changed(wxCommandEvent& event);

    wxRadioBox*  m_rbMethod;
    wxTextCtrl*  m_tcStartPA;
    wxTextCtrl*  m_tcEndPA;
    wxTextCtrl*  m_tcPAStep;
    wxButton*    m_btnStart;
    Plater*      m_plater;
};

}} // namespace Slic3r::GUI

#endif
