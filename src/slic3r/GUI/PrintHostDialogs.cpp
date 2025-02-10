#include "PrintHostDialogs.hpp"

#include <algorithm>
#include <iomanip>

#include <wx/frame.h>
#include <wx/progdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/button.h>
#include <wx/dataview.h>
#include <wx/wupdlock.h>
#include <wx/debug.h>
#include <wx/msgdlg.h>

#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/convert.hpp>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "libslic3r/AppConfig.hpp"
#include "NotificationManager.hpp"
#include "ExtraRenderers.hpp"
#include "format.hpp"

#include <wx/tooltip.h>

namespace fs = boost::filesystem;

namespace Slic3r {
namespace GUI {

static const char *CONFIG_KEY_PATH  = "printhost_path";
static const char *CONFIG_KEY_GROUP = "printhost_group";
static const char* CONFIG_KEY_STORAGE = "printhost_storage";

//B61//y20
PrintHostSendDialog::PrintHostSendDialog(const fs::path &path, PrintHostPostUploadActions post_actions, const wxArrayString &groups, const wxArrayString& storage_paths, const wxArrayString& storage_names, Plater* plater, bool onlylink)
    : MsgDialog(static_cast<wxWindow*>(wxGetApp().mainframe), _L("Send G-Code to printer host"), _L(""), 0) // Set style = 0 to avoid default creation of the "OK" button. 
                                                                                                                                                               // All buttons will be added later in this constructor 
    , txt_filename(new wxTextCtrl(this, wxID_ANY))
    , combo_groups(!groups.IsEmpty() ? new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, groups, wxCB_READONLY) : nullptr)
    , combo_storage(storage_names.GetCount() > 1 ? new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, storage_names, wxCB_READONLY) : nullptr)
    , post_upload_action(PrintHostPostUploadAction::None)
    , m_paths(storage_paths)
    , m_plater(plater)
{
#ifdef __APPLE__
    txt_filename->OSXDisableAllSmartSubstitutions();
#endif
    const AppConfig *app_config = wxGetApp().app_config;

    auto *label_dir_hint = new wxStaticText(this, wxID_ANY, _L("Use forward slashes ( / ) as a directory separator if needed."));
    label_dir_hint->Wrap(CONTENT_WIDTH * wxGetApp().em_unit());
    //B61
    auto *label_dir_hint2 = new wxStaticText(this, wxID_ANY, _L("Upload to Printer Host with the following filename:"));
    label_dir_hint2->Wrap(CONTENT_WIDTH * wxGetApp().em_unit());

    //B64//y20
    int bed_num = m_plater->get_beds_num();
    ThumbnailData thumbnail_data;
    if (bed_num > 1) {
        int active_bed = m_plater->get_active_bed();
        thumbnail_data = m_plater->get_thumbnailldate_from_bed(active_bed);
    }
    else
        thumbnail_data = m_plater->get_thumbnailldate_send();

    wxImage image(thumbnail_data.width, thumbnail_data.height);
    image.InitAlpha();

    for (unsigned int r = 0; r < thumbnail_data.height; ++r) {
        unsigned int rr = (thumbnail_data.height - 1 - r) * thumbnail_data.width;
        for (unsigned int c = 0; c < thumbnail_data.width; ++c) {
            unsigned char *px = (unsigned char *) thumbnail_data.pixels.data() + 4 * (rr + c);
            image.SetRGB((int) c, (int) r, px[0], px[1], px[2]);
            image.SetAlpha((int) c, (int) r, px[3]);
        }
    }
    //y21
    image.Rescale(128, 160);
    wxBitmap        bitmap(image);
    wxStaticBitmap *static_bitmap = new wxStaticBitmap(this, wxID_ANY, bitmap);

    //static_bitmap->SetSize(wxSize(20, 20));
    //static_bitmap->SetMinSize(wxSize(100, 100));

    wxBoxSizer *row_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *hbox1 = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *hbox2 = new wxBoxSizer(wxHORIZONTAL);


    wxBoxSizer *vbox1 = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *vbox2 = new wxBoxSizer(wxVERTICAL);

    vbox1->Add(static_bitmap, 0, wxALL | wxALIGN_CENTER);
    // Add add.svg image
    //wxBitmap        add_bitmap(*get_bmp_bundle("add.svg"), wxBITMAP_TYPE_SVG);
    //y20
    PrintStatistics print_statistic = wxGetApp().plater()->get_fff_prints()[m_plater->get_active_bed()].get()->print_statistics();
    wxStaticBitmap *add_bitmap = new wxStaticBitmap(this, wxID_ANY, *get_bmp_bundle("print_time", 20));
    row_sizer->Add(add_bitmap, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    // Add print_statisticestimated_normal_print_time text
    wxStaticText *estimated_print_time_text = new wxStaticText(this, wxID_ANY, wxString::Format("%s", print_statistic.estimated_normal_print_time));
    row_sizer->Add(estimated_print_time_text, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    // Add delete.svg image
    wxStaticBitmap *delete_static_bitmap = new wxStaticBitmap(this, wxID_ANY, *get_bmp_bundle("cost_weight", 20));
    row_sizer->Add(delete_static_bitmap, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    // Add print_statistictotal_weight text
    wxStaticText *total_weight_text = new wxStaticText(this, wxID_ANY, wxString::Format("%.4fg", print_statistic.total_weight));
    row_sizer->Add(total_weight_text, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    vbox1->Add(row_sizer, 0, wxALIGN_CENTER);
    //B61
    vbox2->Add(label_dir_hint2);
    vbox2->Add(txt_filename, 0, wxEXPAND);
    vbox2->Add(label_dir_hint);
    vbox2->AddSpacer(VERT_SPACING);

    auto *label_input_max_send = new wxStaticText(this, wxID_ANY, _L("(It depends on how many devices can undergo heating at the same time.)"));
    label_input_max_send->Wrap(CONTENT_WIDTH * wxGetApp().em_unit());

    auto *label_input_sending_interval = new wxStaticText(this, wxID_ANY, _L("(It depends on how long it takes to complete the heating.)"));
    label_input_sending_interval->Wrap(CONTENT_WIDTH * wxGetApp().em_unit());

    //y16
    m_switch_to_device = new wxCheckBox(this, wxID_ANY, _L("Switch to Device tab"), wxDefaultPosition);
    m_switch_to_device->SetValue((wxGetApp().app_config->get("switch to device tab after upload") == "1") ? true : false);
    wxToolTip* switch_tips = new wxToolTip(_L("Switch to Device tab after upload."));
    m_switch_to_device->SetToolTip(switch_tips);
    m_switch_to_device->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, [this](wxCommandEvent &event){
        if(event.GetInt() == 1)
            wxGetApp().app_config->set("switch to device tab after upload", "1");
        else
            wxGetApp().app_config->set("switch to device tab after upload", "0");
    });

    wxBoxSizer *max_printer_send =
        create_item_input(_L("Send"),
                          _L("printers at the same time."), this,
                          "", "max_send");

    vbox2->Add(max_printer_send);
    vbox2->Add(label_input_max_send);
    vbox2->Add(0, 0, 0, wxEXPAND | wxTOP, 23);

    wxBoxSizer *delay_time = create_item_input(_L("Wait"),
                                               _L("minute each batch."),
                                               this, "", "sending_interval");


    vbox2->Add(delay_time);
    vbox2->Add(label_input_sending_interval);

    //y16
    vbox2->Add(0, 0, 0, wxEXPAND | wxTOP, 15);
    vbox2->Add(m_switch_to_device);

    hbox1->Add(vbox1);
    hbox1->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);
    hbox1->Add(vbox2);
    content_sizer->Add(0, 0, 0, wxEXPAND | wxTOP, 23);

    content_sizer->Add(hbox1);
    content_sizer->Add(0, 0, 0, wxEXPAND | wxTOP, 23);
    //B53
    wxBoxSizer *                           checkbox_sizer = new wxBoxSizer(wxVERTICAL);
    PresetBundle &                         preset_bundle  = *wxGetApp().preset_bundle;

    wxScrolledWindow *scroll_macine_list = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(800), FromDIP(300)),
                                                                wxHSCROLL | wxVSCROLL);
    if(check_dark_mode())
        scroll_macine_list->SetBackgroundColour(wxColour(62,62,62));
    else
        scroll_macine_list->SetBackgroundColour(*wxWHITE);
    scroll_macine_list->SetScrollRate(5, 5);
    scroll_macine_list->SetMinSize(wxSize(FromDIP(320), 10 * FromDIP(27)));
    scroll_macine_list->SetMaxSize(wxSize(FromDIP(320), 10 * FromDIP(27)));
    wxBoxSizer *sizer_machine_list = new wxBoxSizer(wxVERTICAL);
    scroll_macine_list->SetSizer(sizer_machine_list);
    scroll_macine_list->Layout();

    //B64
    const PhysicalPrinterCollection &      ph_printers    = preset_bundle.physical_printers;
    std::vector<PhysicalPrinterPresetData> preset_data;
    for (PhysicalPrinterCollection::ConstIterator it = ph_printers.begin(); it != ph_printers.end(); ++it) {
        for (const std::string &preset_name : it->get_preset_names()) {
            Preset *preset = wxGetApp().preset_bundle->printers.find_preset(preset_name);
            if (preset != nullptr) {
                wxStringTokenizer   tokenizer(wxString::FromUTF8(it->get_full_name(preset_name)), "*");
                wxString            tokenTemp    = tokenizer.GetNextToken().Trim();
                std::string         tem_name     = into_u8(tokenTemp);
                auto *            printer      = preset_bundle.physical_printers.find_printer(tem_name);
                wxString          host         = "";
                DynamicPrintConfig *cfg_t        = nullptr;
                if (printer != nullptr) {
                    host                                 = (printer->config.opt_string("print_host"));
                    cfg_t            = &(printer->config);
                }
                //B62 // y1
                preset_data.push_back({from_u8(it->get_full_name(preset_name)).Lower(), from_u8(preset_name),
                                       from_u8(it->get_full_name(preset_name)), ph_printers.is_selected(it, preset_name),
                                       preset_name, host, cfg_t
                });
            }
        }
    }
    //y4
    std::string printer_preset = ""; 
    if (!onlylink) {
        PhysicalPrinter selected_printer = ph_printers.get_selected_printer();
        std::set<std::string> selected_printer_presets = selected_printer.get_preset_names();
        printer_preset                     = *selected_printer_presets.begin();
    } else {
        Preset &select_preset = preset_bundle.printers.get_edited_preset();
        printer_preset        = select_preset.name;
    }
    m_presetData = preset_data;
    for (const PhysicalPrinterPresetData &data : preset_data) {
        //y4
        SendCheckBox *checkbox = new SendCheckBox(scroll_macine_list, wxID_ANY, " " + data.fullname + "\n IP: " + data.host);
        unSelectedBoxes.push_back(checkbox);
        checkbox->Bind(wxEVT_CHECKBOX, &PrintHostSendDialog::OnCheckBoxClicked, this);
        checkbox->SetState(data.selected);
         if (printer_preset.find(data.preset_name) == std::string::npos) {
            checkbox->Disable();
         }
        sizer_machine_list->Add(checkbox, 0, wxEXPAND | wxALL, 5);
    }

    wxBoxSizer *scrool_box_sizer = new wxBoxSizer(wxVERTICAL);

    wxPanel *panel = new wxPanel(this, wxID_ANY);
    if(check_dark_mode())
        panel->SetBackgroundColour(wxColour(62,62,62));
    else
        panel->SetBackgroundColour(*wxWHITE);

    wxBoxSizer *box_sizer = new wxBoxSizer(wxHORIZONTAL);
    panel->SetSizer(box_sizer);

    wxCheckBox *selectcheckbox = new wxCheckBox(panel, wxID_ANY, "");
    //y4
    selectcheckbox->Bind(wxEVT_CHECKBOX, [sizer_machine_list, this](wxCommandEvent &event) {
        bool isChecked = event.IsChecked();
        for (int i = 0; i < sizer_machine_list->GetItemCount(); i++) {
            SendCheckBox *checkbox = dynamic_cast<SendCheckBox *>(sizer_machine_list->GetItem(i)->GetWindow());
            if ((isChecked && checkbox->IsChecked()) || (!isChecked && !checkbox->IsChecked())) {
                continue;
            }
            else if (checkbox && checkbox->IsEnabled()) {
                checkbox->SetState(isChecked);
            }
            checkbox->Refresh();
            if (SelectedBoxes.size() == 6)
                break;
        }
        
    });

    wxStaticText *text = new wxStaticText(panel, wxID_ANY, _L("QIDI Local's Physical Printer"));
    text->SetWindowStyle(wxALIGN_CENTER_HORIZONTAL);

    box_sizer->Add(selectcheckbox, 0, wxEXPAND | wxALL, 5);
    box_sizer->Add(text, 0, wxEXPAND | wxALL, 5);

    wxStaticLine *line = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    line->SetForegroundColour(wxColour(220, 220, 220));

    scrool_box_sizer->Add(panel, 0, wxEXPAND);
    scrool_box_sizer->Add(line, 0, wxEXPAND | wxTOP | wxBOTTOM, 5);
    scrool_box_sizer->Add(scroll_macine_list);

    wxScrolledWindow *scroll_macine_list2 = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(800), FromDIP(300)),
                                                                wxHSCROLL | wxVSCROLL);
    if(check_dark_mode())
        scroll_macine_list2->SetBackgroundColour(wxColour(62,62,62));
    else
        scroll_macine_list2->SetBackgroundColour(*wxWHITE);
    scroll_macine_list2->SetScrollRate(5, 5);
    scroll_macine_list2->SetMinSize(wxSize(FromDIP(320), 10 * FromDIP(27)));
    scroll_macine_list2->SetMaxSize(wxSize(FromDIP(320), 10 * FromDIP(27)));
    wxBoxSizer* sizer_machine_list2 = new wxBoxSizer(wxVERTICAL);
    scroll_macine_list2->SetSizer(sizer_machine_list2);
    scroll_macine_list2->Layout();

