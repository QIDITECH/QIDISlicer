#ifndef slic3r_BugWizard_private_hpp_
#define slic3r_BugWizard_private_hpp_

#include "BugWizard.hpp"

#include <vector>
#include <set>
#include <unordered_map>
#include <functional>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <wx/panel.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/spinctrl.h>
#include <wx/listbox.h>
#include <wx/checklst.h>
#include <wx/radiobut.h>
#include <wx/html/htmlwin.h>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/Utils/PresetUpdater.hpp"
#include "BedShapeDialog.hpp"
#include "GUI.hpp"
#include "SavePresetDialog.hpp"
#include "wxExtensions.hpp"


namespace fs = boost::filesystem;

namespace Slic3r {
namespace GUI {

enum {
    WRAP_WIDTH = 500,
    MODEL_MIN_WRAP = 150,

    DIALOG_MARGIN = 15,
    INDEX_MARGIN = 40,
    BTN_SPACING = 10,
    INDENT_SPACING = 30,
    VERTICAL_SPACING = 10,

    MAX_COLS = 4,
    ROW_SPACING = 75,
};



// Configuration data structures extensions needed for the wizard

enum BugTechnology {
    // Bitflag equivalent of PrinterBugTechnology
    T_FFF = 0x1,
    T_SLA = 0x2,
    T_ANY = ~0,
};
enum BugBundleLocation{
    IN_VENDOR,
    IN_ARCHIVE,
    IN_RESOURCES
};

struct BugBundle
{
    std::unique_ptr<PresetBundle> preset_bundle;
	VendorProfile* vendor_profile{ nullptr };
	//bool is_in_resources{ false };
    BugBundleLocation location;
	bool is_qidi_bundle{ false };

	BugBundle() = default;
    BugBundle(BugBundle &&other);

	// Returns false if not loaded. Reason for that is logged as boost::log error.
    bool load(fs::path source_path, BugBundleLocation location, bool is_qidi_bundle = false);

	const std::string& vendor_id() const { return vendor_profile->id; }
};

struct BugBundleMap : std::unordered_map<std::string /* = vendor ID */, BugBundle>
{
	static BugBundleMap load();

	BugBundle& qidi_bundle();
	const BugBundle& qidi_bundle() const;
};

struct BugMaterials
{
    BugTechnology technology;
    // use vector for the presets to purpose of save of presets sorting in the bundle
    std::vector<const Preset*> presets;
    // String is alias of material, size_t number of compatible counters 
    std::vector<std::pair<std::string, size_t>> compatibility_counter;
    std::set<std::string> types;
	std::set<const Preset*> printers;

    BugMaterials(BugTechnology technology) : technology(technology) {}

    void push(const Preset *preset);
	void add_printer(const Preset* preset);
    void clear();
    bool containts(const Preset *preset) const {
        //return std::find(presets.begin(), presets.end(), preset) != presets.end(); 
		return std::find_if(presets.begin(), presets.end(),
			[preset](const Preset* element) { return element == preset; }) != presets.end();

    }
	
	bool get_omnipresent(const Preset* preset) {
		return get_printer_counter(preset) == printers.size();
	}

    const std::vector<const Preset*> get_presets_by_alias(const std::string name) {
        std::vector<const Preset*> ret_vec;
        for (auto it = presets.begin(); it != presets.end(); ++it) {
            if ((*it)->alias == name)
                ret_vec.push_back((*it));
        }
        return ret_vec;
    }

	

	size_t get_printer_counter(const Preset* preset) {
		for (auto it : compatibility_counter) {
			if (it.first == preset->alias)
                return it.second;
        }
		return 0;
	}

    const std::string& appconfig_section() const;
    const std::string& get_type(const Preset *preset) const;
    const std::string& get_vendor(const Preset *preset) const;
	
	template<class F> void filter_presets(const Preset* printer, const std::string& type, const std::string& vendor, F cb) {
		for (auto preset : presets) {
			const Preset& prst = *(preset);
			const Preset& prntr = *printer;
		      if ((printer == nullptr || is_compatible_with_printer(PresetWithVendorProfile(prst, prst.vendor), PresetWithVendorProfile(prntr, prntr.vendor))) &&
			    (type.empty() || get_type(preset) == type) &&
				(vendor.empty() || get_vendor(preset) == vendor)) {

				cb(preset);
			}
		}
	}

