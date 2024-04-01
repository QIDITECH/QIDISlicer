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


//wxBEGIN_EVENT_TABLE(MachineListButton, wxButton) EVT_PAINT(MachineListButton::OnPaint) EVT_ENTER_WINDOW(MachineListButton::OnMouseEnter)
//    EVT_LEAVE_WINDOW(MachineListButton::OnMouseLeave)
//    // EVT_LEFT_DOWN(MachineListButton::OnMouseLeftDown)
//    //    EVT_LEFT_UP(MachineListButton::OnMouseLeftUp)
//    EVT_SET_FOCUS(MachineListButton::OnSetFocus) EVT_KILL_FOCUS(MachineListButton::OnKillFocus) EVT_KEY_DOWN(MachineListButton::OnKeyDown)
//        EVT_KEY_UP(MachineListButton::OnKeyUp) wxEND_EVENT_TABLE()

 //B45

void MachineListButton::OnPaint(wxPaintEvent &event)
{
    wxPaintDC dc(this);
    wxRect rect = GetClientRect();
    if (m_isHovered || m_isSelected)
        dc.SetBrush(wxBrush(wxColour(100, 100, 105)));
    else
        dc.SetBrush(wxBrush(wxColour(67, 67, 71)));
    if (full_label == "") {
        dc.DrawRoundedRectangle(rect, 5);

        int imgWidth  = m_bitmap.GetWidth();
        int imgHeight = m_bitmap.GetHeight();
        int x         = (rect.GetWidth() - imgWidth) / 2;
        int y         = (rect.GetHeight() - imgHeight) / 2;
        dc.DrawBitmap(m_bitmap, x, y);
    }
    else if (m_isSimpleMode) {
        dc.DrawRoundedRectangle(rect, 8);

        dc.SetFont(wxFont(15, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        dc.SetTextForeground(wxColour(230, 230, 230));
        dc.DrawText(m_name_text, 10, 10);
    } else {
        dc.DrawRoundedRectangle(rect, 8);
        dc.DrawBitmap(m_bitmap, 10, (GetSize().GetHeight() - m_bitmap.GetHeight()) / 2, true);

        dc.SetFont(wxFont(15, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        dc.SetTextForeground(wxColour(230, 230, 230));
        dc.DrawText(m_name_text, 10 + m_bitmap.GetWidth() + 10, 10);
        dc.SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        dc.SetTextForeground(wxColour(174, 174, 174));

        dc.DrawText("IP:" + m_ip_text, 10 + m_bitmap.GetWidth() + 10, 40);

        wxBitmap m_bitmap_state = get_bmp_bundle("printer_state", 20)->GetBitmapFor(this);
        dc.DrawBitmap(m_bitmap_state, 10 + m_bitmap.GetWidth() + 10, 55, true);

        dc.SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        dc.SetTextForeground(wxColour(174, 174, 174));

        dc.DrawText(m_state_text, 10 + m_bitmap.GetWidth() + m_bitmap_state.GetWidth() + 15, 60);
        if (m_state_text == "printing") {
            dc.SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
            dc.SetTextForeground(wxColour(33, 148, 239));
            dc.DrawText(m_progress_text, 10 + m_bitmap.GetWidth() + m_bitmap_state.GetWidth() + 77, 62);
        }
    }
}



void MachineListButton::OnSetFocus(wxFocusEvent &event)
{
    event.Skip();
    Refresh();
}

void MachineListButton::OnKillFocus(wxFocusEvent &event)
{
    event.Skip();
    Refresh();
}

void MachineListButton::OnKeyDown(wxKeyEvent &event)
{
    event.Skip();
    Refresh();
}

void MachineListButton::OnKeyUp(wxKeyEvent &event)
{
    event.Skip();
    Refresh();
}

void MachineListButton::OnMouseEnter(wxMouseEvent &event)
{
    #if defined(__WIN32__) || defined(__WXMAC__)
        m_isHovered = true;
    #else
        SetBackgroundColour(wxColour(100, 100, 105));
    #endif
    Refresh();
    Update();
}

void MachineListButton::OnMouseLeave(wxMouseEvent &event)
{
    #if defined(__WIN32__) || defined(__WXMAC__)
        m_isHovered = false;
    #else
        if (m_isSelected)
            SetBackgroundColour(wxColour(100, 100, 105));
        else
            SetBackgroundColour(wxColour(67, 67, 71)); 
    #endif
    Refresh();
    Update();
}

//void MachineListButton::OnMouseLeftDown(wxMouseEvent &event)
//{
//    Refresh();
//}
//
//void MachineListButton::OnMouseLeftUp(wxMouseEvent &event)
//{
//    if (m_handlerl) {
//        m_handlerl(event);
//    }
//    Refresh();
//}


//B45
PrinterWebView::PrinterWebView(wxWindow *parent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
 {

    #if defined(__WIN32__) || defined(__WXMAC__)
         int leftsizerWidth = 300;
    #else
         int leftsizerWidth = 210;
    #endif
    topsizer  = new wxBoxSizer(wxHORIZONTAL); 

    leftScrolledWindow = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxVSCROLL);
    //leftScrolledWindow->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_NEVER);
    leftScrolledWindow->SetBackgroundColour(wxColour(30, 30, 32));
    leftsizer = new wxBoxSizer(wxVERTICAL);
    wxFont font(wxFontInfo().Bold());

    wxPanel *buttonPanel = new wxPanel(this, wxID_ANY);
    buttonPanel->SetBackgroundColour(wxColour(30, 30, 32));
    wxBoxSizer *buttonSizer = new wxBoxSizer(wxVERTICAL);
    leftallsizer            = new wxBoxSizer(wxVERTICAL);


    wxBoxSizer *titlesizer = new wxBoxSizer(wxHORIZONTAL); 
    text_static            = new wxStaticText(buttonPanel, wxID_ANY, "MACHINE LIST", wxDefaultPosition, wxDefaultSize);
    text_static->SetForegroundColour(wxColour(255, 255, 255));
    text_static->SetFont(wxFont(wxFontInfo(18).Bold()));
    #if defined __linux__
        text_static->SetMinSize(wxSize(200, 40));
        text_static->SetFont(wxFont(wxFontInfo(12).Bold()));
    #endif

    titlesizer->Add(text_static, wxSizerFlags().Align(wxALIGN_LEFT).Border(wxALL, 5));
    titlesizer->AddStretchSpacer();
    wxBoxSizer *buttonsizer = new wxBoxSizer(wxHORIZONTAL); 

    m_isSimpleMode = wxGetApp().app_config->get_bool("machine_list_minification");


    buttonPanel->SetSizer(buttonSizer);

    //#if defined(__WIN32__) || defined(__WXMAC__)
        //MachineListButton *add_button = new MachineListButton(buttonPanel, wxID_ANY, "", "", wxDefaultPosition, wxDefaultSize, wxBU_LEFT,
        //                                                                wxDefaultValidator, wxButtonNameStr);
        //wxButton *add_button = new wxButton(buttonPanel, wxID_ANY, "", wxDefaultPosition, wxSize(20, 20), wxBORDER_NONE);
    //B63
    add_button = new MachineListButton(buttonPanel, wxID_ANY, "", "", wxDefaultPosition, wxDefaultSize, wxBU_LEFT,
                                                            wxDefaultValidator, wxButtonNameStr);
    #if defined(__WIN32__) || defined(__WXMAC__)
        add_button->SetBackgroundColour(wxColour(30, 30, 21));
        add_button->SetBitMap(get_bmp_bundle("add_machine_list", 20)->GetBitmapFor(this));
    #else
        add_button->SetBackgroundColour(wxColour(67, 67, 71)); 
    #endif
    add_button->SetMinSize(wxSize(40, -1));
    add_button->SetBitmap(*get_bmp_bundle("add_machine_list", 20));
    buttonsizer->Add(add_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 2));
    add_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnAddButtonClick, this);


    //wxButton *delete_button = new wxButton(buttonPanel, wxID_ANY, "", wxDefaultPosition, wxSize(20, 20), wxBORDER_NONE);
    //B63
    delete_button = new MachineListButton(buttonPanel, wxID_ANY, "", "", wxDefaultPosition, wxDefaultSize, wxBU_LEFT,
                                                                wxDefaultValidator, wxButtonNameStr);
    #if defined(__WIN32__) || defined(__WXMAC__)
        delete_button->SetBackgroundColour(wxColour(30, 30, 21));
        delete_button->SetBitMap(get_bmp_bundle("delete_machine_list", 20)->GetBitmapFor(this));
    #else
        delete_button->SetBackgroundColour(wxColour(67, 67, 71)); 
    #endif
    delete_button->SetMinSize(wxSize(40, -1));
    delete_button->SetBitmap(*get_bmp_bundle("delete_machine_list", 20));
    buttonsizer->Add(delete_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 2));
    delete_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnDeleteButtonClick, this);

    //B63
    edit_button = new MachineListButton(buttonPanel, wxID_ANY, "", "", wxDefaultPosition, wxDefaultSize, wxBU_LEFT,
                                                wxDefaultValidator, wxButtonNameStr);
    //wxButton *edit_button = new wxButton(buttonPanel, wxID_ANY, "", wxDefaultPosition, wxSize(20, 20), wxBORDER_NONE);
    #if defined(__WIN32__) || defined(__WXMAC__)
        edit_button->SetBackgroundColour(wxColour(30, 30, 21));
        edit_button->SetBitMap(get_bmp_bundle("edit_machine_list", 20)->GetBitmapFor(this));
    #else
        edit_button->SetBackgroundColour(wxColour(67, 67, 71)); 
    #endif
    edit_button->SetMinSize(wxSize(40, -1));
    edit_button->SetBitmap(*get_bmp_bundle("edit_machine_list", 20));
    buttonsizer->Add(edit_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 2));
    edit_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnEditButtonClick, this);

    refresh_button = new MachineListButton(buttonPanel, wxID_ANY, "", "", wxDefaultPosition, wxDefaultSize,
                                                                wxBU_LEFT, wxDefaultValidator, wxButtonNameStr);
    //wxButton *refresh_button = new wxButton(buttonPanel, wxID_ANY, "", wxDefaultPosition, wxSize(20, 20), wxBORDER_NONE);
    #if defined(__WIN32__) || defined(__WXMAC__)
        refresh_button->SetBackgroundColour(wxColour(30, 30, 21));
    #else
        refresh_button->SetBackgroundColour(wxColour(67, 67, 71)); 
    #endif
    refresh_button->SetBitMap(get_bmp_bundle("refresh-line", 20)->GetBitmapFor(this));
    refresh_button->SetMinSize(wxSize(40, -1));
    refresh_button->SetBitmap(*get_bmp_bundle("refresh-line", 20));
    buttonsizer->Add(refresh_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 2));
    refresh_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnRightButtonClick, this);
    #if defined(__WIN32__) || defined(__WXMAC__)

        arrow_button = new wxButton(buttonPanel, wxID_ANY, "", wxDefaultPosition, wxSize(20, 20), wxBORDER_NONE);
        arrow_button->SetFont(font);
        arrow_button->SetBackgroundColour(buttonPanel->GetBackgroundColour());
        arrow_button->SetForegroundColour(buttonPanel->GetBackgroundColour());
        arrow_button->SetMinSize(wxSize(40, -1));
        if (m_isSimpleMode)
            arrow_button->SetBitmap(*get_bmp_bundle("arrow-right-s-line", 20));
        else
            arrow_button->SetBitmap(*get_bmp_bundle("arrow-left-s-line", 20));
        titlesizer->Add(arrow_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 2));
        arrow_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnLeftButtonClick, this);
    #endif

    titlesizer->Layout();
    buttonsizer->Layout();

    buttonSizer->Add(titlesizer, wxSizerFlags(0).Expand().Align(wxALIGN_TOP).Border(wxALL, 0));
    buttonSizer->Add(buttonsizer, wxSizerFlags(1).Expand().Align(wxALIGN_TOP).Border(wxALL, 0));
    buttonPanel->Layout();

    leftsizer->SetMinSize(wxSize(300, -1));


    leftsizer->Layout();
    leftScrolledWindow->SetSizer(leftsizer);
    leftScrolledWindow->SetScrollRate(10, 10);
    leftScrolledWindow->SetMinSize(wxSize(leftsizerWidth, -1));
    leftScrolledWindow->FitInside();

    m_browser = WebView::CreateWebView(this, "");
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }

    SetSizer(topsizer);

    leftallsizer->Add(buttonPanel, wxSizerFlags(0).Expand());
    leftallsizer->Add(leftScrolledWindow, wxSizerFlags(1).Expand());

    topsizer->Add(leftallsizer, wxSizerFlags(0).Expand());
    topsizer->Add(m_browser, wxSizerFlags(1).Expand().Border(wxALL, 0));

    // Zoom
    m_zoomFactor = 100;

    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_TOP, &PrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_BOTTOM, &PrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_LINEUP, &PrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_LINEDOWN, &PrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_PAGEUP, &PrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_PAGEDOWN, &PrinterWebView::OnScroll, this);

    //B45
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &PrinterWebView::OnScriptMessage, this);


    //Connect the idle events
    Bind(wxEVT_CLOSE_WINDOW, &PrinterWebView::OnClose, this);

    if (m_isSimpleMode) {
        arrow_button->SetBitmap(*get_bmp_bundle("arrow-right-s-line", 20));
        leftsizer->SetMinSize(wxSize(190, -1));
        leftScrolledWindow->SetMinSize(wxSize(190, -1));
        text_static->SetFont(wxFont(wxFontInfo(12).Bold()));
        leftsizer->Layout();

        leftScrolledWindow->Layout();
        buttonSizer->Layout();

        topsizer->Layout();
    }

 }






 //B55
