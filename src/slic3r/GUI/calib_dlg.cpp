#include "calib_dlg.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include <wx/dcgraph.h>
#include "MainFrame.hpp"
#include <string>
#include <iomanip>
namespace Slic3r { 
namespace GUI {

FRF_Calibration_Dlg::FRF_Calibration_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("Flowrate Fine Calibration"), wxDefaultPosition, wxSize(-1, 280), wxDEFAULT_DIALOG_STYLE | wxNO_BORDER), m_plater(plater)
{
    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);

    // desc
    std::string setting_desc_message = _u8L("Please input the best value from the coarse calibration to further determine a more accurate extrusion multiplier.");
    auto setting_desc = new wxStaticText(this, wxID_ANY, setting_desc_message, wxDefaultPosition, wxSize(340, -1), wxALIGN_LEFT);
    setting_desc->Wrap(setting_desc->GetClientSize().x);
    v_sizer->Add(setting_desc, 0, wxTOP | wxRIGHT | wxLEFT | wxALIGN_CENTER_VERTICAL, 15);

    // Settings
    wxStaticBoxSizer *settings_sizer = new wxStaticBoxSizer(wxHORIZONTAL, this, _L("Settings"));

    auto extrusion_multiplier_text = new wxStaticText(this, wxID_ANY, _L("Extrusion Multiplier: "), wxDefaultPosition, wxSize(230, -1), wxALIGN_LEFT);
    settings_sizer->Add(extrusion_multiplier_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);

    // extru
    auto filament_config = &wxGetApp().preset_bundle->filaments.get_edited_preset().config;
    auto read_extrusion_multiplier = filament_config->opt_float("extrusion_multiplier", 0);
    m_tc_extrusion_multiplier = new wxTextCtrl(this, wxID_ANY, wxString::FromDouble(read_extrusion_multiplier), wxDefaultPosition, wxSize(100, -1), wxBORDER_SIMPLE);
    m_tc_extrusion_multiplier->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    settings_sizer->Add(m_tc_extrusion_multiplier, 0, wxRIGHT | wxALIGN_RIGHT, 0);

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxALIGN_CENTER_VERTICAL, 15);
    v_sizer->Add(0, 5, 0, wxEXPAND, 5);

    m_btnStart = new wxButton(this, wxID_ANY, _L("OK"));
    m_btnStart->Bind(wxEVT_BUTTON, &FRF_Calibration_Dlg::on_start, this);
    v_sizer->Add(m_btnStart, 0, wxRIGHT | wxALIGN_RIGHT, 15);
    v_sizer->Add(0, 8, 0, wxEXPAND, 5);

    m_btnStart->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(FRF_Calibration_Dlg::on_start), NULL, this);

    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
}

FRF_Calibration_Dlg::~FRF_Calibration_Dlg() {
    // Disconnect Events
    m_btnStart->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(FRF_Calibration_Dlg::on_start), NULL, this);
}

void FRF_Calibration_Dlg::on_start(wxCommandEvent& event) {
    bool read_double = false;
    double target_extrusion_multiplier;
    read_double = m_tc_extrusion_multiplier->GetValue().ToDouble(&target_extrusion_multiplier);

    if (!read_double || target_extrusion_multiplier < 0.5) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\n 0.5 <= Extrusion Multiplier <= 1.5\n"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        m_tc_extrusion_multiplier->SetValue("0.5");
        return;
    } else if (!read_double || target_extrusion_multiplier > 1.5) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\n 0.5 <= Extrusion Multiplier <= 1.5\n"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        m_tc_extrusion_multiplier->SetValue("1.5");
        return;
    }

    m_plater->calib_flowrate_fine(target_extrusion_multiplier);
    EndModal(wxID_OK);
}

void FRF_Calibration_Dlg::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh();
    Fit();
}