#if QDT_RELEASE_TO_PUBLIC
    auto m_devices = wxGetApp().get_devices();
     for (const auto &device : m_devices) 
     {
        //y4
        SendCheckBox *checkbox = new SendCheckBox(scroll_macine_list2, wxID_ANY,
                                                " " + from_u8(device.device_name) + "\n IP: " + device.local_ip);
        unSelectedBoxes.push_back(checkbox);
        checkbox->Bind(wxEVT_CHECKBOX, &PrintHostSendDialog::OnCheckBoxClicked, this);
        checkbox->SetState(false);
        std::string machine_type = device.machine_type;
        if (machine_type.empty()) 
        {
            std::size_t found = device.device_name.find('@');
            if (found != std::string::npos)
                machine_type = device.device_name.substr(found + 1);
        }
        if (NormalizeVendor(printer_preset).find(NormalizeVendor(machine_type)) == std::string::npos || machine_type.empty())
            checkbox->Disable();
        sizer_machine_list2->Add(checkbox, 0, wxEXPAND | wxALL, 5);
     }
#endif


    wxBoxSizer *scrool_box_sizer2 = new wxBoxSizer(wxVERTICAL);

    wxPanel *panel2 = new wxPanel(this, wxID_ANY);
    if(check_dark_mode())
        panel2->SetBackgroundColour(wxColour(62,62,62));
    else
        panel2->SetBackgroundColour(*wxWHITE);

    wxBoxSizer *box_sizer2 = new wxBoxSizer(wxHORIZONTAL);
    panel2->SetSizer(box_sizer2);

    wxCheckBox *selectcheckbox2 = new wxCheckBox(panel2, wxID_ANY, "");
    //y4
    selectcheckbox2->Bind(wxEVT_CHECKBOX, [sizer_machine_list2, this](wxCommandEvent &event) {
        bool isChecked = event.IsChecked();
        for (int i = 0; i < sizer_machine_list2->GetItemCount(); i++) {
            SendCheckBox *checkbox = dynamic_cast<SendCheckBox *>(sizer_machine_list2->GetItem(i)->GetWindow());
            if ((isChecked && checkbox->IsChecked()) || (!isChecked && !checkbox->IsChecked())) {
                continue;
            } else if (checkbox && checkbox->IsEnabled()) {
                checkbox->SetState(isChecked);
            }
            checkbox->Refresh();
            if (SelectedBoxes.size() == 6)
                break;
        }
        sizer_machine_list2->Layout();
    });

    wxStaticText *text2 = new wxStaticText(panel2, wxID_ANY, _L("QIDI Link's Physical Printer"));
    text2->SetWindowStyle(wxALIGN_CENTER_HORIZONTAL);

    box_sizer2->Add(selectcheckbox2, 0, wxEXPAND | wxALL, 5);
    box_sizer2->Add(text2, 0, wxEXPAND | wxALL, 5);

    wxStaticLine *line2 = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    line2->SetForegroundColour(wxColour(220, 220, 220));



    scrool_box_sizer2->Add(panel2, 0, wxEXPAND);
    scrool_box_sizer2->Add(line2, 0, wxEXPAND | wxTOP | wxBOTTOM, 5);
    scrool_box_sizer2->Add(scroll_macine_list2);


    wxStaticBoxSizer *sizer1 = new wxStaticBoxSizer(wxVERTICAL, this, _L(""));
    sizer1->Add(scrool_box_sizer);


    wxStaticBoxSizer *sizer2 = new wxStaticBoxSizer(wxVERTICAL, this, _L(""));
    sizer2->Add(scrool_box_sizer2);

    hbox2->Add(sizer1);
    hbox2->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);
    hbox2->Add(sizer2);

    content_sizer->Add(hbox2 , 1, wxEXPAND);
    content_sizer->Add(0, 0, 0, wxEXPAND | wxBOTTOM, 23);

    if (combo_groups != nullptr) {
        // Repetier specific: Show a selection of file groups.
        auto *label_group = new wxStaticText(this, wxID_ANY, _L("Group"));
        content_sizer->Add(label_group);
        content_sizer->Add(combo_groups, 0, wxBOTTOM, 2*VERT_SPACING);        
        wxString recent_group = from_u8(app_config->get("recent", CONFIG_KEY_GROUP));
        if (! recent_group.empty())
            combo_groups->SetValue(recent_group);
    }

    if (combo_storage != nullptr) {
        // QIDILink specific: User needs to choose a storage
        auto* label_group = new wxStaticText(this, wxID_ANY, _L("Upload to storage") + ":");
        content_sizer->Add(label_group);
        content_sizer->Add(combo_storage, 0, wxBOTTOM, 2 * VERT_SPACING);
        combo_storage->SetValue(storage_names.front());
        wxString recent_storage = from_u8(app_config->get("recent", CONFIG_KEY_STORAGE));
        if (!recent_storage.empty())
            combo_storage->SetValue(recent_storage); 
    } else if (storage_names.GetCount() == 1){
        // QIDILink specific: Show which storage has been detected.
        auto* label_group = new wxStaticText(this, wxID_ANY, _L("Upload to storage") + ": " + storage_names.front());
        content_sizer->Add(label_group);
        m_preselected_storage = storage_paths.front();
    }


    wxString recent_path = from_u8(app_config->get("recent", CONFIG_KEY_PATH));
    if (recent_path.Length() > 0 && recent_path[recent_path.Length() - 1] != '/') {
        recent_path += '/';
    }
    const auto recent_path_len = recent_path.Length();
    recent_path += path.filename().wstring();
    wxString stem(path.stem().wstring());
    const auto stem_len = stem.Length();

    txt_filename->SetValue(recent_path);

    if (size_t extension_start = recent_path.find_last_of('.'); extension_start != std::string::npos)
        m_valid_suffix = recent_path.substr(extension_start);
    // .gcode suffix control
    auto validate_path = [this](const wxString &path) -> bool {
        if (!path.Lower().EndsWith(m_valid_suffix.Lower())) {
            MessageDialog msg_wingow(this, wxString::Format(_L("Upload filename doesn't end with \"%s\". Do you wish to continue?"), m_valid_suffix), wxString(SLIC3R_APP_NAME), wxYES | wxNO);
            return msg_wingow.ShowModal() == wxID_YES;
        }
        
        //y21
        wxString unusable_symbols = "<>[]:\\|?*\"";
        for(auto c : path){
            if(unusable_symbols.find(c) != std::string::npos){
                ErrorDialog msg(this, format_wxstr("%1%\n%2% %3%", _L("The provided name is not valid;"),
									_L("the following characters are not allowed:"), unusable_symbols), wxOK);
                msg.ShowModal();
                return false;
            }
        }
        return true;
    };

    //B53 //B64
    auto* btn_ok = add_button(wxID_OK, false, _L("Upload"));
    btn_ok->Bind(wxEVT_BUTTON, [this, validate_path, sizer_machine_list, sizer_machine_list2](wxCommandEvent &) {
        if (validate_path(txt_filename->GetValue())) {
            std::vector<bool> checkbox_states;

            for (int i = 0; i < sizer_machine_list->GetItemCount(); i++) {
                wxCheckBox *checkbox = dynamic_cast<wxCheckBox *>(sizer_machine_list->GetItem(i)->GetWindow());
                if (checkbox) {
                    checkbox_states.push_back(checkbox->GetValue());
                }
            }
            m_checkbox_states  = checkbox_states;
            checkbox_states.clear();

            for (int i = 0; i < sizer_machine_list2->GetItemCount(); i++) {
                wxCheckBox *checkbox = dynamic_cast<wxCheckBox *>(sizer_machine_list2->GetItem(i)->GetWindow());
                if (checkbox) {
                    checkbox_states.push_back(checkbox->GetValue());
                }
            }
            m_checkbox_net_states = checkbox_states;
            post_upload_action = PrintHostPostUploadAction::None;
            EndDialog(wxID_OK);
        }
        txt_filename->SetFocus();
    });
    txt_filename->SetFocus();
    
    //B53
    Bind(wxEVT_CHECKBOX, [btn_ok, sizer_machine_list, sizer_machine_list2, this](wxCommandEvent &event) {
        bool any_checkbox_selected = false;
        for (int i = 0; i < sizer_machine_list->GetItemCount(); i++) {
            wxCheckBox *checkbox = dynamic_cast<wxCheckBox *>(sizer_machine_list->GetItem(i)->GetWindow());
            if (checkbox && checkbox->GetValue()) {
                any_checkbox_selected = true;
                break;
            }
        }
        for (int i = 0; i < sizer_machine_list2->GetItemCount(); i++) {
            wxCheckBox *checkbox = dynamic_cast<wxCheckBox *>(sizer_machine_list2->GetItem(i)->GetWindow());
            if (checkbox && checkbox->GetValue()) {
                any_checkbox_selected = true;
                break;
            }
        }
        btn_ok->Enable(any_checkbox_selected);
    });
    if (post_actions.has(PrintHostPostUploadAction::QueuePrint)) {
        auto* btn_print = add_button(wxID_ADD, false, _L("Upload to Queue"));
        btn_print->Bind(wxEVT_BUTTON, [this, validate_path](wxCommandEvent&) {
            if (validate_path(txt_filename->GetValue())) {
                post_upload_action = PrintHostPostUploadAction::QueuePrint;
                EndDialog(wxID_OK);
            }
            });
    }

    //B53 //B64
    if (post_actions.has(PrintHostPostUploadAction::StartPrint)) {
        auto* btn_print = add_button(wxID_YES, false, _L("Upload and Print"));
        btn_print->Bind(wxEVT_BUTTON, [this, validate_path, sizer_machine_list, sizer_machine_list2](wxCommandEvent &) {
            if (validate_path(txt_filename->GetValue())) {
                std::vector<bool> checkbox_states;

                for (int i = 0; i < sizer_machine_list->GetItemCount(); i++) {
                    wxCheckBox *checkbox = dynamic_cast<wxCheckBox *>(sizer_machine_list->GetItem(i)->GetWindow());
                    if (checkbox) {
                        checkbox_states.push_back(checkbox->GetValue());
                    }
                }
                m_checkbox_states  = checkbox_states;
                checkbox_states.clear();

                for (int i = 0; i < sizer_machine_list2->GetItemCount(); i++) {
                    wxCheckBox *checkbox = dynamic_cast<wxCheckBox *>(sizer_machine_list2->GetItem(i)->GetWindow());
                    if (checkbox) {
                        checkbox_states.push_back(checkbox->GetValue());
                    }
                }
                m_checkbox_net_states = checkbox_states;
                post_upload_action = PrintHostPostUploadAction::StartPrint;
                EndDialog(wxID_OK);
            }
            txt_filename->SetFocus();
        });
        //B53 //B64
        Bind(wxEVT_CHECKBOX, [btn_ok, btn_print, sizer_machine_list, sizer_machine_list2, this](wxCommandEvent &event) {
            bool any_checkbox_selected = false;
            for (int i = 0; i < sizer_machine_list->GetItemCount(); i++) {
                wxCheckBox *checkbox = dynamic_cast<wxCheckBox *>(sizer_machine_list->GetItem(i)->GetWindow());
                if (checkbox && checkbox->GetValue()) {
                    any_checkbox_selected = true;
                    break;
                }
            }

            for (int i = 0; i < sizer_machine_list2->GetItemCount(); i++) {
                wxCheckBox *checkbox = dynamic_cast<wxCheckBox *>(sizer_machine_list2->GetItem(i)->GetWindow());
                if (checkbox && checkbox->GetValue()) {
                    any_checkbox_selected = true;
                    break;
                }
            }
            btn_print->Enable(any_checkbox_selected);
            btn_ok->Enable(any_checkbox_selected);
        });
    }

    if (post_actions.has(PrintHostPostUploadAction::StartSimulation)) {
        // Using wxID_MORE as a button identifier to be different from the other buttons, wxID_MORE has no other meaning here.
        auto* btn_simulate = add_button(wxID_MORE, false, _L("Upload and Simulate"));
        btn_simulate->Bind(wxEVT_BUTTON, [this, validate_path](wxCommandEvent&) {
            if (validate_path(txt_filename->GetValue())) {
                post_upload_action = PrintHostPostUploadAction::StartSimulation;
                EndDialog(wxID_OK);
            }        
        });
    }

    add_button(wxID_CANCEL);
    finalize();

