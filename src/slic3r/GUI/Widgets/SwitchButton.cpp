#include "SwitchButton.hpp"

#include "../wxExtensions.hpp"
#include "../../Utils/MacDarkMode.hpp"

#include <wx/dcgraph.h>
#include <wx/dcmemory.h>
#include <wx/dcclient.h>

//B64
SwitchButton::SwitchButton(wxWindow* parent, const wxString& name, wxWindowID id)
	: BitmapToggleButton(parent, name, id)
    , m_on(this, "toggle_on", 28, 16)
	, m_off(this, "toggle_off", 28, 16)
    , text_color(std::pair{ 0x4479FB, (int) StateColor::Checked}, std::pair{0x6B6B6B, (int) StateColor::Normal})
	, track_color(0x333337)
    , thumb_color(std::pair{0x4479FB, (int) StateColor::Checked}, std::pair{0x333337, (int) StateColor::Normal})
{
	Rescale();
}

void SwitchButton::SetLabels(wxString const& lbl_on, wxString const& lbl_off)
{
	labels[0] = lbl_on;
	labels[1] = lbl_off;
	Rescale();
}

void SwitchButton::SetTextColor(StateColor const& color)
{
	text_color = color;
}

void SwitchButton::SetTrackColor(StateColor const& color)
{
	track_color = color;
}

void SwitchButton::SetThumbColor(StateColor const& color)
{
	thumb_color = color;
}

void SwitchButton::SetValue(bool value)
{
	if (value != GetValue())
		wxBitmapToggleButton::SetValue(value);
	update();
}

void SwitchButton::SetSize(int size)
{
    m_size = size;
    update();
    Rescale();
}

//B64
void SwitchButton::Rescale()
{
	if (!labels[0].IsEmpty()) {
#ifdef __WXOSX__
        auto scale = Slic3r::GUI::mac_max_scaling_factor();
        int BS = (int) scale;
#else
        constexpr int BS = 1;
#endif
		wxSize thumbSize;
		wxSize trackSize;
		wxClientDC dc(this);
#ifdef __WXOSX__
        dc.SetFont(dc.GetFont().Scaled(scale));
#endif
        wxSize textSize[2];
		{
			textSize[0] = dc.GetTextExtent(labels[0]);
			textSize[1] = dc.GetTextExtent(labels[1]);
		}
		{
			thumbSize = textSize[0];
			auto size = textSize[1];
			if (size.x > thumbSize.x) thumbSize.x = size.x;
			else size.x = thumbSize.x;
			thumbSize.x = m_size/2;
			//y3
			thumbSize.y = 30;
			trackSize.x = m_size;
			trackSize.y = 35;
            auto maxWidth = GetMaxWidth();
#ifdef __WXOSX__
            maxWidth *= scale;
#endif
			if (trackSize.x > maxWidth) {
                thumbSize.x -= (trackSize.x - maxWidth) / 2;
                trackSize.x = maxWidth;
			}
		}
		for (int i = 0; i < 2; ++i) {
			wxMemoryDC memdc(&dc);
			wxBitmap bmp(trackSize.x, trackSize.y);
			memdc.SelectObject(bmp);
			memdc.SetBackground(wxBrush(GetBackgroundColour()));
			memdc.Clear();
            memdc.SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
			auto state = i == 0 ? StateColor::Enabled : (StateColor::Checked | StateColor::Enabled);
            {
#ifdef __WXMSW__
				wxGCDC dc2(memdc);
#else
                wxDC &dc2(memdc);
#endif
				dc2.SetBrush(wxBrush(track_color.colorForStates(state)));
				dc2.SetPen(wxPen(track_color.colorForStates(state)));
                dc2.DrawRectangle(wxRect({0, 0}, trackSize));
                dc2.SetBrush(wxBrush(track_color.colorForStates(state)));
                dc2.SetPen(wxPen(track_color.colorForStates(state)));
                dc2.DrawRectangle(wxRect({i == 0 ? BS : (trackSize.x - thumbSize.x - BS), BS}, thumbSize));
				//y3
				dc2.SetPen(wxPen(thumb_color.colorForStates(StateColor::Checked | StateColor::Enabled), 3));
                dc2.DrawLine(i == 0 ? 1 : trackSize.x / 2 + 2 * BS, thumbSize.y - 1, i == 0 ? trackSize.x / 2 - 2 * BS : trackSize.x,
                             thumbSize.y - 1);
                dc2.SetPen(wxPen(wxColour(66, 66, 69), 1));
                dc2.DrawLine(trackSize.x / 2, 1, trackSize.x / 2, thumbSize.y - 1);
                dc2.DrawLine(0, thumbSize.y, trackSize.x, thumbSize.y);
			}
            memdc.SetTextForeground(text_color.colorForStates(state ^ StateColor::Checked));
            memdc.DrawText(labels[0], {BS + (thumbSize.x - textSize[0].x) / 2 - 7, BS + (thumbSize.y - textSize[0].y) / 2 - 4 * BS});
            memdc.SetTextForeground(text_color.colorForStates(state));
            memdc.DrawText(labels[1], {trackSize.x - thumbSize.x - BS + (thumbSize.x - textSize[1].x) / 2 - 4, BS + (thumbSize.y - textSize[1].y) / 2 - 4 * BS});
			memdc.SelectObject(wxNullBitmap);
#ifdef __WXOSX__
            bmp = wxBitmap(bmp.ConvertToImage(), -1, scale);
#endif
			(i == 0 ? m_off : m_on).SetBitmap(bmp);
		}
	}

	update();
}

void SwitchButton::SysColorChange()
{
	m_on.sys_color_changed();
	m_off.sys_color_changed();

	update();
}

void SwitchButton::update()
{
	SetBitmap((GetValue() ? m_on : m_off).bmp());
	update_size();
}