    static const std::string UNKNOWN;
    static const std::string& get_filament_type(const Preset *preset);
    static const std::string& get_filament_vendor(const Preset *preset);
    static const std::string& get_material_type(const Preset *preset);
    static const std::string& get_material_vendor(const Preset *preset);
};


struct BugPrinterPickerEvent;

// GUI elements

typedef std::function<bool(const VendorProfile::PrinterModel&)> BugModelFilter;

struct BugPrinterPicker: wxPanel
{
    struct Checkbox : wxCheckBox
    {
        Checkbox(wxWindow *parent, const wxString &label, const std::string &model, const std::string &variant) :
            wxCheckBox(parent, wxID_ANY, label),
            model(model),
            variant(variant)
        {}

        std::string model;
        std::string variant;
    };

    const std::string vendor_id;
    std::vector<Checkbox*> cboxes;
    std::vector<Checkbox*> cboxes_alt;

    BugPrinterPicker(wxWindow *parent, const VendorProfile &vendor, wxString title, size_t max_cols, const AppConfig &appconfig, const BugModelFilter &filter);
    BugPrinterPicker(wxWindow *parent, const VendorProfile &vendor, wxString title, size_t max_cols, const AppConfig &appconfig);

    void select_all(bool select, bool alternates = false);
    void select_one(size_t i, bool select);
    bool any_selected() const;
    std::set<std::string> get_selected_models() const ;

    int get_width() const { return width; }
    const std::vector<int>& get_button_indexes() { return m_button_indexes; }

    static const std::string PRINTER_PLACEHOLDER;
private:
    int width;
    std::vector<int> m_button_indexes;

    void on_checkbox(const Checkbox *cbox, bool checked);
};

struct BugWizardPage: wxPanel
{
    BugWizard *parent;
    const wxString shortname;
    wxBoxSizer *content;
    const unsigned indent;

    BugWizardPage(BugWizard *parent, wxString title, wxString shortname, unsigned indent = 0);
    virtual ~BugWizardPage();

    template<class T>
    T* append(T *thing, int proportion = 0, int flag = wxEXPAND|wxTOP|wxBOTTOM, int border = 10)
    {
        content->Add(thing, proportion, flag, border);
        return thing;
    }

    wxStaticText* append_text(wxString text);
    void append_spacer(int space);

    BugWizard::priv *wizard_p() const { return parent->p.get(); }

    virtual void apply_custom_config(DynamicPrintConfig &config) {}
    virtual void set_run_reason(BugWizard::BugRunReason run_reason) {}
    virtual void on_activate() {}
};

struct BugPageWelcome: BugWizardPage
{
    wxStaticText *welcome_text;
    wxCheckBox *cbox_reset;
    wxCheckBox *cbox_integrate;

    BugPageWelcome(BugWizard *parent);

    bool reset_user_profile() const { return cbox_reset != nullptr ? cbox_reset->GetValue() : false; }
    bool integrate_desktop() const { return cbox_integrate != nullptr ? cbox_integrate->GetValue() : false; }

    virtual void set_run_reason(BugWizard::BugRunReason run_reason) override;
};

struct BugPagePrinters: BugWizardPage
{
    std::vector<BugPrinterPicker *> printer_pickers;
    BugTechnology technology;
    bool install;

    BugPagePrinters(BugWizard *parent,
        wxString title,
        wxString shortname,
        const VendorProfile &vendor,
        unsigned indent, BugTechnology technology);

    void select_all(bool select, bool alternates = false);
    int get_width() const;
    bool any_selected() const;
    std::set<std::string> get_selected_models();

    std::string get_vendor_id() const { return printer_pickers.empty() ? "" : printer_pickers[0]->vendor_id; }

    virtual void set_run_reason(BugWizard::BugRunReason run_reason) override;

