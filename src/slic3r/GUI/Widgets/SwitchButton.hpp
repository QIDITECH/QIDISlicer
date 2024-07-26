#ifndef slic3r_GUI_SwitchButton_hpp_
#define slic3r_GUI_SwitchButton_hpp_

#include "../wxExtensions.hpp"
#include "StateColor.hpp"

#include "BitmapToggleButton.hpp"

class SwitchButton : public BitmapToggleButton
{
public:
	//B64
	SwitchButton(wxWindow * parent = NULL, const wxString& name = wxEmptyString, wxWindowID id = wxID_ANY);

public:
	void SetLabels(wxString const & lbl_on, wxString const & lbl_off);

	void SetTextColor(StateColor const &color);

	void SetTrackColor(StateColor const &color);

	void SetThumbColor(StateColor const &color);

	void SetValue(bool value) override;

	void Rescale();

	void SysColorChange();

	//B64
    void SetSize(int size);


private:
	void update() override;

private:
	ScalableBitmap m_on;
	ScalableBitmap m_off;
	//B64
	int m_size = 300;

	wxString labels[2];
	StateColor   text_color;
	StateColor   track_color;
	StateColor   thumb_color;
};

#endif // !slic3r_GUI_SwitchButton_hpp_
