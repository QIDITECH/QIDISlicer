#ifndef slic3r_GUI_wxExtensions_hpp_
#define slic3r_GUI_wxExtensions_hpp_

#include <wx/checklst.h>
#include <wx/combo.h>
#include <wx/dataview.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/menu.h>
#include <wx/bmpcbox.h>
#include <wx/bmpbndl.h>
#include <wx/statbmp.h>
#include <wx/timer.h>

#include <vector>
#include <functional>


#ifndef __linux__
void                sys_color_changed_menu(wxMenu* menu);
#else 
inline void         sys_color_changed_menu(wxMenu* /* menu */) {}
#endif // no __linux__

wxMenuItem* append_menu_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, wxBitmapBundle* icon, wxEvtHandler* event_handler = nullptr,
    std::function<bool()> const cb_condition = []() { return true;}, wxWindow* parent = nullptr, int insert_pos = wxNOT_FOUND);
wxMenuItem* append_menu_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, const std::string& icon = "", wxEvtHandler* event_handler = nullptr,
    std::function<bool()> const cb_condition = []() { return true; }, wxWindow* parent = nullptr, int insert_pos = wxNOT_FOUND);

wxMenuItem* append_submenu(wxMenu* menu, wxMenu* sub_menu, int id, const wxString& string, const wxString& description,
    const std::string& icon = "",
    std::function<bool()> const cb_condition = []() { return true; }, wxWindow* parent = nullptr);

wxMenuItem* append_menu_radio_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, wxEvtHandler* event_handler);

wxMenuItem* append_menu_check_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent & event)> cb, wxEvtHandler* event_handler,
    std::function<bool()> const enable_condition = []() { return true; }, 
    std::function<bool()> const check_condition = []() { return true; }, wxWindow* parent = nullptr);

void enable_menu_item(wxUpdateUIEvent& evt, std::function<bool()> const cb_condition, wxMenuItem* item, wxWindow* win);

class wxDialog;

void    edit_tooltip(wxString& tooltip);
void    msw_buttons_rescale(wxDialog* dlg, const int em_unit, const std::vector<int>& btn_ids);
int     em_unit(wxWindow* win);
int     mode_icon_px_size();

wxBitmapBundle* get_bmp_bundle(const std::string& bmp_name, int px_cnt = 16, const std::string& new_color_rgb = std::string());
wxBitmapBundle* get_empty_bmp_bundle(int width, int height);
wxBitmapBundle* get_solid_bmp_bundle(int width, int height, const std::string& color);

std::vector<wxBitmapBundle*> get_extruder_color_icons(bool thin_icon = false);

namespace Slic3r {
namespace GUI {
class BitmapComboBox;
}
}
void apply_extruder_selector(Slic3r::GUI::BitmapComboBox** ctrl,
                             wxWindow* parent,
                             const std::string& first_item = "",
                             wxPoint pos = wxDefaultPosition,
                             wxSize size = wxDefaultSize,
                             bool use_thin_icon = false);

class wxCheckListBoxComboPopup : public wxCheckListBox, public wxComboPopup
{
    static const unsigned int DefaultWidth;
    static const unsigned int DefaultHeight;

    wxString m_text;

    // Events sent on mouseclick are quite complex. Function OnListBoxSelection is supposed to pass the event to the checkbox, which works fine on
    // Win. On OSX and Linux the events are generated differently - clicking on the checkbox square generates the event twice (and the square
    // therefore seems not to respond).
    // This enum is meant to save current state of affairs, i.e., if the event forwarding is ok to do or not. It is only used on Linux
    // and OSX by some #ifdefs. It also stores information whether OnListBoxSelection is supposed to change the checkbox status,
    // or if it changed status on its own already (which happens when the square is clicked). More comments in OnCheckListBox(...)
    // There indeed is a better solution, maybe making a custom event used for the event passing to distinguish the original and passed message
    // and blocking one of them on OSX and Linux. Feel free to refactor, but carefully test on all platforms.
    enum class OnCheckListBoxFunction{
        FreeToProceed,
        RefuseToProceed,
        WasRefusedLastTime
    } m_check_box_events_status = OnCheckListBoxFunction::FreeToProceed;


public:
    virtual bool Create(wxWindow* parent);
    virtual wxWindow* GetControl();
    virtual void SetStringValue(const wxString& value);
    virtual wxString GetStringValue() const;
    virtual wxSize GetAdjustedSize(int minWidth, int prefHeight, int maxHeight);

    virtual void OnKeyEvent(wxKeyEvent& evt);

    void OnCheckListBox(wxCommandEvent& evt);
    void OnListBoxSelection(wxCommandEvent& evt);
};


// ***  wxDataViewTreeCtrlComboBox  ***

