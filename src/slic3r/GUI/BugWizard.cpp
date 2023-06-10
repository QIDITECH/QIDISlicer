// FIXME: extract absolute units -> em

#include "BugWizard_private.hpp"

#include <algorithm>
#include <numeric>
#include <utility>
#include <unordered_map>
#include <stdexcept>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/dll/runtime_symbol_info.hpp>

#include <wx/settings.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/dcclient.h>
#include <wx/statbmp.h>
#include <wx/checkbox.h>
#include <wx/statline.h>
#include <wx/dataview.h>
#include <wx/notebook.h>
#include <wx/listbook.h>
#include <wx/display.h>
#include <wx/filefn.h>
#include <wx/wupdlock.h>
#include <wx/debug.h>

#ifdef _MSW_DARK_MODE
#include <wx/msw/dark_mode.h>
#endif // _MSW_DARK_MODE

#ifdef WIN32
#include <wx/msw/registry.h>
#include <KnownFolders.h>
#include <Shlobj_core.h>
#endif // WIN32

#include "libslic3r/Platform.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Color.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#include "GUI_ObjectManipulation.hpp"
#include "Field.hpp"
#include "DesktopIntegrationDialog.hpp"
#include "slic3r/Config/Snapshot.hpp"
#include "slic3r/Utils/PresetUpdater.hpp"
#include "format.hpp"
#include "MsgDialog.hpp"
#include "UnsavedChangesDialog.hpp"
#include "slic3r/Utils/AppUpdater.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/Config/Version.hpp"

#if defined(__linux__) && defined(__WXGTK3__)
#define wxLinux_gtk3 true
#else
#define wxLinux_gtk3 false
#endif //defined(__linux__) && defined(__WXGTK3__)

namespace Slic3r {
namespace GUI {


using Config::Snapshot;
using Config::SnapshotDB;


// Configuration data structures extensions needed for the wizard

bool BugBundle::load(fs::path source_path, BugBundleLocation location, bool ais_qidi_bundle)
{
    this->preset_bundle = std::make_unique<PresetBundle>();
    this->location = location;
    this->is_qidi_bundle = ais_qidi_bundle;

    std::string path_string = source_path.string();
    // Throw when parsing invalid configuration. Only valid configuration is supposed to be provided over the air.
    auto [config_substitutions, presets_loaded] = preset_bundle->load_configbundle(
        path_string, PresetBundle::LoadConfigBundleAttribute::LoadSystem, ForwardCompatibilitySubstitutionRule::Disable);
    UNUSED(config_substitutions);
    // No substitutions shall be reported when loading a system config bundle, no substitutions are allowed.
    assert(config_substitutions.empty());
    auto first_vendor = preset_bundle->vendors.begin();
    if (first_vendor == preset_bundle->vendors.end()) {
        BOOST_LOG_TRIVIAL(error) << boost::format("Vendor bundle: `%1%`: No vendor information defined, cannot install.") % path_string;
        return false;
    }
    if (presets_loaded == 0) {
        BOOST_LOG_TRIVIAL(error) << boost::format("Vendor bundle: `%1%`: No profile loaded.") % path_string;
        return false;
    } 

    BOOST_LOG_TRIVIAL(trace) << boost::format("Vendor bundle: `%1%`: %2% profiles loaded.") % path_string % presets_loaded;
    this->vendor_profile = &first_vendor->second;
    return true;
}

BugBundle::BugBundle(BugBundle &&other)
    : preset_bundle(std::move(other.preset_bundle))
    , vendor_profile(other.vendor_profile)
    , location(other.location)
    , is_qidi_bundle(other.is_qidi_bundle)
{
    other.vendor_profile = nullptr;
}

BugBundleMap BugBundleMap::load()
{
    BugBundleMap res;

    const auto vendor_dir = (boost::filesystem::path(Slic3r::data_dir()) / "vendor").make_preferred();
    const auto archive_dir = (boost::filesystem::path(Slic3r::data_dir()) / "cache" / "vendor").make_preferred();
    const auto rsrc_vendor_dir = (boost::filesystem::path(resources_dir()) / "profiles").make_preferred();
    const auto cache_dir = boost::filesystem::path(Slic3r::data_dir()) / "cache"; // for Index
    // Load QIDI bundle from the datadir/vendor directory or from datadir/cache/vendor (archive) or from resources/profiles.
    auto qidi_bundle_path = (vendor_dir / PresetBundle::PRUSA_BUNDLE).replace_extension(".ini");
    BugBundleLocation qidi_bundle_loc = BugBundleLocation::IN_VENDOR;
    if (! boost::filesystem::exists(qidi_bundle_path)) {
        qidi_bundle_path = (archive_dir / PresetBundle::PRUSA_BUNDLE).replace_extension(".ini");
        qidi_bundle_loc = BugBundleLocation::IN_ARCHIVE;
    }
    if (!boost::filesystem::exists(qidi_bundle_path)) {
        qidi_bundle_path = (rsrc_vendor_dir / PresetBundle::PRUSA_BUNDLE).replace_extension(".ini");
        qidi_bundle_loc = BugBundleLocation::IN_RESOURCES;
    }
    {
        BugBundle qidi_bundle;
        if (qidi_bundle.load(std::move(qidi_bundle_path), qidi_bundle_loc, true))
            res.emplace(PresetBundle::PRUSA_BUNDLE, std::move(qidi_bundle)); 
    }

    // Load the other bundles in the datadir/vendor directory
    // and then additionally from datadir/cache/vendor (archive) and resources/profiles.
    // Should we concider case where archive has older profiles than resources (shouldnt happen)? -> YES, it happens during re-configuration when running older PS after newer version
    typedef std::pair<const fs::path&, BugBundleLocation> DirData;
    std::vector<DirData> dir_list { {vendor_dir, BugBundleLocation::IN_VENDOR},  {archive_dir, BugBundleLocation::IN_ARCHIVE},  {rsrc_vendor_dir, BugBundleLocation::IN_RESOURCES} };
    for ( auto dir : dir_list) {
        if (!fs::exists(dir.first))
            continue;
        for (const auto &dir_entry : boost::filesystem::directory_iterator(dir.first)) {
            if (Slic3r::is_ini_file(dir_entry)) {
                std::string id = dir_entry.path().stem().string();  // stem() = filename() without the trailing ".ini" part

                // Don't load this bundle if we've already loaded it.
                if (res.find(id) != res.end()) { continue; }

                // Fresh index should be in archive_dir, otherwise look for it in cache 
                // Then if not in archive or cache - it could be 3rd party profile that user just copied to vendor folder (both ini and cache)
                
                fs::path idx_path (archive_dir / (id + ".idx"));
                if (!boost::filesystem::exists(idx_path)) {
                    BOOST_LOG_TRIVIAL(error) << format("Missing index %1% when loading bundle %2%. Going to search for it in cache folder.", idx_path.string(), id);
                    idx_path = fs::path(cache_dir / (id + ".idx"));
                }
                if (!boost::filesystem::exists(idx_path)) {
                    BOOST_LOG_TRIVIAL(error) << format("Missing index %1% when loading bundle %2%. Going to search for it in vendor folder. Is it a 3rd party profile?", idx_path.string(), id);
                    idx_path = fs::path(vendor_dir / (id + ".idx"));
                }
                if (!boost::filesystem::exists(idx_path)) {
                    BOOST_LOG_TRIVIAL(error) << format("Could not load bundle %1% due to missing index %2%.", id, idx_path.string());
                    continue;
                }

                Slic3r::GUI::Config::Index index;
                try {
                    index.load(idx_path);
                }
                catch (const std::exception& /* err */) {
                    BOOST_LOG_TRIVIAL(error) << format("Could not load bundle %1% due to invalid index %2%.", id, idx_path.string());
                    continue;
                }
                const auto recommended_it = index.recommended();
                if (recommended_it == index.end()) {
                    BOOST_LOG_TRIVIAL(error) << format("Could not load bundle %1% due to no recommended version in index %2%.", id, idx_path.string());
                    continue;
                }
                const auto recommended = recommended_it->config_version;
                VendorProfile vp;
                try {
                    vp = VendorProfile::from_ini(dir_entry, true);
                }
                catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << format("Could not load bundle %1% due to corrupted profile file %2%. Message: %3%", id, dir_entry.path().string(), e.what());
                    continue;
                }
                // Don't load
                if (vp.config_version > recommended)
                    continue;

                BugBundle bundle;
                if (bundle.load(dir_entry.path(), dir.second))
                    res.emplace(std::move(id), std::move(bundle));
            }
        }
    }