    bool has_printers { false };
    bool is_primary_printer_page { false };
};

// Here we extend wxListBox and wxCheckListBox
// to make the client data API much easier to use.
template<class T, class D> struct DataList : public T
{
    DataList(wxWindow *parent) : T(parent, wxID_ANY) {}
	DataList(wxWindow* parent, int style) : T(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, NULL, style) {}

    // Note: We're _not_ using wxLB_SORT here because it doesn't do the right thing,
    // eg. "ABS" is sorted before "(All)"

    int append(const std::string &label, const D *data) {
        void *ptr = reinterpret_cast<void*>(const_cast<D*>(data));
        return this->Append(from_u8(label), ptr);
    }

    int append(const wxString &label, const D *data) {
        void *ptr = reinterpret_cast<void*>(const_cast<D*>(data));
        return this->Append(label, ptr);
    }

    const D& get_data(int n) {
        return *reinterpret_cast<const D*>(this->GetClientData(n));
    }

    int find(const D &data) {
        for (unsigned i = 0; i < this->GetCount(); i++) {
            if (get_data(i) == data) { return i; }
        }

        return wxNOT_FOUND;
    }

    int size() { return this->GetCount(); }

    void on_mouse_move(const wxPoint& position) {
        int item = T::HitTest(position);
       
        if(item == wxHitTest::wxHT_WINDOW_INSIDE)
            BOOST_LOG_TRIVIAL(error) << "hit test wxHT_WINDOW_INSIDE";
        else if (item == wxHitTest::wxHT_WINDOW_OUTSIDE)
            BOOST_LOG_TRIVIAL(error) << "hit test wxHT_WINDOW_OUTSIDE";
        else if(item == wxHitTest::wxHT_WINDOW_CORNER)
            BOOST_LOG_TRIVIAL(error) << "hit test wxHT_WINDOW_CORNER";
        else if (item == wxHitTest::wxHT_WINDOW_VERT_SCROLLBAR)
            BOOST_LOG_TRIVIAL(error) << "hit test wxHT_WINDOW_VERT_SCROLLBAR";
       else if (item == wxHitTest::wxHT_NOWHERE)
            BOOST_LOG_TRIVIAL(error) << "hit test wxHT_NOWHERE";
       else if (item == wxHitTest::wxHT_MAX)
            BOOST_LOG_TRIVIAL(error) << "hit test wxHT_MAX";
       else
            BOOST_LOG_TRIVIAL(error) << "hit test: " << item;
    }
};

typedef DataList<wxListBox, std::string> BugStringList;
typedef DataList<wxCheckListBox, std::string> BugPresetList;

struct BugProfilePrintData
{
    std::reference_wrapper<const std::string> name;
    bool omnipresent;
    bool checked;
    BugProfilePrintData(const std::string& n, bool o, bool c) : name(n), omnipresent(o), checked(c) {}
};

struct BugPageMaterials: BugWizardPage
{
    BugMaterials *materials;
    BugStringList *list_printer, *list_type, *list_vendor;
    BugPresetList *list_profile;
    wxArrayInt sel_printers_prev;
    int sel_type_prev, sel_vendor_prev;
    bool presets_loaded;

    wxFlexGridSizer *grid;
    wxHtmlWindow* html_window;

    int compatible_printers_width = { 100 };
    std::string empty_printers_label;
    bool first_paint = { false };
    static const std::string EMPTY;
    int last_hovered_item = { -1 } ;

    BugPageMaterials(BugWizard *parent, BugMaterials *materials, wxString title, wxString shortname, wxString list1name);

    void reload_presets();
	void update_lists(int sel_type, int sel_vendor, int last_selected_printer = -1);
	void on_material_highlighted(int sel_material);
    void on_material_hovered(int sel_material);
    void select_material(int i);
    void select_all(bool select);
    void clear();
    void set_compatible_printers_html_window(const std::vector<std::string>& printer_names, bool all_printers = false);
    void clear_compatible_printers_label();

    void sort_list_data(BugStringList* list, bool add_All_item, bool material_type_ordering);
    void sort_list_data(BugPresetList* list, const std::vector<BugProfilePrintData>& data);

    void on_paint();
    void on_mouse_move_on_profiles(wxMouseEvent& evt);
    void on_mouse_enter_profiles(wxMouseEvent& evt);
    void on_mouse_leave_profiles(wxMouseEvent& evt);
    virtual void on_activate() override;
};

struct BugPageCustom: BugWizardPage
{
    BugPageCustom(BugWizard *parent);

    bool custom_wanted() const { return cb_custom->GetValue(); }
    std::string profile_name() const { return into_u8(tc_profile_name->GetValue()); }

private:
    static const char* default_profile_name;

    wxCheckBox *cb_custom;
    wxTextCtrl *tc_profile_name;
    wxString profile_name_prev;

};

struct BugPageUpdate: BugWizardPage
{
    bool version_check;
    bool preset_update;

