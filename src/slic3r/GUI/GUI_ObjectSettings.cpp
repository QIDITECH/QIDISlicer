#include "GUI_ObjectSettings.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_Factories.hpp"

#include "OptionsGroup.hpp"
#include "GUI_App.hpp"
#include "wxExtensions.hpp"
#include "Plater.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Model.hpp"

#include <boost/algorithm/string.hpp>

#include "I18N.hpp"
#include "format.hpp"
#include "ConfigManipulation.hpp"

#include <wx/wupdlock.h>

namespace Slic3r
{
namespace GUI
{

OG_Settings::OG_Settings(wxWindow* parent, const bool staticbox) :
    m_parent(parent)
{
    wxString title = staticbox ? " " : ""; // temporary workaround - #ys_FIXME
    m_og = std::make_shared<ConfigOptionsGroup>(parent, title);
}

bool OG_Settings::IsShown()
{
    return m_og->sizer->IsEmpty() ? false : m_og->sizer->IsShown(size_t(0));
}

void OG_Settings::Show(const bool show)
{
    m_og->Show(show);
}

void OG_Settings::Hide()
{
    Show(false);
}

void OG_Settings::UpdateAndShow(const bool show)
{
    Show(show);
//    m_parent->Layout();
}

wxSizer* OG_Settings::get_sizer()
{
    return m_og->sizer;
}



ObjectSettings::ObjectSettings(wxWindow* parent) :
    OG_Settings(parent, true)
{
    m_og->activate();
    m_og->set_name(_(L("Additional Settings")));    

    m_settings_list_sizer = new wxBoxSizer(wxVERTICAL);
    m_og->sizer->Add(m_settings_list_sizer, 1, wxEXPAND | wxLEFT, 5);

    m_bmp_delete = ScalableBitmap(parent, "cross");
    m_bmp_delete_focus = ScalableBitmap(parent, "cross_focus");
}

bool ObjectSettings::update_settings_list()
{
    m_settings_list_sizer->Clear(true);
    m_og_settings.resize(0);

    auto objects_ctrl   = wxGetApp().obj_list();
    auto objects_model  = wxGetApp().obj_list()->GetModel();
    auto config         = wxGetApp().obj_list()->config();

    const auto item = objects_ctrl->GetSelection();
    
    if (!item || !objects_model->IsSettingsItem(item) || !config || objects_ctrl->multiple_selection())
        return false;

    const bool is_object_settings = objects_model->GetItemType(objects_model->GetParent(item)) == itObject;
    SettingsFactory::Bundle cat_options = SettingsFactory::get_bundle(&config->get(), is_object_settings);

    if (!cat_options.empty())
    {
	    std::vector<std::string> categories;
        categories.reserve(cat_options.size());

        auto extra_column = [config, this](wxWindow* parent, const Line& line)
		{
			auto opt_key = (line.get_options())[0].opt_id;  //we assume that we have one option per line

			auto btn = new ScalableButton(parent, wxID_ANY, m_bmp_delete);
            btn->SetToolTip(_(L("Remove parameter")));

            btn->SetBitmapFocus(m_bmp_delete_focus.bmp());
            btn->SetBitmapCurrent(m_bmp_delete_focus.bmp());

			btn->Bind(wxEVT_BUTTON, [opt_key, config, this](wxEvent &event) {
                wxGetApp().plater()->take_snapshot(format_wxstr(_L("Delete Option %s"), opt_key));
				config->erase(opt_key);
                wxGetApp().obj_list()->changed_object();
                wxTheApp->CallAfter([this]() {
                    wxWindowUpdateLocker noUpdates(m_parent);
                    update_settings_list(); 
                    m_parent->Layout(); 
                });
			});
			return btn;
		};

        for (auto& cat : cat_options)
        {
            categories.push_back(cat.first);

            auto optgroup = std::make_shared<ConfigOptionsGroup>(m_og->ctrl_parent(), _(cat.first), config, false, extra_column);
            optgroup->label_width = 15;
            optgroup->sidetext_width = 5;

            optgroup->on_change = [this, config](const t_config_option_key& opt_id, const boost::any& value) {
                                    this->update_config_values(config);
                                    wxGetApp().obj_list()->changed_object(); };

            // call back for rescaling of the extracolumn control
            optgroup->rescale_extra_column_item = [this](wxWindow* win) {
                auto *ctrl = dynamic_cast<ScalableButton*>(win);
                if (ctrl == nullptr)
                    return;
                ctrl->SetBitmap_(m_bmp_delete);
                ctrl->SetBitmapFocus(m_bmp_delete_focus.bmp()); 
                ctrl->SetBitmapCurrent(m_bmp_delete_focus.bmp());
            };

            const bool is_extruders_cat = cat.first == "Extruders";
            for (auto& opt : cat.second)
            {
                Option option = optgroup->get_option(opt);
                option.opt.width = 12;
                if (!option.opt.full_label.empty())
                    option.opt.label = option.opt.full_label;
                if (is_extruders_cat)
                    option.opt.max = wxGetApp().extruders_edited_cnt();
                optgroup->append_single_option_line(option);
            }
            optgroup->activate();
            for (auto& opt : cat.second)
                optgroup->get_field(opt)->m_on_change = [optgroup](const std::string& opt_id, const boost::any& value) {
                    // first of all take a snapshot and then change value in configuration
                    wxGetApp().plater()->take_snapshot(format_wxstr(_L("Change Option %s"), opt_id));
                    optgroup->on_change_OG(opt_id, value);
                };

            optgroup->reload_config();

            m_settings_list_sizer->Add(optgroup->sizer, 0, wxEXPAND | wxALL, 0);
            m_og_settings.push_back(optgroup);
        }

        if (!categories.empty()) {
            objects_model->UpdateSettingsDigest(item, categories);
            update_config_values(config);
        }
    }
    else
    {
        objects_ctrl->select_item(objects_model->Delete(item));
        return false;
    } 
            
    return true;
}

bool ObjectSettings::add_missed_options(ModelConfig* config_to, const DynamicPrintConfig& config_from)
{
    const DynamicPrintConfig& print_config = wxGetApp().plater()->printer_technology() == ptFFF ?
                                             wxGetApp().preset_bundle->prints.get_edited_preset().config :
                                             wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
    bool is_added = false;

    for (auto opt_key : config_from.diff(print_config))
        if (!config_to->has(opt_key)) {
            config_to->set_key_value(opt_key, config_from.option(opt_key)->clone());
            is_added = true;
        }

    return is_added;
}

void ObjectSettings::update_config_values(ModelConfig* config)
{
    const auto objects_model        = wxGetApp().obj_list()->GetModel();
    const auto item                 = wxGetApp().obj_list()->GetSelection();
    const auto printer_technology   = wxGetApp().plater()->printer_technology();
    const bool is_object_settings   = objects_model->GetItemType(objects_model->GetParent(item)) == itObject;

    if (!item || !objects_model->IsSettingsItem(item) || !config)
        return;

    // update config values according to configuration hierarchy
    DynamicPrintConfig  main_config   = printer_technology == ptFFF ?
                                        wxGetApp().preset_bundle->prints.get_edited_preset().config :
                                        wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;

    auto load_config = [this, config, &main_config]()
    {
        /* Additional check for overrided options.
         * There is a case, when some options should to be added, 
         * to avoid check loop in the next configuration update
         */
        bool is_added = add_missed_options(config, main_config);

        // load checked values from main_config to config
        config->apply_only(main_config, config->keys(), true);
        // Initialize UI components with the config values.
        for (auto og : m_og_settings)
            og->reload_config();
        // next config check
        update_config_values(config);

        if (is_added) {
            // #ysNOTE - CallAfter is needed here to avoid crash on add new override params! see GH#13450
            wxTheApp->CallAfter([this]() {
                wxWindowUpdateLocker noUpdates(m_parent);
                update_settings_list();
                m_parent->Layout();
           });
        }
    };

    auto toggle_field = [this](const t_config_option_key & opt_key, bool toggle, int opt_index)
    {
        Field* field = nullptr;
        for (auto og : m_og_settings) {
            field = og->get_fieldc(opt_key, opt_index);
            if (field != nullptr)
                break;
        }
        if (field)
            field->toggle(toggle);
    };

    ConfigManipulation config_manipulation(load_config, toggle_field, nullptr, config);

    if (!is_object_settings)
    {
        const int obj_idx = objects_model->GetObjectIdByItem(item);
        assert(obj_idx >= 0);
        // for object's part first of all update konfiguration from object 
        main_config.apply(wxGetApp().model().objects[obj_idx]->config.get(), true);
        // and then from its own config
    }

    main_config.apply(config->get(), true);
    if (printer_technology == ptFFF) 
        config_manipulation.update_print_fff_config(&main_config);

    printer_technology == ptFFF  ?  config_manipulation.toggle_print_fff_options(&main_config) :
                                    config_manipulation.toggle_print_sla_options(&main_config) ;
}

void ObjectSettings::UpdateAndShow(const bool show)
{
    OG_Settings::UpdateAndShow(show ? update_settings_list() : false);
}

void ObjectSettings::sys_color_changed()
{
    m_og->sys_color_changed();

    for (auto group : m_og_settings)
        group->sys_color_changed();
}

} //namespace GUI
} //namespace Slic3r 
