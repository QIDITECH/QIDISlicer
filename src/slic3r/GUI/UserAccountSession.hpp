#ifndef slic3r_UserAccountSession_hpp_
#define slic3r_UserAccountSession_hpp_

#include "Event.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/Utils/ServiceConfig.hpp"

#include <queue>
#include <map>
#include <thread>
#include <mutex>
#include <memory>

namespace Slic3r {
namespace GUI {

using OpenQIDIAuthEvent = Event<std::pair<wxString,wxString>>;
using UserAccountSuccessEvent = Event<std::string>;
using UserAccountFailEvent = Event<std::string>;
using UserAccountTimeEvent = Event<int>;
wxDECLARE_EVENT(EVT_OPEN_QIDIAUTH, OpenQIDIAuthEvent);
wxDECLARE_EVENT(EVT_UA_LOGGEDOUT, UserAccountSuccessEvent);
wxDECLARE_EVENT(EVT_UA_ID_USER_SUCCESS, UserAccountSuccessEvent);
wxDECLARE_EVENT(EVT_UA_ID_USER_SUCCESS_AFTER_TOKEN_SUCCESS, UserAccountSuccessEvent);
wxDECLARE_EVENT(EVT_UA_SUCCESS, UserAccountSuccessEvent);
wxDECLARE_EVENT(EVT_UA_QIDICONNECT_STATUS_SUCCESS, UserAccountSuccessEvent);
wxDECLARE_EVENT(EVT_UA_QIDICONNECT_PRINTER_MODELS_SUCCESS, UserAccountSuccessEvent);
wxDECLARE_EVENT(EVT_UA_AVATAR_SUCCESS, UserAccountSuccessEvent);
wxDECLARE_EVENT(EVT_UA_QIDICONNECT_PRINTER_DATA_SUCCESS, UserAccountSuccessEvent);
wxDECLARE_EVENT(EVT_UA_FAIL, UserAccountFailEvent); // Soft fail - clears only after some number of fails
wxDECLARE_EVENT(EVT_UA_RESET, UserAccountFailEvent); // Hard fail - clears all
wxDECLARE_EVENT(EVT_UA_RACE_LOST, UserAccountFailEvent); // Hard fail - clears all
wxDECLARE_EVENT(EVT_UA_QIDICONNECT_PRINTER_DATA_FAIL, UserAccountFailEvent); // Failed to get data for printer to select, soft fail, action does not repeat
wxDECLARE_EVENT(EVT_UA_REFRESH_TIME, UserAccountTimeEvent);
wxDECLARE_EVENT(EVT_UA_ENQUEUED_REFRESH, SimpleEvent);
wxDECLARE_EVENT(EVT_UA_RETRY_NOTIFY, UserAccountFailEvent); // Not fail yet, just retry attempt. string is message to ui.
wxDECLARE_EVENT(EVT_UA_CLOSE_RETRY_NOTIFICATION, SimpleEvent);

typedef std::function<void(const std::string& body)> UserActionSuccessFn;
typedef std::function<void(const std::string& body)> UserActionFailFn;

// UserActions implements different operations via trigger() method. Stored in m_actions.
enum class UserAccountActionID {
    USER_ACCOUNT_ACTION_DUMMY,
    USER_ACCOUNT_ACTION_REFRESH_TOKEN,
    USER_ACCOUNT_ACTION_CODE_FOR_TOKEN,
    USER_ACCOUNT_ACTION_USER_ID,
    USER_ACCOUNT_ACTION_USER_ID_AFTER_TOKEN_SUCCESS,
    USER_ACCOUNT_ACTION_TEST_ACCESS_TOKEN,
    USER_ACCOUNT_ACTION_TEST_CONNECTION,
    USER_ACCOUNT_ACTION_CONNECT_STATUS, // status of all printers by UUID
    USER_ACCOUNT_ACTION_CONNECT_PRINTER_MODELS, // status of all printers by UUID with printer_model. Should be called once to save printer models.
    USER_ACCOUNT_ACTION_AVATAR_OLD,
    USER_ACCOUNT_ACTION_AVATAR_NEW,
    USER_ACCOUNT_ACTION_CONNECT_DATA_FROM_UUID,
};
class UserAction
{
public:
    UserAction(const std::string name, const std::string url, bool requires_auth_token) : m_action_name(name), m_url(url), m_requires_auth_token(requires_auth_token){}
    virtual ~UserAction() = default;
    virtual void perform(wxEvtHandler* evt_handler, const std::string& access_token, UserActionSuccessFn success_callback, UserActionFailFn fail_callback, const std::string& input) const = 0;
    bool get_requires_auth_token() { return m_requires_auth_token; }
protected:
    std::string m_action_name;
    std::string m_url;
    bool        m_requires_auth_token;
};

class UserActionGetWithEvent : public UserAction
{
public:
    UserActionGetWithEvent(const std::string name, const std::string url, wxEventType succ_event_type, wxEventType fail_event_type, bool requires_auth_token = true)
        : m_succ_evt_type(succ_event_type)
        , m_fail_evt_type(fail_event_type)
        , UserAction(name, url, requires_auth_token)
    {}
    ~UserActionGetWithEvent() {}
    void perform(wxEvtHandler* evt_handler, const std::string& access_token, UserActionSuccessFn success_callback, UserActionFailFn fail_callback, const std::string& input) const override;
private:
    wxEventType   m_succ_evt_type;
    wxEventType   m_fail_evt_type;
};

class UserActionPost : public UserAction
{
public:
    UserActionPost(const std::string name, const std::string url, bool requires_auth_token = true) : UserAction(name, url, requires_auth_token) {}
    ~UserActionPost() {}
    void perform(wxEvtHandler* evt_handler, const std::string& access_token, UserActionSuccessFn success_callback, UserActionFailFn fail_callback, const std::string& input) const override;
};

class DummyUserAction : public UserAction
{
public:
    DummyUserAction() : UserAction("Dummy", {}, false) {}
    ~DummyUserAction() {}
    void perform(wxEvtHandler* evt_handler, const std::string& access_token, UserActionSuccessFn success_callback, UserActionFailFn fail_callback, const std::string& input) const override { }
};

struct ActionQueueData
{
    UserAccountActionID     action_id;
    UserActionSuccessFn     success_callback;
    UserActionFailFn        fail_callback;
    std::string             input;
};

class UserAccountSession
{
public:
    UserAccountSession(wxEvtHandler* evt_handler, const std::string& access_token, const std::string& refresh_token, const std::string& shared_session_key, long long next_token_timeout, bool polling_enabled)
        : p_evt_handler(evt_handler)
        , m_access_token(access_token)
        , m_refresh_token(refresh_token)
        , m_shared_session_key(shared_session_key)
        , m_next_token_timeout(next_token_timeout)
        , m_polling_action(polling_enabled ? UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_PRINTER_MODELS : UserAccountActionID::USER_ACCOUNT_ACTION_DUMMY)
       