    BugPageUpdate(BugWizard *parent);
};

struct BugPageReloadFromDisk : BugWizardPage
{
    bool full_pathnames;

    BugPageReloadFromDisk(BugWizard* parent);
};

#ifdef _WIN32
struct BugPageFilesAssociation : BugWizardPage
{
private:
    wxCheckBox* cb_3mf{ nullptr };
    wxCheckBox* cb_stl{ nullptr };
//    wxCheckBox* cb_gcode;

public:
    BugPageFilesAssociation(BugWizard* parent);

    bool associate_3mf() const { return cb_3mf->IsChecked(); }
    bool associate_stl() const { return cb_stl->IsChecked(); }
//    bool associate_gcode() const { return cb_gcode->IsChecked(); }
};
#endif // _WIN32

struct BugPageMode: BugWizardPage
{
    wxRadioButton *radio_simple;
    wxRadioButton *radio_advanced;
    wxRadioButton *radio_expert;

    wxCheckBox    *check_inch;

    BugPageMode(BugWizard *parent);

    void serialize_mode(AppConfig *app_config) const;

    virtual void on_activate();
};

struct BugPageVendors: BugWizardPage
{
    BugPageVendors(BugWizard *parent);
};

struct BugPageFirmware: BugWizardPage
{
    const ConfigOptionDef &gcode_opt;
    wxChoice *gcode_picker;

    BugPageFirmware(BugWizard *parent);
    virtual void apply_custom_config(DynamicPrintConfig &config);
};

struct BugPageBedShape: BugWizardPage
{
    BedShapePanel *shape_panel;

    BugPageBedShape(BugWizard *parent);
    virtual void apply_custom_config(DynamicPrintConfig &config);
};

struct BugPageDiameters: BugWizardPage
{
    wxTextCtrl *diam_nozzle;
    wxTextCtrl *diam_filam;

    BugPageDiameters(BugWizard *parent);
    virtual void apply_custom_config(DynamicPrintConfig &config);
};

struct BugPageTemperatures: BugWizardPage
{
    wxSpinCtrlDouble *spin_extr;
    wxSpinCtrlDouble *spin_bed;

    BugPageTemperatures(BugWizard *parent);
    virtual void apply_custom_config(DynamicPrintConfig &config);
};

// hypothetically, each vendor can has printers both of technologies (FFF and SLA)
typedef std::map<std::string /* = vendor ID */, 
                 std::pair<BugPagePrinters* /* = FFF page */, 
                           BugPagePrinters* /* = SLA page */>> BugPages3rdparty;


class BugWizardIndex: public wxPanel
{
public:
    BugWizardIndex(wxWindow *parent);

    void add_page(BugWizardPage *page);
    void add_label(wxString label, unsigned indent = 0);

    size_t active_item() const { return item_active; }
    BugWizardPage* active_page() const;
    bool active_is_last() const { return item_active < items.size() && item_active == last_page; }

    void go_prev();
    void go_next();
    void go_to(size_t i);
    void go_to(const BugWizardPage *page);

    void clear();
    void msw_rescale();

    int em() const { return em_w; }

    static const size_t NO_ITEM = size_t(-1);
private:
    struct Item
    {
        wxString label;
        unsigned indent;
        BugWizardPage *page;     // nullptr page => label-only item

        bool operator==(BugWizardPage *page) const { return this->page == page; }
    };

    int em_w;
    int em_h;
    ScalableBitmap bg;
    ScalableBitmap bullet_black;
    ScalableBitmap bullet_blue;
    ScalableBitmap bullet_white;

    std::vector<Item> items;
    size_t item_active;
    ssize_t item_hover;
    size_t last_page;

    int item_height() const { return std::max(bullet_black.GetHeight(), em_w) + em_w; }
    //int item_height() const { return std::max(bullet_black.bmp().GetSize().GetHeight(), em_w) + em_w; }