    return res;
}

BugBundle& BugBundleMap::qidi_bundle()
{
    auto it = find(PresetBundle::PRUSA_BUNDLE);
    if (it == end()) {
        throw Slic3r::RuntimeError("ConfigWizard: Internal error in BundleMap: PRUSA_BUNDLE not loaded");
    }

    return it->second;
}

const BugBundle& BugBundleMap::qidi_bundle() const
{
    return const_cast<BugBundleMap*>(this)->qidi_bundle();
}


// Printer model picker GUI control

struct BugPrinterPickerEvent : public wxEvent
{
    std::string vendor_id;
    std::string model_id;
    std::string variant_name;
    bool enable;

    BugPrinterPickerEvent(wxEventType eventType, int winid, std::string vendor_id, std::string model_id, std::string variant_name, bool enable)
        : wxEvent(winid, eventType)
        , vendor_id(std::move(vendor_id))
        , model_id(std::move(model_id))
        , variant_name(std::move(variant_name))
        , enable(enable)
    {}

    virtual wxEvent *Clone() const
    {
        return new BugPrinterPickerEvent(*this);
    }
};

wxDEFINE_EVENT(EVT_PRINTER_PICK, BugPrinterPickerEvent);

const std::string BugPrinterPicker::PRINTER_PLACEHOLDER = "printer_placeholder.png";

BugPrinterPicker::BugPrinterPicker(wxWindow *parent, const VendorProfile &vendor, wxString title, size_t max_cols, const AppConfig &appconfig, const BugModelFilter &filter)
    : wxPanel(parent)
    , vendor_id(vendor.id)
    , width(0)
{
    wxGetApp().UpdateDarkUI(this);
    const auto &models = vendor.models;

    auto *sizer = new wxBoxSizer(wxVERTICAL);

    const auto font_title = GetFont().MakeBold().Scaled(1.3f);
    const auto font_name = GetFont().MakeBold().Scaled(1.2f);
    const auto font_email = GetFont().MakeBold();
    const auto font_alt_nozzle = GetFont().Scaled(0.9f);

    // wxGrid appends widgets by rows, but we need to construct them in columns.
    // These vectors are used to hold the elements so that they can be appended in the right order.
    std::vector<wxStaticText*> titles;
    std::vector<wxStaticBitmap*> bitmaps;
    std::vector<wxStaticText*> lbemails;
    std::vector<wxPanel*> emails_panels;
    std::vector<wxStaticText*> lbskypes;
    std::vector<wxPanel*> skypes_panels;

    int max_row_width = 0;
    int current_row_width = 0;

    bool is_emails   = false;
    bool is_skypes   = false;

    const fs::path vendor_dir_path = (fs::path(Slic3r::data_dir()) / "vendor").make_preferred();
    const fs::path cache_dir_path = (fs::path(Slic3r::data_dir()) / "cache").make_preferred();
    const fs::path rsrc_dir_path = (fs::path(resources_dir()) / "profiles").make_preferred();

    for (const auto &model : models) {
        if (! filter(model)) { continue; }

        wxBitmap bitmap;
        int bitmap_width = 0;
        auto load_bitmap = [](const wxString& bitmap_file, wxBitmap& bitmap, int& bitmap_width) {
            bitmap.LoadFile(bitmap_file, wxBITMAP_TYPE_PNG);
            bitmap_width = bitmap.GetWidth();
        };
        
        bool found = false;
        for (const fs::path& res : { rsrc_dir_path   / vendor.id / model.thumbnail
                                   , vendor_dir_path / vendor.id / model.thumbnail
                                   , cache_dir_path  / vendor.id / model.thumbnail })
        {
            if (!fs::exists(res))
                continue;
            load_bitmap(GUI::from_u8(res.string()), bitmap, bitmap_width);
            found = true;
            break;
        }
        
        if (!found) {
            BOOST_LOG_TRIVIAL(warning) << boost::format("Can't find bitmap file `%1%` for vendor `%2%`, printer `%3%`, using placeholder icon instead")
                % model.thumbnail
                % vendor.id
                % model.id;
            load_bitmap(Slic3r::var(PRINTER_PLACEHOLDER), bitmap, bitmap_width);
        }
        
        wxStaticText* title = new wxStaticText(this, wxID_ANY, from_u8(model.name), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
        title->SetFont(font_name);
        wxStaticText* lbemail = new wxStaticText(this, wxID_ANY, from_u8("E-mail"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
        lbemail->SetFont(font_email);
        wxStaticText *lbskype = new wxStaticText(this, wxID_ANY, from_u8("SKYPE"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
        lbskype->SetFont(font_email);
        const int wrap_width = std::max((int)MODEL_MIN_WRAP, bitmap_width);
        title->Wrap(wrap_width);

        current_row_width += wrap_width;
        if (titles.size() % max_cols == max_cols - 1) {
            max_row_width = std::max(max_row_width, current_row_width);
            current_row_width = 0;
        }
        titles.push_back(title);

        if (lbemails.size() % max_cols == max_cols - 1) {
            max_row_width     = std::max(max_row_width, current_row_width);
            current_row_width = 0;
        }
        lbemails.push_back(lbemail);

        if (lbskypes.size() % max_cols == max_cols - 1) {
            max_row_width     = std::max(max_row_width, current_row_width);
            current_row_width = 0;
        }
        lbskypes.push_back(lbskype);

        wxStaticBitmap* bitmap_widget = new wxStaticBitmap(this, wxID_ANY, bitmap);
        bitmaps.push_back(bitmap_widget);

        auto *emails_panel   = new wxPanel(this);
        auto *skypes_panel   = new wxPanel(this);
        wxGetApp().UpdateDarkUI(emails_panel);
        wxGetApp().UpdateDarkUI(skypes_panel);
        auto *emails_sizer   = new wxBoxSizer(wxVERTICAL | wxHORIZONTAL);
        auto *skypes_sizer   = new wxBoxSizer(wxVERTICAL | wxHORIZONTAL);
        emails_panel->SetSizer(emails_sizer);
        skypes_panel->SetSizer(skypes_sizer);
        const auto model_id = model.id;

        for (size_t i = 0; i < model.emails.size(); i++) {
            const auto &email    = model.emails[i];
            auto *      btn_cpye = new ScalableButton(emails_panel, wxID_ANY, "copy_menu", email.name);
            btn_cpye->SetToolTip(_L("Copy the e-mail address"));

            const bool enabled = appconfig.get_email(vendor.id, model_id, email.name);

            emails_sizer->Add(btn_cpye, 0, wxLEFT, 0);
            btn_cpye->Bind(wxEVT_BUTTON, [this, i, &email](wxEvent &) {
                if (wxTheClipboard->Open()) {
                    wxTheClipboard->SetData(new wxTextDataObject(email.name));
                    wxTheClipboard->Close();
                }
            });
            // btn_cpye->Bind(wxEVT_BUTTON, &BugPrinterPicker::CopyEmail, this);
        }
        emails_panels.push_back(emails_panel);

        for (size_t i = 0; i < model.skypes.size(); i++) {
            const auto &skype    = model.skypes[i];
            auto *      btn_cpys = new ScalableButton(skypes_panel, wxID_ANY, "copy_menu", skype.name);
            wxTheClipboard->SetData(new wxTextDataObject(skype.name));
            btn_cpys->SetToolTip(_L("Copy the skype address"));

            const bool enabled = appconfig.get_skype(vendor.id, model_id, skype.name);

            skypes_sizer->Add(btn_cpys, 0, wxLEFT, 0);
            btn_cpys->Bind(wxEVT_BUTTON, [this, i, &skype](wxEvent &) {
                if (wxTheClipboard->Open()) {
                    wxTheClipboard->SetData(new wxTextDataObject(skype.name));
                    wxTheClipboard->Close();
                }
            });
        }
        skypes_panels.push_back(skypes_panel);
    }

    width = std::max(max_row_width, current_row_width);

    const size_t cols = std::min(max_cols, titles.size());

    auto *printer_grid = new wxFlexGridSizer(cols, 0, 100);
    printer_grid->SetFlexibleDirection(wxVERTICAL | wxHORIZONTAL);

    if (titles.size() > 0) {
        const size_t odd_items = titles.size() % cols;

        for (size_t i = 0; i < titles.size() - odd_items; i += cols) {
            for (size_t j = i; j < i + cols; j++) { printer_grid->Add(bitmaps[j], 0, wxBOTTOM, 0); }
            for (size_t j = i; j < i + cols; j++) { printer_grid->Add(titles[j], 0, wxBOTTOM, 3); }
            for (size_t j = i; j < i + cols; j++) { printer_grid->Add(lbemails[j], 0, wxBOTTOM, 3); }
            for (size_t j = i; j < i + cols; j++) { printer_grid->Add(emails_panels[j], 0, wxBOTTOM, 3); }
            for (size_t j = i; j < i + cols; j++) { printer_grid->Add(lbskypes[j], 0, wxBOTTOM, 3); }
            for (size_t j = i; j < i + cols; j++) { printer_grid->Add(skypes_panels[j]); }

            // Add separator space to multiliners
            if (titles.size() > cols) {
                for (size_t j = i; j < i + cols; j++) { printer_grid->Add(1, 30); }
            }
        }
        if (odd_items > 0) {
            const size_t rem = titles.size() - odd_items;

            for (size_t i = rem; i < titles.size(); i++) { printer_grid->Add(bitmaps[i], 0, wxBOTTOM, 20); }
            for (size_t i = 0; i < cols - odd_items; i++) { printer_grid->AddSpacer(1); }
            for (size_t i = rem; i < titles.size(); i++) { printer_grid->Add(titles[i], 0, wxBOTTOM, 3); }
            for (size_t i = 0; i < cols - odd_items; i++) { printer_grid->AddSpacer(1); }
            for (size_t i = rem; i < titles.size(); i++) { printer_grid->Add(skypes_panels[i]); }
        }
    }

    auto *title_sizer = new wxBoxSizer(wxHORIZONTAL);
    if (! title.IsEmpty()) {
        auto *title_widget = new wxStaticText(this, wxID_ANY, title);
        title_widget->SetFont(font_title);
        title_sizer->Add(title_widget);
    }
    title_sizer->AddStretchSpacer();

    sizer->Add(title_sizer, 0, wxEXPAND | wxBOTTOM, BTN_SPACING);
    sizer->Add(printer_grid);

    SetSizer(sizer);
}
/*void BugPrinterPicker::CopyEmail(wxEvent &)
{
    wxTheClipboard->Open();
    wxTheClipboard->SetData(new wxTextDataObject(_L("Version") + " " + std::string(SLIC3R_VERSION)));
    wxTheClipboard->Close();
}*/
BugPrinterPicker::BugPrinterPicker(wxWindow *parent, const VendorProfile &vendor, wxString title, size_t max_cols, const AppConfig &appconfig)
    : BugPrinterPicker(parent, vendor, std::move(title), max_cols, appconfig, [](const VendorProfile::PrinterModel&) { return true; })
{}

void BugPrinterPicker::select_all(bool select, bool alternates)
{
}

void BugPrinterPicker::select_one(size_t i, bool select)
{
}

bool BugPrinterPicker::any_selected() const
{
    for (const auto &cb : cboxes) {
        if (cb->GetValue()) { return true; }
    }

    for (const auto &cb : cboxes_alt) {
        if (cb->GetValue()) { return true; }
    }

    return false;
}

std::set<std::string> BugPrinterPicker::get_selected_models() const 
{
    std::set<std::string> ret_set;

    for (const auto& cb : cboxes)
        if (cb->GetValue())
            ret_set.emplace(cb->model);

    for (const auto& cb : cboxes_alt)
        if (cb->GetValue())
            ret_set.emplace(cb->model);

    return ret_set;
}

void BugPrinterPicker::on_checkbox(const Checkbox *cbox, bool checked)
{
}


// Wizard page base

BugWizardPage::BugWizardPage(BugWizard *parent, wxString title, wxString shortname, unsigned indent)
    : wxPanel(parent->p->hscroll)
    , parent(parent)
    , shortname(std::move(shortname))
    , indent(indent)
{
    wxGetApp().UpdateDarkUI(this);

    auto *sizer = new wxBoxSizer(wxVERTICAL);

    auto *text = new wxStaticText(this, wxID_ANY, std::move(title), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    const auto font = GetFont().MakeBold().Scaled(1.5);
    text->SetFont(font);
    sizer->Add(text, 0, wxALIGN_LEFT, 0);
    sizer->AddSpacer(10);

    content = new wxBoxSizer(wxVERTICAL);
    sizer->Add(content, 1, wxEXPAND);

    SetSizer(sizer);

    // There is strange layout on Linux with GTK3, 
    // see https://github.com/qidi3d/QIDISlicer/issues/5103 and https://github.com/qidi3d/QIDISlicer/issues/4861
    // So, non-active pages will be hidden later, on wxEVT_SHOW, after completed Layout() for all pages 
    if (!wxLinux_gtk3)
        this->Hide();

    Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
        this->Layout();
        event.Skip();
    });
}

BugWizardPage::~BugWizardPage() {}

wxStaticText* BugWizardPage::append_text(wxString text)
{
    auto *widget = new wxStaticText(this, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    widget->Wrap(WRAP_WIDTH);
    widget->SetMinSize(wxSize(WRAP_WIDTH, -1));
    append(widget);
    return widget;
}

void BugWizardPage::append_spacer(int space)
{
    // FIXME: scaling
    content->AddSpacer(space);
}

// Wizard pages

BugPageWelcome::BugPageWelcome(BugWizard *parent)
    : BugWizardPage(parent, from_u8((boost::format(
#ifdef __APPLE__
            _utf8(L("Welcome to the %s Configuration Assistant"))
#else
            /*_utf8*/(L("Welcome to the %s Configuration Wizard"))
#endif
            ) % SLIC3R_APP_NAME).str()), _L("Welcome"))
    , welcome_text(append_text(from_u8((boost::format(
        /*_utf8*/(L("Hello, welcome to %s! This %s helps you with the initial configuration; just a few settings and you will be ready to print.")))
        % SLIC3R_APP_NAME
        % /*_utf8*/(BugWizard::name())).str())
    ))
    , cbox_reset(append(
        new wxCheckBox(this, wxID_ANY, _L("Remove user profiles (a snapshot will be taken beforehand)"))
    ))
    , cbox_integrate(append(
        new wxCheckBox(this, wxID_ANY, _L("Perform desktop integration (Sets this binary to be searchable by the system)."))
    ))
{
    welcome_text->Hide();
    cbox_reset->Hide();
    cbox_integrate->Hide();    
}

void BugPageWelcome::set_run_reason(BugWizard::BugRunReason run_reason)
{
    const bool data_empty = run_reason == BugWizard::RR_DATA_EMPTY;
    welcome_text->Show(data_empty);
    cbox_reset->Show(!data_empty);
#if defined(__linux__) && defined(SLIC3R_DESKTOP_INTEGRATION)
    if (!DesktopIntegrationDialog::is_integrated())
        cbox_integrate->Show(true);
    else
        cbox_integrate->Hide();
#else
    cbox_integrate->Hide();
#endif
}


BugPagePrinters::BugPagePrinters(BugWizard *parent,
    wxString title,
    wxString shortname,
    const VendorProfile &vendor,
    unsigned indent,
    BugTechnology technology)
    : BugWizardPage(parent, std::move(title), std::move(shortname), indent)
    , technology(technology)
    , install(false)   // only used for 3rd party vendors
{
    enum {
        COL_SIZE = 200,
    };

    AppConfig *appconfig = &this->wizard_p()->appconfig_new;

    const auto families = vendor.families();
    for (const auto &family : families) {
        const auto filter = [&](const VendorProfile::PrinterModel &model) {
            return ((model.technology == ptFFF && technology & T_FFF)
                    || (model.technology == ptSLA && technology & T_SLA))
                && model.family == family;
        };

        if (std::find_if(vendor.models.begin(), vendor.models.end(), filter) == vendor.models.end()) {
            continue;
        }

        const auto picker_title = family.empty() ? wxString() : from_u8((boost::format(/*_utf8*/(L("%s Family"))) % family).str());
        auto *picker = new BugPrinterPicker(this, vendor, picker_title, MAX_COLS, *appconfig, filter);

        picker->Bind(EVT_PRINTER_PICK, [this, appconfig](const BugPrinterPickerEvent &evt) {
            appconfig->set_variant(evt.vendor_id, evt.model_id, evt.variant_name, evt.enable);
            wizard_p()->on_printer_pick(this, evt);
        });
        append(new StaticLine(this));
        append(picker);
        printer_pickers.push_back(picker);
        has_printers = true;
    }

}

void BugPagePrinters::select_all(bool select, bool alternates)
{
}

int BugPagePrinters::get_width() const
{
    return std::accumulate(printer_pickers.begin(), printer_pickers.end(), 0,
        [](int acc, const BugPrinterPicker *picker) { return std::max(acc, picker->get_width()); });
}

bool BugPagePrinters::any_selected() const
{
    for (const auto *picker : printer_pickers) {
        if (picker->any_selected()) { return true; }
    }

    return false;
}

std::set<std::string> BugPagePrinters::get_selected_models()
{
    std::set<std::string> ret_set;

    for (const auto *picker : printer_pickers)
    {
        std::set<std::string> tmp_models = picker->get_selected_models();
        ret_set.insert(tmp_models.begin(), tmp_models.end());
    }

    return ret_set;
}

void BugPagePrinters::set_run_reason(BugWizard::BugRunReason run_reason)
{
    if (is_primary_printer_page
        && (run_reason == BugWizard::RR_DATA_EMPTY || run_reason == BugWizard::RR_DATA_LEGACY)
        && printer_pickers.size() > 0 
        && printer_pickers[0]->vendor_id == PresetBundle::PRUSA_BUNDLE) {
        printer_pickers[0]->select_one(0, true);
    }
}


const std::string BugPageMaterials::EMPTY;

BugPageMaterials::BugPageMaterials(BugWizard *parent, BugMaterials *materials, wxString title, wxString shortname, wxString list1name)
    : BugWizardPage(parent, std::move(title), std::move(shortname))
    , materials(materials)
	, list_printer(new  BugStringList(this, wxLB_MULTIPLE))
    , list_type(new BugStringList(this))
    , list_vendor(new BugStringList(this))
    , list_profile(new BugPresetList(this))
{
}
void BugPageMaterials::on_paint()
{
}
void BugPageMaterials::on_mouse_move_on_profiles(wxMouseEvent& evt)
{
    const wxClientDC dc(list_profile);
    const wxPoint pos = evt.GetLogicalPosition(dc);
    int item = list_profile->HitTest(pos);
    on_material_hovered(item);
}
void BugPageMaterials::on_mouse_enter_profiles(wxMouseEvent& evt)
{}
void BugPageMaterials::on_mouse_leave_profiles(wxMouseEvent& evt)
{
    on_material_hovered(-1);
}
void BugPageMaterials::reload_presets()
{
    clear();

	list_printer->append(_L("(All)"), &EMPTY);
    //list_printer->SetLabelMarkup("<b>bald</b>");
	for (const Preset* printer : materials->printers) {
		list_printer->append(printer->name, &printer->name);
	}
    sort_list_data(list_printer, true, false);
    if (list_printer->GetCount() > 0) {
        list_printer->SetSelection(0);
        sel_printers_prev.Clear();
        sel_type_prev = wxNOT_FOUND;
        sel_vendor_prev = wxNOT_FOUND;
        update_lists(0, 0, 0);
    }

    presets_loaded = true;
}

void BugPageMaterials::set_compatible_printers_html_window(const std::vector<std::string>& printer_names, bool all_printers)
{
}

void BugPageMaterials::clear_compatible_printers_label()
{
    set_compatible_printers_html_window(std::vector<std::string>(), false);
}

void BugPageMaterials::on_material_hovered(int sel_material)
{

}

void BugPageMaterials::on_material_highlighted(int sel_material){}

void BugPageMaterials::update_lists(int sel_type, int sel_vendor, int last_selected_printer/* = -1*/){}

void BugPageMaterials::sort_list_data(BugStringList* list, bool add_All_item, bool material_type_ordering){}     

void BugPageMaterials::sort_list_data(BugPresetList* list, const std::vector<BugProfilePrintData>& data){}

void BugPageMaterials::select_material(int i){}

void BugPageMaterials::select_all(bool select){}

void BugPageMaterials::clear(){}

void BugPageMaterials::on_activate(){}


const char *BugPageCustom::default_profile_name = "My Settings";

BugPageCustom::BugPageCustom(BugWizard *parent)
    : BugWizardPage(parent, _L("Custom Printer Setup"), _L("Custom Printer"))
{}

BugPageUpdate::BugPageUpdate(BugWizard *parent)
    : BugWizardPage(parent, _L("Automatic updates"), _L("Updates"))
    , version_check(true)
    , preset_update(true)
{}

BugPageReloadFromDisk::BugPageReloadFromDisk(BugWizard* parent)
    : BugWizardPage(parent, _L("Reload from disk"), _L("Reload from disk"))
    , full_pathnames(false)
{}

#ifdef _WIN32
BugPageFilesAssociation::BugPageFilesAssociation(BugWizard* parent)
    : BugWizardPage(parent, _L("Files association"), _L("Files association"))
{
}
#endif // _WIN32

BugPageMode::BugPageMode(BugWizard *parent)
    : BugWizardPage(parent, _L("View mode"), _L("View mode"))
{
}

void BugPageMode::on_activate()
{
}

void BugPageMode::serialize_mode(AppConfig *app_config) const
{
}

BugPageVendors::BugPageVendors(BugWizard *parent)
    : BugWizardPage(parent, _L("Other Vendors"), _L("Other Vendors"))
{
}

BugPageFirmware::BugPageFirmware(BugWizard *parent)
    : BugWizardPage(parent, _L("Firmware Type"), _L("Firmware"), 1)
    , gcode_opt(*print_config_def.get("gcode_flavor"))
    , gcode_picker(nullptr)
{
}

void BugPageFirmware::apply_custom_config(DynamicPrintConfig &config)
{
}

BugPageBedShape::BugPageBedShape(BugWizard *parent)
    : BugWizardPage(parent, _L("Bed Shape and Size"), _L("Bed Shape"), 1)
    , shape_panel(new BedShapePanel(this))
{
}

void BugPageBedShape::apply_custom_config(DynamicPrintConfig &config)
{
}

static void focus_event(wxFocusEvent& e, wxTextCtrl* ctrl, double def_value) 
{
    e.Skip();
    wxString str = ctrl->GetValue();

    const char dec_sep = is_decimal_separator_point() ? '.' : ',';
    const char dec_sep_alt = dec_sep == '.' ? ',' : '.';
    // Replace the first incorrect separator in decimal number.
    bool was_replaced = str.Replace(dec_sep_alt, dec_sep, false) != 0;

    double val = 0.0;
    if (!str.ToDouble(&val)) {
        if (val == 0.0)
            val = def_value;
        ctrl->SetValue(double_to_string(val));
        show_error(nullptr, _L("Invalid numeric input."));
        ctrl->SetFocus();
    }
    else if (was_replaced)
        ctrl->SetValue(double_to_string(val));
}

class DiamTextCtrl : public wxTextCtrl
{
public:
    DiamTextCtrl(wxWindow* parent)
    {
#ifdef _WIN32
        long style = wxBORDER_SIMPLE;
#else
        long style = 0;
#endif
        Create(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(Field::def_width_thinner() * wxGetApp().em_unit(), wxDefaultCoord), style);
        wxGetApp().UpdateDarkUI(this);
    }
    ~DiamTextCtrl() {}
};

BugPageDiameters::BugPageDiameters(BugWizard *parent)
    : BugWizardPage(parent, _L("Filament and Nozzle Diameters"), _L("Print Diameters"), 1)
    , diam_nozzle(new DiamTextCtrl(this))
    , diam_filam (new DiamTextCtrl(this))
{}

void BugPageDiameters::apply_custom_config(DynamicPrintConfig &config)
{
}

class SpinCtrlDouble: public wxSpinCtrlDouble
{
public:
    SpinCtrlDouble(wxWindow* parent)
    {
#ifdef _WIN32
        long style = wxSP_ARROW_KEYS | wxBORDER_SIMPLE;
#else
        long style = wxSP_ARROW_KEYS;
#endif
        Create(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, style);
#ifdef _WIN32
        wxGetApp().UpdateDarkUI(this->GetText());
#endif
        this->Refresh();
    }
    ~SpinCtrlDouble() {}
};

BugPageTemperatures::BugPageTemperatures(BugWizard *parent)
    : BugWizardPage(parent, _L("Nozzle and Bed Temperatures"), _L("Temperatures"), 1)
    , spin_extr(new SpinCtrlDouble(this))
    , spin_bed (new SpinCtrlDouble(this))
{
}

void BugPageTemperatures::apply_custom_config(DynamicPrintConfig &config)
{
}


// Index

BugWizardIndex::BugWizardIndex(wxWindow *parent)
    : wxPanel(parent)
    , bg(ScalableBitmap(parent, "QIDISlicer_192px_transparent.png", 192))
    , bullet_black(ScalableBitmap(parent, "bullet_black.png"))
    , bullet_blue(ScalableBitmap(parent, "bullet_blue.png"))
    , bullet_white(ScalableBitmap(parent, "bullet_white.png"))
    , item_active(NO_ITEM)
    , item_hover(NO_ITEM)
    , last_page((size_t)-1)
{
#ifndef __WXOSX__ 
    SetDoubleBuffered(true);// SetDoubleBuffered exists on Win and Linux/GTK, but is missing on OSX
#endif //__WXOSX__
    SetMinSize(bg.GetSize());

    const wxSize size = GetTextExtent("m");
    em_w = size.x;
    em_h = size.y;

    Bind(wxEVT_PAINT, &BugWizardIndex::on_paint, this);
    Bind(wxEVT_SIZE, [this](wxEvent& e) { e.Skip(); Refresh(); });
    Bind(wxEVT_MOTION, &BugWizardIndex::on_mouse_move, this);

    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &evt) {
        if (item_hover != -1) {
            item_hover = -1;
            Refresh();
        }
        evt.Skip();
    });

    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &evt) {
        if (item_hover >= 0) { go_to(item_hover); }
    });
}

wxDECLARE_EVENT(EVT_INDEX_PAGE, wxCommandEvent);

void BugWizardIndex::add_page(BugWizardPage *page)
{
    last_page = items.size();
    items.emplace_back(Item { page->shortname, page->indent, page });
    Refresh();
}

void BugWizardIndex::add_label(wxString label, unsigned indent)
{
    items.emplace_back(Item { std::move(label), indent, nullptr });
    Refresh();
}

BugWizardPage* BugWizardIndex::active_page() const
{
    if (item_active >= items.size()) { return nullptr; }

    return items[item_active].page;
}

void BugWizardIndex::go_prev()
{
}

void BugWizardIndex::go_next()
{
}

// This one actually performs the go-to op
void BugWizardIndex::go_to(size_t i)
{
    if (i != item_active
        && i < items.size()
        && items[i].page != nullptr) {
        auto *new_active = items[i].page;
        auto *former_active = active_page();
        if (former_active != nullptr) {
            former_active->Hide();
        }

        item_active = i;
        new_active->Show();

        wxCommandEvent evt(EVT_INDEX_PAGE, GetId());
        AddPendingEvent(evt);

        Refresh();

        new_active->on_activate();
    }
}

void BugWizardIndex::go_to(const BugWizardPage *page)
{
    if (page == nullptr) { return; }

    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].page == page) {
            go_to(i);
            return;
        }
    }
}