    {
        auto& sc = Utils::ServiceConfig::instance();
        
        // do not forget to add delete to destructor
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_DUMMY] = std::make_unique<DummyUserAction>();
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_REFRESH_TOKEN] = std::make_unique<UserActionPost>("EXCHANGE_TOKENS", sc.account_token_url());
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_CODE_FOR_TOKEN] = std::make_unique<UserActionPost>("EXCHANGE_TOKENS", sc.account_token_url());
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_USER_ID] = std::make_unique<UserActionGetWithEvent>("USER_ID", sc.account_me_url(), EVT_UA_ID_USER_SUCCESS, EVT_UA_RESET);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_USER_ID_AFTER_TOKEN_SUCCESS] = std::make_unique<UserActionGetWithEvent>("USER_ID_AFTER_TOKEN_SUCCESS", sc.account_me_url(), EVT_UA_ID_USER_SUCCESS_AFTER_TOKEN_SUCCESS, EVT_UA_RESET);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_TEST_ACCESS_TOKEN] = std::make_unique<UserActionGetWithEvent>("TEST_ACCESS_TOKEN", sc.account_me_url(), EVT_UA_ID_USER_SUCCESS, EVT_UA_FAIL);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_TEST_CONNECTION] = std::make_unique<UserActionGetWithEvent>("TEST_CONNECTION", sc.account_me_url(), wxEVT_NULL, EVT_UA_RESET);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_STATUS] = std::make_unique<UserActionGetWithEvent>("CONNECT_STATUS", sc.connect_status_url(), EVT_UA_QIDICONNECT_STATUS_SUCCESS, EVT_UA_FAIL);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_PRINTER_MODELS] = std::make_unique<UserActionGetWithEvent>("CONNECT_PRINTER_MODELS", sc.connect_printer_list_url(), EVT_UA_QIDICONNECT_PRINTER_MODELS_SUCCESS, EVT_UA_FAIL);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_AVATAR_OLD] = std::make_unique<UserActionGetWithEvent>("AVATAR", sc.media_url(), EVT_UA_AVATAR_SUCCESS, EVT_UA_FAIL, false);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_AVATAR_NEW] = std::make_unique<UserActionGetWithEvent>("AVATAR", std::string(), EVT_UA_AVATAR_SUCCESS, EVT_UA_FAIL, false);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_DATA_FROM_UUID] = std::make_unique<UserActionGetWithEvent>("USER_ACCOUNT_ACTION_CONNECT_DATA_FROM_UUID", sc.connect_printers_url(), EVT_UA_QIDICONNECT_PRINTER_DATA_SUCCESS, EVT_UA_QIDICONNECT_PRINTER_DATA_FAIL);
    }
    ~UserAccountSession()
    {
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_DUMMY].reset(nullptr);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_REFRESH_TOKEN].reset(nullptr);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_CODE_FOR_TOKEN].reset(nullptr);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_USER_ID].reset(nullptr);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_TEST_ACCESS_TOKEN].reset(nullptr);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_TEST_CONNECTION].reset(nullptr);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_STATUS].reset(nullptr);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_AVATAR_OLD].reset(nullptr);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_AVATAR_NEW].reset(nullptr);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_DATA_FROM_UUID].reset(nullptr);
    }
    void clear() {
        {
            std::lock_guard<std::mutex> lock(m_credentials_mutex);
            m_access_token.clear();
            m_refresh_token.clear();
            m_shared_session_key.clear();
        }
        {
            std::lock_guard<std::mutex> lock(m_session_mutex);
            m_proccessing_enabled = false;
        }
    }

    // Functions that automatically enable action queu processing
    void init_with_code(const std::string& code, const std::string& code_verifier);
    void enqueue_action(UserAccountActionID id, UserActionSuccessFn success_callback, UserActionFailFn fail_callback, const std::string& input);
    // Special enques, that sets callbacks.
    void enqueue_test_with_refresh();
    void enqueue_refresh(const std::string& body);
    void enqueue_refresh_race(const std::string refresh_token_from_store = std::string());
    void process_action_queue();

    bool is_initialized() const {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        return !m_access_token.empty() || !m_refresh_token.empty();
    }
    bool is_enqueued(UserAccountActionID action_id) const;
    std::string get_access_token() const {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        return m_access_token;
    }
    std::string get_refresh_token() const {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        return m_refresh_token;
    }
    std::string get_shared_session_key() const {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        return m_shared_session_key;
    }
    long long get_next_token_timeout() const {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        return m_next_token_timeout;
    }

    void set_tokens(const std::string& access_token, const std::string& refresh_token, const std::string& shared_session_key, long long expires_in);

    void set_polling_action(UserAccountActionID action) { 
        std::lock_guard<std::mutex> lock(m_session_mutex);
        m_polling_action = action; 
    }
