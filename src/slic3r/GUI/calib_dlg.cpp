#include "calib_dlg.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include <wx/dcgraph.h>
#include "MainFrame.hpp"
#include <string>
namespace Slic3r { 
namespace GUI {
    
wxBoxSizer *create_item_checkbox(wxString title, wxWindow *parent, bool *value, CheckBoxInWT *&checkbox)
{
    wxBoxSizer* m_sizer_checkbox = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 5);

    checkbox = new ::CheckBoxInWT(parent);
    m_sizer_checkbox->Add(checkbox, 0, wxALIGN_CENTER, 0);
    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 8);

    auto checkbox_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxSize(-1, -1), 0);
    checkbox_title->SetForegroundColour(wxColour(144, 144, 144));
    checkbox_title->SetFont(::Label::Body_13);
    checkbox_title->Wrap(-1);
    m_sizer_checkbox->Add(checkbox_title, 0, wxALIGN_CENTER | wxALL, 3);

    checkbox->SetValue(true);

    checkbox->Bind(wxEVT_TOGGLEBUTTON, [parent, checkbox, value](wxCommandEvent& e) {
        (*value) = (*value) ? false : true;
        e.Skip();
        });

    return m_sizer_checkbox;
}

PA_Calibration_Dlg::PA_Calibration_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("PA Calibration"), wxDefaultPosition, parent->FromDIP(wxSize(-1, 280)), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), m_plater(plater)
{
    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);
	wxBoxSizer* choice_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxString m_rbExtruderTypeChoices[] = { _L("DDE"), _L("Bowden") };
	int m_rbExtruderTypeNChoices = sizeof(m_rbExtruderTypeChoices) / sizeof(wxString);
	m_rbExtruderType = new wxRadioBox(this, wxID_ANY, _L("Extruder type"), wxDefaultPosition, wxDefaultSize, m_rbExtruderTypeNChoices, m_rbExtruderTypeChoices, 2, wxRA_SPECIFY_COLS);
	m_rbExtruderType->SetSelection(0);
	choice_sizer->Add(m_rbExtruderType, 0, wxALL, 5);
	choice_sizer->Add(FromDIP(5), 0, 0, wxEXPAND, 5);
	wxString m_rbMethodChoices[] = { _L("PA Tower"), _L("PA Line"), _L("PA Pattern") };
	int m_rbMethodNChoices = sizeof(m_rbMethodChoices) / sizeof(wxString);
	m_rbMethod = new wxRadioBox(this, wxID_ANY, _L("Method"), wxDefaultPosition, wxDefaultSize, m_rbMethodNChoices, m_rbMethodChoices, 2, wxRA_SPECIFY_COLS);
	m_rbMethod->SetSelection(0);
	choice_sizer->Add(m_rbMethod, 0, wxALL, 5);

	v_sizer->Add(choice_sizer);

    // Settings
    //
    wxString start_pa_str = _L("Start PA: ");
    wxString end_pa_str = _L("End PA: ");
    wxString PA_step_str = _L("PA step: ");
	auto text_size = wxWindow::GetTextExtent(start_pa_str);
	text_size.IncTo(wxWindow::GetTextExtent(end_pa_str));
	text_size.IncTo(wxWindow::GetTextExtent(PA_step_str));
	text_size.x = text_size.x * 1.5;
	wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(wxVERTICAL, this, _L("Settings"));

	auto st_size = FromDIP(wxSize(text_size.x, -1));
	auto ti_size = FromDIP(wxSize(90, -1));
    // start PA
    auto start_PA_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_pa_text = new wxStaticText(this, wxID_ANY, start_pa_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStartPA = new TextInput(this, "", "", "", wxDefaultPosition, ti_size, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_tiStartPA->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));

	start_PA_sizer->Add(start_pa_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    start_PA_sizer->Add(m_tiStartPA, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    settings_sizer->Add(start_PA_sizer);

    // end PA
    auto end_PA_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto end_pa_text = new wxStaticText(this, wxID_ANY, end_pa_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiEndPA = new TextInput(this, "", "", "", wxDefaultPosition, ti_size, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_tiStartPA->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    end_PA_sizer->Add(end_pa_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    end_PA_sizer->Add(m_tiEndPA, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    settings_sizer->Add(end_PA_sizer);

    // PA step
    auto PA_step_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto PA_step_text = new wxStaticText(this, wxID_ANY, PA_step_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiPAStep = new TextInput(this, "", "", "", wxDefaultPosition, ti_size, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_tiStartPA->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    PA_step_sizer->Add(PA_step_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    PA_step_sizer->Add(m_tiPAStep, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    settings_sizer->Add(PA_step_sizer);

	settings_sizer->Add(create_item_checkbox(_L("Print numbers"), this, &m_params.print_numbers, m_cbPrintNum));
    m_cbPrintNum->SetValue(false);

    v_sizer->Add(settings_sizer);
	v_sizer->Add(0, FromDIP(10), 0, wxEXPAND, 5);
    m_btnStart = new Button(this, _L("OK"));
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
		std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
		std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal));

	m_btnStart->SetBackgroundColor(btn_bg_green);
	m_btnStart->SetBorderColor(wxColour(0, 150, 136));
	m_btnStart->SetTextColor(wxColour("#FFFFFE"));
	m_btnStart->SetSize(wxSize(FromDIP(48), FromDIP(24)));
	m_btnStart->SetMinSize(wxSize(FromDIP(48), FromDIP(24)));
	m_btnStart->SetCornerRadius(FromDIP(3));
	m_btnStart->Bind(wxEVT_BUTTON, &PA_Calibration_Dlg::on_start, this);
	v_sizer->Add(m_btnStart, 0, wxALL | wxALIGN_RIGHT, FromDIP(5));

    PA_Calibration_Dlg::reset_params();

    // Connect Events
    m_rbExtruderType->Connect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(PA_Calibration_Dlg::on_extruder_type_changed), NULL, this);
    m_rbMethod->Connect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(PA_Calibration_Dlg::on_method_changed), NULL, this);
    this->Connect(wxEVT_SHOW, wxShowEventHandler(PA_Calibration_Dlg::on_show));
    //wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
}

PA_Calibration_Dlg::~PA_Calibration_Dlg() {
    // Disconnect Events
    m_rbExtruderType->Disconnect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(PA_Calibration_Dlg::on_extruder_type_changed), NULL, this);
    m_rbMethod->Disconnect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(PA_Calibration_Dlg::on_method_changed), NULL, this);
    m_btnStart->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PA_Calibration_Dlg::on_start), NULL, this);
}

void PA_Calibration_Dlg::reset_params() {
    bool isDDE = m_rbExtruderType->GetSelection() == 0 ? true : false;
    int method = m_rbMethod->GetSelection();

    m_tiStartPA->GetTextCtrl()->SetValue(wxString::FromDouble(0.0));

    switch (method) {
        case 1:
            m_params.mode = CalibMode::Calib_PA_Line;
            m_tiEndPA->GetTextCtrl()->SetValue(wxString::FromDouble(0.1));
            m_tiPAStep->GetTextCtrl()->SetValue(wxString::FromDouble(0.002));
            m_cbPrintNum->SetValue(true);
            m_cbPrintNum->Enable(true);
            break;
        case 2:
            m_params.mode = CalibMode::Calib_PA_Pattern;
            m_tiEndPA->GetTextCtrl()->SetValue(wxString::FromDouble(0.08));
            m_tiPAStep->GetTextCtrl()->SetValue(wxString::FromDouble(0.005));
            m_cbPrintNum->SetValue(true);
            m_cbPrintNum->Enable(false);
            break;
        default:
            m_params.mode = CalibMode::Calib_PA_Tower;
            m_tiEndPA->GetTextCtrl()->SetValue(wxString::FromDouble(0.1));
            m_tiPAStep->GetTextCtrl()->SetValue(wxString::FromDouble(0.002));
            m_cbPrintNum->SetValue(false);
            m_cbPrintNum->Enable(false);
            break;
    }

    if (!isDDE) {
        m_tiEndPA->GetTextCtrl()->SetValue(wxString::FromDouble(1.0));
        
        if (m_params.mode == CalibMode::Calib_PA_Pattern) {
            m_tiPAStep->GetTextCtrl()->SetValue(wxString::FromDouble(0.05));
        } else {
            m_tiPAStep->GetTextCtrl()->SetValue(wxString::FromDouble(0.02));
        }
    }
}

void PA_Calibration_Dlg::on_start(wxCommandEvent& event) { 
    bool read_double = false;
    read_double = m_tiStartPA->GetTextCtrl()->GetValue().ToDouble(&m_params.start);
    read_double = read_double && m_tiEndPA->GetTextCtrl()->GetValue().ToDouble(&m_params.end);
    read_double = read_double && m_tiPAStep->GetTextCtrl()->GetValue().ToDouble(&m_params.step);
    if (!read_double || m_params.start < 0 || m_params.step < EPSILON || m_params.end < m_params.start + m_params.step) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nStart PA: >= 0.0\nEnd PA: > Start PA\nPA step: >= 0.001)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    switch (m_rbMethod->GetSelection()) {
        case 1:
            m_params.mode = CalibMode::Calib_PA_Line;
            break;
        case 2:
            m_params.mode = CalibMode::Calib_PA_Pattern;
            break;
        default:
            m_params.mode = CalibMode::Calib_PA_Tower;
    }

    m_params.print_numbers = m_cbPrintNum->GetValue();

    m_plater->calib_pa(m_params);
    EndModal(wxID_OK);

}
void PA_Calibration_Dlg::on_extruder_type_changed(wxCommandEvent& event) { 
    PA_Calibration_Dlg::reset_params();
    event.Skip(); 
}
void PA_Calibration_Dlg::on_method_changed(wxCommandEvent& event) { 
    PA_Calibration_Dlg::reset_params();
    event.Skip(); 
}

void PA_Calibration_Dlg::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh(); 
    Fit();
}

void PA_Calibration_Dlg::on_show(wxShowEvent& event) {
    PA_Calibration_Dlg::reset_params();
}

// Temp Calib dlg
//
enum FILAMENT_TYPE : int
{
    tPLA = 0,
    tABS_ASA,
    tPETG,
    tTPU,
    tPA_CF,
    tPET_CF,
    tCustom
};






}} // namespace Slic3r::GUI