void BugWizardIndex::clear()
{
    auto *former_active = active_page();
    if (former_active != nullptr) { former_active->Hide(); }

    items.clear();
    item_active = NO_ITEM;
}

void BugWizardIndex::on_paint(wxPaintEvent & evt)
{
}

void BugWizardIndex::on_mouse_move(wxMouseEvent &evt)
{
}

void BugWizardIndex::msw_rescale()
{
}


// BugMaterials

const std::string BugMaterials::UNKNOWN = "(Unknown)";

void BugMaterials::push(const Preset *preset)
{
}

void  BugMaterials::add_printer(const Preset* preset)
{
}

void BugMaterials::clear()
{
}

const std::string& BugMaterials::appconfig_section() const
{
    return (technology & T_FFF) ? AppConfig::SECTION_FILAMENTS : AppConfig::SECTION_MATERIALS;
}

const std::string& BugMaterials::get_type(const Preset *preset) const
{
    return (technology & T_FFF) ? get_filament_type(preset) : get_material_type(preset);
}

const std::string& BugMaterials::get_vendor(const Preset *preset) const
{
    return (technology & T_FFF) ? get_filament_vendor(preset) : get_material_vendor(preset);
}

const std::string& BugMaterials::get_filament_type(const Preset *preset)
{
    const auto *opt = preset->config.opt<ConfigOptionStrings>("filament_type");
    if (opt != nullptr && opt->values.size() > 0) {
        return opt->values[0];
    } else {
        return UNKNOWN;
    }
}