    void on_paint(wxPaintEvent &evt);
    void on_mouse_move(wxMouseEvent &evt);
};

wxDEFINE_EVENT(EVT_INDEX_PAGE, wxCommandEvent);



// BugWizard private data

typedef std::map<std::string, std::set<std::string>> BugPresetAliases;

struct BugWizard::priv
{
    BugWizard *q;
    BugWizard::BugRunReason run_reason = RR_USER;
    AppConfig appconfig_new;      // Backing for vendor/model/variant and material selections in the GUI
    BugBundleMap bundles;            // Holds all loaded config bundles, the key is the vendor names.
                                  // BugMaterials refers to Presets in those bundles by pointers.
                                  // Also we update the is_visible flag in printer Presets according to the
                                  // BugPrinterPickers state.
    BugMaterials filaments;          // Holds available filament presets and their types & vendors
    BugMaterials sla_materials;      // Ditto for SLA materials
    BugPresetAliases aliases_fff;    // Map of aliase to preset names
    BugPresetAliases aliases_sla;    // Map of aliase to preset names
    std::unique_ptr<DynamicPrintConfig> custom_config;           // Backing for custom printer definition
    bool any_fff_selected;        // Used to decide whether to display Filaments page
    bool any_sla_selected;        // Used to decide whether to display SLA BugMaterials page
    bool custom_printer_selected { false }; 
    // Set to true if there are none FFF printers on the main FFF page. If true, only SLA printers are shown (not even custum printers)
    bool only_sla_mode { false };

    wxScrolledWindow *hscroll = nullptr;
    wxBoxSizer *hscroll_sizer = nullptr;
    wxBoxSizer *btnsizer = nullptr;
    BugWizardPage *page_current = nullptr;
    BugWizardIndex *index = nullptr;
    wxButton *btn_sel_all = nullptr;
    wxButton *btn_prev = nullptr;
    wxButton *btn_next = nullptr;
    wxButton *btn_finish = nullptr;
    wxButton *btn_cancel = nullptr;
    wxStaticText *head_label = nullptr;

    BugPageWelcome      *page_welcome = nullptr;
    BugPagePrinters     *page_fff = nullptr;
    BugPagePrinters     *page_msla = nullptr;
    BugPageMaterials    *page_filaments = nullptr;
    BugPageMaterials    *page_sla_materials = nullptr;
    BugPageCustom       *page_custom = nullptr;
    BugPageUpdate       *page_update = nullptr;
    BugPageReloadFromDisk *page_reload_from_disk = nullptr;
#ifdef _WIN32
    BugPageFilesAssociation* page_files_association = nullptr;
#endif // _WIN32
    BugPageMode         *page_mode = nullptr;
    BugPageVendors      *page_vendors = nullptr;
    BugPages3rdparty     pages_3rdparty;

    // Custom setup pages
    BugPageFirmware     *page_firmware = nullptr;
    BugPageBedShape     *page_bed = nullptr;
    BugPageDiameters    *page_diams = nullptr;
    BugPageTemperatures *page_temps = nullptr;

    // Pointers to all pages (regardless or whether currently part of the BugWizardIndex)
    std::vector<BugWizardPage*> all_pages;

    priv(BugWizard *q)
        : q(q)
        , appconfig_new(AppConfig::EAppMode::Editor)
        , filaments(T_FFF)
        , sla_materials(T_SLA)
    {}

    void load_pages();
    void init_dialog_size();

    void load_vendors();
    void add_page(BugWizardPage *page);
    void enable_next(bool enable);
    void set_start_page(BugWizard::BugStartPage start_page);
    void create_3rdparty_pages();
    void set_run_reason(BugRunReason run_reason);
    void update_materials(BugTechnology technology);

    void on_custom_setup(const bool custom_wanted);
    void on_printer_pick(BugPagePrinters *page, const BugPrinterPickerEvent &evt);
    void select_default_materials_for_printer_model(const VendorProfile::PrinterModel &printer_model, BugTechnology technology);
    void select_default_materials_for_printer_models(BugTechnology technology, const std::set<const VendorProfile::PrinterModel*> &printer_models);
    void on_3rdparty_install(const VendorProfile *vendor, bool install);

    bool on_bnt_finish();
    bool check_and_install_missing_materials(BugTechnology technology, const std::string &only_for_model_id = std::string());
    bool apply_config(AppConfig *app_config, PresetBundle *preset_bundle, const PresetUpdater *updater, bool& apply_keeped_changes);
    // #ys_FIXME_alise
    void update_presets_in_config(const std::string& section, const std::string& alias_key, bool add);
#ifdef __linux__
    void perform_desktop_integration() const;
#endif
    bool check_fff_selected();        // Used to decide whether to display Filaments page
    bool check_sla_selected();        // Used to decide whether to display SLA BugMaterials page

    int em() const { return index->em(); }
};

}
}

#endif