class wxDataViewTreeCtrlComboPopup: public wxDataViewTreeCtrl, public wxComboPopup
{
    static const unsigned int DefaultWidth;
    static const unsigned int DefaultHeight;
    static const unsigned int DefaultItemHeight;

    wxString	m_text;
    int			m_cnt_open_items{0};

public:
    virtual bool		Create(wxWindow* parent);
    virtual wxWindow*	GetControl() { return this; }
    virtual void		SetStringValue(const wxString& value) { m_text = value; }
    virtual wxString	GetStringValue() const { return m_text; }
//	virtual wxSize		GetAdjustedSize(int minWidth, int prefHeight, int maxHeight);

    virtual void		OnKeyEvent(wxKeyEvent& evt);
    void				OnDataViewTreeCtrlSelection(wxCommandEvent& evt);
    void				SetItemsCnt(int cnt) { m_cnt_open_items = cnt; }
};


// ----------------------------------------------------------------------------
// ScalableBitmap
// ----------------------------------------------------------------------------

class ScalableBitmap
{
public:
    ScalableBitmap() {};
    ScalableBitmap( wxWindow *parent,
                    const std::string& icon_name = "",
                    const int px_cnt = 16, 
                    const bool grayscale = false);

    ~ScalableBitmap() {}

    void                sys_color_changed();

    const wxBitmapBundle& bmp()   const { return m_bmp; }
    wxBitmap            get_bitmap()    { return m_bmp.GetBitmapFor(m_parent); }
    wxWindow*           parent()  const { return m_parent;}
    const std::string&  name()    const { return m_icon_name; }
    int                 px_cnt()  const { return m_px_cnt;}

    wxSize              GetSize()   const {
#ifdef __WIN32__
        return m_bmp.GetPreferredBitmapSizeFor(m_parent);
#else
        return m_bmp.GetDefaultSize();
#endif
    }
    int                 GetWidth()  const { return GetSize().GetWidth(); }
    int                 GetHeight() const { return GetSize().GetHeight(); }

private:
    wxWindow*       m_parent{ nullptr };
    wxBitmapBundle  m_bmp = wxBitmapBundle();
    wxBitmap        m_bitmap = wxBitmap();
    std::string     m_icon_name = "";
    int             m_px_cnt {16};
};


// ----------------------------------------------------------------------------
// LockButton
// ----------------------------------------------------------------------------

class LockButton : public wxButton
{
public:
    LockButton(
        wxWindow *parent,
        wxWindowID id,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize);
    ~LockButton() {}

    void    OnButton(wxCommandEvent& event);

    bool    IsLocked() const                { return m_is_pushed; }
    void    SetLock(bool lock);

    // create its own Enable/Disable functions to not really disabled button because of tooltip enabling
    void    enable()                        { m_disabled = false; }
    void    disable()                       { m_disabled = true;  }

    void    sys_color_changed();

protected:
    void    update_button_bitmaps();

private:
    bool        m_is_pushed = false;
    bool        m_disabled = false;

    ScalableBitmap    m_bmp_lock_closed;
    ScalableBitmap    m_bmp_lock_closed_f;
    ScalableBitmap    m_bmp_lock_open;
    ScalableBitmap    m_bmp_lock_open_f;
};


// ----------------------------------------------------------------------------
// ScalableButton
// ----------------------------------------------------------------------------

class ScalableButton : public wxButton
{
public:
    ScalableButton(){}
    ScalableButton(
        wxWindow *          parent,
        wxWindowID          id,
        const std::string&  icon_name = "",
        const wxString&     label = wxEmptyString,
        const wxSize&       size = wxDefaultSize,
        const wxPoint&      pos = wxDefaultPosition,
        long                style = wxBU_EXACTFIT | wxNO_BORDER,
        int                 bmp_px_cnt = 16);

    ScalableButton(
        wxWindow *          parent,
        wxWindowID          id,
        const ScalableBitmap&  bitmap,
        const wxString&     label = wxEmptyString,
        long                style = wxBU_EXACTFIT | wxNO_BORDER);

    ~ScalableButton() {}

    void SetBitmap_(const ScalableBitmap& bmp);
    bool SetBitmap_(const std::string& bmp_name);
    void SetBitmapDisabled_(const ScalableBitmap &bmp);
    int  GetBitmapHeight();

    virtual void    sys_color_changed();

private:
    wxWindow*       m_parent { nullptr };
    std::string     m_current_icon_name;
    std::string     m_disabled_icon_name;
    int             m_width {-1}; // should be multiplied to em_unit
    int             m_height{-1}; // should be multiplied to em_unit

protected:
    // bitmap dimensions 
    int             m_px_cnt{ 16 };
    bool            m_has_border {false};
};


// ----------------------------------------------------------------------------
// ModeButton
// ----------------------------------------------------------------------------

