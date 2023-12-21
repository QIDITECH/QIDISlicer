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

namespace pt = boost::property_tree;

namespace Slic3r {
namespace GUI {


wxBEGIN_EVENT_TABLE(MachineListButton, wxButton) EVT_PAINT(MachineListButton::OnPaint) EVT_ENTER_WINDOW(MachineListButton::OnMouseEnter)
    EVT_LEAVE_WINDOW(MachineListButton::OnMouseLeave) EVT_LEFT_DOWN(MachineListButton::OnMouseLeftDown) EVT_LEFT_UP(MachineListButton::OnMouseLeftUp)
        wxEND_EVENT_TABLE()


void MachineListButton::OnPaint(wxPaintEvent &event)
{
    
    wxPaintDC dc(this);
    //m_bitmap = get_bmp_bundle("X-MAX 3_thumbnail", 80)->GetBitmapFor(this);
    //w13
    wxRect rect = GetClientRect();
    dc.SetPen(wxPen(wxColour(30, 30, 32)));
    if (mouseOnButton) {
        //dc.SetPen(wxPen(wxColour(110, 110, 110)));

        dc.SetBrush(wxBrush(wxColour(100, 100, 105)));

        dc.DrawRoundedRectangle(rect, 5);
    } else {
        //dc.SetPen(wxPen(wxColour(110, 110, 110)));

        dc.SetBrush(wxBrush(wxColour(67, 67, 71)));

        dc.DrawRoundedRectangle(rect, 5);
    }
    if (m_isSelected) {
        //dc.SetPen(wxPen(wxColour(110, 110, 110)));

        dc.SetBrush(wxBrush(wxColour(100, 100, 105)));

        dc.DrawRoundedRectangle(rect, 5);
    }
    if (mousePressed) {
        dc.SetPen(wxPen(wxColour(110, 110, 110)));

        dc.SetBrush(wxBrush(wxColour(109, 109, 113)));

        dc.DrawRoundedRectangle(rect, 5);
    }
    if (m_isSimpleMode) {
        dc.SetFont(wxFont(15, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        dc.SetTextForeground(wxColour(230, 230, 230));
        dc.DrawText(m_name_text, 10 , 10);
    } else {
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

void MachineListButton::OnMouseEnter(wxMouseEvent &event)
{
    //w13
    mouseOnButton = true;
    //SetBackgroundColour(wxColour(100, 100, 105));
    Refresh();
    Update();
}

void MachineListButton::OnMouseLeave(wxMouseEvent &event)
{
    //w13
    mouseOnButton = false;
    /* if (m_isSelected)
        SetBackgroundColour(wxColour(100, 100, 105));
    else
        SetBackgroundColour(wxColour(100, 100, 105)); */
    Refresh();
    Update();
}

void MachineListButton::OnMouseLeftDown(wxMouseEvent &event)
{
    //w13
    mousePressed = true;
    SetBackgroundColour(wxColour(30, 30, 32));
    Refresh();
}

void MachineListButton::OnMouseLeftUp(wxMouseEvent &event)
{
    //w13
    mousePressed = false;
    //SetBackgroundColour(wxColour(100, 100, 105));
    if (m_handlerl) {
        m_handlerl(event);
    }
    Refresh();
}


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
    leftScrolledWindow = new wxScrolledWindow(this, wxID_ANY);
    leftScrolledWindow->SetBackgroundColour(wxColour(30, 30, 32));
    leftsizer = new wxBoxSizer(wxVERTICAL);
    wxFont font(wxFontInfo().Bold());

    wxBoxSizer *titlesizer = new wxBoxSizer(wxHORIZONTAL); 
    text_static            = new wxStaticText(leftScrolledWindow, wxID_ANY, "MACHINE LIST", wxDefaultPosition, wxDefaultSize);
    text_static->SetForegroundColour(wxColour(255, 255, 255));
    text_static->SetFont(wxFont(wxFontInfo(18).Bold()));
    #if defined __linux__
        text_static->SetMinSize(wxSize(200, 40));
        text_static->SetFont(wxFont(wxFontInfo(12).Bold()));
    #endif

    titlesizer->Add(text_static, wxSizerFlags().Align(wxALIGN_LEFT).Border(wxALL, 5));
    titlesizer->AddStretchSpacer();
    wxBoxSizer *buttonsizer = new wxBoxSizer(wxHORIZONTAL); 

    //wxBU_EXACTFIT wxBORDER_NONE
    //w13
    #if defined(__WIN32__) || defined(__WXMAC__)
        MyRoundButton *add_button = new MyRoundButton(leftScrolledWindow, wxID_ANY, "", "add_machine_list", wxDefaultPosition, wxSize(20, 20), wxBORDER_NONE);
        add_button->SetBackgroundColour(wxColour(30, 30, 32));
        //add_button->SetForegroundColour(leftScrolledWindow->GetBackgroundColour());

        add_button->SetMinSize(wxSize(40, -1));
        //add_button->SetBitmap(*get_bmp_bundle("add_machine_list", 20));
        buttonsizer->Add(add_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 2));
        add_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnAddButtonClick, this);


        MyRoundButton *delete_button = new MyRoundButton(leftScrolledWindow, wxID_ANY, "", "delete_machine_list", wxDefaultPosition, wxSize(20, 20), wxBORDER_NONE);
        //wxButton *delete_button = new wxButton(leftScrolledWindow, wxID_ANY, "", wxDefaultPosition, wxSize(20, 20), wxBORDER_NONE);
        delete_button->SetBackgroundColour(wxColour(30, 30, 32));
        //delete_button->SetForegroundColour(leftScrolledWindow->GetBackgroundColour());

        delete_button->SetMinSize(wxSize(40, -1));
        //delete_button->SetBitmap(*get_bmp_bundle("delete_machine_list", 20));
        buttonsizer->Add(delete_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 2));
        delete_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnDeleteButtonClick, this);