#ifdef __linux__
    // On Linux with GTK2 when text control lose the focus then selection (colored background) disappears but text color stay white
    // and as a result the text is invisible with light mode
    // Workaround: Unselect text selection explicitly on kill focus
    txt_filename->Bind(wxEVT_KILL_FOCUS, [this](wxEvent& e) {
        e.Skip();
        txt_filename->SetInsertionPoint(txt_filename->GetLastPosition());
    }, txt_filename->GetId());
#endif /* __linux__ */

    Bind(wxEVT_SHOW, [=](const wxShowEvent &) {
        // Another similar case where the function only works with EVT_SHOW + CallAfter,
        // this time on Mac.
        CallAfter([=]() {
            txt_filename->SetInsertionPoint(0);
            txt_filename->SetSelection(recent_path_len, recent_path_len + stem_len);
        });
    });
}

fs::path PrintHostSendDialog::filename() const
{
    return into_path(txt_filename->GetValue());
}

PrintHostPostUploadAction PrintHostSendDialog::post_action() const
{
    return post_upload_action;
}

std::string PrintHostSendDialog::group() const
{
     if (combo_groups == nullptr) {
         return "";
     } else {
         wxString group = combo_groups->GetValue();
         return into_u8(group);
    }
}

std::string PrintHostSendDialog::storage() const
{
    if (!combo_storage)
        return GUI::format("%1%", m_preselected_storage);
    if (combo_storage->GetSelection() < 0 || combo_storage->GetSelection() >= int(m_paths.size()))
        return {};
    return into_u8(m_paths[combo_storage->GetSelection()]);
}
//B64
wxBoxSizer *PrintHostSendDialog::create_item_input(
    wxString str_before, wxString str_after, wxWindow *parent, wxString tooltip, std::string param)
{
    wxBoxSizer *sizer_input = new wxBoxSizer(wxHORIZONTAL);
    auto        input_title = new wxStaticText(parent, wxID_ANY, str_before);
    input_title->SetForegroundColour(wxColour(38, 46, 48));
    input_title->SetFont(::Label::Body_13);
    input_title->SetToolTip(tooltip);
    input_title->Wrap(-1);

    auto       input = new ::TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(50), -1),
                                 wxTE_PROCESS_ENTER);
    StateColor input_bg(std::pair<wxColour, int>(wxColour("#F0F0F1"), StateColor::Disabled),
                        std::pair<wxColour, int>(*wxWHITE, StateColor::Enabled));
    input->SetBackgroundColor(input_bg);
    input->GetTextCtrl()->SetValue(wxGetApp().app_config->get(param));
    wxTextValidator validator(wxFILTER_DIGITS);
    input->GetTextCtrl()->SetValidator(validator);

    auto second_title = new wxStaticText(parent, wxID_ANY, str_after, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    second_title->SetForegroundColour(wxColour(38, 46, 48));
    second_title->SetFont(::Label::Body_13);
    second_title->SetToolTip(tooltip);
    second_title->Wrap(-1);

    //sizer_input->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);
    sizer_input->Add(input_title, 0, wxALIGN_CENTER_VERTICAL | wxALL, 3);
    sizer_input->Add(input, 0, wxALIGN_CENTER_VERTICAL, 0);
    sizer_input->Add(0, 0, 0, wxEXPAND | wxLEFT, 3);
    sizer_input->Add(second_title, 0, wxALIGN_CENTER_VERTICAL | wxALL, 3);

    input->GetTextCtrl()->Bind(wxEVT_TEXT, [this, param, input](wxCommandEvent &e) {
        auto value = input->GetTextCtrl()->GetValue();
        if (!value.empty()) {
            if (std::stoi(into_u8(value)) > 6 && (param == "max_send")) {
                MessageDialog msg_wingow(nullptr, _L("The max send number cannot exceed 6"), "", wxICON_WARNING | wxOK);
                msg_wingow.ShowModal();
                value = "6";
                input->GetTextCtrl()->SetValue(value);
            } else if (std::stoi(into_u8(value)) > 240 && (param == "sending_interval")) {
                MessageDialog msg_wingow(nullptr, _L("The sending interval cannot exceed 240"), "", wxICON_WARNING | wxOK);
                msg_wingow.ShowModal();
                value = "240";
                input->GetTextCtrl()->SetValue(value);
            }
            wxGetApp().app_config->set(param, std::string(value.mb_str()));
            wxGetApp().app_config->save();
        }
        e.Skip();
    });
    return sizer_input;
}
void PrintHostSendDialog::EndModal(int ret)
{
    if (ret == wxID_OK) {
        // Persist path and print settings
        wxString path = txt_filename->GetValue();
        int last_slash = path.Find('/', true);
		if (last_slash == wxNOT_FOUND)
			path.clear();
		else
            path = path.SubString(0, last_slash);
                
		AppConfig *app_config = wxGetApp().app_config;
		app_config->set("recent", CONFIG_KEY_PATH, into_u8(path));

        if (combo_groups != nullptr) {
            wxString group = combo_groups->GetValue();
            app_config->set("recent", CONFIG_KEY_GROUP, into_u8(group));
        }
        if (combo_storage != nullptr) {
            wxString storage = combo_storage->GetValue();
            app_config->set("recent", CONFIG_KEY_STORAGE, into_u8(storage));
        }
    }

    MsgDialog::EndModal(ret);
}
//y4
std::string PrintHostSendDialog::NormalizeVendor(const std::string &str)
{
    std::string normalized;
    for (char c : str) {
        if (std::isalnum(c)) {
            normalized += std::tolower(c);
        }
    }
    return normalized;
}
//y4
void PrintHostSendDialog::OnCheckBoxClicked(wxCommandEvent &event) 
{ 
    SendCheckBox *checkbox = dynamic_cast<SendCheckBox *>(event.GetEventObject());
    if (checkbox->IsChecked()) {
        if (SelectedBoxes.size() < 6) {
            SelectedBoxes.push_back(checkbox);
            unSelectedBoxes.erase(std::remove(unSelectedBoxes.begin(), unSelectedBoxes.end(), checkbox), unSelectedBoxes.end()); 
        } else {
            checkbox->SetValue(false);
            wxString      msg_text = _L("The max selected printer number is 6.");
            MessageDialog dialog(nullptr, msg_text, _L("Information"), wxICON_INFORMATION | wxOK);
            dialog.ShowModal();
            return;
        }
    } else {
        SelectedBoxes.erase(std::remove(SelectedBoxes.begin(), SelectedBoxes.end(), checkbox), SelectedBoxes.end()); 
        unSelectedBoxes.push_back(checkbox);
    }
}


