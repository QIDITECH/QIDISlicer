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

//B64
#include "../QIDI/QIDINetwork.hpp"
#include <boost/thread.hpp>
#include "./Widgets/SwitchButton.hpp"
#include "./Widgets/DeviceButton.hpp"
namespace Slic3r {
namespace GUI {





class PrinterWebView : public wxPanel {
public:
    PrinterWebView(wxWindow *parent);
    virtual ~PrinterWebView();

    wxBoxSizer *init_menu_bar(wxPanel *Panel);
    wxBoxSizer *init_login_bar(wxPanel *Panel);
    void        init_scroll_window(wxPanel *Panel);
    void        CreatThread();
    void load_url(wxString& url);
    void        load_net_url(std::string url, std::string ip);
    void UpdateState();
    void OnClose(wxCloseEvent& evt);

    void        OnZoomButtonClick(wxCommandEvent &event);
    void        OnRefreshButtonClick(wxCommandEvent &event);
    void OnAddButtonClick(wxCommandEvent &event);
    void OnDeleteButtonClick(wxCommandEvent &event);
    void OnEditButtonClick(wxCommandEvent &event);

    void        OnLoginButtonClick(wxCommandEvent &event);
    void RunScript(const wxString &javascript);
    //void OnScriptMessageReceived(wxWebViewEvent &event);
    void OnScriptMessage(wxWebViewEvent &evt);
    void UpdateLayout();
    void OnScroll(wxScrollWinEvent &event);
    void OnScrollup(wxScrollWinEvent &event);
    void OnScrolldown(wxScrollWinEvent &event);
    //void SendRecentList(int images);
    void SetButtons(std::vector<DeviceButton *> buttons);
    void  AddButton(const wxString &                           device_name,
                    const wxString &                           ip,
                    const wxString &                           machine_type,
                    const wxString &                           fullname,
                    bool                                       isSelected,
                   bool                isQIDI);
    void                        DeleteButton();
    void                        UnSelectedButton();
    void ShowNetPrinterButton();
    void ShowLocalPrinterButton();
#if QDT_RELEASE_TO_PUBLIC
    void AddNetButton(const Device device);
#endif

    void DeleteNetButton();
    void                        RefreshButton();
    void SetUpdateHandler(const std::function<void(wxCommandEvent &)> &handler) { m_handlerl = handler; }
    void StopStatusThread()
    {
        m_stopThread = true;
        if (m_statusThread.joinable()) {
            m_statusThread.join();
        }
    };
    void SetPauseThread(bool status) { m_pauseThread = status; };
    void SetPresetChanged(bool status);
    void SetLoginStatus(bool status);

    std::vector<DeviceButton *> GetButton() { return m_buttons; };
    bool                        GetNetMode() { return m_isNetMode; };
    std::vector<DeviceButton *> GetNetButton() { return m_net_buttons; };

private:
    wxBoxSizer *leftallsizer;

    wxBoxSizer *                          devicesizer;
    wxBoxSizer *                          allsizer;
    bool        m_isSimpleMode = false;
    bool                                  m_isNetMode    = false;
    wxButton *arrow_button;
    wxStaticText *    text_static;

    int height = 0; 
    wxString  m_web;
    std::function<void(wxCommandEvent &)> m_handlerl;
    std::function<void(wxCommandEvent &)> m_delete_handlerl;

    wxScrolledWindow *          leftScrolledWindow;
    wxPanel *         leftPanel;


    std::vector<DeviceButton *>           m_buttons;
    std::vector<DeviceButton *>           m_net_buttons;
    std::string                           m_select_type;
    std::thread                           m_statusThread;
    std::atomic<bool>                     m_stopThread{false};
    std::atomic<bool>                     m_pauseThread{true};

    wxWebView* m_browser;
    long m_zoomFactor;

    DeviceButton *                        add_button;
    DeviceButton *                        delete_button;
    DeviceButton *                        edit_button;
    DeviceButton *                        refresh_button;
    DeviceButton *                        login_button;
    bool                                  m_isloginin;
    SwitchButton *                        toggleBar;
    wxStaticBitmap *                      staticBitmap;
};



} // GUI
} // Slic3r

#endif /* slic3r_Tab_hpp_ */
