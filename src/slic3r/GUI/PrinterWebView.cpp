#include "PrinterWebView.hpp"

#include "I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r_version.h"

#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>

#include <slic3r/GUI/Widgets/WebView.hpp>

#include "PhysicalPrinterDialog.hpp"
//B45
#include <wx/regex.h>
#include <boost/regex.hpp>
#include <wx/graphics.h>
//B55
#include "../Utils/PrintHost.hpp"

namespace pt = boost::property_tree;

namespace Slic3r {
namespace GUI {
//B45
PrinterWebView::PrinterWebView(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    m_isSimpleMode = wxGetApp().app_config->get_bool("machine_list_minification");
    m_isNetMode    = wxGetApp().app_config->get_bool("machine_list_net");
    m_isloginin    = (wxGetApp().app_config->get("user_token") != "");

    if (m_isloginin) {
#if QDT_RELEASE_TO_PUBLIC
        wxString    msg;
        QIDINetwork m_qidinetwork;
        m_qidinetwork.get_device_list(msg);
#endif
    }

    m_select_type  = "null";

    allsizer = new wxBoxSizer(wxHORIZONTAL);
    devicesizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *buttonSizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *menuPanelSizer = new wxBoxSizer(wxVERTICAL);
    leftallsizer               = new wxBoxSizer(wxVERTICAL);

    devicesizer->SetMinSize(wxSize(300, -1));

    devicesizer->Layout();

    devicesizer->Add(0, 3);

    init_scroll_window(this);

    wxPanel *titlePanel = new wxPanel(this, wxID_ANY);
    titlePanel->SetBackgroundColour(wxColour(30, 30, 32));

    wxPanel *menuPanel = new wxPanel(titlePanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxTAB_TRAVERSAL);
    menuPanel->SetSizer(menuPanelSizer);
    menuPanel->SetBackgroundColour(wxColour(51, 51, 55));

    wxBoxSizer *loginsizer = init_login_bar(titlePanel);

    buttonSizer->Add(0, 10);
    buttonSizer->Add(loginsizer, wxSizerFlags().Border(wxALL, 1).Expand());
    buttonSizer->Add(0, 10);

    wxBoxSizer *menu_bar_sizer = new wxBoxSizer(wxHORIZONTAL);

    menu_bar_sizer = init_menu_bar(menuPanel);

    titlePanel->SetSizer(buttonSizer);

    toggleBar = new SwitchButton(menuPanel);
    toggleBar->SetSize(300);
    toggleBar->SetMaxSize({em_unit(this) * 40, -1});
    toggleBar->SetLabels(_L("Slicer"), _L("Link"));
    toggleBar->SetValue(m_isNetMode);
    toggleBar->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &evt) {
        bool is_checked = evt.GetInt();
        toggleBar->SetValue(is_checked);

        m_isNetMode = is_checked;
        if (!m_isNetMode) {
            wxGetApp().app_config->set("machine_list_net", "0");
            ShowLocalPrinterButton();
        } else {
            wxGetApp().app_config->set("machine_list_net", "1");
            ShowNetPrinterButton();
        }
    });

    menuPanelSizer->Add(toggleBar);

    menuPanelSizer->Add(menu_bar_sizer, wxSizerFlags(1).Expand().Align(wxALIGN_TOP).Border(wxALL, 0));
    menuPanelSizer->Add(0, 5);

    buttonSizer->Add(menuPanel, wxSizerFlags(1).Expand());

    titlePanel->Layout();


