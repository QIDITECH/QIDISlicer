#ifndef slic3r_UserAccountCommunication_hpp_
#define slic3r_UserAccountCommunication_hpp_

#include "UserAccountSession.hpp"
#include "Event.hpp"
#include "libslic3r/AppConfig.hpp"

#include <wx/timer.h>

#include <queue>
#include <condition_variable>
#include <map>
#include <thread>
#include <mutex>
#include <memory>
#include <ctime>

namespace Slic3r {
namespace GUI {
class CodeChalengeGenerator
{
public:
    CodeChalengeGenerator() {}
    ~CodeChalengeGenerator() {}
    std::string generate_chalenge(const std::string& verifier);
    std::string generate_verifier();
private:
    std::string generate_code_verifier(size_t length);
    std::string base64_encode(const std::string& input);
    std::string sha256(const std::string& input);
};

class UserAccountCommunication : public wxEvtHandler 
{
public:
    UserAccountCommunication(wxEvtHandler* evt_handler, AppConfig* app_config);
    ~UserAccountCommunication();

    // UI Session thread Interface 
    //
    bool is_logged();
    void do_login();
    void do_logout();
    void do_clear();
    // Generates and stores Code Verifier - second call deletes previous one.
    wxString generate_login_redirect_url();
    // Only recreates the url with already existing url (if generate was not called before - url will be faulty)
    wxString get_login_redirect_url(const std::string& service = std::string()) const;
    // Trigger function starts various remote operations
    void enqueue_connect_status_action();
    void enqueue_connect_printer_models_action();
    void enqueue_avatar_action(const std::string& url);
    void enqueue_test_connection();
    void enqueue_printer_data_action(const std::string& uuid);
    void enqueue_refresh();

    // Callbacks - called from UI after receiving Event from Session thread. Some might use Session thread.
    // 
    // Called when browser returns code via qidislicer:// custom url.
    // Exchanges code for tokens and shared_session_key
    void on_login_code_recieved(const std::string& url_message);

    void on_activate_app(bool active);

    void set_username(const std::string& username);
    void set_remember_session(bool b);
    bool get_remember_session() const {return m_remember_session; }

    std::string get_username() const { return m_username; }
    std::string get_access_token();
    std::string get_shared_session_key();

    void set_polling_enabled(bool enabled);
    // we have map of uuids and printer_models - set polling action to lightweight STATUS action
    void on_uuid_map_success();

    void set_refresh_time(int seconds);
    void on_token_timer(wxTimerEvent& evt);
    void on_polling_timer(wxTimerEvent& evt);
private:
    std::unique_ptr<UserAccountSession>     m_session;
    std::thread                             m_thread;
    std::mutex                              m_session_mutex;
    std::mutex                              m_thread_stop_mutex;
    std::condition_variable                 m_thread_stop_condition;
    bool                                    m_thread_stop { false };
    bool                                    m_thread_wakeup{ false };
    bool                                    m_window_is_active{ true };
    wxTimer*                                m_polling_timer;

    std::string                             m_code_verifier;
    wxEvtHandler*                           m_evt_handler;
    AppConfig*                              m_app_config;
    // if not empty - user is logged in
    std::string                             m_username;
    bool                                    m_remember_session { true }; // if default is true, on every login Remember me will be checked.

    wxTimer*                                m_token_timer;
    wxEvtHandler*                           m_timer_evt_handler;
    std::time_t                             m_next_token_refresh_at{0};

    void wakeup_session_thread();
    void init_session_thread();
    void login_redirect();
    std::string client_id() const { return Utils::ServiceConfig::instance().account_client_id(); }

    
    
};
}
}
#endif