const std::string& BugMaterials::get_filament_vendor(const Preset *preset)
{
    const auto *opt = preset->config.opt<ConfigOptionString>("filament_vendor");
    return opt != nullptr ? opt->value : UNKNOWN;
}

const std::string& BugMaterials::get_material_type(const Preset *preset)
{
    const auto *opt = preset->config.opt<ConfigOptionString>("material_type");
    if (opt != nullptr) {
        return opt->value;
    } else {
        return UNKNOWN;
    }
}

const std::string& BugMaterials::get_material_vendor(const Preset *preset)
{
    const auto *opt = preset->config.opt<ConfigOptionString>("material_vendor");
    return opt != nullptr ? opt->value : UNKNOWN;
}

// priv

static const std::unordered_map<std::string, std::pair<std::string, std::string>> legacy_preset_map {{
    { "Original QIDI i3 MK2.ini",                           std::make_pair("MK2S", "0.4") },
    { "Original QIDI i3 MK2 MM Single Mode.ini",            std::make_pair("MK2SMM", "0.4") },
    { "Original QIDI i3 MK2 MM Single Mode 0.6 nozzle.ini", std::make_pair("MK2SMM", "0.6") },
    { "Original QIDI i3 MK2 MultiMaterial.ini",             std::make_pair("MK2SMM", "0.4") },
    { "Original QIDI i3 MK2 MultiMaterial 0.6 nozzle.ini",  std::make_pair("MK2SMM", "0.6") },
    { "Original QIDI i3 MK2 0.25 nozzle.ini",               std::make_pair("MK2S", "0.25") },
    { "Original QIDI i3 MK2 0.6 nozzle.ini",                std::make_pair("MK2S", "0.6") },
    { "Original QIDI i3 MK3.ini",                           std::make_pair("MK3",  "0.4") },
}};