    m_browser = WebView::CreateWebView(this, "");
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }

    SetSizer(allsizer);

    leftallsizer->Add(titlePanel, wxSizerFlags(0).Expand());
    leftallsizer->Add(leftScrolledWindow, wxSizerFlags(1).Expand());

    allsizer->Add(leftallsizer, wxSizerFlags(0).Expand());
    allsizer->Add(m_browser, wxSizerFlags(1).Expand().Border(wxALL, 0));

    // Zoom
    m_zoomFactor = 100;

    // B45
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &PrinterWebView::OnScriptMessage, this);

    Bind(wxEVT_CLOSE_WINDOW, &PrinterWebView::OnClose, this);

    // B64
    if (m_isSimpleMode) {
        arrow_button->SetBitmap(*get_bmp_bundle("arrow-right-s-line", 20));
        devicesizer->SetMinSize(wxSize(190, -1));
        toggleBar->SetSize(190);
        leftScrolledWindow->SetMinSize(wxSize(190, -1));
        devicesizer->Layout();
        leftScrolledWindow->Layout();
        buttonSizer->Layout();
        allsizer->Layout();
    }
    // B64
    //SetPresetChanged(true);
    SetLoginStatus(m_isloginin);
    //CreatThread();
}
wxBoxSizer *PrinterWebView::init_login_bar(wxPanel *Panel)
{
    wxBoxSizer *buttonsizer = new wxBoxSizer(wxHORIZONTAL);

    if (m_isSimpleMode)
        staticBitmap = new wxStaticBitmap(Panel, wxID_ANY, ScalableBitmap(this, "user_dark", wxSize(40, 40)).get_bitmap());
    else
        staticBitmap = new wxStaticBitmap(Panel, wxID_ANY, ScalableBitmap(this, "user_dark", wxSize(60, 60)).get_bitmap());

    StateColor  text_color(std::pair<wxColour, int>(wxColour(57, 57, 61), StateColor::Disabled),
                          std::pair<wxColour, int>(wxColour(68, 121, 251), StateColor::Pressed),
                          std::pair<wxColour, int>(wxColour(68, 121, 251), StateColor::Hovered),
                          std::pair<wxColour, int>(wxColour(198, 198, 200), StateColor::Normal));

    StateColor btn_bg(std::pair<wxColour, int>(wxColour(30, 30, 32), StateColor::Disabled),
                      std::pair<wxColour, int>(wxColour(30, 30, 32), StateColor::Pressed),
                      std::pair<wxColour, int>(wxColour(30, 30, 32), StateColor::Hovered),
                      std::pair<wxColour, int>(wxColour(30, 30, 32), StateColor::Normal));

    login_button = new DeviceButton(Panel, "Login/Register", "", wxBU_LEFT);
    login_button->SetTextColor(text_color);
    login_button->SetBackgroundColor(btn_bg);
    login_button->SetBorderColor(btn_bg);
    login_button->SetCanFocus(false);
    login_button->SetIsSimpleMode(m_isSimpleMode);
    login_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnLoginButtonClick, this);
    buttonsizer->AddSpacer(5);
    buttonsizer->Add(staticBitmap);
    buttonsizer->Add(login_button, wxSizerFlags().Border(wxALL, 1).Expand());

    return buttonsizer;
}
wxBoxSizer *PrinterWebView::init_menu_bar(wxPanel *Panel)
{
    wxBoxSizer *buttonsizer = new wxBoxSizer(wxHORIZONTAL);

    StateColor add_btn_bg(std::pair<wxColour, int>(wxColour(57, 57, 61), StateColor::Disabled),
                          std::pair<wxColour, int>(wxColour(26, 26, 28), StateColor::Pressed),
                          std::pair<wxColour, int>(wxColour(26, 26, 28), StateColor::Hovered),
                          std::pair<wxColour, int>(wxColour(76, 76, 79), StateColor::Normal));

    // B63
    add_button = new DeviceButton(Panel, "", "add_machine_list_able", wxBU_LEFT);
    add_button->SetBackgroundColor(add_btn_bg);
    add_button->SetCanFocus(false);
    buttonsizer->Add(add_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 2));
    add_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnAddButtonClick, this);

    // B63
    delete_button = new DeviceButton(Panel, "", "delete_machine_list_able", wxBU_LEFT);
    delete_button->SetBackgroundColor(add_btn_bg);
    delete_button->SetCanFocus(false);
    buttonsizer->Add(delete_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 2));
    delete_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnDeleteButtonClick, this);

    // B63
    edit_button = new DeviceButton(Panel, "", "edit_machine_list_able", wxBU_LEFT);
    edit_button->SetBackgroundColor(add_btn_bg);
    edit_button->SetCanFocus(false);
    buttonsizer->Add(edit_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 2));
    edit_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnEditButtonClick, this);

    refresh_button = new DeviceButton(Panel, "", "refresh_machine_list_able", wxBU_LEFT);
    refresh_button->SetBackgroundColor(add_btn_bg);
    refresh_button->SetCanFocus(false);
    buttonsizer->Add(refresh_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 2));
    refresh_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnRefreshButtonClick, this);

    text_static = new wxStaticText(Panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize);
    text_static->SetForegroundColour(wxColour(255, 255, 255));
    text_static->SetFont(wxFont(wxFontInfo(18).Bold()));
    text_static->SetMinSize(wxSize(300 - 11 * add_button->GetSize().GetWidth(), -1));
    buttonsizer->Add(text_static, wxSizerFlags().Align(wxALIGN_LEFT).Border(wxALL, 5));

    if (m_isSimpleMode)
        text_static->Hide();

    arrow_button = new wxButton(Panel, wxID_ANY, "", wxDefaultPosition, wxSize(20, 20), wxBORDER_NONE);
    arrow_button->SetBackgroundColour(Panel->GetBackgroundColour());
    arrow_button->SetForegroundColour(Panel->GetBackgroundColour());
    arrow_button->SetMinSize(wxSize(40, -1));
    if (m_isSimpleMode)
        arrow_button->SetBitmap(*get_bmp_bundle("arrow-right-s-line", 20));
    else
        arrow_button->SetBitmap(*get_bmp_bundle("arrow-left-s-line", 20));
    buttonsizer->Add(arrow_button, wxSizerFlags().Align(wxALIGN_RIGHT).CenterVertical().Border(wxALL, 2));
    arrow_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnZoomButtonClick, this);

    buttonsizer->Layout();

    return buttonsizer;
}
void PrinterWebView::init_scroll_window(wxPanel* Panel) {
    leftScrolledWindow = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxVSCROLL);
    leftScrolledWindow->SetBackgroundColour(wxColour(30, 30, 32));
    leftScrolledWindow->SetSizer(devicesizer);
    leftScrolledWindow->SetScrollRate(10, 10);
    leftScrolledWindow->SetMinSize(wxSize(300, -1));
    leftScrolledWindow->FitInside();
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_TOP, &PrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_BOTTOM, &PrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_LINEUP, &PrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_LINEDOWN, &PrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_PAGEUP, &PrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_PAGEDOWN, &PrinterWebView::OnScroll, this);
}
void PrinterWebView::CreatThread() {
    std::thread myThread([&]() {
        while (true) {
            for (DeviceButton *button : m_buttons) {
                if (m_stopThread)
                    break;
                if (m_pauseThread)
                    break;


                BOOST_LOG_TRIVIAL(error) << "machine IP: " << (button->getIPLabel());
                std::unique_ptr<PrintHost> printhost(
                    PrintHost::get_print_host_url((button->getIPLabel()).ToStdString(), (button->getIPLabel()).ToStdString()));
                if (!printhost) {
                    BOOST_LOG_TRIVIAL(error) << ("Could not get a valid Printer Host reference");
                    return;
                }
                wxString    msg;
                std::string state    = "standby";
                float       progress = 0;
                state                = printhost->get_status(msg);

                if ((button->GetStateText()).ToStdString() != state)
                    button->SetStateText(state);

                if (state == "printing") {
                    progress        = (printhost->get_progress(msg)) * 100;
                    int progressInt = static_cast<int>(progress);
                    button->SetProgressText(wxString::Format(wxT("(%d%%)"), progressInt));
                }
            }
#if QDT_RELEASE_TO_PUBLIC
            auto m_devices = wxGetApp().get_devices();
            int  count     = 0;
            for (const auto &device : m_devices) {
                if (m_stopThread)
                    break;
                if (m_pauseThread)
                    break;
                if (!m_net_buttons.empty()) {
                    BOOST_LOG_TRIVIAL(error) << "machine IP: " << device.local_ip;
                    std::unique_ptr<PrintHost> printhost(PrintHost::get_print_host_url(device.url, device.local_ip));
                    if (!printhost) {
                        BOOST_LOG_TRIVIAL(error) << ("Could not get a valid Printer Host reference");
                        return;
                    }
                    wxString    msg;
                    std::string state    = "standby";
                    float       progress = 0;
                    state                = printhost->get_status(msg);

                    if ((m_net_buttons[count]->GetStateText()).ToStdString() != state)
                        m_net_buttons[count]->SetStateText(state);

                    if (state == "printing") {
                        progress        = (printhost->get_progress(msg)) * 100;
                        int progressInt = static_cast<int>(progress);
                        m_net_buttons[count]->SetProgressText(wxString::Format(wxT("(%d%%)"), progressInt));
                    }
                    count++;
                }
            }
#endif
            if (m_stopThread)
                break;
            boost::this_thread::sleep(boost::posix_time::seconds(1));
        }
    });
    m_statusThread = std::move(myThread);
}
void PrinterWebView::SetPresetChanged(bool status) {
    if (status) {
        StopStatusThread();
        m_stopThread = false;
        DeleteButton();
        DeleteNetButton();

        //ShowLocalPrinterButton();
        PresetBundle &preset_bundle = *wxGetApp().preset_bundle;

        PhysicalPrinterCollection &ph_printers = preset_bundle.physical_printers;
        for (PhysicalPrinterCollection::ConstIterator it = ph_printers.begin(); it != ph_printers.end(); ++it) {
            std::string host = (it->config.opt_string("print_host"));
            for (const std::string &preset_name : it->get_preset_names()) {
                Preset *    preset    = wxGetApp().preset_bundle->printers.find_preset(preset_name);
                std::string full_name = it->get_full_name(preset_name);
                std::string model_id;
                if (preset != nullptr) {
                    if ((preset->config.opt_string("printer_model").empty()))
                        model_id = "my_printer";
                    else
                        model_id = preset->config.opt_string("printer_model");
                }
                const DynamicPrintConfig *cfg_t = &(it->config);

                const auto        opt       = cfg_t->option<ConfigOptionEnum<PrintHostType>>("host_type");
                auto       host_type = opt != nullptr ? opt->value : htOctoPrint;

                 //BOOST_LOG_TRIVIAL(error) << preset_name;
                 //BOOST_LOG_TRIVIAL(error) << full_name;
                 //BOOST_LOG_TRIVIAL(error) << (it->get_short_name(full_name));
                 //BOOST_LOG_TRIVIAL(error) << (it->get_preset_name(full_name));
                 //BOOST_LOG_TRIVIAL(error) << model_id;
                 AddButton((it->get_short_name(full_name)), host, model_id, full_name, ph_printers.is_selected(it, preset_name),
                           (host_type == htMoonraker));
            }
        }
#if QDT_RELEASE_TO_PUBLIC
        auto m_devices = wxGetApp().get_devices();

        for (const auto &device : m_devices) {
            AddNetButton(device);
        }
#endif

        if (GetNetMode()) {
            ShowNetPrinterButton();
        } else {
            ShowLocalPrinterButton();
        }

        CreatThread();
    }
}
void PrinterWebView::SetLoginStatus(bool status) { 

    m_isloginin = status;
    if (m_isloginin) {
        /*login_button->SetLabel("123456");*/
#if QDT_RELEASE_TO_PUBLIC
        wxString    msg;
        QIDINetwork m_qidinetwork;
        std::string name = m_qidinetwork.user_info(msg);
        // y1
        wxString wxname = from_u8(name);
        login_button->SetLabel(wxname);
        std::vector<Device> m_devices = m_qidinetwork.get_device_list(msg);
        SetPresetChanged(true);
#endif
        m_isloginin = true;
        UpdateState();
    } else {
        login_button->SetLabel("Login/Register");
#if QDT_RELEASE_TO_PUBLIC
        std::vector<Device> devices;
        wxGetApp().set_devices(devices);
#endif

        SetPresetChanged(true);
        m_isNetMode = false;
        toggleBar->SetValue(m_isNetMode);
        ShowLocalPrinterButton();
        wxGetApp().app_config->set("machine_list_net", "0");
        UpdateState();
    }
}