void PrinterWebView::AddButton(const wxString &                             device_name,
                                const wxString &                            ip,
                                const wxString &                            machine_type,
                                const wxString &                            fullname,
                                bool                                         isSelected,
                                bool                isQIDI,
                                DynamicPrintConfig *                         cfg_t)
 {
    wxString Machine_Name = Machine_Name.Format("%s%s", machine_type, "_thumbnail");

    MachineListButton *customButton = new MachineListButton(leftScrolledWindow, wxID_ANY, device_name,
                                                            fullname,
                                                            wxDefaultPosition,
                                                            wxDefaultSize, wxBU_LEFT,
                                          wxDefaultValidator, wxButtonNameStr, isSelected);
    //customButton->SetMinSize(wxSize(80, -1));
    #if defined(__WIN32__) || defined(__WXMAC__)
        customButton->SetBitmap(*get_bmp_bundle(std::string(Machine_Name.mb_str()), 80));
    #endif
    customButton->SetBitMap(get_bmp_bundle(std::string(Machine_Name.mb_str()), 80)->GetBitmapFor(this));
    customButton->SetForegroundColour(wxColour(255, 255, 255));
    customButton->SetNameText(device_name);
    customButton->SetIPText(ip);
    customButton->SetStateText("standby");
    customButton->SetProgressText("(0%)");

    customButton->SetIsQIDI(isQIDI);
    customButton->Bind(wxEVT_BUTTON, [this, ip, customButton, isQIDI](wxCommandEvent &event) { 
        //B55
        wxString   formattedHost = ip;
        if (!formattedHost.Lower().starts_with("http"))
            formattedHost = wxString::Format("http://%s", formattedHost);
            if (isQIDI) {
            if (!formattedHost.Lower().ends_with("10088"))
                formattedHost = wxString::Format("%s:10088", formattedHost);
        }
        load_url(formattedHost);
        customButton->ResumeStatusThread();
    });
    #if defined(__WIN32__) || defined(__WXMAC__)
        customButton->SetStatusThread(std::move(customButton->CreatThread(device_name,ip, cfg_t)));
        if (m_isSimpleMode) {
            customButton->SetBitmap(*get_bmp_bundle(std::string("X-MAX 3_thumbnail"), 30));
            customButton->SetSimpleMode(m_isSimpleMode);
            customButton->SetSize(wxSize(180, -1));
        } else {
            customButton->SetBitmap(*get_bmp_bundle(std::string("X-MAX 3_thumbnail"), 80));
            customButton->SetSimpleMode(m_isSimpleMode);
            customButton->SetSize(wxSize(300, -1));
        }
    #else
        customButton->SetSize(wxSize(200, -1));
    #endif
    //B63
    customButton->Bind(wxEVT_KEY_UP, &PrinterWebView::OnKeyUp, this);

    leftsizer->Add(customButton, wxSizerFlags().Border(wxALL, 1).Expand());
    leftsizer->Layout();
    m_buttons.push_back(customButton);
 }
