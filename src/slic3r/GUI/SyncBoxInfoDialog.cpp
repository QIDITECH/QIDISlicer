
#include "SyncBoxInfoDialog.hpp"

#include "MainFrame.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/Label.hpp"
#include "I18N.hpp"
#include "Tab.hpp"

#include "libslic3r/Utils.hpp"

//y25
using namespace Slic3r;
using namespace Slic3r::GUI;

namespace Slic3r { namespace GUI {

    GetBoxInfoDialog::GetBoxInfoDialog(Plater* plater)
    : DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, _L("Sync Box information"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    , m_plater(plater)
{
    SetFont(wxGetApp().normal_font());

    std::string icon_path = (boost::format("%1%/icons/QIDISlicer.ico") % resources_dir()).str();
    SetIcon(wxIcon(icon_path.c_str(), wxBITMAP_TYPE_ICO));

    Freeze();

    m_sizer_main = new wxBoxSizer(wxVERTICAL);
    m_sizer_main->SetMinSize(wxSize(0, -1));

    wxPanel* m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));

    wxStaticText* m_tips_text = new wxStaticText(this, wxID_ANY, _L("Please select the printer in the list to get box info."), wxDefaultPosition, wxSize(-1, -1), 0);

    wxBoxSizer *m_sizer_printer = new wxBoxSizer(wxHORIZONTAL);
    m_stext_printer_title = new wxStaticText(this, wxID_ANY, _L("Printer"), wxDefaultPosition, wxSize(-1, -1), 0);
    m_stext_printer_title->SetFont(::Label::Head_14);
    m_stext_printer_title->Wrap(-1);

    m_comboBox_printer = new ::ComboBox(this, wxID_ANY, "", wxDefaultPosition, wxSize(FromDIP(250), 20), 0, nullptr, wxCB_READONLY);

    m_sizer_printer->Add(m_stext_printer_title, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
    m_sizer_printer->Add(0, 0, 0, wxEXPAND | wxALIGN_CENTER | wxLEFT, FromDIP(12));
    m_sizer_printer->Add(m_comboBox_printer, 1, wxEXPAND | wxALIGN_CENTER | wxALL, FromDIP(5));

    wxPanel* switch_button_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxTAB_TRAVERSAL | wxBU_RIGHT);
    wxBoxSizer* sizer_switch_area = new wxBoxSizer(wxHORIZONTAL);
//y28
#ifdef _WIN32
    bool is_dark = wxGetApp().app_config->get_bool("dark_color_mode");
    switch_button_panel->SetBackgroundColour(is_dark ? wxColour(43, 43, 43) : wxColour(255, 255, 255));
#endif
    m_switch_button = new SwitchButton(switch_button_panel);
    m_switch_button->SetMaxSize(wxSize(100, 100));
    m_switch_button->SetLabels(_L("Local"), _L("Link"));
    m_switch_button->SetValue(m_isNetMode);
    m_switch_button->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& evt) {
        bool is_checked = evt.GetInt();
        m_switch_button->SetValue(is_checked);
        m_isNetMode = is_checked;
        m_comboBox_printer->SetValue("");
        m_comboBox_printer->Clear();
        m_printer_ip.clear();
        //y28
        m_printer_api_key.clear();
        PresetBundle& preset_bundle = *wxGetApp().preset_bundle;
        PhysicalPrinterCollection& ph_printers = wxGetApp().preset_bundle->physical_printers;
        std::string preset_typename = NormalizeVendor(preset_bundle.printers.get_edited_preset().name);
        if (!m_isNetMode){
            for (PhysicalPrinterCollection::ConstIterator it = ph_printers.begin(); it != ph_printers.end(); ++it) {
                std::string printer_preset = (it->config.opt_string("preset_name"));
                std::string printer_name = it->name;
                if (preset_typename.find(NormalizeVendor(printer_preset)) != std::string::npos) {
                    m_comboBox_printer->Append(from_u8(printer_name));
                    m_printer_ip.push_back((it->config.opt_string("print_host")));
                    //y28
                    m_printer_api_key.push_back((it->config.opt_string("printhost_apikey")));
                }
            }
            m_comboBox_printer->SetSelection(0);
        }
#if QDT_RELEASE_TO_PUBLIC
        else{
            if (wxGetApp().app_config->get("user_token") != ""){
                auto m_devices = wxGetApp().get_devices();
                for (const auto& device : m_devices) {
                    if (preset_typename.find(NormalizeVendor(device.machine_type)) != std::string::npos)
                    {
                        m_comboBox_printer->Append(from_u8(device.device_name));
                        m_printer_ip.push_back(device.url);
                    }
                }
                m_comboBox_printer->SetSelection(0);
            }
        }
#endif 
        if (m_comboBox_printer->GetValue().empty()){
            m_button_sync->Disable();
        }
        else {
            m_button_sync->Enable();
        }
    });
    sizer_switch_area->Add(m_switch_button, 0, wxALIGN_CENTER);
    switch_button_panel->SetSizer(sizer_switch_area);
    switch_button_panel->Layout();
    m_sizer_printer->Add(switch_button_panel, 0, wxALL | wxALIGN_CENTER, FromDIP(5));

    //y28
    wxStdDialogButtonSizer* btns = this->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    m_button_sync = static_cast<wxButton*>(this->FindWindowById(wxID_OK, this));
    m_button_cancel = static_cast<wxButton*>(this->FindWindowById(wxID_CANCEL, this));
    m_button_sync->Bind(wxEVT_BUTTON, &GetBoxInfoDialog::synchronization, this);
    m_button_cancel->Bind(wxEVT_BUTTON, &GetBoxInfoDialog::cancel, this);
