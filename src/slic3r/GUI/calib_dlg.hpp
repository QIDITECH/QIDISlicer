#ifndef slic3r_calib_dlg_hpp_
#define slic3r_calib_dlg_hpp_

#include "wxExtensions.hpp"
#include "GUI_Utils.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/RadioBox.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/CheckBoxInWT.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"
#include "GUI_App.hpp"
#include "wx/hyperlink.h"
#include <wx/radiobox.h>
#include "libslic3r/calib.hpp"
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
    Calib_Params m_params;

    TextInput *m_tiExtru;
    Button *   m_btnStart;
    Plater *   m_plater;
};

class PA_Calibration_Dlg : public DPIDialog
{
public:
    PA_Calibration_Dlg(wxWindow* parent, wxWindowID id, Plater* plater);
    ~PA_Calibration_Dlg();
    void on_dpi_changed(const wxRect& suggested_rect) override;
	void on_show(wxShowEvent& event);
protected:
    void reset_params();
	virtual void on_start(wxCommandEvent& event);
	virtual void on_extruder_type_changed(wxCommandEvent& event);
	virtual void on_method_changed(wxCommandEvent& event);

protected:
	bool m_bDDE;
	Calib_Params m_params;


	wxRadioBox* m_rbExtruderType;
	wxRadioBox* m_rbMethod;
	TextInput* m_tiStartPA;
	TextInput* m_tiEndPA;
	TextInput* m_tiPAStep;
    CheckBoxInWT *m_cbPrintNum;
	Button* m_btnStart;

	Plater* m_plater;
};

}} // namespace Slic3r::GUI

#endif