private:
    void refresh_fail_callback(const std::string& body);
    void refresh_fail_soft_callback(const std::string& body);
    void cancel_queue();
    void code_exchange_fail_callback(const std::string& body);
    void token_success_callback(const std::string& body);
    std::string client_id() const { return Utils::ServiceConfig::instance().account_client_id(); }
    void process_action_queue_inner();

    void remove_from_queue(UserAccountActionID action_id);

    // called from m_session_mutex protected code only
    void enqueue_action_inner(UserAccountActionID id, UserActionSuccessFn success_callback, UserActionFailFn fail_callback, const std::string& input);

 

    // Section of following vars is guarded by this mutex
    mutable std::mutex m_credentials_mutex;
    std::string m_access_token;
    std::string m_refresh_token;
    std::string m_shared_session_key;
    long long m_next_token_timeout;
    // End of section guarded by m_credentials_mutex


     // Section of following vars is guarded by this mutex
    mutable std::mutex m_session_mutex;

    std::queue<ActionQueueData>                                    m_action_queue;
    std::deque<ActionQueueData>                                    m_priority_action_queue; 
     // false prevents action queue to be processed - no communication is done
    // sets to true by init_with_code or enqueue_action call
    bool        m_proccessing_enabled {false}; 
    // action when woken up on idle - switches between USER_ACCOUNT_ACTION_CONNECT_PRINTER_MODELS and USER_ACCOUNT_ACTION_CONNECT_STATUS
    // set to USER_ACCOUNT_ACTION_DUMMY to switch off polling
    UserAccountActionID m_polling_action;

     // End of section guarded by m_session_mutex
 
    std::map<UserAccountActionID, std::unique_ptr<UserAction>>     m_actions;

    wxEvtHandler* p_evt_handler;
};

}
}
#endif