#include "Downloader.hpp"
#include "GUI_App.hpp"
#include "NotificationManager.hpp"
#include "format.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>
#include <curl/curl.h>

namespace Slic3r {
namespace GUI {

namespace {
void open_folder(const std::string& path)
{
	// Code taken from NotificationManager.cpp

	// Execute command to open a file explorer, platform dependent.
	// FIXME: The const_casts aren't needed in wxWidgets 3.1, remove them when we upgrade.

#ifdef _WIN32
	const wxString widepath = from_u8(path);
	const wchar_t* argv[] = { L"explorer", widepath.GetData(), nullptr };
	::wxExecute(const_cast<wchar_t**>(argv), wxEXEC_ASYNC, nullptr);
#elif __APPLE__
	const char* argv[] = { "open", path.data(), nullptr };
	::wxExecute(const_cast<char**>(argv), wxEXEC_ASYNC, nullptr);
#else
	const char* argv[] = { "xdg-open", path.data(), nullptr };

	// Check if we're running in an AppImage container, if so, we need to remove AppImage's env vars,
	// because they may mess up the environment expected by the file manager.
	// Mostly this is about LD_LIBRARY_PATH, but we remove a few more too for good measure.
	if (wxGetEnv("APPIMAGE", nullptr)) {
		// We're running from AppImage
		wxEnvVariableHashMap env_vars;
		wxGetEnvMap(&env_vars);

		env_vars.erase("APPIMAGE");
		env_vars.erase("APPDIR");
		env_vars.erase("LD_LIBRARY_PATH");
		env_vars.erase("LD_PRELOAD");
		env_vars.erase("UNION_PRELOAD");

		wxExecuteEnv exec_env;
		exec_env.env = std::move(env_vars);

		wxString owd;
		if (wxGetEnv("OWD", &owd)) {
			// This is the original work directory from which the AppImage image was run,
			// set it as CWD for the child process:
			exec_env.cwd = std::move(owd);
		}

		::wxExecute(const_cast<char**>(argv), wxEXEC_ASYNC, nullptr, &exec_env);
	}
	else {
		// Looks like we're NOT running from AppImage, we'll make no changes to the environment.
		::wxExecute(const_cast<char**>(argv), wxEXEC_ASYNC, nullptr, nullptr);
	}
#endif
}

std::string filename_from_url(const std::string& url)
{
	std::string url_plain = std::string(url.begin(), std::find(url.begin(), url.end(), '?'));
	size_t slash = url_plain.find_last_of("/");
	if (slash == std::string::npos)
		return std::string();
	return std::string(url_plain.begin() + slash + 1, url_plain.end());
}

std::string unescape_url(const std::string& unescaped)
{
	std::string ret_val;
	CURL* curl = curl_easy_init();
	if (curl) {
		int decodelen;
		char* decoded = curl_easy_unescape(curl, unescaped.c_str(), unescaped.size(), &decodelen);
		if (decoded) {
			ret_val = std::string(decoded);
			curl_free(decoded);
		}
		curl_easy_cleanup(curl);
	}
	return ret_val;
}
}

Download::Download(int ID, std::string url, wxEvtHandler* evt_handler, const boost::filesystem::path& dest_folder, bool load_after)
    : m_id(ID)
	, m_filename(filename_from_url(url))
	, m_dest_folder(dest_folder)
{
	assert(boost::filesystem::is_directory(dest_folder));
	m_final_path = dest_folder / m_filename;
    m_file_get = std::make_shared<FileGet>(ID, std::move(url), m_filename, evt_handler, dest_folder, load_after);
}

void Download::start()
{
	m_state = DownloadState::DownloadOngoing;
	m_file_get->get();
}
void Download::cancel()
{
	m_state = DownloadState::DownloadStopped;
	m_file_get->cancel();
}
void Download::pause()
{
	//assert(m_state == DownloadState::DownloadOngoing);
	// if instead of assert - it can happen that user clicks on pause several times before the pause happens
	if (m_state != DownloadState::DownloadOngoing)
		return;
	m_state = DownloadState::DownloadPaused;
	m_file_get->pause();
}
void Download::resume()
{
	//assert(m_state == DownloadState::DownloadPaused);
	if (m_state != DownloadState::DownloadPaused)
		return;
	m_state = DownloadState::DownloadOngoing;
	m_file_get->resume();
}


Downloader::Downloader()
	: wxEvtHandler()
{
	Bind(EVT_DWNLDR_FILE_COMPLETE, &Downloader::on_complete, this);
	Bind(EVT_DWNLDR_FILE_PROGRESS, &Downloader::on_progress, this);
	Bind(EVT_DWNLDR_FILE_ERROR, &Downloader::on_error, this);
	Bind(EVT_DWNLDR_FILE_NAME_CHANGE, &Downloader::on_name_change, this);
	Bind(EVT_DWNLDR_FILE_PAUSED, &Downloader::on_paused, this);
	Bind(EVT_DWNLDR_FILE_CANCELED, &Downloader::on_canceled, this);
}

namespace {
bool is_any_subdomain(const std::string& url, const std::vector<std::string>& subdomains)
{
    for (const std::string& sub : subdomains)
    {
        if(FileGet::is_subdomain(url, sub))
            return true;
    }
    return false;
}

}

void Downloader::start_download(const std::string& full_url)
{
	assert(m_initialized);
	
    std::string escaped_url = unescape_url(full_url);
    if (boost::starts_with(escaped_url, "qidislicer://open?file=")) {
        escaped_url = escaped_url.substr(24);
    }else if (boost::starts_with(escaped_url, "qidislicer://open/?file=")) {
        escaped_url = escaped_url.substr(25);
    } else {
        BOOST_LOG_TRIVIAL(error) << "Could not start download due to wrong URL: " << full_url;
		return;
    }
    
    size_t id = get_next_id();

    if (!boost::starts_with(escaped_url, "https://") || !is_any_subdomain(escaped_url, {"printables.com", "thingiverse.com"})) {
		std::string msg = format(_L("Download won't start. Download URL doesn't point to allowed subdomains : %1%"), escaped_url);
		BOOST_LOG_TRIVIAL(error) << msg;
		NotificationManager* ntf_mngr = wxGetApp().notification_manager();
		ntf_mngr->push_notification(NotificationType::CustomNotification, NotificationManager::NotificationLevel::RegularNotificationLevel, msg);
		return;
	}

    m_downloads.emplace_back(std::make_unique<Download>(id, std::move(escaped_url), this, m_dest_folder, true));
	NotificationManager* ntf_mngr = wxGetApp().notification_manager();
	ntf_mngr->push_download_URL_progress_notification(id, m_downloads.back()->get_filename(), std::bind(&Downloader::user_action_callback, this, std::placeholders::_1, std::placeholders::_2));
	m_downloads.back()->start();
	BOOST_LOG_TRIVIAL(debug) << "started download";
}

void Downloader::start_download_printables(const std::string& url, bool load_after, const std::string& printables_url, GUI_App* app)
{
    assert(m_initialized);
    
    size_t id = get_next_id();

	if (!boost::starts_with(url, "https://") || !FileGet::is_subdomain(url, "printables.com")) {
		std::string msg = format(_L("Download won't start. Download URL doesn't point to https://printables.com : %1%"), url);
		BOOST_LOG_TRIVIAL(error) << msg;
		NotificationManager* ntf_mngr = wxGetApp().notification_manager();
		ntf_mngr->push_notification(NotificationType::CustomNotification, NotificationManager::NotificationLevel::RegularNotificationLevel, msg);
		return;
	}
	
    m_downloads.emplace_back(std::make_unique<Download>(id, url, this, m_dest_folder, load_after));
	NotificationManager* ntf_mngr = wxGetApp().notification_manager();
	ntf_mngr->push_download_URL_progress_notification_with_printables_link( id
        , m_downloads.back()->get_filename()
        , printables_url
        , std::bind(&Downloader::user_action_callback, this, std::placeholders::_1, std::placeholders::_2)
        , std::bind(&GUI_App::open_link_in_printables, app, std::placeholders::_1)
    );
	m_downloads.back()->start();
}

void Downloader::on_progress(wxCommandEvent& event)
{
	size_t id = event.GetInt();
	float percent = (float)std::stoi(into_u8(event.GetString())) / 100.f;
	//BOOST_LOG_TRIVIAL(error) << "progress " << id << ": " << percent;
	NotificationManager* ntf_mngr = wxGetApp().notification_manager();
	//BOOST_LOG_TRIVIAL(trace) << "Download "<< id << ": " << percent;
	ntf_mngr->set_download_URL_progress(id, percent);
}
void Downloader::on_error(wxCommandEvent& event)
{
	size_t id = event.GetInt();
    set_download_state(event.GetInt(), DownloadState::DownloadError);   
    BOOST_LOG_TRIVIAL(error) << "Download error: " << event.GetString();
	NotificationManager* ntf_mngr = wxGetApp().notification_manager();
	ntf_mngr->set_download_URL_error(id, into_u8(event.GetString()));
	show_error(nullptr, format_wxstr(L"%1%\n%2%", _L("The download has failed") + ":", event.GetString()));
}
void Downloader::on_complete(Event<DownloadEventData>& event)
{
	// here we open the file itself, notification should get 1.f progress from on progress.
    set_download_state(event.data.id, DownloadState::DownloadDone);
	wxArrayString paths;
	paths.Add(event.data.path);
    if (event.data.load_after)
	    wxGetApp().plater()->load_files(paths);
}
bool Downloader::user_action_callback(DownloaderUserAction action, int id)
{
	for (size_t i = 0; i < m_downloads.size(); ++i) {
		if (m_downloads[i]->get_id() == id) {
			switch (action) {
			case DownloadUserCanceled:
				m_downloads[i]->cancel();
				return true;
			case DownloadUserPaused:
				m_downloads[i]->pause();
				return true;
			case DownloadUserContinued:
				m_downloads[i]->resume();
				return true;
			case DownloadUserOpenedFolder:
				open_folder(m_downloads[i]->get_dest_folder());
				return true;
			default:
				return false;
			}
		}
	}
	return false;
}

void Downloader::on_name_change(wxCommandEvent& event)
{
    size_t id = event.GetInt();
	NotificationManager* ntf_mngr = wxGetApp().notification_manager();
	ntf_mngr->set_download_URL_filename(id, into_u8(event.GetString()));
}

void Downloader::on_paused(wxCommandEvent& event)
{
	size_t id = event.GetInt();
	NotificationManager* ntf_mngr = wxGetApp().notification_manager();
	ntf_mngr->set_download_URL_paused(id);
}

void Downloader::on_canceled(wxCommandEvent& event)
{
	size_t id = event.GetInt();
	NotificationManager* ntf_mngr = wxGetApp().notification_manager();
	ntf_mngr->set_download_URL_canceled(id);
}

void Downloader::set_download_state(int id, DownloadState state)
{
    for (size_t i = 0; i < m_downloads.size(); ++i) {
        if (m_downloads[i]->get_id() == id) {
            m_downloads[i]->set_state(state);
            return;
        }
    }
}

}
}