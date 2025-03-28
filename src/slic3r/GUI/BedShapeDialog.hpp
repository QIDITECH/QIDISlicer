#ifndef slic3r_BedShapeDialog_hpp_
#define slic3r_BedShapeDialog_hpp_
// The bed shape dialog.
// The dialog opens from Print Settins tab->Bed Shape : Set...

#include "GUI_Utils.hpp"
#include "2DBed.hpp"

#include <libslic3r/BuildVolume.hpp>

#include <wx/dialog.h>
#include <wx/choicebk.h>

namespace Slic3r {
namespace GUI {

class ConfigOptionsGroup;

using ConfigOptionsGroupShp = std::shared_ptr<ConfigOptionsGroup>;
using ConfigOptionsGroupWkp = std::weak_ptr<ConfigOptionsGroup>;

struct BedShape
{
    enum class PageType {
        Rectangle,
        Circle,
        Custom
    };

    enum class Parameter {
        RectSize,
        RectOrigin,
        Diameter,
        //Y20
        ExcludeMax,
        ExcludeMin
    };

    //B52
    BedShape(const ConfigOptionPoints &points1, const ConfigOptionPoints &points2);

    bool            is_custom() { return m_build_volume.type() == BuildVolume::Type::Convex || m_build_volume.type() == BuildVolume::Type::Custom; }

    static void     append_option_line(ConfigOptionsGroupShp optgroup, Parameter param);
    static wxString get_name(PageType type);

    PageType        get_page_type();

    wxString        get_full_name_with_params();
    void            apply_optgroup_values(ConfigOptionsGroupShp optgroup);
    //Y20 //B52
    void apply_exclude_values(const ConfigOptionPoints &points , ConfigOptionsGroupShp optgroup1, ConfigOptionsGroupShp optgroup2);

private:
    BuildVolume m_build_volume;
};

class BedShapePanel : public wxPanel
{
    static const std::string NONE;
    static const std::string EMPTY_STRING;

	Bed_2D*			   m_canvas;
    std::vector<Vec2d> m_shape;
    std::vector<Vec2d> m_loaded_shape;
    //Y20 //B52
    std::vector<Vec2d> m_exclude_area;
    std::string        m_custom_texture;
    std::string        m_custom_model;

public:
    BedShapePanel(wxWindow* parent) : wxPanel(parent, wxID_ANY), m_custom_texture(NONE), m_custom_model(NONE) {}
//Y20 //B52
    void build_panel(const ConfigOptionPoints& default_pt, const ConfigOptionPoints& exclude_area_0, const ConfigOptionString& custom_texture, const ConfigOptionString& custom_model);

    // Returns the resulting bed shape polygon. This value will be stored to the ini file.
    const std::vector<Vec2d>& get_shape() const { return m_shape; }
    //Y20 //B52
    const std::vector<Vec2d>& get_exclude_area() const { return m_exclude_area; }
    //const std::vector<Vec2d>& get_exclude_area_1() const { return m_exclude_area_1; }
    const std::string& get_custom_texture() const { return (m_custom_texture != NONE) ? m_custom_texture : EMPTY_STRING; }
    const std::string& get_custom_model() const { return (m_custom_model != NONE) ? m_custom_model : EMPTY_STRING; }

private:
    ConfigOptionsGroupShp	init_shape_options_page(const wxString& title);
    void	    activate_options_page(ConfigOptionsGroupShp options_group);
//Y20
    wxSizer*    init_exclude_sizer();
    ConfigOptionsGroupShp exclude_optgroup_0;
    ConfigOptionsGroupShp exclude_optgroup_1;
    wxPanel*    init_texture_panel();
    wxPanel*    init_model_panel();
    //B52
    void        set_shape(const ConfigOptionPoints &points1, const ConfigOptionPoints &points2);
    //Y20 //B52
    void        set_exclude_area(const ConfigOptionPoints &points1, const ConfigOptionPoints &points2);
    void		update_preview();
	void		update_shape();
    //B52
    const std::vector<Vec2d> update_exclude_area(ConfigOptionsGroupShp options_group_0, ConfigOptionsGroupShp options_group_1);
    mutable std::vector<BoundingBoxf3> m_exclude_bounding_box;
	void		load_stl();
    void		load_texture();
    void		load_model();

	wxChoicebook*	m_shape_options_book;
	std::vector <ConfigOptionsGroupShp>	m_optgroups;

    friend class BedShapeDialog;
};

class BedShapeDialog : public DPIDialog
{
	BedShapePanel*	m_panel;
public:
	BedShapeDialog(wxWindow* parent);
//Y20 //B52
    void build_dialog(const ConfigOptionPoints& default_pt, const ConfigOptionPoints& exclude_area, const ConfigOptionString& custom_texture, const ConfigOptionString& custom_model);

    const std::vector<Vec2d>& get_shape() const { return m_panel->get_shape(); }
//Y20 //B52
    const std::vector<Vec2d>& get_exclude_area() const { return m_panel->get_exclude_area(); }
    const std::string& get_custom_texture() const { return m_panel->get_custom_texture(); }
    const std::string& get_custom_model() const { return m_panel->get_custom_model(); }

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;
};

} // GUI
} // Slic3r


#endif  /* slic3r_BedShapeDialog_hpp_ */