#ifdef _WIN32
    wxGetApp().UpdateDarkUI(m_button_sync);
    wxGetApp().UpdateDarkUI(m_button_cancel);
#endif

    m_sizer_main->Add(m_line_top, 0, wxEXPAND | wxTOP, FromDIP(0));
    m_sizer_main->AddSpacer(FromDIP(10));
    m_sizer_main->Add(m_tips_text, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    m_sizer_main->AddSpacer(FromDIP(15));
    m_sizer_main->Add(m_sizer_printer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    m_sizer_main->AddSpacer(FromDIP(25));
    m_sizer_main->Add(btns, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    m_sizer_main->AddSpacer(FromDIP(10));

    SetSizer(m_sizer_main);

    init_printer_combox();
    Layout();
    Fit();
    Thaw();
    Centre(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);
}

GetBoxInfoDialog::~GetBoxInfoDialog() {}

void GetBoxInfoDialog::init_printer_combox()
{
    m_isNetMode = wxGetApp().app_config->get("machine_list_net") == "1";
    m_switch_button->SetValue(m_isNetMode);
    wxCommandEvent event(wxEVT_TOGGLEBUTTON, m_switch_button->GetId());
    event.SetEventObject(m_switch_button);
    event.SetInt(m_switch_button->GetValue());
    m_switch_button->GetEventHandler()->ProcessEvent(event);
}

void GetBoxInfoDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    Fit();
    Refresh();
}

void GetBoxInfoDialog::synchronization(wxCommandEvent &event)
{
#if QDT_RELEASE_TO_PUBLIC
    int selected_idx = m_comboBox_printer->GetSelection();
    std::string printer_ip = m_printer_ip[selected_idx];
    //y28
    std::string printer_api_key = "";
    if(!m_printer_api_key.empty())
        printer_api_key = m_printer_api_key[selected_idx];

    m_sync_printer_ip = printer_ip;
    m_sync_printer_api_key = printer_api_key;

    QIDINetwork qidi;
    wxString msg = "";
    //y28
    bool has_box = qidi.get_box_state(msg, printer_ip, printer_api_key);
     if (!has_box) {
         WarningDialog(this, _L("This Printer has not connect the box, please check.")).ShowModal();
     }
     else {
        //Get Box_info
        GUI::Box_info filament_info;
        filament_info = qidi.get_box_info(msg, printer_ip, printer_api_key);
        m_plater->current_box_info = filament_info;
        qidi.get_color_filament_str(msg, filament_info, printer_ip, printer_api_key);
        generate_filament_id(filament_info);
        syn_box_info = std::move(filament_info);
        update_filament_info(filament_info);

        sync_box_list();

        if(m_isNetMode)
            wxGetApp().app_config->set("machine_list_net", "1");
        else
            wxGetApp().app_config->set("machine_list_net", "0");
        this->EndModal(wxID_OK);
     }
#endif
}

//y28
void GetBoxInfoDialog::synchronize_by_ip(std::string ip, std::string api_key)
{
#if QDT_RELEASE_TO_PUBLIC
    m_sync_printer_ip = ip;
    m_sync_printer_api_key = api_key;

    QIDINetwork qidi;
    wxString msg = "";
    bool has_box = qidi.get_box_state(msg, m_sync_printer_ip, m_sync_printer_api_key);
     if (!has_box) {
        WarningDialog(this, _L("This Printer has not connect the box, please check.")).ShowModal();
     }
     else {
        //Get Box_info
        GUI::Box_info filament_info;
        filament_info = qidi.get_box_info(msg, m_sync_printer_ip, m_sync_printer_api_key);
        m_plater->current_box_info = filament_info;
        qidi.get_color_filament_str(msg, filament_info, m_sync_printer_ip, m_sync_printer_api_key);
        generate_filament_id(filament_info);
        syn_box_info = filament_info;
        update_filament_info(filament_info);

        sync_box_list();

        if(m_isNetMode)
            wxGetApp().app_config->set("machine_list_net", "1");
        else
            wxGetApp().app_config->set("machine_list_net", "0");
        this->EndModal(wxID_OK);
     }
#endif
}

void GetBoxInfoDialog::generate_filament_id(GUI::Box_info& machine_filament_info)
{
    std::string filament_id = "QD";
    std::string box_id = wxGetApp().preset_bundle->printers.get_selected_preset().config.opt_string("box_id");
    filament_id = filament_id + "_" + box_id;
    
    for(int i = 0; i < machine_filament_info.box_count * 4; i++){
        if (machine_filament_info.slot_state[i] == 0)
            continue;
        std::string temp_filament_id = filament_id;
        temp_filament_id = temp_filament_id + "_" + std::to_string(machine_filament_info.filament_vendor[i]);
        temp_filament_id = temp_filament_id + "_" + std::to_string(machine_filament_info.filament_index[i]);
        machine_filament_info.filament_id[i] = temp_filament_id;
    }

    if(machine_filament_info.slot_state.back() != 0)
    {
        std::string temp_ext_filament_id = filament_id;
        temp_ext_filament_id = temp_ext_filament_id + "_" + std::to_string(machine_filament_info.filament_vendor.back());
        temp_ext_filament_id = temp_ext_filament_id + "_" + std::to_string(machine_filament_info.filament_index.back());
        machine_filament_info.filament_id.back() = temp_ext_filament_id;
    }
}

void GetBoxInfoDialog::update_filament_info(GUI::Box_info& machine_filament_info)
{
    m_plater->box_msg.slot_state = std::move(machine_filament_info.slot_state);
    m_plater->box_msg.filament_id = std::move(machine_filament_info.filament_id);
    m_plater->box_msg.filament_colors = std::move(machine_filament_info.filament_colors);
    m_plater->box_msg.box_count = std::move(machine_filament_info.box_count);
    m_plater->box_msg.filament_type = std::move(machine_filament_info.filament_type);
    m_plater->box_msg.slot_id = std::move(machine_filament_info.slot_id);
    m_plater->box_msg.auto_reload_detect = std::move(machine_filament_info.auto_reload_detect);
    PresetBundle &preset_bundle = *wxGetApp().preset_bundle;
    std::string preset_name_box = NormalizeVendor(preset_bundle.printers.get_edited_preset().name);
    m_plater->box_msg.box_list_preset_name = preset_name_box;

    m_plater->box_msg.box_list_printer_ip = m_sync_printer_ip;
    m_plater->box_msg.box_list_printer_api_key = m_sync_printer_api_key;
}
 
void GetBoxInfoDialog::cancel(wxCommandEvent &event)
{
    this->EndModal(wxID_CANCEL);
}

std::string GetBoxInfoDialog::NormalizeVendor(const std::string& str)
{
    std::string normalized;
    for (char c : str) {
        if (std::isalnum(c)) {
            normalized += std::tolower(c);
        }
    }
    return normalized;
}

void GetBoxInfoDialog::sync_box_list(){
    load_box_list();

    Tab* tab_print = wxGetApp().get_tab(Preset::TYPE_PRINT);
    Tab* tab_printer = wxGetApp().get_tab(Preset::TYPE_PRINTER);

    DynamicPrintConfig new_config_printer = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    DynamicPrintConfig new_config_prints = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    ConfigOptionFloats* nozzle_config = new ConfigOptionFloats();
    //y26
    for (int i = 0; i < wxGetApp().preset_bundle->filament_box_list.size(); i++) {
        nozzle_config->values.push_back(new_config_printer.opt_float("nozzle_diameter", 0u));
    }
    new_config_printer.set_key_value("nozzle_diameter", nozzle_config);
    new_config_printer.set_key_value("single_extruder_multi_material", new ConfigOptionBool(true));

    new_config_prints.set_key_value("wipe_tower", new ConfigOptionBool(true));

    tab_print->load_config(new_config_prints);
    tab_printer->load_config(new_config_printer);

    wxGetApp().load_current_presets();

    int idx = 0;
    for (auto& entry : wxGetApp().preset_bundle->filament_box_list) {
        auto& tray = entry.second;
        std::string preset_name = "";
        //y26
        if(tray.has("preset_name"))
            preset_name = tray.opt_string("preset_name", 0u);
        else {
            std::string printer_preset_name = wxGetApp().preset_bundle->physical_printers.get_selected_printer_preset_name();
            preset_name = "Generic PLA @" + printer_preset_name;
        }
        wxGetApp().preset_bundle->set_filament_preset(idx, preset_name);
        dynamic_cast<TabFilament*>(wxGetApp().get_tab(Preset::TYPE_FILAMENT))->set_active_extruder(idx);
        idx++;
    }
    wxGetApp().sidebar().update_all_filament_comboboxes();

    std::vector<std::string> colors;
    for (auto& filament : wxGetApp().preset_bundle->filament_box_list) {
        auto info = filament.second;
        std::string color = info.opt_string("filament_colour", 0u);
        colors.push_back(color);
    }
    DynamicPrintConfig cfg_new = *wxGetApp().get_tab(Preset::TYPE_PRINTER)->get_config();
    cfg_new.set_key_value("extruder_colour", new ConfigOptionStrings(colors));

    wxGetApp().get_tab(Preset::TYPE_PRINTER)->load_config(cfg_new);
    wxGetApp().plater()->on_config_change(cfg_new);
}

void GetBoxInfoDialog::load_box_list(){
    std::map<int, DynamicPrintConfig> filament_box_list;
    char n = 'A';
    char t = 0;
    int count = 0;
    for (int i = 0; i < 16; i++) {
        count++;
        if (syn_box_info.slot_state[i] == 0)
            continue;
        DynamicPrintConfig tray_config;
        tray_config.set_key_value("filament_id", new ConfigOptionStrings{ syn_box_info.filament_id[i] });
        tray_config.set_key_value("tag_uid", new ConfigOptionStrings{ "" });  //clear
        tray_config.set_key_value("filament_type", new ConfigOptionStrings{syn_box_info.filament_type[i]}); //clear
        tray_config.set_key_value("slot_state", new ConfigOptionStrings{std::to_string(syn_box_info.slot_state[i])});
        tray_config.set_key_value("slot_id", new ConfigOptionStrings{ std::to_string(syn_box_info.slot_id[i]) });
        int group = i / 4 + 1;
        char suffix = 'A' + (i % 4); 
        //std::string tray_name = "1" + std::string(1, 'A' + i);
        std::string tray_name = std::to_string(group) + suffix;
        tray_config.set_key_value("tray_name", new ConfigOptionStrings{ tray_name });  //1A 1B 1C
        tray_config.set_key_value("filament_colour", new ConfigOptionStrings{ into_u8(wxColour(syn_box_info.filament_colors[i]).GetAsString(wxC2S_HTML_SYNTAX)) });//filament_color
        tray_config.set_key_value("filament_exist", new ConfigOptionBools{ true });  //default

        // tray_config.set_key_value("filament_multi_colors", new ConfigOptionStrings{});
        // tray_config.opt<ConfigOptionStrings>("filament_multi_colors")->values.push_back(into_u8(wxColour(syn_box_info.filament_colors[i]).GetAsString(wxC2S_HTML_SYNTAX)));
        filament_box_list.emplace('A' + i, std::move(tray_config));
    }
    
    wxGetApp().preset_bundle->filament_box_list = filament_box_list;
}

}} // namespace Slic3r
