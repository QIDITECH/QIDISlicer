
#ifndef _SyncBoxInfoDialog_H_
#define _SyncBoxInfoDialog_H_

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#include "Plater.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/SwitchButton.hpp"
#include "Widgets/ComboBox.hpp"
#include "MainFrame.hpp"

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif


//y25
namespace Slic3r{
namespace GUI{

class GetBoxInfoDialog : public DPIDialog
{
public:
    GetBoxInfoDialog(Plater* plater = nullptr);
    ~GetBoxInfoDialog();
    void synchronization(wxCommandEvent &event);
    void synchronize_by_ip(std::string ip);
    void cancel(wxCommandEvent &event);
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void init_printer_combox();
    std::string NormalizeVendor(const std::string& str);
    void generate_filament_id(GUI::Box_info& machine_filament_info);
    void update_filament_info(GUI::Box_info& machine_filament_info);
    void sync_box_list();
    void load_box_list();

    std::vector<std::string> m_printer_ip;

private:
    Plater*             m_plater{ nullptr };
    wxBoxSizer*         m_sizer_main;
    wxStaticText*       m_stext_printer_title;
    ComboBox*           m_comboBox_printer;
    StateColor          btn_bg_enable;
    wxButton*           m_button_sync;
    wxButton*           m_button_cancel;
    wxColour            m_colour_def_color{ wxColour(255, 255, 255) };
    wxColour            m_colour_bold_color{ wxColour(38, 46, 48) };
    SwitchButton*       m_switch_button;
    bool                m_isNetMode;
    GUI::Box_info       syn_box_info;
    std::string         m_sync_printer_ip;
};

}}  // namespace Slic3r::GUI

#endif