void BugWizard::priv::load_pages()
{
    wxWindowUpdateLocker freeze_guard(q);
    (void)freeze_guard;

    const BugWizardPage *former_active = index->active_page();

    index->clear();

   

    // Printers
    index->add_page(page_fff);


    //index->go_to(former_active);   // Will restore the active item/page if possible

    q->Layout();
// This Refresh() is needed to avoid ugly artifacts after printer selection, when no one vendor was selected from the very beginnig
    q->Refresh();
}

void BugWizard::priv::init_dialog_size()
{
    const auto idx = wxDisplay::GetFromWindow(q);
    wxDisplay display(idx != wxNOT_FOUND ? idx : 0u);

    const auto disp_rect = display.GetClientArea();
    wxRect window_rect(
        disp_rect.x + disp_rect.width / 20,
        disp_rect.y + disp_rect.height / 20,
        9*disp_rect.width / 10,
        9*disp_rect.height / 10);

    const int width_hint = index->GetSize().GetWidth() + std::max(0, (only_sla_mode ? page_msla->get_width() : page_fff->get_width()) + 35 * em());    // XXX: magic constant, I found no better solution
    if (width_hint < window_rect.width) {
        window_rect.x += (window_rect.width - width_hint) / 2;
        window_rect.width = width_hint;
    }

    q->SetSize(window_rect);
    //q->SetMaxSize(wxSize(width_hint, 9 * disp_rect.height / 10));
}

