#ifndef slic3r_WxFontUtils_hpp_
#define slic3r_WxFontUtils_hpp_

#include <boost/bimap.hpp>
#include <wx/font.h>
#include <memory>
#include <optional>
#include <string_view>
#include <string>

#include "libslic3r/Emboss.hpp"
#include "libslic3r/TextConfiguration.hpp"

namespace Slic3r {
namespace Emboss {
struct FontFile;
}  // namespace Emboss
}  // namespace Slic3r

namespace Slic3r::GUI {

// Help class to  work with wx widget font object( wxFont )
class WxFontUtils
{
public:
    // only static functions
    WxFontUtils() = delete;

    // check if exist file for wxFont
    // return pointer on data or nullptr when can't load
    static bool can_load(const wxFont &font);

    // os specific load of wxFont
    static std::unique_ptr<Slic3r::Emboss::FontFile> create_font_file(const wxFont &font);

    static EmbossStyle::Type get_current_type();
    static EmbossStyle create_emboss_style(const wxFont &font, const std::string& name = "");

    static std::string get_human_readable_name(const wxFont &font);

    // serialize / deserialize font
    static std::string store_wxFont(const wxFont &font);
    static wxFont load_wxFont(const std::string &font_descriptor);

    // Try to create similar font, loaded from 3mf from different Computer
    static wxFont create_wxFont(const EmbossStyle &style);
    // update font property by wxFont - without emboss depth and font size
    static void update_property(FontProp &font_prop, const wxFont &font);

    static bool is_italic(const wxFont &font);
    static bool is_bold(const wxFont &font);

    /// <summary>
    /// Set italic into wx font
    /// When italic font is same as original return nullptr.
    /// To not load font file twice on success is font_file returned.
    /// </summary>
    /// <param name="font">wx descriptor of font</param>
    /// <param name="font_file">file described in wx font</param> 
    /// <returns>New created font fileon success otherwise nullptr</returns>
    static std::unique_ptr<Slic3r::Emboss::FontFile> set_italic(wxFont &font, const Slic3r::Emboss::FontFile &prev_font_file);

    /// <summary>
    /// Set boldness into wx font
    /// When bolded font is same as original return nullptr.
    /// To not load font file twice on success is font_file returned.
    /// </summary>
    /// <param name="font">wx descriptor of font</param>
    /// <param name="font_file">file described in wx font</param> 
    /// <returns>New created font fileon success otherwise nullptr</returns>
    static std::unique_ptr<Slic3r::Emboss::FontFile> set_bold(wxFont &font, const Slic3r::Emboss::FontFile &font_file);

    // convert wxFont types to string and vice versa
    static const boost::bimap<wxFontFamily, std::string_view> type_to_family;
    static const boost::bimap<wxFontStyle, std::string_view> type_to_style;
    static const boost::bimap<wxFontWeight, std::string_view> type_to_weight;
};

} // namespace Slic3r::GUI
#endif // slic3r_WxFontUtils_hpp_
