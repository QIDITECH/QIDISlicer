#ifndef slic3r_PrinterWebView_hpp_
#define slic3r_PrinterWebView_hpp_


#include "wx/artprov.h"
#include "wx/cmdline.h"
#include "wx/notifmsg.h"
#include "wx/settings.h"
#include "wx/webview.h"

#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif

#include "wx/webviewarchivehandler.h"
#include "wx/webviewfshandler.h"
#include "wx/numdlg.h"
#include "wx/infobar.h"
#include "wx/filesys.h"
#include "wx/fs_arc.h"
#include "wx/fs_mem.h"
#include "wx/stdpaths.h"
#include <wx/panel.h>
#include <wx/tbarbase.h>
#include "wx/textctrl.h"
#include <wx/timer.h>

//B35
//B45
#if defined __linux__
#include <boost/log/trivial.hpp>
#include <wx/wx.h>
#include <thread>
#include <boost/format.hpp>
#endif

//B45
#include "PrintHostDialogs.hpp"
#include <wx/tokenzr.h>

namespace Slic3r {
namespace GUI {

class MachineListButton : public wxButton
{
public:
    MachineListButton(wxWindow *         parent,
             wxWindowID         id,
             const wxString &   label,
             const wxString &fullname,
             const wxPoint &    pos       = wxDefaultPosition,
             const wxSize &     size      = wxDefaultSize,
             long               style     = wxBORDER_DOUBLE,
             const wxValidator &validator = wxDefaultValidator,
             const wxString &   name      = wxButtonNameStr,
             bool isSelected = false)
        : wxButton(parent, id, label, pos, size, style, validator, name)
    {
        SetBackgroundColour(wxColour(30, 30, 32));
        full_label   = fullname;
        m_isSelected = isSelected;
        //if (isSelected)
        //    SetBackgroundColour(wxColour(100, 100, 105)); 
        //else
        //    SetBackgroundColour(wxColour(67, 67, 71)); 
        #if defined(__WIN32__) || defined(__WXMAC__)
            SetBackgroundColour(wxColour(30, 30, 32));
        #else
            if (isSelected)
                SetBackgroundColour(wxColour(100, 100, 105));
            else
                SetBackgroundColour(wxColour(67, 67, 71)); 
        #endif
    }

    void SetLabel(const wxString &fullname) { full_label = fullname; }


    wxString getLabel() { return full_label; }
    wxString getIPLabel() { return m_ip_text; }


    void SetBitMap(const wxBitmap &bitmap)
    {
        m_bitmap = bitmap;
        Refresh();
    }


    void SetNameText(const wxString &text)
    {
        m_name_text = text;
        Refresh();
    }

    wxString GetNameText()
    {
        return m_name_text;
    }


    void SetIPText(const wxString &text)
    {
        m_ip_text = text;
        Refresh();
    }
    void SetStateText(const wxString &text)
    {
        m_state_text = text;
        Refresh();
    }

    void SetProgressText(const wxString &text)
    {
        m_progress_text = text;
        Refresh();
    }
    void SetSelect(bool isselectd)
    {
        m_isSelected = isselectd;
        #if defined(__WIN32__) || defined(__WXMAC__)
            SetBackgroundColour(wxColour(30, 30, 32));
        #else
            if (m_isSelected)
                SetBackgroundColour(wxColour(100, 100, 105));
            else
                SetBackgroundColour(wxColour(67, 67, 71));
        #endif
        Refresh();
    }
    bool GetSelected() { return m_isSelected;}
    void SetSimpleMode(bool issimplemode)
    {
        m_isSimpleMode = issimplemode;
        Refresh();
    }