//B64
wxDEFINE_EVENT(EVT_PRINTHOST_WAIT, PrintHostQueueDialog::Event);

wxDEFINE_EVENT(EVT_PRINTHOST_PROGRESS, PrintHostQueueDialog::Event);
wxDEFINE_EVENT(EVT_PRINTHOST_ERROR,    PrintHostQueueDialog::Event);
wxDEFINE_EVENT(EVT_PRINTHOST_CANCEL,   PrintHostQueueDialog::Event);
wxDEFINE_EVENT(EVT_PRINTHOST_INFO,  PrintHostQueueDialog::Event);

PrintHostQueueDialog::Event::Event(wxEventType eventType, int winid, size_t job_id)
    : wxEvent(winid, eventType)
    , job_id(job_id)
{}

PrintHostQueueDialog::Event::Event(wxEventType eventType, int winid, size_t job_id, int progress)
    : wxEvent(winid, eventType)
    , job_id(job_id)
    , progress(progress)
{}

//B64
PrintHostQueueDialog::Event::Event(wxEventType eventType, int winid, size_t job_id, int waittime, int progress)
    : wxEvent(winid, eventType), job_id(job_id), waittime(waittime), progress(progress)
{}
PrintHostQueueDialog::Event::Event(wxEventType eventType, int winid, size_t job_id, wxString error)
    : wxEvent(winid, eventType)
    , job_id(job_id)
    , status(std::move(error))
{}