PrinterWebView::~PrinterWebView()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Start";
    SetEvtHandlerEnabled(false);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " End";
}

// B55
void PrinterWebView::AddButton(const wxString &    device_name,
                               const wxString &    ip,
                               const wxString &    machine_type,
                               const wxString &    fullname,
                               bool                isSelected,
                               bool                isQIDI)
{
    wxString Machine_Name = Machine_Name.Format("%s%s", machine_type, "_thumbnail");

    StateColor mac_btn_bg(std::pair<wxColour, int>(wxColour(26, 26, 28), StateColor::Pressed),
                    std::pair<wxColour, int>(wxColour(26, 26, 28), StateColor::Hovered),
                    std::pair<wxColour, int>(wxColour(38, 38, 41), StateColor::Normal));

    DeviceButton *machine_button = new DeviceButton(leftScrolledWindow, fullname, Machine_Name, wxBU_LEFT, wxSize(80, 80), device_name, ip);
    machine_button->SetBackgroundColor(mac_btn_bg);
    machine_button->SetCanFocus(false);
    machine_button->SetIsSimpleMode(m_isSimpleMode);
    wxString formattedHost = ip;
    if (!formattedHost.Lower().starts_with("http"))
        formattedHost = wxString::Format("http://%s", formattedHost);
    if (isQIDI) {
        if (!formattedHost.Lower().ends_with("10088"))
            formattedHost = wxString::Format("%s:10088", formattedHost);
    }

    machine_button->Bind(wxEVT_BUTTON, [this, formattedHost](wxCommandEvent &event) { 
        wxString url = formattedHost;
        load_url(url);
    });
    devicesizer->Add(machine_button, wxSizerFlags().Border(wxALL, 1).Expand());
    devicesizer->Layout();
    m_buttons.push_back(machine_button);
}
#if QDT_RELEASE_TO_PUBLIC
void PrinterWebView::AddNetButton(const Device device)
{
    std::size_t found = device.device_name.find('@');
    wxString    Machine_Name;
    wxString    device_name;
    if (found != std::string::npos) {
        std::string extracted = device.device_name.substr(found + 1);
        Machine_Name          = Machine_Name.Format("%s%s", extracted, "_thumbnail");
        device_name           = device_name.Format("%s", device.device_name.substr(0, found));
    } else {
        Machine_Name = Machine_Name.Format("%s%s", "my_printer", "_thumbnail");
        device_name  = device_name.Format("%s", device.device_name);
    }
    StateColor     mac_btn_bg(std::pair<wxColour, int>(wxColour(26, 26, 28), StateColor::Pressed),
                          std::pair<wxColour, int>(wxColour(26, 26, 28), StateColor::Hovered),
                          std::pair<wxColour, int>(wxColour(38, 38, 41), StateColor::Normal));
    QIDINetwork m_qidinetwork;


    // device_name                  = m_qidinetwork.UTF8ToGBK(device_name.c_str());
    DeviceButton *machine_button = new DeviceButton(leftScrolledWindow, device.device_name, Machine_Name, wxBU_LEFT, wxSize(80, 80),

                                                    device_name, device.local_ip);
    machine_button->SetBackgroundColor(mac_btn_bg);
    machine_button->SetCanFocus(false);
    machine_button->SetIsSimpleMode(m_isSimpleMode);

    machine_button->Bind(wxEVT_BUTTON, [this, device](wxCommandEvent &event) {
        std::string formattedHost = "http://" + device.url;
        load_net_url(formattedHost, device.local_ip);
    });


    devicesizer->Add(machine_button, wxSizerFlags().Border(wxALL, 1).Expand());
    devicesizer->Layout();
    m_net_buttons.push_back(machine_button);
}
#endif

