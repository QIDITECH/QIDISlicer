#ifndef slic3r_FreqChangedParams_hpp_
#define slic3r_FreqChangedParams_hpp_

#include <memory>

#include "Event.hpp"

class wxButton;
class wxSizer;
class wxWindow;
class ScalableButton;

namespace Slic3r {

namespace GUI {

class ConfigOptionsGroup;

class FreqChangedParams
{
    double		    m_brim_width = 0.0;
    wxButton*       m_wiping_dialog_button{ nullptr };
    wxSizer*        m_sizer {nullptr};

//Y26
    std::shared_ptr<ConfigOptionsGroup> m_og_filament;
    //y25
    std::shared_ptr<ConfigOptionsGroup> m_og_sync;
    std::shared_ptr<ConfigOptionsGroup> m_og_fff;
    std::shared_ptr<ConfigOptionsGroup> m_og_sla;

    std::vector<ScalableButton*>        m_empty_buttons;

public:

    FreqChangedParams(wxWindow* parent);
    ~FreqChangedParams() = default;

    wxButton*       get_wiping_dialog_button() noexcept { return m_wiping_dialog_button; }
    wxSizer*        get_sizer() noexcept                { return m_sizer; }
    void            Show(bool is_fff) const;

    ConfigOptionsGroup* get_og(bool is_fff);
//Y26
    ConfigOptionsGroup* get_og_filament();
    void            msw_rescale();
    void            sys_color_changed();
};

} // namespace GUI
} // namespace Slic3r

#endif