//B63
 void PrinterWebView::RefreshButton()
 {
     if (m_buttons.empty()) {
         BOOST_LOG_TRIVIAL(info) << " empty";
     } else {
         for (MachineListButton *button : m_buttons) {
             button->Refresh();
         }
         add_button->Refresh();
         delete_button->Refresh();
         edit_button->Refresh();
         refresh_button->Refresh();
     }
 }

 //B45
 void PrinterWebView::PauseButton()
 {

     if (m_buttons.empty()) {
         BOOST_LOG_TRIVIAL(info) << " empty";
     } else {
         for (MachineListButton *button : m_buttons) {
             button->PauseStatusThread();
         }
     }
 }
 //B45
 void PrinterWebView::ResumeButton()
 {

     if (m_buttons.empty()) {
         BOOST_LOG_TRIVIAL(info) << " empty";
     } else {
         for (MachineListButton *button : m_buttons) {
             button->ResumeStatusThread();
         }
     }
 }

  //B45
 void PrinterWebView::StopAllThread()
 {

     if (m_buttons.empty()) {
         BOOST_LOG_TRIVIAL(info) << " empty";
     } else {
         for (MachineListButton *button : m_buttons) {
             button->StopStatusThread();
         }
     }
 }

  //B45
 void PrinterWebView::UnSelectedButton()
 {

     if (m_buttons.empty()) {
         BOOST_LOG_TRIVIAL(info) << " empty";
     } else {
         for (MachineListButton *button : m_buttons) {
             button->SetSelect(false);
         }
     }
 }



 //B45
 void PrinterWebView::DeleteButton()
{
     if (m_buttons.empty()) {
        BOOST_LOG_TRIVIAL(info) <<" empty";
     } else {
         for (MachineListButton *button : m_buttons) {

             button->StopStatusThread();

             delete button;
         }
         m_buttons.clear();
     }
}


  //B45