class ModeButton : public ScalableButton
{
public:
    ModeButton(
        wxWindow*           parent,
        wxWindowID          id,
        const std::string&  icon_name = "",
        const wxString&     mode = wxEmptyString,
        const wxSize&       size = wxDefaultSize,
        const wxPoint&      pos = wxDefaultPosition);

    ModeButton(
        wxWindow*           parent,
        const wxString&     mode = wxEmptyString,
        const std::string&  icon_name = "",
        int                 px_cnt = 16);

    ModeButton(
        wxWindow*           parent,
        int                 mode_id,/*ConfigOptionMode*/
        const wxString&     mode = wxEmptyString,
        int                 px_cnt = 16);

    ~ModeButton() {}

    void Init(const wxString& mode);

    void    OnButton(wxCommandEvent& event);
    void    OnEnterBtn(wxMouseEvent& event) { focus_button(true); event.Skip(); }
    void    OnLeaveBtn(wxMouseEvent& event) { focus_button(m_is_selected); event.Skip(); }

    void    SetState(const bool state);
    void    update_bitmap();
    bool    is_selected() { return m_is_selected; }
    void    sys_color_changed() override;

protected:
    void    focus_button(const bool focus);

private:
    bool        m_is_selected   {false};
    int         m_mode_id       {-1};

    wxString    m_tt_selected;
    wxString    m_tt_focused;
    wxBitmapBundle    m_bmp;
};



// ----------------------------------------------------------------------------
// ModeSizer
// ----------------------------------------------------------------------------

class ModeSizer : public wxFlexGridSizer
{
public:
    ModeSizer( wxWindow *parent, int hgap = 0);
    ~ModeSizer() {}

    void SetMode(const /*ConfigOptionMode*/int mode);

    void set_items_flag(int flag);
    void set_items_border(int border);

    void sys_color_changed();
    void update_mode_markers();
    const std::vector<ModeButton*>& get_btns() { return m_mode_btns; }

private:
    std::vector<ModeButton*> m_mode_btns;
    double                   m_hgap_unscaled;
};



// ----------------------------------------------------------------------------
// MenuWithSeparators
// ----------------------------------------------------------------------------

class MenuWithSeparators : public wxMenu
{
public:
    MenuWithSeparators(const wxString& title, long style = 0)
        : wxMenu(title, style) {}

    MenuWithSeparators(long style = 0)
        : wxMenu(style) {}

    ~MenuWithSeparators() {}

    void DestroySeparators();
    void SetFirstSeparator();
    void SetSecondSeparator();

private:
    wxMenuItem* m_separator_frst { nullptr };    // use like separator before settings item
    wxMenuItem* m_separator_scnd { nullptr };   // use like separator between settings items
};


// ----------------------------------------------------------------------------
// BlinkingBitmap
// ----------------------------------------------------------------------------

class BlinkingBitmap : public wxStaticBitmap
{
public:
    BlinkingBitmap() {};
    BlinkingBitmap(wxWindow* parent, const std::string& icon_name = "search_blink");

    ~BlinkingBitmap() {}

    void    invalidate();
    void    activate();
    void    blink();

    const wxBitmapBundle& get_bmp() const { return bmp.bmp(); }

private:
    ScalableBitmap  bmp;
    bool            show {false};
};

// ----------------------------------------------------------------------------
// Highlighter
// ----------------------------------------------------------------------------

namespace Slic3r {
namespace GUI {

class OG_CustomCtrl;

// Highlighter is used as an instrument to put attention to some UI control

class Highlighter
{
    int             m_blink_counter     { 0 };
    wxTimer         m_timer;

public:
    Highlighter() {}
    ~Highlighter() {}

    void set_timer_owner(wxWindow* owner, int timerid = wxID_ANY);
    virtual void bind_timer(wxWindow* owner) = 0;

    bool init(bool input_failed);
    void blink();
    void invalidate();
};

class HighlighterForWx : public Highlighter
{
// There are 2 possible cases to use HighlighterForWx:
// - using a BlinkingBitmap. Change state of this bitmap
    BlinkingBitmap* m_blinking_bitmap   { nullptr };
// - using OG_CustomCtrl where arrow will be rendered and flag indicated "show/hide" state of this arrow
    OG_CustomCtrl*  m_custom_ctrl       { nullptr };
    bool*           m_show_blink_ptr    { nullptr };

public:
    HighlighterForWx() {}
    ~HighlighterForWx() {}

    void bind_timer(wxWindow* owner) override;
    void init(BlinkingBitmap* blinking_bitmap);
    void init(std::pair<OG_CustomCtrl*, bool*>);
    void blink();
    void invalidate();
};
/*
class HighlighterForImGUI : public Highlighter
{

public:
    HighlighterForImGUI() {}
    ~HighlighterForImGUI() {}

    void init();
    void blink();
    void invalidate();
};
*/
} // GUI
} // Slic3r

#endif // slic3r_GUI_wxExtensions_hpp_