        MyRoundButton *edit_button = new MyRoundButton(leftScrolledWindow, wxID_ANY, "", "edit_machine_list", wxDefaultPosition, wxSize(20, 20), wxBORDER_NONE);
        //wxButton *edit_button = new wxButton(leftScrolledWindow, wxID_ANY, "", wxDefaultPosition, wxSize(20, 20), wxBORDER_NONE);
        edit_button->SetBackgroundColour(wxColour(30, 30, 32));
        //edit_button->SetForegroundColour(leftScrolledWindow->GetBackgroundColour());

        edit_button->SetMinSize(wxSize(40, -1));
        //edit_button->SetBitmap(*get_bmp_bundle("edit_machine_list", 20));
        buttonsizer->Add(edit_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 2));
        edit_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnEditButtonClick, this);


        MyRoundButton *refresh_button = new MyRoundButton(leftScrolledWindow, wxID_ANY, "", "refresh-line", wxDefaultPosition, wxSize(20, 20), wxBORDER_NONE);
        //wxButton *refresh_button = new wxButton(leftScrolledWindow, wxID_ANY, "", wxDefaultPosition, wxSize(20, 20), wxBORDER_NONE);
        refresh_button->SetBackgroundColour(wxColour(30, 30, 32));
        //refresh_button->SetForegroundColour(leftScrolledWindow->GetBackgroundColour());

        refresh_button->SetMinSize(wxSize(40, -1));
        //refresh_button->SetBitmap(*get_bmp_bundle("refresh-line", 20));
        buttonsizer->Add(refresh_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 2));
        refresh_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnRightButtonClick, this);

        arrow_button = new wxButton(leftScrolledWindow, wxID_ANY, "", wxDefaultPosition, wxSize(20, 20), wxBORDER_NONE);
        arrow_button->SetFont(font);
        arrow_button->SetBackgroundColour(leftScrolledWindow->GetBackgroundColour());
        arrow_button->SetForegroundColour(leftScrolledWindow->GetBackgroundColour());
        arrow_button->SetMinSize(wxSize(40, -1));
        arrow_button->SetBitmap(*get_bmp_bundle("arrow-left-s-line", 20));
        titlesizer->Add(arrow_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 2));
        arrow_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnLeftButtonClick, this);
    #endif

    titlesizer->Layout();
    buttonsizer->Layout();

    leftsizer->Add(titlesizer, wxSizerFlags().Expand().Align(wxALIGN_TOP).Border(wxALL, 0));
    leftsizer->Add(buttonsizer, wxSizerFlags().Expand().Align(wxALIGN_TOP).Border(wxALL, 0));

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

    topsizer->Add(leftScrolledWindow, wxSizerFlags(0).Expand());
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

 }