void BugWizard::priv::load_vendors()
{
    bundles = BugBundleMap::load();

    // Load up the set of vendors / models / variants the user has had enabled up till now
    AppConfig *app_config = wxGetApp().app_config;
    if (! app_config->legacy_datadir()) {
        appconfig_new.set_vendors(*app_config);
    } else {
        // In case of legacy datadir, try to guess the preference based on the printer preset files that are present
        const auto printer_dir = fs::path(Slic3r::data_dir()) / "printer";
        for (auto &dir_entry : boost::filesystem::directory_iterator(printer_dir))
            if (Slic3r::is_ini_file(dir_entry)) {
                auto needle = legacy_preset_map.find(dir_entry.path().filename().string());
                if (needle == legacy_preset_map.end()) { continue; }

                const auto &model = needle->second.first;
                const auto &variant = needle->second.second;
                appconfig_new.set_variant("QIDIResearch", model, variant, true);
            }
        /*for (auto &dir_entry : boost::filesystem::directory_iterator(printer_dir))
            if (Slic3r::is_ini_file(dir_entry)) {
                auto needle = legacy_preset_map.find(dir_entry.path().filename().string());
                if (needle == legacy_preset_map.end()) { continue; }

                const auto &model = needle->second.first;
                const auto &skype = needle->second.second;
                appconfig_new.set_skype("QIDIResearch", model, skype, true);
            }*/
    }

    // Initialize the is_visible flag in printer Presets
    for (auto &pair : bundles) {
        pair.second.preset_bundle->load_installed_printers(appconfig_new);
    }

    // Copy installed filaments and SLA material names from app_config to appconfig_new
    // while resolving current names of profiles, which were renamed in the meantime.
    for (PrinterTechnology technology : { ptFFF, ptSLA }) {
    	const std::string &section_name = (technology == ptFFF) ? AppConfig::SECTION_FILAMENTS : AppConfig::SECTION_MATERIALS;
		std::map<std::string, std::string> section_new;
		if (app_config->has_section(section_name)) {
			const std::map<std::string, std::string> &section_old = app_config->get_section(section_name);
            for (const auto& material_name_and_installed : section_old)
				if (material_name_and_installed.second == "1") {
					// Material is installed. Resolve it in bundles.
                    size_t num_found = 0;
					const std::string &material_name = material_name_and_installed.first;
				    for (auto &bundle : bundles) {
				    	const PresetCollection &materials = bundle.second.preset_bundle->materials(technology);
				    	const Preset           *preset    = materials.find_preset(material_name);
				    	if (preset == nullptr) {
				    		// Not found. Maybe the material preset is there, bu it was was renamed?
							const std::string *new_name = materials.get_preset_name_renamed(material_name);
							if (new_name != nullptr)
								preset = materials.find_preset(*new_name);
				    	}
                        if (preset != nullptr) {
                            // Materal preset was found, mark it as installed.
                            section_new[preset->name] = "1";
                            ++ num_found;
                        }
				    }
                    if (num_found == 0)
            	        BOOST_LOG_TRIVIAL(error) << boost::format("Profile %1% was not found in installed vendor Preset BugBundles.") % material_name;
                    else if (num_found > 1)
            	        BOOST_LOG_TRIVIAL(error) << boost::format("Profile %1% was found in %2% vendor Preset BugBundles.") % material_name % num_found;
                }
		}
        appconfig_new.set_section(section_name, section_new);
    };
}

