#ifndef slic3r_PrintHostSendDialog_hpp_
#define slic3r_PrintHostSendDialog_hpp_

#include <set>
#include <string>
#include <boost/filesystem/path.hpp>

#include <wx/string.h>
#include <wx/event.h>
#include <wx/dialog.h>

#include "GUI_Utils.hpp"
#include "MsgDialog.hpp"
#include "../Utils/PrintHost.hpp"

//B61
#include "Plater.hpp"
#include "libslic3r/Print.hpp"
#include <wx/wx.h>

class wxButton;
class wxTextCtrl;
class wxChoice;
class wxComboBox;
class wxDataViewListCtrl;
class wxCheckBox;

namespace Slic3r {

namespace GUI {
//y4
class SendCheckBox : public wxCheckBox
{
public:
    SendCheckBox(wxWindow *parent, wxWindowID id, const wxString &label)
        : wxCheckBox(parent, id, label)
    {}

    void SetState(bool value)
    {
        wxCheckBox::SetValue(value);
        wxCommandEvent event(wxEVT_CHECKBOX, GetId());
        event.SetEventObject(this);
        event.SetInt(value);
        GetEventHandler()->ProcessEvent(event);
    }
};



//B53 //B62
struct PhysicalPrinterPresetData
{
    wxString lower_name; // just for sorting
    wxString name;       // preset_name
    wxString fullname;   // full name
    bool     selected;   // is selected
    std::string preset_name;
    wxString    host;
    DynamicPrintConfig *cfg_t;
};
class PrintHostSendDialog : public GUI::MsgDialog
{
public:
    //B61
    PrintHostSendDialog(const boost::filesystem::path &path, PrintHostPostUploadActions post_actions, const wxArrayString& groups, const wxArrayString& storage_paths, const wxArrayString& storage_names, Plater* plater, const PrintStatistics& ps, bool onlyLik);
    boost::filesystem::path filename() const;
    PrintHostPostUploadAction post_action() const;
    std::string group() const;
    std::string storage() const;
    //B53
    std::vector<PhysicalPrinterPresetData> pppd() { return m_presetData; }
    std::vector<bool>                      checkbox_states() { return m_checkbox_states; }

    //B64
    std::vector<bool> checkbox_net_states() { return m_checkbox_net_states; }
    virtual void EndModal(int ret) override;
    //B64
    wxBoxSizer *create_item_input(
        wxString str_before, wxString str_after, wxWindow *parent, wxString tooltip, std::string param);
    //y4
    std::string NormalizeVendor(const std::string &str);
    void        OnCheckBoxClicked(wxCommandEvent &event);

private:
    wxTextCtrl *txt_filename;
    wxComboBox *combo_groups;
    wxComboBox* combo_storage;
    PrintHostPostUploadAction post_upload_action;
    wxString    m_valid_suffix;
    wxString    m_preselected_storage;
    wxArrayString m_paths;
    //B53
    std::vector<PhysicalPrinterPresetData> m_presetData;
    std::vector<bool>                      m_checkbox_states;
    //B64
    std::vector<bool> m_checkbox_net_states;
    //B61
    Plater *m_plater{nullptr};
    //y4
    std::vector<SendCheckBox*> unSelectedBoxes;
    std::vector<SendCheckBox*> SelectedBoxes;

    //y16
    wxCheckBox* m_switch_to_device{ nullptr };
};


class PrintHostQueueDialog : public DPIDialog
{
public:
    class Event : public wxEvent
    {
    public:
        size_t job_id;
        int progress = 0;    // in percent
        wxString tag;
        wxString status;
        //B64
        int waittime = 0;

        Event(wxEventType eventType, int winid, size_t job_id);
        Event(wxEventType eventType, int winid, size_t job_id, int progress);
        Event(wxEventType eventType, int winid, size_t job_id, wxString error);
        Event(wxEventType eventType, int winid, size_t job_id, wxString tag, wxString status);
        //B64
        Event(wxEventType eventType, int winid, size_t job_id, int waittime,int progress);

        virtual wxEvent *Clone() const;
    };


    PrintHostQueueDialog(wxWindow *parent);

    void append_job(const PrintHostJob &job);
    void get_active_jobs(std::vector<std::pair<std::string, std::string>>& ret);

    virtual bool Show(bool show = true) override
    {
        if(!show)
            save_user_data(UDT_SIZE | UDT_POSITION | UDT_COLS);
        return DPIDialog::Show(show);
    }
protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void on_sys_color_changed() override;

private:
    enum Column {
        COL_ID,
        COL_PROGRESS,
        COL_STATUS,
        COL_HOST,
        COL_SIZE,
        COL_FILENAME,
        COL_ERRORMSG
    };

    enum JobState {
        ST_NEW,
        ST_PROGRESS,
        ST_ERROR,
        ST_CANCELLING,
        ST_CANCELLED,
        ST_COMPLETED,
    };

    enum { HEIGHT = 60, WIDTH = 30, SPACING = 5 };

    enum UserDataType{
        UDT_SIZE = 1,
        UDT_POSITION = 2,
        UDT_COLS = 4
    };

    wxButton *btn_cancel;
    wxButton *btn_error;
    wxDataViewListCtrl *job_list;
    // Note: EventGuard prevents delivery of progress evts to a freed PrintHostQueueDialog
    //B64
    EventGuard on_wait_evt;
    EventGuard on_progress_evt;
    EventGuard on_error_evt;
    EventGuard on_cancel_evt;
    EventGuard on_info_evt;

    JobState get_state(int idx);
    void set_state(int idx, JobState);
    void on_list_select();
    void on_progress(Event&);
    //B64
    void on_wait(Event &);
    void on_error(Event&);
    void on_cancel(Event&);
    void on_info(Event&);
    // This vector keep adress and filename of uploads. It is used when checking for running uploads during exit.
    std::vector<std::pair<std::string, std::string>> upload_names;
    void save_user_data(int);
    bool load_user_data(int, std::vector<int>&);
};

//B64
wxDECLARE_EVENT(EVT_PRINTHOST_WAIT, PrintHostQueueDialog::Event);
wxDECLARE_EVENT(EVT_PRINTHOST_PROGRESS, PrintHostQueueDialog::Event);
wxDECLARE_EVENT(EVT_PRINTHOST_ERROR, PrintHostQueueDialog::Event);
wxDECLARE_EVENT(EVT_PRINTHOST_CANCEL, PrintHostQueueDialog::Event);
wxDECLARE_EVENT(EVT_PRINTHOST_INFO, PrintHostQueueDialog::Event);
}}

#endif