void PrinterWebView::AddButton(const wxString &                             device_name,
                                const wxString &                            ip,
                                const wxString &                            machine_type,
                                const wxString &                            fullname,
                                const std::function<void(wxMouseEvent &)> &handler,
                                bool                                         isSelected,
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
    //customButton->SetMinSize(wxSize(200, -1));
    customButton->SetClickHandler(handler);
    #if defined(__WIN32__) || defined(__WXMAC__)
        customButton->SetStatusThread(std::move(customButton->CreatThread(device_name,ip, cfg_t)));
    #else
        customButton->SetSize(wxSize(200, -1));
    #endif
    customButton->SetSimpleMode(false);

    leftsizer->Add(customButton, wxSizerFlags().Border(wxALL, 1).Expand());
    leftsizer->Layout();
    m_buttons.push_back(customButton);
 }

 //B45
 void PrinterWebView::PauseButton()
 {
     //BOOST_LOG_TRIVIAL(error) << " Pause";

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
     //BOOST_LOG_TRIVIAL(error) << " Resume";

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
     // BOOST_LOG_TRIVIAL(error) << " Stop";

     if (m_buttons.empty()) {
         BOOST_LOG_TRIVIAL(info) << " empty";
     } else {
         for (MachineListButton *button : m_buttons) {
             button->StopStatusThread();
         }
     }
 }

  // B45
 void PrinterWebView::UnSelectedButton()
 {
     // BOOST_LOG_TRIVIAL(error) << " Resume";

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


  // B45
void PrinterWebView::SetButtons(std::vector<MachineListButton *> buttons) { m_buttons = buttons; }

 PrinterWebView::~PrinterWebView()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Start";
    SetEvtHandlerEnabled(false);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " End";
}

void PrinterWebView::OnLeftButtonClick(wxCommandEvent &event)
{
    m_isSimpleMode = !m_isSimpleMode;

    if (!m_isSimpleMode) {
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
        arrow_button->SetBitmap(*get_bmp_bundle("arrow-right-s-line", 20));
        leftsizer->SetMinSize(wxSize(210, -1));
        leftScrolledWindow->SetMinSize(wxSize(210, -1));
        text_static->SetFont(wxFont(wxFontInfo(12).Bold()));
        for (MachineListButton *button : m_buttons) {
            button->SetBitmap(*get_bmp_bundle(std::string("X-MAX 3_thumbnail"), 30));
            button->SetSimpleMode(m_isSimpleMode);
            button->SetSize(wxSize(200, -1));
        }
    }

    leftsizer->Layout();


    leftScrolledWindow->Layout();

    topsizer->Layout();
    //UpdateLayout();
}

void PrinterWebView::OnRightButtonClick(wxCommandEvent &event)
{
    for (MachineListButton *button : m_buttons) {
        button->ResumeStatusThread();
    }
    //w13
    Refresh();
}

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

        wxString formattedHost = host;
        if (!formattedHost.Lower().starts_with("http"))
            formattedHost = wxString::Format("http://%s", formattedHost);
        if (!formattedHost.Lower().ends_with("10088"))
            formattedHost = wxString::Format("%s:10088", formattedHost);

        std::string   fullname      = preset_bundle.physical_printers.get_selected_full_printer_name();
        std::string preset_name = printer.get_preset_name(fullname);
        Preset *    preset      = wxGetApp().preset_bundle->printers.find_preset(preset_name);
        std::string model_id    = "X-MAX 3";
        if (preset != nullptr) {
            model_id = preset->config.opt_string("printer_model");
        }




        boost::regex        ipRegex(R"(\b(?:\d{1,3}\.){3}\d{1,3}\b)");
        bool                isValidIPAddress = boost::regex_match(host.ToStdString(), ipRegex);
        DynamicPrintConfig *cfg_t = &(printer.config);

        UnSelectedButton();
        if (isValidIPAddress)
            AddButton(
                printer_name, host, model_id, fullname,
                [formattedHost, this](wxMouseEvent &event) {
                    wxString host = formattedHost;
                    load_url(host);
                },
                true, cfg_t);
        load_url(formattedHost);
        UpdateLayout();
        //w13
        //Refresh();
    }
    Refresh();
}