void BugWizard::priv::add_page(BugWizardPage *page)
{
    const int proportion = (page->shortname == _L("Filaments")) || (page->shortname == _L("SLA BugMaterials")) ? 1 : 0;
    hscroll_sizer->Add(page, proportion, wxEXPAND);
    all_pages.push_back(page);
}

void BugWizard::priv::enable_next(bool enable)
{
    btn_next->Enable(enable);
    btn_finish->Enable(enable);
}

void BugWizard::priv::set_start_page(BugWizard::BugStartPage start_page)
{
    switch (start_page) {
        case BugWizard::SP_PRINTERS: 
            index->go_to(page_fff); 
            break;
        default:
            index->go_to(page_fff);
            break;
    }
}

void BugWizard::priv::create_3rdparty_pages(){}

void BugWizard::priv::set_run_reason(BugRunReason run_reason)
{
    this->run_reason = run_reason;
    for (auto &page : all_pages) {
        page->set_run_reason(run_reason);
    }
}

void BugWizard::priv::update_materials(BugTechnology technology){}

void BugWizard::priv::on_custom_setup(const bool custom_wanted){}

void BugWizard::priv::on_printer_pick(BugPagePrinters *page, const BugPrinterPickerEvent &evt){}

void BugWizard::priv::select_default_materials_for_printer_model(const VendorProfile::PrinterModel &printer_model, BugTechnology technology){}

void BugWizard::priv::select_default_materials_for_printer_models(BugTechnology technology, const std::set<const VendorProfile::PrinterModel*> &printer_models){}

void BugWizard::priv::on_3rdparty_install(const VendorProfile *vendor, bool install){}

bool BugWizard::priv::on_bnt_finish()
{
    return check_and_install_missing_materials(T_ANY);
}

// This allmighty method verifies, whether there is at least a single compatible filament or SLA material installed
// for each Printer preset of each Printer Model installed.
//
// In case only_for_model_id is set, then the test is done for that particular printer model only, and the default materials are installed silently.
// Otherwise the user is quieried whether to install the missing default materials or not.
// 
// Return true if the tested Printer Models already had materials installed.
// Return false if there were some Printer Models with missing materials, independent from whether the defaults were installed for these
// respective Printer Models or not.
bool BugWizard::priv::check_and_install_missing_materials(BugTechnology technology, const std::string &only_for_model_id)
{
    return true;
}

bool BugWizard::priv::apply_config(AppConfig *app_config, PresetBundle *preset_bundle, const PresetUpdater *updater, bool& apply_keeped_changes)
{
    return true;
}
void BugWizard::priv::update_presets_in_config(const std::string& section, const std::string& alias_key, bool add)
{
    const BugPresetAliases& aliases = section == AppConfig::SECTION_FILAMENTS ? aliases_fff : aliases_sla;

    auto update = [this, add](const std::string& s, const std::string& key) {
    	assert(! s.empty());
        if (add)
            appconfig_new.set(s, key, "1");
        else
            appconfig_new.erase(s, key); 
    };

    // add or delete presets had a same alias 
    auto it = aliases.find(alias_key);
    if (it != aliases.end())
        for (const std::string& name : it->second)
            update(section, name);
}

bool BugWizard::priv::check_fff_selected()
{
    bool ret = page_fff->any_selected();
    for (const auto& printer: pages_3rdparty)
        if (printer.second.first)               // FFF page
            ret |= printer.second.first->any_selected();
    return ret;
}

bool BugWizard::priv::check_sla_selected()
{
    bool ret = page_msla->any_selected();
    for (const auto& printer: pages_3rdparty)
        if (printer.second.second)               // SLA page
            ret |= printer.second.second->any_selected();
    return ret;
}


// Public