PrintHostQueueDialog::Event::Event(wxEventType eventType, int winid, size_t job_id, wxString tag, wxString status)
    : wxEvent(winid, eventType)
    , job_id(job_id)
    , tag(std::move(tag))
    , status(std::move(status))
{}

wxEvent *PrintHostQueueDialog::Event::Clone() const
{
    return new Event(*this);
}

//B64
PrintHostQueueDialog::PrintHostQueueDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("Print host upload queue"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , on_wait_evt(this, EVT_PRINTHOST_WAIT, &PrintHostQueueDialog::on_wait, this)
    , on_progress_evt(this, EVT_PRINTHOST_PROGRESS, &PrintHostQueueDialog::on_progress, this)
    , on_error_evt(this, EVT_PRINTHOST_ERROR, &PrintHostQueueDialog::on_error, this)
    , on_cancel_evt(this, EVT_PRINTHOST_CANCEL, &PrintHostQueueDialog::on_cancel, this)
    , on_info_evt(this, EVT_PRINTHOST_INFO, &PrintHostQueueDialog::on_info, this)
{
    const auto em = GetTextExtent("m").x;

    auto *topsizer = new wxBoxSizer(wxVERTICAL);

    std::vector<int> widths;
    widths.reserve(7);
    if (!load_user_data(UDT_COLS, widths)) {
        widths.clear();
        for (size_t i = 0; i < 7; i++)
            widths.push_back(-1);
    }

    job_list = new wxDataViewListCtrl(this, wxID_ANY);

    // MSW DarkMode: workaround for the selected item in the list
    auto append_text_column = [this](const wxString& label, int width, wxAlignment align = wxALIGN_LEFT,
                                     int flags = wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE) {
#ifdef _WIN32
            job_list->AppendColumn(new wxDataViewColumn(label, new TextRenderer(), job_list->GetColumnCount(), width, align, flags));
#else
            job_list->AppendTextColumn(label, wxDATAVIEW_CELL_INERT, width, align, flags);
#endif
    };

    // Note: Keep these in sync with Column
    append_text_column(_L("ID"), widths[0]);
    job_list->AppendProgressColumn(_L("Progress"),      wxDATAVIEW_CELL_INERT, widths[1], wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
    append_text_column(_L("Status"),widths[2]);
    append_text_column(_L("Host"),  widths[3]);
    append_text_column(_CTX(L_CONTEXT("Size", "OfFile"), "OfFile"), widths[4]);
    append_text_column(_L("Filename"),      widths[5]);
    append_text_column(_L("Message"), widths[6]);
    //append_text_column(_L("Error Message"), -1, wxALIGN_CENTER, wxDATAVIEW_COL_HIDDEN);
 
    auto *btnsizer = new wxBoxSizer(wxHORIZONTAL);
    btn_cancel = new wxButton(this, wxID_DELETE, _L("Cancel selected"));
    btn_cancel->Disable();
    btn_error = new wxButton(this, wxID_ANY, _L("Show error message"));
    btn_error->Disable();
    // Note: The label needs to be present, otherwise we get accelerator bugs on Mac
    auto *btn_close = new wxButton(this, wxID_CANCEL, _L("Close"));
    btnsizer->Add(btn_cancel, 0, wxRIGHT, SPACING);
    btnsizer->Add(btn_error, 0);
    btnsizer->AddStretchSpacer();
    btnsizer->Add(btn_close);

    topsizer->Add(job_list, 1, wxEXPAND | wxBOTTOM, SPACING);
    topsizer->Add(btnsizer, 0, wxEXPAND);
    SetSizer(topsizer);

    wxGetApp().UpdateDlgDarkUI(this);
    wxGetApp().UpdateDVCDarkUI(job_list);

    std::vector<int> size;
    SetSize(load_user_data(UDT_SIZE, size) ? wxSize(size[0] * em, size[1] * em) : wxSize(HEIGHT * em, WIDTH * em));

    Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) {
        OnSize(evt); 
        save_user_data(UDT_SIZE | UDT_POSITION | UDT_COLS);
     });
    
    std::vector<int> pos;
    if (load_user_data(UDT_POSITION, pos))
        SetPosition(wxPoint(pos[0], pos[1]));

    Bind(wxEVT_MOVE, [this](wxMoveEvent& evt) {
        save_user_data(UDT_SIZE | UDT_POSITION | UDT_COLS);
    });

    job_list->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](wxDataViewEvent&) { on_list_select(); });

    btn_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        int selected = job_list->GetSelectedRow();
        if (selected == wxNOT_FOUND) { return; }

        const JobState state = get_state(selected);
        if (state < ST_ERROR) {
            GUI::wxGetApp().printhost_job_queue().cancel(selected);
        }
    });

    btn_error->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        int selected = job_list->GetSelectedRow();
        if (selected == wxNOT_FOUND) { return; }
        GUI::show_error(nullptr, job_list->GetTextValue(selected, COL_ERRORMSG));
    });
}