PA_Calibration_Dlg::PA_Calibration_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("Pressure Advance Calibration"), wxDefaultPosition, wxSize(-1, 280), wxDEFAULT_DIALOG_STYLE | wxNO_BORDER), m_plater(plater)
{
    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);

    wxString m_rbMethodChoices[] = { _L("PA Line"), _L("PA Pattern"), _L("PA Tower") };
    int m_rbMethodNChoices = sizeof(m_rbMethodChoices) / sizeof(wxString);
    m_rbMethod = new wxRadioBox(this, wxID_ANY, _L("Method"), wxDefaultPosition, wxDefaultSize, m_rbMethodNChoices, m_rbMethodChoices, 1, wxRA_SPECIFY_COLS);
    m_rbMethod->SetSelection(0);
    v_sizer->Add(m_rbMethod, 0, wxRIGHT | wxLEFT | wxALIGN_CENTER_VERTICAL, 15);

    // Settings
    wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(wxVERTICAL, this, _L("Settings"));

    // start PA
    auto start_PA_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_pa_text = new wxStaticText(this, wxID_ANY, _L("Start PA: "), wxDefaultPosition, wxSize(80, -1), wxALIGN_LEFT);
    start_PA_sizer->Add(start_pa_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);

    m_tcStartPA = new wxTextCtrl(this, wxID_ANY, wxString::FromDouble(0.0), wxDefaultPosition, wxSize(100, -1), wxBORDER_SIMPLE);
    m_tcStartPA->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    start_PA_sizer->Add(m_tcStartPA, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    settings_sizer->Add(start_PA_sizer);

    // end PA
    auto end_PA_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto end_pa_text = new wxStaticText(this, wxID_ANY, _L("End PA: "), wxDefaultPosition, wxSize(80, -1), wxALIGN_LEFT);
    end_PA_sizer->Add(end_pa_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);

    m_tcEndPA = new wxTextCtrl(this, wxID_ANY, wxString::FromDouble(0.04), wxDefaultPosition, wxSize(100, -1), wxBORDER_SIMPLE);
    m_tcStartPA->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    end_PA_sizer->Add(m_tcEndPA, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    settings_sizer->Add(end_PA_sizer);

    // PA step
    auto PA_step_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto PA_step_text = new wxStaticText(this, wxID_ANY, _L("PA step: "), wxDefaultPosition, wxSize(80, -1), wxALIGN_LEFT);
    PA_step_sizer->Add(PA_step_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);

    m_tcPAStep = new wxTextCtrl(this, wxID_ANY, wxString::FromDouble(0.002), wxDefaultPosition, wxSize(100, -1), wxBORDER_SIMPLE);
    m_tcStartPA->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    PA_step_sizer->Add(m_tcPAStep, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    settings_sizer->Add(PA_step_sizer);

    v_sizer->Add(0, 5, 0, wxEXPAND, 5);
    v_sizer->Add(settings_sizer, 0, wxRIGHT | wxLEFT | wxALIGN_CENTER_VERTICAL, 15);
    v_sizer->Add(0, 5, 0, wxEXPAND, 5);

    m_btnStart = new wxButton(this, wxID_ANY, _L("OK"));
    m_btnStart->Bind(wxEVT_BUTTON, &PA_Calibration_Dlg::on_start, this);
    v_sizer->Add(m_btnStart, 0, wxRIGHT | wxALIGN_RIGHT, 15);
    v_sizer->Add(0, 8, 0, wxEXPAND, 5);

    m_rbMethod->Connect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(PA_Calibration_Dlg::on_method_changed), NULL, this);
    m_btnStart->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PA_Calibration_Dlg::on_start), NULL, this);
    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
}

PA_Calibration_Dlg::~PA_Calibration_Dlg() {
    // Disconnect Events
    m_rbMethod->Disconnect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(PA_Calibration_Dlg::on_method_changed), NULL, this);
    m_btnStart->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PA_Calibration_Dlg::on_start), NULL, this);
}

void PA_Calibration_Dlg::on_start(wxCommandEvent& event) { 
    bool read_double = false;
    double start_pa;
    double end_pa;
    double pa_step;
    read_double = m_tcStartPA->GetValue().ToDouble(&start_pa);
    read_double = read_double && m_tcEndPA->GetValue().ToDouble(&end_pa);
    read_double = read_double && m_tcPAStep->GetValue().ToDouble(&pa_step);
    if (!read_double || start_pa < 0 || pa_step < 0.001 || end_pa < start_pa + pa_step) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nStart PA: >= 0.0\nEnd PA: > Start PA + PA step\nPA step: >= 0.001)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    switch (m_rbMethod->GetSelection()) {
        case 0:{
            m_plater->calib_pa_line( start_pa, end_pa, pa_step);
            break;
        }
        case 1:{
            m_plater->calib_pa_pattern( start_pa, end_pa, pa_step);
            break;
        }
        case 2:{
            m_plater->calib_pa_tower( start_pa, end_pa, pa_step);
            break;
        }
        default: break;
    }

    EndModal(wxID_OK);
}

void PA_Calibration_Dlg::on_method_changed(wxCommandEvent& event) { 
    event.Skip(); 
}

void PA_Calibration_Dlg::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh(); 
    Fit();
}

}} // namespace Slic3r::GUI
