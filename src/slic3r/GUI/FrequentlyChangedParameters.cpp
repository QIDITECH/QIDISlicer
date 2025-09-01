#include "FrequentlyChangedParameters.hpp"
#include "Plater.hpp"

#include <string>

#include <wx/sizer.h>
#include <wx/button.h>

#include "libslic3r/PresetBundle.hpp"

#include "GUI_App.hpp"
#include "wxExtensions.hpp"
#include "format.hpp"
#include "Tab.hpp"
#include "I18N.hpp"

#include "WipeTowerDialog.hpp"

//y25
#include "SyncBoxInfoDialog.hpp"

#include "Tab.hpp"

using Slic3r::Preset;
using Slic3r::GUI::format_wxstr;

namespace Slic3r {
namespace GUI {

// Trigger Plater::schedule_background_process().
wxDEFINE_EVENT(EVT_SCHEDULE_BACKGROUND_PROCESS,     SimpleEvent);

FreqChangedParams::FreqChangedParams(wxWindow* parent)
{
    DynamicPrintConfig*	config = &wxGetApp().preset_bundle->prints.get_edited_preset().config;

    // Frequently changed parameters for FFF_technology

    m_og_fff = std::make_shared<ConfigOptionsGroup>(parent, "");
    m_og_fff->set_config(config);
    m_og_fff->hide_labels();

    m_og_fff->on_change = [config, this](t_config_option_key opt_key, boost::any value) {
        Tab* tab_print = wxGetApp().get_tab(Preset::TYPE_PRINT);
        if (!tab_print) return;

        if (opt_key == "fill_density") {
            tab_print->update_dirty();
            tab_print->reload_config();
            tab_print->update();
        }
        else
        {
            DynamicPrintConfig new_conf = *config;
            if (opt_key == "brim") {
                double new_val;
                double brim_width = config->opt_float("brim_width");
                if (boost::any_cast<bool>(value) == true)
                {
                    new_val = m_brim_width == 0.0 ? 5 :
                        m_brim_width < 0.0 ? m_brim_width * (-1) :
                        m_brim_width;
                }
                else {
                    m_brim_width = brim_width * (-1);
                    new_val = 0;
                }
                new_conf.set_key_value("brim_width", new ConfigOptionFloat(new_val));
            }
            else {
                assert(opt_key == "support");
                const wxString& selection = boost::any_cast<wxString>(value);
                PrinterTechnology printer_technology = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology();

                auto support_material = selection == _("None") ? false : true;
                new_conf.set_key_value("support_material", new ConfigOptionBool(support_material));

                if (selection == _("Everywhere")) {
                    new_conf.set_key_value("support_material_buildplate_only", new ConfigOptionBool(false));
                    if (printer_technology == ptFFF)
                        new_conf.set_key_value("support_material_auto", new ConfigOptionBool(true));
                } else if (selection == _("Support on build plate only")) {
                    new_conf.set_key_value("support_material_buildplate_only", new ConfigOptionBool(true));
                    if (printer_technology == ptFFF)
                        new_conf.set_key_value("support_material_auto", new ConfigOptionBool(true));
                } else if (selection == _("For support enforcers only")) {
                    assert(printer_technology == ptFFF);
                    new_conf.set_key_value("support_material_buildplate_only", new ConfigOptionBool(false));
                    new_conf.set_key_value("support_material_auto", new ConfigOptionBool(false));
                }
            }
            tab_print->load_config(new_conf);
        }
    };


    Line line = Line { "", "" };

    ConfigOptionDef support_def;
    support_def.label = L("Supports");
    support_def.type = coStrings;
    support_def.tooltip = L("Select what kind of support do you need");
    support_def.set_enum_labels(ConfigOptionDef::GUIType::select_close, {
        L("None"),
        L("Support on build plate only"),
        L("For support enforcers only"),
        L("Everywhere")
    });
    support_def.set_default_value(new ConfigOptionStrings{ "None" });
    Option option = Option(support_def, "support");
    option.opt.full_width = true;
    line.append_option(option);

    /* Not a best solution, but
     * Temporary workaround for right border alignment
     */
    auto empty_widget = [this] (wxWindow* parent) {
        auto sizer = new wxBoxSizer(wxHORIZONTAL);
        auto btn = new ScalableButton(parent, wxID_ANY, "mirroring_transparent", wxEmptyString,
            wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER | wxTRANSPARENT_WINDOW);
        sizer->Add(btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, int(0.3 * wxGetApp().em_unit()));
        m_empty_buttons.push_back(btn);
        return sizer;
    };
    line.append_widget(empty_widget);

    m_og_fff->append_line(line);


    line = Line { "", "" };

    option = m_og_fff->get_option("fill_density");
    option.opt.label = L("Infill");
    option.opt.width = 8;
    option.opt.sidetext = "   ";
    line.append_option(option);

    m_brim_width = config->opt_float("brim_width");
    ConfigOptionDef def;
    def.label = L("Brim");
    def.type = coBool;
    def.tooltip = L("This flag enables the brim that will be printed around each object on the first layer.");
    def.gui_type = ConfigOptionDef::GUIType::undefined;
    def.set_default_value(new ConfigOptionBool{ m_brim_width > 0.0 ? true : false });
    option = Option(def, "brim");
    option.opt.sidetext = "";
    line.append_option(option);

    auto wiping_dialog_btn = [this](wxWindow* parent) {
        m_wiping_dialog_button = new wxButton(parent, wxID_ANY, _L("Purging volumes") + dots, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        wxGetApp().SetWindowVariantForButton(m_wiping_dialog_button);
        wxGetApp().UpdateDarkUI(m_wiping_dialog_button, true);

        auto sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(m_wiping_dialog_button, 0, wxALIGN_CENTER_VERTICAL);
        m_wiping_dialog_button->Bind(wxEVT_BUTTON, ([parent](wxCommandEvent& e)
        {
            PresetBundle* preset_bundle = wxGetApp().preset_bundle;
            DynamicPrintConfig& project_config = preset_bundle->project_config;
            const bool use_custom_matrix = (project_config.option<ConfigOptionBool>("wiping_volumes_use_custom_matrix"))->value;
            const std::vector<double> &init_matrix = (project_config.option<ConfigOptionFloats>("wiping_volumes_matrix"))->values;

            const std::vector<std::string> extruder_colours = wxGetApp().plater()->get_extruder_color_strings_from_plater_config();

            // Extract the relevant config options, even values from possibly modified presets.
            const double default_purge = static_cast<const ConfigOptionFloat*>(preset_bundle->printers.get_edited_preset().config.option("multimaterial_purging"))->value;
            std::vector<double> filament_purging_multipliers = preset_bundle->get_config_options_for_current_filaments<ConfigOptionPercents>("filament_purge_multiplier");

            WipingDialog dlg(parent, cast<float>(init_matrix), extruder_colours, default_purge, filament_purging_multipliers, use_custom_matrix);

            if (dlg.ShowModal() == wxID_OK) {
                std::vector<float> matrix = dlg.get_matrix();
                (project_config.option<ConfigOptionFloats>("wiping_volumes_matrix"))->values = std::vector<double>(matrix.begin(), matrix.end());
                (project_config.option<ConfigOptionBool>("wiping_volumes_use_custom_matrix"))->value = dlg.get_use_custom_matrix();
                // Update Project dirty state, update application title bar.
                Plater* plater = wxGetApp().plater();
                plater->update_project_dirty_from_presets();
                wxPostEvent(plater, SimpleEvent(EVT_SCHEDULE_BACKGROUND_PROCESS, plater));
            }
        }));

        auto btn = new ScalableButton(parent, wxID_ANY, "mirroring_transparent", wxEmptyString,
                                      wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER | wxTRANSPARENT_WINDOW);
        sizer->Add(btn , 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT,
            int(0.3 * wxGetApp().em_unit()));
        m_empty_buttons.push_back(btn);

        return sizer;
    };
    line.append_widget(wiping_dialog_btn);
    m_og_fff->append_line(line);

    m_og_fff->activate();

    Choice* choice = dynamic_cast<Choice*>(m_og_fff->get_field("support"));
    choice->suppress_scroll();

    // Frequently changed parameters for SLA_technology

    m_og_sla = std::make_shared<ConfigOptionsGroup>(parent, "");
    m_og_sla->hide_labels();
    DynamicPrintConfig*	config_sla = &wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
    m_og_sla->set_config(config_sla);

    m_og_sla->on_change = [config_sla](t_config_option_key opt_key, boost::any value) {
        Tab* tab = wxGetApp().get_tab(Preset::TYPE_SLA_PRINT);
        if (!tab) return;

        DynamicPrintConfig new_conf = *config_sla;
        if (opt_key == "pad") {
            const wxString& selection = boost::any_cast<wxString>(value);

            const bool pad_enable = selection == _("None") ? false : true;
            new_conf.set_key_value("pad_enable", new ConfigOptionBool(pad_enable));

            if (selection == _("Below object"))
                new_conf.set_key_value("pad_around_object", new ConfigOptionBool(false));
            else if (selection == _("Around object"))
                new_conf.set_key_value("pad_around_object", new ConfigOptionBool(true));
        }
        else
        {
            assert(opt_key == "support");
            const wxString& selection = boost::any_cast<wxString>(value);

            const bool supports_enable = selection == _("None") ? false : true;
            new_conf.set_key_value("supports_enable", new ConfigOptionBool(supports_enable));

            std::string treetype = get_sla_suptree_prefix(new_conf);

            if (selection == _("Everywhere")) {
                new_conf.set_key_value(treetype + "support_buildplate_only", new ConfigOptionBool(false));
                new_conf.set_key_value("support_enforcers_only", new ConfigOptionBool(false));
            }
            else if (selection == _("Support on build plate only")) {
                new_conf.set_key_value(treetype + "support_buildplate_only", new ConfigOptionBool(true));
                new_conf.set_key_value("support_enforcers_only", new ConfigOptionBool(false));
            }
            else if (selection == _("For support enforcers only")) {
                new_conf.set_key_value("support_enforcers_only", new ConfigOptionBool(true));
            }
        }

        tab->load_config(new_conf);
        tab->update_dirty();
    };

    line = Line{ "", "" };

    ConfigOptionDef support_def_sla = support_def;
    support_def_sla.set_default_value(new ConfigOptionStrings{ "None" });
    option = Option(support_def_sla, "support");
    option.opt.full_width = true;
    line.append_option(option);
    line.append_widget(empty_widget);
    m_og_sla->append_line(line);

    line = Line{ "", "" };

    ConfigOptionDef pad_def;
    pad_def.label = L("Pad");
    pad_def.type = coStrings;
    pad_def.tooltip = L("Select what kind of pad do you need");
    pad_def.set_enum_labels(ConfigOptionDef::GUIType::select_close, {
        L("None"),
        L("Below object"),
        L("Around object")
    });
    pad_def.set_default_value(new ConfigOptionStrings{ "Below object" });
    option = Option(pad_def, "pad");
    option.opt.full_width = true;
    line.append_option(option);
    line.append_widget(empty_widget);

    m_og_sla->append_line(line);

    m_og_sla->activate();
    choice = dynamic_cast<Choice*>(m_og_sla->get_field("support"));
    choice->suppress_scroll();
    choice = dynamic_cast<Choice*>(m_og_sla->get_field("pad"));
    choice->suppress_scroll();

//Y26
    m_og_filament = std::make_shared<ConfigOptionsGroup>(parent, "");
    DynamicPrintConfig* filament_config = &wxGetApp().preset_bundle->filaments.get_edited_preset().config;

    m_og_filament->set_config(filament_config);
    m_og_filament->hide_labels();

    m_og_filament->on_change = [filament_config, this](t_config_option_key opt_key, boost::any value) {
        Tab* tab_filament = wxGetApp().get_tab(Preset::TYPE_FILAMENT);
        if (!tab_filament) return;

        if (opt_key == "seal_print") {
            tab_filament->update_dirty();
            tab_filament->reload_config();
            tab_filament->update();
        }
    };

    line = Line { "", "" };

    option = m_og_filament->get_option("seal_print");
    option.opt.label = L("Seal");
    line.append_option(option);
    line.append_widget(empty_widget);

    m_og_filament->append_line(line);
    m_og_filament->activate();

    //y25
    m_og_sync = std::make_shared<ConfigOptionsGroup>(parent, "");
    DynamicPrintConfig* printer_config = &wxGetApp().preset_bundle->printers.get_edited_preset().config;

    m_og_sync->set_config(printer_config);
    m_og_sync->hide_labels();
    auto add_sync_btn = [this](wxWindow* parent) {

        //y26
        auto sync_btn = new wxButton(parent, wxID_ANY, _L("Sync filament info from the box"), wxDefaultPosition, wxSize(200, 30), wxBU_EXACTFIT);
        wxGetApp().UpdateDarkUI(sync_btn, true);

        sync_btn->SetToolTip(_L("Click the sync button to synchronize the Box information to the filament column."));
        auto sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(sync_btn, 0, wxALIGN_CENTER_VERTICAL);
        sync_btn->Bind(wxEVT_BUTTON, [parent](wxCommandEvent& e){
            //y25 y28
            std::string      ph_host = "";
            std::string      ph_api_key = "";
            bool has_select_printer = wxGetApp().preset_bundle->physical_printers.has_selection();
            if (has_select_printer) {
                PhysicalPrinter& ph_printer = wxGetApp().preset_bundle->physical_printers.get_selected_printer();
                ph_host = ph_printer.config.opt_string("print_host");
                ph_api_key = ph_printer.config.opt_string("printhost_apikey");
            }

            GetBoxInfoDialog dlg(wxGetApp().plater());
            if(ph_host.empty()){
                dlg.ShowModal();
            }
            else{
                dlg.synchronize_by_ip(ph_host, ph_api_key);
            }
        });
        return sizer;
    };
    line = Line { "", "" };
    line.append_only_widget(add_sync_btn);
    m_og_sync->append_line(line);
    m_og_sync->activate();

    m_sizer = new wxBoxSizer(wxVERTICAL);
    m_sizer->Add(m_og_fff->sizer, 0, wxEXPAND);

    //Y26
    m_sizer->Add(m_og_filament->sizer, 0, wxEXPAND);
    m_sizer->Add(m_og_sla->sizer, 0, wxEXPAND);

    //y25
    m_sizer->Add(m_og_sync->sizer, 0, wxEXPAND);
}

void FreqChangedParams::msw_rescale()
{
    m_og_fff->msw_rescale();
//Y26
    m_og_filament->msw_rescale();
    m_og_sla->msw_rescale();
}

void FreqChangedParams::sys_color_changed()
{
    m_og_fff->sys_color_changed();
//Y26
    m_og_filament->sys_color_changed();
    m_og_sla->sys_color_changed();

    for (auto btn: m_empty_buttons)
        btn->sys_color_changed();

    wxGetApp().UpdateDarkUI(m_wiping_dialog_button, true);
}

void FreqChangedParams::Show(bool is_fff) const
{
    const bool is_wdb_shown = m_wiping_dialog_button->IsShown();
    m_og_fff->Show(is_fff);
//Y26
    m_og_filament->Show(is_fff);
    m_og_sla->Show(!is_fff);
    //y25
    m_og_sync->Show(is_fff);
    // correct showing of the FreqChangedParams sizer when m_wiping_dialog_button is hidden
    if (is_fff && !is_wdb_shown)
        m_wiping_dialog_button->Hide();
}

ConfigOptionsGroup* FreqChangedParams::get_og(bool is_fff)
{
    return is_fff ? m_og_fff.get() : m_og_sla.get();
}

//Y26
ConfigOptionsGroup* FreqChangedParams::get_og_filament()
{
    return m_og_filament.get();
}

}}    // namespace Slic3r::GUI
