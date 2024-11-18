#ifndef slic3r_PhysicalPrinterDialog_hpp_
#define slic3r_PhysicalPrinterDialog_hpp_

#include <vector>

#include <wx/gdicmn.h>

#include "libslic3r/Preset.hpp"
#include "Widgets/TextInput.hpp"
#include "GUI_Utils.hpp"

class wxString;
class wxStaticText;
class ScalableButton;
class wxBoxSizer;

namespace Slic3r {

namespace GUI {

class PresetComboBox;

//------------------------------------------
//          PresetForPrinter
//------------------------------------------
//static std::string g_info_string = " (modified)";
class PhysicalPrinterDialog;
class PresetForPrinter
{
    PhysicalPrinterDialog* m_parent         { nullptr };

    PresetComboBox*     m_presets_list      { nullptr };
    ScalableButton*     m_delete_preset_btn { nullptr };
    wxStaticText*       m_info_line         { nullptr };
    wxStaticText*       m_full_printer_name { nullptr };

    wxBoxSizer*         m_sizer             { nullptr };

    void DeletePreset(wxEvent& event);

public:
    PresetForPrinter(PhysicalPrinterDialog* parent, const std::string& preset_name = "");
    ~PresetForPrinter();

    wxBoxSizer*         sizer() { return m_sizer; }
    void                update_full_printer_name();
    std::string         get_preset_name();
    void                SuppressDelete();
    void                AllowDelete();

    void                on_sys_color_changed();
};


//------------------------------------------
//          PhysicalPrinterDialog
//------------------------------------------

class ConfigOptionsGroup;
class PhysicalPrinterDialog : public DPIDialog
{
    PhysicalPrinter     m_printer;
    wxString            m_default_name;
    DynamicPrintConfig* m_config            { nullptr };
    ::TextInput*        m_printer_name      { nullptr };
    std::vector<PresetForPrinter*> m_presets;

    ConfigOptionsGroup* m_optgroup          { nullptr };

    ScalableButton*     m_add_preset_btn                {nullptr};
    ScalableButton*     m_printhost_browse_btn          {nullptr};
    ScalableButton*     m_printhost_test_btn            {nullptr};
    ScalableButton*     m_printhost_cafile_browse_btn   {nullptr};
    ScalableButton*     m_printhost_port_browse_btn     {nullptr};
    //y15
    // ScalableButton*     m_api_key_copy_btn              {nullptr};

    wxBoxSizer*         m_presets_sizer                 {nullptr};

    wxString            m_stored_host;
    PrintHostType       m_last_host_type;
    bool                m_opened_as_connect {false};
    //y3
    std::string         m_machine_name;
    std::string         m_machine_host;

    void build_printhost_settings(ConfigOptionsGroup* optgroup);
    void OnOK(wxEvent& event);
    void AddPreset(wxEvent& event);

public:
    //y3
    PhysicalPrinterDialog(wxWindow* parent, wxString printer_name, std::set<std::string> exit_host);
    ~PhysicalPrinterDialog();

    void        update(bool printer_change = false);
    void        update_host_type(bool printer_change);
    void        update_printhost_buttons();
    void        update_printers();
    wxString    get_printer_name();
    void        update_full_printer_names();
    PhysicalPrinter*    get_printer() {return &m_printer; }
    void                set_printer_technology(PrinterTechnology pt);
    PrinterTechnology   get_printer_technology();

    void        DeletePreset(PresetForPrinter* preset_for_printer);
    //y3
    std::string get_name();
    std::string get_host();

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override;

private:
    //y3
    std::set<std::string> m_exit_host;
    std::string           old_name;

};

} // namespace GUI
} // namespace Slic3r

#endif