void PrinterWebView::SetButtons(std::vector<MachineListButton *> buttons) { m_buttons = buttons; }

 PrinterWebView::~PrinterWebView()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Start";
    SetEvtHandlerEnabled(false);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " End";
}
//B45
void PrinterWebView::OnLeftButtonClick(wxCommandEvent &event)
{
    m_isSimpleMode = !m_isSimpleMode;

    if (!m_isSimpleMode) {
        wxGetApp().app_config->set("machine_list_minification","0");

        leftsizer->SetMinSize(wxSize(300, -1));
        leftScrolledWindow->SetMinSize(wxSize(300, -1));
        arrow_button->SetBitmap(*get_bmp_bundle("arrow-left-s-line", 20));
        text_static->SetFont(wxFont(wxFontInfo(18).Bold()));
        for (MachineListButton *button : m_buttons) {
            button->SetBitmap(*get_bmp_bundle(std::string("X-MAX 3_thumbnail"), 80));
            button->SetSimpleMode(m_isSimpleMode);
            button->SetSize(wxSize(300, -1));

        }
    }
        else {
        wxGetApp().app_config->set("machine_list_minification", "1");

        arrow_button->SetBitmap(*get_bmp_bundle("arrow-right-s-line", 20));
        leftsizer->SetMinSize(wxSize(190, -1));
        leftScrolledWindow->SetMinSize(wxSize(190, -1));
        text_static->SetFont(wxFont(wxFontInfo(12).Bold()));
        for (MachineListButton *button : m_buttons) {
            button->SetBitmap(*get_bmp_bundle(std::string("X-MAX 3_thumbnail"), 30));
            button->SetSimpleMode(m_isSimpleMode);
            button->SetSize(wxSize(180, -1));
        }
    }

    leftsizer->Layout();


    leftScrolledWindow->Layout();

    topsizer->Layout();
    UpdateLayout();
}
//B45
void PrinterWebView::OnRightButtonClick(wxCommandEvent &event)
{
    for (MachineListButton *button : m_buttons) {
        button->ResumeStatusThread();
    }
}


 void PrinterWebView::OnCustomButtonClick(std::function<void(wxCommandEvent &)> handler, wxCommandEvent &event)
{
     if (handler) {
        handler(event);
    }
    Refresh();
}