    void SetClickHandler(const std::function<void(wxMouseEvent &)> &handler) { m_handlerl = handler; }
    void PauseStatusThread() { m_pauseThread = true; }
    void ResumeStatusThread() { m_pauseThread = false; }
    void StopStatusThread()
    {
        m_stopThread = true;
        if (m_statusThread.joinable()) {
            m_statusThread.join();
        }
    }
    void        OnPaint(wxPaintEvent &event);
    void        OnSetFocus(wxFocusEvent &event);
    void        OnKillFocus(wxFocusEvent &event);
    void        OnKeyDown(wxKeyEvent &event);
    void        OnKeyUp(wxKeyEvent &event);
    void        OnMouseEnter(wxMouseEvent &event);
    void        OnMouseLeave(wxMouseEvent &event);
    void        OnMouseLeftDown(wxMouseEvent &event);
    //void        OnMouseLeftUp(wxMouseEvent &event);
    //void        OnClickHandler(wxCommandEvent &event);
    void SetStatusThread(std::thread thread) { m_statusThread = std::move(thread); }
    std::thread CreatThread(const wxString &buttonText, const wxString &ip, DynamicPrintConfig *cfg_t)
    {

        std::thread thread([this, buttonText,ip, cfg_t]() {
             std::unique_ptr<PrintHost> printhost(PrintHost::get_print_host(cfg_t));
             if (!printhost) {
                 BOOST_LOG_TRIVIAL(error) << ("Could not get a valid Printer Host reference");
                 return;
             }
            wxString msg;
            std::string state = "standby";
            float       progress      = 0;
            int         timeout_times = 0;
            while (true) {
                if (!m_pauseThread) {
                    state = printhost->get_status(msg);
                    if (state == "offline") {
                        BOOST_LOG_TRIVIAL(info) << boost::format("%1%Got state: %2%") % buttonText % state;
                        timeout_times += 1;
                        if (timeout_times>3)
                            m_pauseThread = true;
                    }
                    BOOST_LOG_TRIVIAL(info) << boost::format("%1%Got state: %2%") % buttonText % state;
                    if (m_state_text != state)
                        SetStateText(state);

                    if (state == "printing") {
                        timeout_times   = 0;
                        progress        = (printhost->get_progress(msg)) * 100;
                        int progressInt = static_cast<int>(progress);
                        SetProgressText(wxString::Format(wxT("(%d%%)"), progressInt));

                        BOOST_LOG_TRIVIAL(info) << boost::format("%1%Got progress: %2%") % buttonText % progress;
                    } else if (state == "standby")
                        timeout_times = 0;
                } 
                if (m_stopThread)
                    break;
            }
        });
        return thread;
    }

private:
    std::atomic<bool> m_stopThread{false};
    std::atomic<bool> m_pauseThread{false};

    bool m_isSimpleMode;
    bool m_isSelected = false;
    bool m_isHovered  = false;
    //bool m_isClicked  = false; 

    std::thread m_statusThread;
    //wxGraphicsContext *gc;
    wxPaintDC         * m_dc;

    wxBitmap m_bitmap;    
    wxString full_label;
    wxString m_name_text; 
    wxString m_ip_text; 
    wxString m_state_text; 
    wxString m_progress_text; 
    std::function<void(wxMouseEvent &)> m_handlerl;
    wxDECLARE_EVENT_TABLE();
};




class PrinterWebView : public wxPanel {
public:
    PrinterWebView(wxWindow *parent);
    virtual ~PrinterWebView();

    void load_url(wxString& url);
    void UpdateState();
    void OnClose(wxCloseEvent& evt);

    //B45
    void OnLeftButtonClick(wxCommandEvent &event);
    void OnRightButtonClick(wxCommandEvent &event);
    void OnCustomButtonClick(std::function<void(wxCommandEvent &)> m_handler, wxCommandEvent &event);
    void OnAddButtonClick(wxCommandEvent &event);
    void OnDeleteButtonClick(wxCommandEvent &event);
    void OnEditButtonClick(wxCommandEvent &event);

    void RunScript(const wxString &javascript);
    //void OnScriptMessageReceived(wxWebViewEvent &event);
    void OnScriptMessage(wxWebViewEvent &evt);
    void UpdateLayout();
    void OnScroll(wxScrollWinEvent &event);
    void OnScrollup(wxScrollWinEvent &event);
    void OnScrolldown(wxScrollWinEvent &event);

    void SetUpdateHandler(const std::function<void(wxCommandEvent &)> &handler) { m_handlerl = handler; }
    void SetDeleteHandler(const std::function<void(wxCommandEvent &)> &handler) { m_delete_handlerl = handler; }


    //B45
    //void SendRecentList(int images);
    void SetButtons(std::vector<MachineListButton *> buttons);
    void  AddButton(const wxString &                           device_name,
                    const wxString &                           ip,
                    const wxString &                           machine_type,
                    const wxString &                           fullname,
                    const std::function<void(wxMouseEvent &)> &handler,
                    bool                                       isSelected,
                    DynamicPrintConfig *                       cfg_t);
    void                        DeleteButton();
    void                        PauseButton();
    void                        ResumeButton();
    void                        StopAllThread();
    void                        UnSelectedButton();

    std::vector<MachineListButton *>     GetButton() { return m_buttons; };

private:
    //B45
    wxBoxSizer *leftallsizer;

    wxBoxSizer *leftsizer;
    wxBoxSizer *topsizer;
    bool        m_isSimpleMode = false;
    wxButton *arrow_button;
    wxStaticText *    text_static;

    int height = 0; 
    wxString  m_web;
    std::function<void(wxCommandEvent &)> m_handlerl;
    std::function<void(wxCommandEvent &)> m_delete_handlerl;

    wxScrolledWindow *          leftScrolledWindow;
    wxPanel *         leftPanel;


    std::vector<MachineListButton *> m_buttons;

    wxWebView* m_browser;
    long m_zoomFactor;

    // DECLARE_EVENT_TABLE()
};



} // GUI
} // Slic3r

#endif /* slic3r_Tab_hpp_ */