void PrintHostQueueDialog::append_job(const PrintHostJob &job)
{
    wxCHECK_RET(!job.empty(), "PrintHostQueueDialog: Attempt to append an empty job");

    wxVector<wxVariant> fields;
    fields.push_back(wxVariant(wxString::Format("%d", job_list->GetItemCount() + 1)));
    fields.push_back(wxVariant(0));
    fields.push_back(wxVariant(_L("Enqueued")));
    fields.push_back(wxVariant(job.printhost->get_host()));
    boost::system::error_code ec;
    boost::uintmax_t size_i = boost::filesystem::file_size(job.upload_data.source_path, ec);
    std::stringstream stream;
    if (ec) {
        stream << "unknown";
        size_i = 0;
        BOOST_LOG_TRIVIAL(error) << ec.message();
    } else 
        stream << std::fixed << std::setprecision(2) << ((float)size_i / 1024 / 1024) << "MB";
    fields.push_back(wxVariant(stream.str()));
    fields.push_back(wxVariant(from_path(job.upload_data.upload_path)));
    fields.push_back(wxVariant(""));
    job_list->AppendItem(fields, static_cast<wxUIntPtr>(ST_NEW));
    // Both strings are UTF-8 encoded.
    upload_names.emplace_back(job.printhost->get_host(), job.upload_data.upload_path.string());

    wxGetApp().notification_manager()->push_upload_job_notification(job_list->GetItemCount(), (float)size_i / 1024 / 1024, job.upload_data.upload_path.string(), job.printhost->get_notification_host());
}

void PrintHostQueueDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_DELETE, wxID_CANCEL, btn_error->GetId() });

    SetMinSize(wxSize(HEIGHT * em, WIDTH * em));

    Fit();
    Refresh();

    save_user_data(UDT_SIZE | UDT_POSITION | UDT_COLS);
}

void PrintHostQueueDialog::on_sys_color_changed()
{
#ifdef _WIN32
    wxGetApp().UpdateDlgDarkUI(this);
    wxGetApp().UpdateDVCDarkUI(job_list);
#endif
}

PrintHostQueueDialog::JobState PrintHostQueueDialog::get_state(int idx)
{
    wxCHECK_MSG(idx >= 0 && idx < job_list->GetItemCount(), ST_ERROR, "Out of bounds access to job list");
    return static_cast<JobState>(job_list->GetItemData(job_list->RowToItem(idx)));
}

void PrintHostQueueDialog::set_state(int idx, JobState state)
{
    wxCHECK_RET(idx >= 0 && idx < job_list->GetItemCount(), "Out of bounds access to job list");
    job_list->SetItemData(job_list->RowToItem(idx), static_cast<wxUIntPtr>(state));

    switch (state) {
        case ST_NEW:        job_list->SetValue(_L("Enqueued"), idx, COL_STATUS); break;
        case ST_PROGRESS:   job_list->SetValue(_L("Uploading"), idx, COL_STATUS); break;
        case ST_ERROR:      job_list->SetValue(_L("Error"), idx, COL_STATUS); break;
        case ST_CANCELLING: job_list->SetValue(_L("Cancelling"), idx, COL_STATUS); break;
        case ST_CANCELLED:  job_list->SetValue(_L("Cancelled"), idx, COL_STATUS); break;
        case ST_COMPLETED:  job_list->SetValue(_L("Completed"), idx, COL_STATUS); break;
    }
    // This might be ambigous call, but user data needs to be saved time to time
    save_user_data(UDT_SIZE | UDT_POSITION | UDT_COLS);
}

void PrintHostQueueDialog::on_list_select()
{
    int selected = job_list->GetSelectedRow();
    if (selected != wxNOT_FOUND) {
        const JobState state = get_state(selected);
        btn_cancel->Enable(state < ST_ERROR);
        btn_error->Enable(state == ST_ERROR);
        Layout();
    } else {
        btn_cancel->Disable();
    }
}

void PrintHostQueueDialog::on_progress(Event &evt)
{
    wxCHECK_RET(evt.job_id < (size_t)job_list->GetItemCount(), "Out of bounds access to job list");

    if (evt.progress < 100) {
        set_state(evt.job_id, ST_PROGRESS);
        job_list->SetValue(wxVariant(evt.progress), evt.job_id, COL_PROGRESS);
    } else {
        set_state(evt.job_id, ST_COMPLETED);
        job_list->SetValue(wxVariant(100), evt.job_id, COL_PROGRESS);
    }

    on_list_select();

    if (evt.progress > 0)
    {
        wxVariant nm, hst;
        job_list->GetValue(nm, evt.job_id, COL_FILENAME);
        job_list->GetValue(hst, evt.job_id, COL_HOST);
        const std::string& nm_str = into_u8(nm.GetString());
        const std::string& hst_str = into_u8(hst.GetString());
        wxGetApp().notification_manager()->set_upload_job_notification_percentage(evt.job_id + 1, nm_str, hst_str, evt.progress / 100.f);
    }
}