//B55
void PrinterWebView::OnAddButtonClick(wxCommandEvent &event)
{
    PhysicalPrinterDialog dlg(this->GetParent(), wxEmptyString);
    if (dlg.ShowModal() == wxID_OK) {
        if (m_handlerl) {
            m_handlerl(event);
        }
        PresetBundle &preset_bundle = *wxGetApp().preset_bundle;
        auto          printer       = preset_bundle.physical_printers.get_selected_printer();
        std::string   printer_name  = printer.name;
        wxString      host          = printer.config.opt_string("print_host");


        std::string fullname    = preset_bundle.physical_printers.get_selected_full_printer_name();
        std::string preset_name = printer.get_preset_name(fullname);
        Preset *    preset      = wxGetApp().preset_bundle->printers.find_preset(preset_name);
        std::string model_id    = "X-MAX 3";
        if (preset != nullptr) {
            if ((preset->config.opt_string("printer_model").empty()))
                model_id = "my_printer";
            else
                model_id = preset->config.opt_string("printer_model");
        }

        //boost::regex        ipRegex(R"(\b(?:\d{1,3}\.){3}\d{1,3}\b)");
        //bool                isValidIPAddress = boost::regex_match(host.ToStdString(), ipRegex);
        bool                isValidIPAddress = true;
        DynamicPrintConfig *cfg_t            = &(printer.config);

        const auto          opt              = cfg_t->option<ConfigOptionEnum<PrintHostType>>("host_type");
        const auto          host_type        = opt != nullptr ? opt->value : htOctoPrint;
        wxString            formattedHost    = host;
        if (!formattedHost.Lower().starts_with("http"))
            formattedHost = wxString::Format("http://%s", formattedHost);
        if (host_type == htMoonraker) {
            if (!formattedHost.Lower().ends_with("10088"))
                formattedHost = wxString::Format("%s:10088", formattedHost);
        }
        UnSelectedButton();
        //B60
        if (isValidIPAddress)
            AddButton(
                printer_name, host,model_id, wxString::FromUTF8(fullname), true, (host_type == htMoonraker), cfg_t);
        load_url(formattedHost);
        UpdateLayout();
        Refresh();
    }
}