void PrinterWebView::RefreshButton()
{
    Refresh();
    if (m_buttons.empty()) {
        BOOST_LOG_TRIVIAL(info) << " empty";
    } else {
        for (DeviceButton *button : m_buttons) {
            button->Refresh();
        }
        add_button->Refresh();
        delete_button->Refresh();
        edit_button->Refresh();
        refresh_button->Refresh();
        login_button->Refresh();
    }
}
void PrinterWebView::UnSelectedButton()
{
    if (m_buttons.empty()) {
        BOOST_LOG_TRIVIAL(info) << " empty";
    } else {
        for (DeviceButton *button : m_buttons) {
            button->SetIsSelected(false);
        }
    }
}
void PrinterWebView::DeleteButton()
{
    if (m_buttons.empty()) {
        BOOST_LOG_TRIVIAL(info) << " empty";
    } else {
        for (DeviceButton *button : m_buttons) {
            delete button;
        }
        m_buttons.clear();
    }
}
void PrinterWebView::DeleteNetButton()
{
    if (m_net_buttons.empty()) {
        BOOST_LOG_TRIVIAL(info) << " empty";
    } else {
        for (DeviceButton *button : m_net_buttons) {
            delete button;
        }
        m_net_buttons.clear();
    }
}
void PrinterWebView::ShowNetPrinterButton()
{
    if (m_buttons.empty()) {
        BOOST_LOG_TRIVIAL(info) << " empty";
    } else {
        for (DeviceButton *button : m_buttons) {
            button->Hide();
        }
    }
    if (m_net_buttons.empty()) {
        BOOST_LOG_TRIVIAL(info) << " empty";
    } else {
        for (DeviceButton *button : m_net_buttons) {
            button->Show();
        }
    }
    leftScrolledWindow->Layout();
    Refresh();
}
void PrinterWebView::ShowLocalPrinterButton()
{
    if (m_net_buttons.empty()) {
        BOOST_LOG_TRIVIAL(info) << " empty";
    } else {
        for (DeviceButton *button : m_net_buttons) {
            button->Hide();
        }
    }
    if (m_buttons.empty()) {
        BOOST_LOG_TRIVIAL(info) << " empty";
    } else {
        for (DeviceButton *button : m_buttons) {
            button->Show();
        }
    }
    leftScrolledWindow->Layout();
    Refresh();
}
void PrinterWebView::SetButtons(std::vector<DeviceButton *> buttons) { m_buttons = buttons; }
void PrinterWebView::OnZoomButtonClick(wxCommandEvent &event)
{
    m_isSimpleMode = !m_isSimpleMode;
    if (!m_isSimpleMode) {
        text_static->Show();
        staticBitmap->SetBitmap(ScalableBitmap(this, "user_dark", wxSize(60, 60)).get_bitmap());
        login_button->SetIsSimpleMode(m_isSimpleMode);
        wxGetApp().app_config->set("machine_list_minification", "0");
        toggleBar->SetSize(300);
        devicesizer->SetMinSize(wxSize(300, -1));
        leftScrolledWindow->SetMinSize(wxSize(300, -1));
        arrow_button->SetBitmap(*get_bmp_bundle("arrow-left-s-line", 20));
        for (DeviceButton *button : m_buttons) {
            button->SetIsSimpleMode(m_isSimpleMode);
        }
        for (DeviceButton *button : m_net_buttons) {
            button->SetIsSimpleMode(m_isSimpleMode);
        }
    } else {
        text_static->Hide();
        staticBitmap->SetBitmap(ScalableBitmap(this, "user_dark", wxSize(40, 40)).get_bitmap());
        login_button->SetIsSimpleMode(m_isSimpleMode);

        wxGetApp().app_config->set("machine_list_minification", "1");
        toggleBar->SetSize(190);
        arrow_button->SetBitmap(*get_bmp_bundle("arrow-right-s-line", 20));
        devicesizer->SetMinSize(wxSize(190, -1));
        leftScrolledWindow->SetMinSize(wxSize(190, -1));
        for (DeviceButton *button : m_buttons) {
            button->SetIsSimpleMode(m_isSimpleMode);
        }
        for (DeviceButton *button : m_net_buttons) {
            button->SetIsSimpleMode(m_isSimpleMode);
        }
    }

    devicesizer->Layout();

    leftScrolledWindow->Layout();

    allsizer->Layout();
    UpdateLayout();
}
void PrinterWebView::OnRefreshButtonClick(wxCommandEvent &event)
{
    PresetBundle &preset_bundle = *wxGetApp().preset_bundle;

    PhysicalPrinterCollection &ph_printers = preset_bundle.physical_printers;

    std::vector<std::string> vec1;
    std::vector<std::string> vec2;
    for (PhysicalPrinterCollection::ConstIterator it = ph_printers.begin(); it != ph_printers.end(); ++it) {
        for (const std::string &preset_name : it->get_preset_names()) {
            std::string full_name = it->get_full_name(preset_name);
            vec1.push_back(full_name);
        }
    }

    for (DeviceButton *button : m_buttons) {
        vec2.push_back(button->GetLabel().ToStdString());
    }

    bool result1 = std::equal(vec1.begin(), vec1.end(), vec2.begin(), vec2.end());
    vec1.clear();
    vec2.clear();
    bool result2 = true;
    #if QDT_RELEASE_TO_PUBLIC
            wxString    msg;
            QIDINetwork m_qidinetwork;
            m_qidinetwork.get_device_list(msg);
            auto m_devices = wxGetApp().get_devices();
            for (const auto &device : m_devices) {
                vec1.push_back(device.device_name);
            }
            for (DeviceButton *button : m_net_buttons) {
                vec2.push_back(button->GetLabel().ToStdString());
            }
            result2 = std::equal(vec1.begin(), vec1.end(), vec2.begin(), vec2.end());
    #endif
    SetPresetChanged(!result1 || !result2);
    Refresh();
}
void PrinterWebView::OnLoginButtonClick(wxCommandEvent &event)
{
    // B64
    wxGetApp().ShowUserLogin(true);
    
//    if (m_isloginin) {
//        wxGetApp().app_config->set("user_token", "");
//        if (m_isNetMode) {
//            ShowLocalPrinterButton();
//            wxGetApp().app_config->set("machine_list_net", "0");
//        }
//        std::vector<Device> devices;
//        wxGetApp().set_devices(devices);
//        SetPresetChanged(true);
//        m_isloginin = false;
//        m_isNetMode = false;
//
//    } else {
//#if QDT_RELEASE_TO_PUBLIC
//        wxString    msg;
//        QIDINetwork m_qidinetwork;
//        m_qidinetwork.logn_in(msg);
//        std::vector<Device> m_devices = m_qidinetwork.get_device_list(msg);
//        SetPresetChanged(true);
//#endif
//        m_isloginin = true;
//    }
}
void PrinterWebView::OnAddButtonClick(wxCommandEvent &event)
{
    PhysicalPrinterDialog dlg(this->GetParent(), wxEmptyString);
    if (dlg.ShowModal() == wxID_OK) {
        if (m_handlerl) {
            m_handlerl(event);
        }
        // SetPresetChanged(true);
        UpdateLayout();
        Refresh();
    }
}
void PrinterWebView::OnDeleteButtonClick(wxCommandEvent &event)
{
    if (m_select_type == "local") {
        PresetBundle &preset_bundle = *wxGetApp().preset_bundle;
        for (DeviceButton *button : m_buttons) {
            if ((button->GetIsSelected())) {
                wxString msg;

#if defined(__WIN32__)
                msg += format_wxstr(_L("Are you sure you want to delete \"%1%\" printer?"), (button->GetLabel()));
#else
                msg += _L("Are you sure you want to delete ") + (button->GetLabel()) + _L("printer?");
#endif
                if (MessageDialog(this, msg, _L("Delete Physical Printer"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION).ShowModal() !=
                    wxID_YES)
                    return;
                // y1
                preset_bundle.physical_printers.select_printer(into_u8(button->GetLabel()));

                preset_bundle.physical_printers.delete_selected_printer();

                SetPresetChanged(true);

                wxString host = wxString::Format("file://%s/web/qidi/missing_connection.html", from_u8(resources_dir()));
                load_url(host);

                UpdateLayout();
                Refresh();
                break;
            }
        }
        if (m_handlerl) {
            m_handlerl(event);
        }
    } else if (m_select_type == "net") {
        for (DeviceButton *button : m_net_buttons) {
            if ((button->GetIsSelected())) {
                wxString msg;

#if defined(__WIN32__)
                msg += format_wxstr(_L("Are you sure you want to delete \"%1%\" printer?"), (button->GetLabel()));
#else
                msg += _L("Are you sure you want to delete ") + (button->GetLabel()) + _L("printer?");
#endif
                if (MessageDialog(this, msg, _L("Delete Physical Printer"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION).ShowModal() !=
                    wxID_YES)
                    return;
#if QDT_RELEASE_TO_PUBLIC

                auto devices = wxGetApp().get_devices();
                for (const auto &device : devices) {
                    if (device.local_ip == (button->getIPLabel())) {
                        wxString    msg;
                        QIDINetwork m_qidinetwork;
                        m_qidinetwork.unbind(msg, device.id);
                        m_qidinetwork.get_device_list(msg);
                    }
                }
#endif

                SetPresetChanged(true);
                wxString host = wxString::Format("file://%s/web/qidi/missing_connection.html", from_u8(resources_dir()));
                load_url(host);

                UpdateLayout();
                Refresh();
                break;
            }
        }
        if (m_handlerl) {
            m_handlerl(event);
        }
    }
}
void PrinterWebView::OnEditButtonClick(wxCommandEvent &event)
{
    for (DeviceButton *button : m_buttons) {
        if ((button->GetIsSelected())) {
            // y1
            PhysicalPrinterDialog dlg(this->GetParent(), button->GetLabel());
            if (dlg.ShowModal() == wxID_OK) {
                if (m_handlerl) {
                    m_handlerl(event);
                }
                // SetPresetChanged(true);
            }
            break;
        }
    }
}
// void PrinterWebView::SendRecentList(int images)
//{
//    boost::property_tree::wptree req;
//    boost::property_tree::wptree data;
//    //wxGetApp().mainframe->get_recent_projects(data, images);
//    req.put(L"sequence_id", "");
//    req.put(L"command", L"studio_set_mallurl");
//    //req.put_child(L"response", data);
//    std::wostringstream oss;
//    pt::write_json(oss, req, false);
//    RunScript(wxString::Format("window.postMessage(%s)", oss.str()));
//}
void PrinterWebView::OnScriptMessage(wxWebViewEvent &evt)
{
    wxLogMessage("Script message received; value = %s, handler = %s", evt.GetString(), evt.GetMessageHandler());
    // std::string response = wxGetApp().handle_web_request(evt.GetString().ToUTF8().data());
    // if (response.empty())
    //    return;
    // SendRecentList(1);
    ///* remove \n in response string */
    // response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());
    // if (!response.empty()) {
    //    m_response_js         = wxString::Format("window.postMessage('%s')", response);
    //    wxCommandEvent *event = new wxCommandEvent(EVT_RESPONSE_MESSAGE, this->GetId());
    //    wxQueueEvent(this, event);
    //} else {
    //    m_response_js.clear();
    //}
}
void PrinterWebView::UpdateLayout()
{
    wxSize size   = devicesizer->GetSize();
    int    height = size.GetHeight();
    int    Width  = size.GetWidth();
    leftScrolledWindow->SetVirtualSize(Width, height);
    devicesizer->Layout();

    leftScrolledWindow->Layout();

    leftScrolledWindow->FitInside();
    allsizer->Layout();
    if (!m_buttons.empty()) {
        for (DeviceButton *button : m_buttons) {
            button->Layout();
            button->Refresh();
        }
    }
    if (!m_net_buttons.empty()) {
        for (DeviceButton *button : m_net_buttons) {
            button->Layout();
            button->Refresh();
        }
    }
}
void PrinterWebView::OnScrollup(wxScrollWinEvent &event)
{
    height -= 5;
    leftScrolledWindow->Scroll(0, height);
    UpdateLayout();
    event.Skip();
}
void PrinterWebView::OnScrolldown(wxScrollWinEvent &event)
{
    height += 5;
    leftScrolledWindow->Scroll(0, height);
    UpdateLayout();
    event.Skip();
}
void PrinterWebView::OnScroll(wxScrollWinEvent &event)
{
    UpdateLayout();
    event.Skip();
}
void PrinterWebView::load_url(wxString &url)
{
    if (m_browser == nullptr || m_web == url)
        return;
    // m_web = url;
    m_browser->LoadURL(url);

    // B55
    if (url.Lower().starts_with("http"))
        url.Remove(0, 7);
    if (url.Lower().ends_with("10088"))
        url.Remove(url.length() - 6);

    for (DeviceButton *button : m_net_buttons) {
        button->SetIsSelected(false);
    }

    for (DeviceButton *button : m_buttons) {
        if (url == (button->getIPLabel()))
            button->SetIsSelected(true);
        else
            button->SetIsSelected(false);
    }
    UpdateState();
}
void PrinterWebView::load_net_url(std::string url, std::string ip)
{
    if (m_browser == nullptr || m_web == url)
        return;
    // m_web = url;
    m_browser->LoadURL(url);

    for (DeviceButton *button : m_buttons) {
        button->SetIsSelected(false);
    }

    for (DeviceButton *button : m_net_buttons) {
        if (ip == (button->getIPLabel()))
            button->SetIsSelected(true);
        else
            button->SetIsSelected(false);
    }
    UpdateState();
}
void PrinterWebView::UpdateState()
{
    m_select_type = "null";
    add_button->SetIcon("add_machine_list_able");
    add_button->Enable(true);
    add_button->Refresh();
    delete_button->SetIcon("delete_machine_list_disable");
    delete_button->Enable(false);
    delete_button->Refresh();
    edit_button->SetIcon("edit_machine_list_disable");
    edit_button->Enable(false);
    edit_button->Refresh();
    refresh_button->SetIcon("refresh_machine_list_disable");
    refresh_button->Enable(false);
    refresh_button->Refresh();
    login_button->Refresh();

    for (DeviceButton *button : m_buttons) {
        if (button->GetIsSelected()) {
            m_select_type = "local";
            add_button->SetIcon("add_machine_list_able");
            add_button->Enable(true);
            add_button->Refresh();
            delete_button->SetIcon("delete_machine_list_able");
            delete_button->Enable(true);
            delete_button->Refresh();
            edit_button->SetIcon("edit_machine_list_able");
            edit_button->Enable(true);
            edit_button->Refresh();
            refresh_button->SetIcon("refresh_machine_list_able");
            refresh_button->Enable(true);
            refresh_button->Refresh();
            login_button->Refresh();
        }
    }
    for (DeviceButton *button : m_net_buttons) {
        if (button->GetIsSelected()) {
            m_select_type = "net";
            add_button->SetIcon("add_machine_list_disable");
            add_button->Enable(false);
            add_button->Refresh();
            delete_button->SetIcon("delete_machine_list_able");
            delete_button->Enable(true);
            delete_button->Refresh();
            edit_button->SetIcon("edit_machine_list_disable");
            edit_button->Enable(false);
            edit_button->Refresh();
            refresh_button->SetIcon("refresh_machine_list_able");
            refresh_button->Enable(true);
            refresh_button->Refresh();
            login_button->Refresh();
        }
    }
}
void PrinterWebView::OnClose(wxCloseEvent &evt) { this->Hide(); }
void PrinterWebView::RunScript(const wxString &javascript)
{
    // Remember the script we run in any case, so the next time the user opens
    // the "Run Script" dialog box, it is shown there for convenient updating.

    WebView::RunScript(m_browser, javascript);
}
} // GUI
} // Slic3r