//B64
void PrintHostQueueDialog::on_wait(Event &evt)
{
    wxCHECK_RET(evt.job_id < (size_t) job_list->GetItemCount(), "Out of bounds access to job list");
    wxVariant nm, hst;
    job_list->GetValue(nm, evt.job_id, COL_FILENAME);
    job_list->GetValue(hst, evt.job_id, COL_HOST);
    wxGetApp().notification_manager()->set_upload_job_notification_waittime(evt.job_id + 1, into_u8(nm.GetString()),
                                                                                 into_u8(hst.GetString()),
                                                                                  evt.waittime);
}
void PrintHostQueueDialog::on_error(Event &evt)
{
    wxCHECK_RET(evt.job_id < (size_t)job_list->GetItemCount(), "Out of bounds access to job list");

    set_state(evt.job_id, ST_ERROR);
    //y
    std::string response_msg = into_u8(evt.status);
    wxString code_msg     = "";
    if (response_msg.find("HTTP 404:") != std::string::npos) {
        size_t isAws = response_msg.find("AWS");
        if(isAws != std::string::npos)
            code_msg = _L("HTTP 404. Unable to get required resources from AWS server, please check your network settings.");
        else
            code_msg = _L("HTTP 404. Unable to get required resources from Aliyun server, please check your network settings.");
    } 
    else if (response_msg.find("HTTP 401:") != std::string::npos)
        code_msg = _L("HTTP 401: Unauthorized. Please check whether your physical printer has added users. If a user exists, add the "
                      "APIKEY when adding/editing the printer.");
    else
        code_msg = response_msg;
    
    auto errormsg = format_wxstr("%1%\n%2%", _L("Error uploading to print host") + ":", code_msg);
    job_list->SetValue(wxVariant(0), evt.job_id, COL_PROGRESS);
    job_list->SetValue(wxVariant(errormsg), evt.job_id, COL_ERRORMSG);    // Stashes the error message into a hidden column for later

    on_list_select();

    GUI::show_error(nullptr, errormsg);

    wxVariant nm, hst;
    job_list->GetValue(nm, evt.job_id, COL_FILENAME);
    job_list->GetValue(hst, evt.job_id, COL_HOST);
    wxGetApp().notification_manager()->upload_job_notification_show_error(evt.job_id + 1, into_u8(nm.GetString()), into_u8(hst.GetString()));
}

void PrintHostQueueDialog::on_cancel(Event &evt)
{
    wxCHECK_RET(evt.job_id < (size_t)job_list->GetItemCount(), "Out of bounds access to job list");

    set_state(evt.job_id, ST_CANCELLED);
    job_list->SetValue(wxVariant(0), evt.job_id, COL_PROGRESS);

    on_list_select();

    wxVariant nm, hst;
    job_list->GetValue(nm, evt.job_id, COL_FILENAME);
    job_list->GetValue(hst, evt.job_id, COL_HOST);
    wxGetApp().notification_manager()->upload_job_notification_show_canceled(evt.job_id + 1, into_u8(nm.GetString()), into_u8(hst.GetString()));
}

void PrintHostQueueDialog::on_info(Event& evt)
{
    wxCHECK_RET(evt.job_id < (size_t)job_list->GetItemCount(), "Out of bounds access to job list");
    
    if (evt.tag == L"resolve") {
        wxVariant hst(evt.status);
        job_list->SetValue(hst, evt.job_id, COL_HOST);
        wxGetApp().notification_manager()->set_upload_job_notification_host(evt.job_id + 1, into_u8(evt.status));
    } else if (evt.tag == L"complete") {
        wxVariant hst(evt.status);
        job_list->SetValue(hst, evt.job_id, COL_ERRORMSG);
        wxGetApp().notification_manager()->set_upload_job_notification_completed(evt.job_id + 1);
        wxGetApp().notification_manager()->set_upload_job_notification_status(evt.job_id + 1, into_u8(evt.status));
    } else if(evt.tag == L"complete_with_warning"){
        wxVariant hst(evt.status);
        job_list->SetValue(hst, evt.job_id, COL_ERRORMSG);
        wxGetApp().notification_manager()->set_upload_job_notification_completed_with_warning(evt.job_id + 1);
        wxGetApp().notification_manager()->set_upload_job_notification_status(evt.job_id + 1, into_u8(evt.status));
    } else if (evt.tag == L"qidiconnect_printer_address") {
        wxGetApp().notification_manager()->set_upload_job_notification_hypertext(evt.job_id + 1
            , [evt](wxEvtHandler *) {
                wxGetApp().mainframe->show_connect_tab(into_u8(evt.status));
                return false ;
            }
            );
    }
}

void PrintHostQueueDialog::get_active_jobs(std::vector<std::pair<std::string, std::string>>& ret)
{
    int ic = job_list->GetItemCount();
    for (int i = 0; i < ic; i++)
    {
        auto item = job_list->RowToItem(i);
        auto data = job_list->GetItemData(item);
        JobState st = static_cast<JobState>(data);
        if(st == JobState::ST_NEW || st == JobState::ST_PROGRESS)
            ret.emplace_back(upload_names[i]);       
    }
}
void PrintHostQueueDialog::save_user_data(int udt)
{
    const auto em = GetTextExtent("m").x;
    auto *app_config = wxGetApp().app_config;
    if (udt & UserDataType::UDT_SIZE) {
        
        app_config->set("print_host_queue_dialog_height", std::to_string(this->GetSize().x / em));
        app_config->set("print_host_queue_dialog_width", std::to_string(this->GetSize().y / em));
    }
    if (udt & UserDataType::UDT_POSITION)
    {
        app_config->set("print_host_queue_dialog_x", std::to_string(this->GetPosition().x));
        app_config->set("print_host_queue_dialog_y", std::to_string(this->GetPosition().y));
    }
    if (udt & UserDataType::UDT_COLS)
    {
        for (size_t i = 0; i < job_list->GetColumnCount() - 1; i++)
        {
            app_config->set("print_host_queue_dialog_column_" + std::to_string(i), std::to_string(job_list->GetColumn(i)->GetWidth()));
        }
    }    
}
bool PrintHostQueueDialog::load_user_data(int udt, std::vector<int>& vector)
{
    auto* app_config = wxGetApp().app_config;
    auto hasget = [app_config](const std::string& name, std::vector<int>& vector)->bool {
        if (app_config->has(name)) {
            std::string val = app_config->get(name);
            if (!val.empty() || val[0]!='\0') {
                vector.push_back(std::stoi(val));
                return true;
            }
        }
        return false;
    };
    if (udt & UserDataType::UDT_SIZE) {
        if (!hasget("print_host_queue_dialog_height",vector))
            return false;
        if (!hasget("print_host_queue_dialog_width", vector))
            return false;
    }
    if (udt & UserDataType::UDT_POSITION)
    {
        if (!hasget("print_host_queue_dialog_x", vector))
            return false;
        if (!hasget("print_host_queue_dialog_y", vector))
            return false;
    }
    if (udt & UserDataType::UDT_COLS)
    {
        for (size_t i = 0; i < 7; i++)
        {
            if (!hasget("print_host_queue_dialog_column_" + std::to_string(i), vector))
                return false;
        }
    }
    return true;
}
}}