void PrinterWebView::OnDeleteButtonClick(wxCommandEvent &event) { 

    PresetBundle &preset_bundle = *wxGetApp().preset_bundle;
    for (MachineListButton *button : m_buttons) {
        if ((button->GetSelected())) {

            wxString msg;
            //if (!note_string.IsEmpty())
            //    msg += note_string + "\n";
            
            #if defined(__WIN32__) || defined(__WXMAC__)
                msg += format_wxstr(_L("Are you sure you want to delete \"%1%\" printer?"), (button->getLabel()));
            #else
                msg += _L("Are you sure you want to delete ") + (button->getLabel()) + _L("printer?");
            #endif
                //w13
                if (MessageDialog(this, msg, _L("Delete Physical Printer"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION).ShowModal() !=
                    wxID_YES) {
                    Refresh();
                    return;
                }

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
            //Refresh();
            break;
        }
    }
    if (m_handlerl) {
        m_handlerl(event);
    }
    Refresh();
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
                wxString Machine_Name = Machine_Name.Format("%s%s", model_id, "_thumbnail");

                button->SetBitMap(get_bmp_bundle(std::string(Machine_Name.mb_str()), 80)->GetBitmapFor(this));
                UpdateLayout();
                //w13
                //Refresh();
            }
            break;
        }
    }
    Refresh();
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
    //leftScrolledWindow->SetVirtualSize(leftsizer->GetMinSize());
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


void PrinterWebView::OnScroll(wxScrollWinEvent &event)
{
    UpdateLayout();
    event.Skip();
}

//B45
void PrinterWebView::load_url(wxString& url)
{
//    this->Show();
//    this->Raise();
    if (m_browser == nullptr)
        return;
    m_browser->LoadURL(url);

    url.Remove(0, 7);
    url.Remove(url.length() - 6);
    for (MachineListButton *button : m_buttons) {

        if (url == (button->getIPLabel()))
            button->SetSelect(true);
        else
            button->SetSelect(false);
    }


    //const char *data = "Hello from C++!";
    //std::string switch_dark_mode_script = "SwitchDarkMode(";
    //switch_dark_mode_script += wxGetApp().app_config->get("dark_color_mode") == "1" ? "true" : "false";
    //switch_dark_mode_script += ");";
    //RunScript("var valueFromCpp = '" + std::string(data) + "';");
    //m_browser->RunScript(switch_dark_mode_script);

    //m_browser->SetFocus();
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
//w13
void MyRoundButton::OnPaint(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    wxRect rect = GetClientRect();
    dc.SetPen(wxPen(wxColour(110, 110, 110)));
    dc.SetBrush(wxBrush(wxColour(85, 85, 90)));
    dc.DrawRoundedRectangle(rect, 5);
    wxBitmap m_bitmap_state = get_bmp_bundle(m_name.ToStdString(), 20)->GetBitmapFor(this);
    int imgWidth = m_bitmap_state.GetWidth();
    int imgHeight = m_bitmap_state.GetHeight();
    int x = (rect.GetWidth() - imgWidth) / 2;
    int y = (rect.GetHeight() - imgHeight) / 2;
    dc.DrawBitmap(m_bitmap_state, x, y);
}
void MyRoundButton::OnFocusEvent(wxFocusEvent &evt)
{
    Refresh();
    evt.Skip();
}
} // GUI
} // Slic3r
