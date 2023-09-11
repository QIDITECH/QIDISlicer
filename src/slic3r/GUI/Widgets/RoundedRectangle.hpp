#ifndef slic3r_GUI_ROUNDEDRECTANGLE_hpp_
#define slic3r_GUI_ROUNDEDRECTANGLE_hpp_

#include "../wxExtensions.hpp"


//B35
#if defined __linux__
#include <wx/panel.h> 
#include <wx/wx.h> 
#endif

class RoundedRectangle : public wxWindow
{
public:
    RoundedRectangle(wxWindow *parent, wxColour col, wxPoint pos, wxSize size, double radius, int type = 0);
    ~RoundedRectangle(){};

private:
    double m_radius;
    int      m_type;
    wxColour m_color;

public:
    void OnPaint(wxPaintEvent &evt);
    DECLARE_EVENT_TABLE()
};
#endif // !slic3r_GUI_RoundedRectangle_hpp_
