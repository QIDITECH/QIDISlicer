#ifdef __linux__
#ifndef slic3r_DesktopIntegrationDialog_hpp_
#define slic3r_DesktopIntegrationDialog_hpp_

#include <wx/dialog.h>

namespace Slic3r {
namespace GUI {
class DesktopIntegrationDialog : public wxDialog
{
public:
	DesktopIntegrationDialog(wxWindow *parent);
	DesktopIntegrationDialog(DesktopIntegrationDialog &&) = delete;
	DesktopIntegrationDialog(const DesktopIntegrationDialog &) = delete;
	DesktopIntegrationDialog &operator=(DesktopIntegrationDialog &&) = delete;
	DesktopIntegrationDialog &operator=(const DesktopIntegrationDialog &) = delete;
	~DesktopIntegrationDialog();

	// methods that actually do / undo desktop integration. Static to be accesible from anywhere.

	// returns true if path to QIDISlicer.desktop is stored in App Config and existence of desktop file. 
	// Does not check if desktop file leads to this binary or existence of icons and viewer desktop file.
	static bool is_integrated();
	// true if appimage
	static bool integration_possible();
	// Creates Desktop files and icons for both QIDISlicer and GcodeViewer.
	// Stores paths into App Config.
	// Rewrites if files already existed.
	// if perform_downloader:
    // Creates Destktop files for QIDISlicer downloader feature
	// Regiters QIDISlicer to start on qidislicer:// URL
	static void perform_desktop_integration();
	// Deletes Desktop files and icons for both QIDISlicer and GcodeViewer at paths stored in App Config.
	static void undo_desktop_intgration();

	static void perform_downloader_desktop_integration();
	static void undo_downloader_registration();
    static void undo_downloader_registration_rigid();    
private:

};
} // namespace GUI
} // namespace Slic3r

#endif // slic3r_DesktopIntegrationDialog_hpp_
#endif // __linux__