void PrinterWebView::OnDeleteButtonClick(wxCommandEvent &event) { 

    PresetBundle &preset_bundle = *wxGetApp().preset_bundle;
    for (MachineListButton *button : m_buttons) {
        if ((button->GetSelected())) {

            wxString msg;
            //if (!note_string.IsEmpty())
            //    msg += note_string + "\n";
            
            #if defined(__WIN32__)
                msg += format_wxstr(_L("Are you sure you want to delete \"%1%\" printer?"), (button->getLabel()));
            #else
                msg += _L("Are you sure you want to delete ") + (button->getLabel()) + _L("printer?");
            #endif
            if (MessageDialog(this, msg, _L("Delete Physical Printer"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION).ShowModal() != wxID_YES)
                    return;

            button->StopStatusThread();
            preset_bundle.physical_printers.select_printer((button->getLabel()).ToStdString());


            preset_bundle.physical_printers.delete_selected_printer();

            auto it = std::find(m_buttons.begin(), m_buttons.end(), button);
            delete button;

            if (it != m_buttons.end()) {
                m_buttons.erase(it);
            }

            leftsizer->Detach(button);
            if (!m_buttons.empty())
                for (MachineListButton *button : m_buttons) {
                    button->SetSelect(true);
                    wxString formattedHost = wxString::Format("http://%s:10088", button->getIPLabel());

                    load_url(formattedHost);
                    preset_bundle.physical_printers.select_printer((button->getLabel()).ToStdString());
                    break;
                }
            else {
                wxString host = wxString::Format("file://%s/web/qidi/missing_connection.html", from_u8(resources_dir()));
                load_url(host);
            }

            UpdateLayout();
            Refresh();
            break;
        }
    }
    if (m_handlerl) {
        m_handlerl(event);
    }
}

void PrinterWebView::OnEditButtonClick(wxCommandEvent &event) { 
    for (MachineListButton *button : m_buttons) {
        if ((button->GetSelected())) {
            PhysicalPrinterDialog dlg(this->GetParent(), (wxString::FromUTF8((button->getLabel()).ToStdString())));
            //BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << (button->getLabel());

            //BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << wxString::FromUTF8((button->getLabel()).ToStdString());

            if (dlg.ShowModal() == wxID_OK) {
                if (m_handlerl) {
                    m_handlerl(event);
                }
                PresetBundle &preset_bundle = *wxGetApp().preset_bundle;
                auto          printer       = preset_bundle.physical_printers.get_selected_printer();
                std::string   printer_name  = printer.name;
                wxString      host          = printer.config.opt_string("print_host");
                std::string   fullname      = preset_bundle.physical_printers.get_selected_full_printer_name();
                std::string   preset_name   = printer.get_preset_name(fullname);
                Preset *      preset        = wxGetApp().preset_bundle->printers.find_preset(preset_name);
                std::string   model_id      = "X-MAX 3";
                if (preset != nullptr) {
                    model_id = preset->config.opt_string("printer_model");
                }
                DynamicPrintConfig *cfg_t            = &(printer.config);



                button->SetNameText((wxString::FromUTF8(printer_name)));
                button->SetIPText(host);
                button->SetLabel(fullname);
                //B59
                const auto opt = cfg_t->option<ConfigOptionEnum<PrintHostType>>("host_type");
                const auto host_type = opt != nullptr ? opt->value : htOctoPrint;
                bool       isQIDI    = (host_type == htMoonraker);
                wxString   formattedHost = host;
                if (!formattedHost.Lower().starts_with("http"))
                    formattedHost = wxString::Format("http://%s", formattedHost);
                if (isQIDI) {
                    if (!formattedHost.Lower().ends_with("10088"))
                        formattedHost = wxString::Format("%s:10088", formattedHost);
                }
                button->Bind(wxEVT_BUTTON, [this, host, button, isQIDI](wxCommandEvent &event) {
                //B55
                    wxString formattedHost = host;
                    if (!formattedHost.Lower().starts_with("http"))
                        formattedHost = wxString::Format("http://%s", formattedHost);
                    if (isQIDI) {
                        if (!formattedHost.Lower().ends_with("10088"))
                            formattedHost = wxString::Format("%s:10088", formattedHost);
                    }
                    load_url(formattedHost);
                    button->ResumeStatusThread();
                });
                wxString Machine_Name = Machine_Name.Format("%s%s", model_id, "_thumbnail");

                button->SetBitMap(get_bmp_bundle(std::string(Machine_Name.mb_str()), 80)->GetBitmapFor(this));
                //B59
                load_url(formattedHost);
                UpdateLayout();
                Refresh();
            }
            break;
        }
    }
}



//void PrinterWebView::SendRecentList(int images)
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
    //std::string response = wxGetApp().handle_web_request(evt.GetString().ToUTF8().data());
    //if (response.empty())
    //    return;
    //SendRecentList(1);
    ///* remove \n in response string */
    //response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());
    //if (!response.empty()) {
    //    m_response_js         = wxString::Format("window.postMessage('%s')", response);
    //    wxCommandEvent *event = new wxCommandEvent(EVT_RESPONSE_MESSAGE, this->GetId());
    //    wxQueueEvent(this, event);
    //} else {
    //    m_response_js.clear();
    //}
}