BugWizard::BugWizard(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, wxString(SLIC3R_APP_NAME) + " - " + _(name()), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , p(new priv(this))
{
    this->SetFont(wxGetApp().normal_font());

    p->load_vendors();
    p->custom_config.reset(DynamicPrintConfig::new_from_defaults_keys({
        "gcode_flavor", "bed_shape", "bed_custom_texture", "bed_custom_model", "nozzle_diameter", "filament_diameter", "temperature", "bed_temperature",
    }));

    p->index = new BugWizardIndex(this);

    auto *vsizer = new wxBoxSizer(wxVERTICAL);
    auto *topsizer = new wxBoxSizer(wxHORIZONTAL);
    auto* hline = new StaticLine(this);
    p->btnsizer = new wxBoxSizer(wxHORIZONTAL);

    // Initially we _do not_ SetScrollRate in order to figure out the overall width of the Wizard  without scrolling.
    // Later, we compare that to the size of the current screen and set minimum width based on that (see below).
    p->hscroll = new wxScrolledWindow(this);
    p->hscroll_sizer = new wxBoxSizer(wxHORIZONTAL);
    p->hscroll->SetSizer(p->hscroll_sizer);

    
    topsizer->AddSpacer(2*DIALOG_MARGIN);
    topsizer->Add(p->hscroll, 1, wxEXPAND);

    p->btn_cancel = new wxButton(this, wxID_ANY, _L("显示配置文件")); // Note: The label needs to be present, otherwise we get accelerator bugs on Mac
    p->btn_cancel->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &) {
        Slic3r::GUI::desktop_open_datadir_folder();
    });
    p->btnsizer->AddStretchSpacer();
    p->btnsizer->Add(p->btn_cancel, 0, wxLEFT, BTN_SPACING);

    wxGetApp().UpdateDarkUI(p->btn_cancel);

    const auto qidi_it = p->bundles.find("QIDITechnology");
    wxCHECK_RET(qidi_it != p->bundles.cend(), "Vendor QIDITechnology not found");
    const VendorProfile *vendor_qidi = qidi_it->second.vendor_profile;

    p->add_page(p->page_welcome = new BugPageWelcome(this));

    
    p->page_fff = new BugPagePrinters(this, _L("QIDI FFF Technology Printers"), "QIDI FFF", *vendor_qidi, 0, T_FFF);
    p->only_sla_mode = !p->page_fff->has_printers;
    if (!p->only_sla_mode) {
        p->add_page(p->page_fff);
        p->page_fff->is_primary_printer_page = true;
    }

    p->load_pages();
    p->index->go_to(size_t{0});

    // head_label
    {
        wxStaticText* head_label = new wxStaticText(this, wxID_ANY, "Printer after-sales email", wxDefaultPosition, wxDefaultSize);
        wxFont head_label_font = GUI::wxGetApp().bold_font();
        head_label->SetForegroundColour(wxColour(68, 121, 251));
        head_label_font.SetFamily(wxFONTFAMILY_ROMAN);
        head_label_font.SetPointSize(24);
        head_label->SetFont(head_label_font);
        vsizer->Add(head_label, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 10);
    }
    // question
    {
        auto question_string = _L("If you have any questions or questions about the printer, please contact us via the appropriate email address or Skype.");
        wxStaticText* question = new wxStaticText(this, wxID_ANY, question_string.c_str(), wxDefaultPosition, wxDefaultSize);
        wxFont question_font = GetFont().Scaled(1.2f);
        #ifdef __WXMSW__
        question_font.SetPointSize(question_font.GetPointSize() - 1);
        #else
            question_font.SetPointSize(11);
        #endif
        question->SetFont(question_font);
        vsizer->Add(question, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);
    }

    vsizer->Add(topsizer, 1, wxEXPAND | wxTOP, 0);
    vsizer->Add(hline, 0, wxEXPAND | wxLEFT | wxRIGHT, VERTICAL_SPACING);
    // note
    {
        auto note_string = _L("Note: please try to tell us your requirements in the form of video or pictures, and provide 3MF file, G-code file, machine number and other necessary information");
        wxStaticText* note = new wxStaticText(this, wxID_ANY, note_string.c_str(), wxDefaultPosition, wxDefaultSize);
        wxFont note_font = GetFont().Scaled(1.2f);
        #ifdef __WXMSW__
        note_font.SetPointSize(note_font.GetPointSize() - 1);
        #else
            note_font.SetPointSize(11);
        #endif
        note->SetFont(note_font);
        const int wrap_width = p->page_fff->get_width();
        note->Wrap(wrap_width*5/3);
        note->SetForegroundColour(*wxRED);
        vsizer->Add(note, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 0);
    }
    vsizer->Add(p->btnsizer, 0, wxEXPAND | wxALL, DIALOG_MARGIN);
    SetSizer(vsizer);
    SetSizerAndFit(vsizer);

    // We can now enable scrolling on hscroll
    p->hscroll->SetScrollRate(30, 30);

    on_window_geometry(this, [this]() {
        p->init_dialog_size();
    });


    if (wxLinux_gtk3)
        this->Bind(wxEVT_SHOW, [this, vsizer](const wxShowEvent& e) {
            BugWizardPage* active_page = p->index->active_page();
            if (!active_page)
                return;
            for (auto page : p->all_pages)
                if (page != active_page)
                    page->Hide();
            // update best size for the dialog after hiding of the non-active pages
            vsizer->SetSizeHints(this);
            // set initial dialog size
            p->init_dialog_size();
        });
}

BugWizard::~BugWizard() {}

bool BugWizard::run(BugRunReason reason, BugStartPage start_page)
{
    BOOST_LOG_TRIVIAL(info) << boost::format("Running BugWizard, reason: %1%, start_page: %2%") % reason % start_page;

    GUI_App &app = wxGetApp();

    p->set_run_reason(reason);
    p->set_start_page(start_page);

    if (ShowModal() == wxID_OK) {
        bool apply_keeped_changes = false;
        if (! p->apply_config(app.app_config, app.preset_bundle, app.preset_updater, apply_keeped_changes))
            return false;

        if (apply_keeped_changes)
            app.apply_keeped_preset_modifications();

        app.app_config->set_legacy_datadir(false);
        app.update_mode();
        app.obj_manipul()->update_ui_from_settings();
        BOOST_LOG_TRIVIAL(info) << "BugWizard applied";
        return true;
    } else {
        BOOST_LOG_TRIVIAL(info) << "BugWizard cancelled";
        return false;
    }
}

const wxString& BugWizard::name(const bool from_menu/* = false*/)
{
    // A different naming convention is used for the Wizard on Windows & GTK vs. OSX.
    // Note: Don't call _() macro here.
    //       This function just return the current name according to the OS.
    //       Translation is implemented inside GUI_App::add_config_menu()
#if __APPLE__
    static const wxString config_wizard_name =  L("Configuration Assistant");
    static const wxString config_wizard_name_menu = L("Configuration &Assistant");
#else
    static const wxString config_wizard_name = L("Configuration Wizard");
    static const wxString config_wizard_name_menu = L("Configuration &Wizard");
#endif
    return from_menu ? config_wizard_name_menu : config_wizard_name;
}

void BugWizard::on_dpi_changed(const wxRect &suggested_rect)
{
    p->index->msw_rescale();

    const int em = em_unit();

    msw_buttons_rescale(this, em, { wxID_APPLY, 
                                    wxID_CANCEL,
                                    p->btn_sel_all->GetId(),
                                    p->btn_next->GetId(),
                                    p->btn_prev->GetId() });

    for (auto printer_picker: p->page_fff->printer_pickers)
        msw_buttons_rescale(this, em, printer_picker->get_button_indexes());

    p->init_dialog_size();

    Refresh();
}

void BugWizard::on_sys_color_changed()
{
    wxGetApp().UpdateDlgDarkUI(this);
    Refresh();
}

}
}