void PrinterWebView::UpdateLayout()
{
    wxSize size   = leftsizer->GetSize();
    int    height = size.GetHeight();
    int    Width  = size.GetWidth();
    leftScrolledWindow->SetVirtualSize(Width, height);
    leftsizer->Layout();

    leftScrolledWindow->Layout();

    leftScrolledWindow->FitInside();
    topsizer->Layout();
    if (!m_buttons.empty()) {
        for (MachineListButton *button : m_buttons) {
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

//B63
void PrinterWebView::OnKeyUp(wxKeyEvent &event)
{
    
    event.Skip();
    RefreshButton();
}
//B45
void PrinterWebView::load_url(wxString& url)
{
    if (m_browser == nullptr || m_web == url)
        return;
    //m_web = url;
    m_browser->LoadURL(url);

    //B55
    if (url.Lower().starts_with("http"))
    url.Remove(0, 7);
    if (url.Lower().ends_with("10088"))
        url.Remove(url.length() - 6);
    for (MachineListButton *button : m_buttons) {
        if (url == (button->getIPLabel()))
            button->SetSelect(true);
        else
            button->SetSelect(false);
    }
    UpdateState();
}
/**
 * Method that retrieves the current state from the web control and updates the
 * GUI the reflect this current state.
 */
void PrinterWebView::UpdateState() {
  // SetTitle(m_browser->GetCurrentTitle());

}

void PrinterWebView::OnClose(wxCloseEvent& evt)
{
    this->Hide();
}

void PrinterWebView::RunScript(const wxString &javascript)
{
    // Remember the script we run in any case, so the next time the user opens
    // the "Run Script" dialog box, it is shown there for convenient updating.


    WebView::RunScript(m_browser, javascript);
}
} // GUI
} // Slic3r
