#include "ConfigWizardWebViewPage.hpp"

#include "WebView.hpp"
#include "UserAccount.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "format.hpp"
#include "Event.hpp"
#include "slic3r/GUI/WebViewPlatformUtils.hpp"

#include <wx/webview.h>

#include <nlohmann/json.hpp>
using namespace std;
using namespace nlohmann;

wxDEFINE_EVENT(EVT_OPEN_EXTERNAL_LOGIN_WIZARD, wxCommandEvent);

namespace Slic3r { 
namespace GUI {
wxDEFINE_EVENT(EVT_LOGIN_VIA_WIZARD, Event<std::string>);

ConfigWizardWebViewPage::ConfigWizardWebViewPage(ConfigWizard *parent)
    // TRN Config wizard page headline.
    : ConfigWizardPage(parent, _L("Log in with Your QIDI Account (optional)"), _L("Log in (optional)"))
{
    p_user_account = wxGetApp().plater()->get_user_account();
    assert(p_user_account);
    bool logged = p_user_account->is_logged();

    // Create the webview
    m_browser_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxString TargetUrl = "";

#if QDT_RELEASE_TO_PUBLIC
    wxString    msg;
    QIDINetwork m_qidinetwork;
    TargetUrl = m_qidinetwork.get_qidi_host();
#endif

    BOOST_LOG_TRIVIAL(error) << "login url = " << TargetUrl.ToStdString();
//
//    TargetUrl = "https://login_aliyun.qidi3dprinter.com/#/account/login";
//    m_browser = WebView::CreateWebView(this, TargetUrl, {});
    // wxString test_url = "https://www.baidu.com";
    m_browser = WebView::CreateWebView(this, TargetUrl, {"wx"});
    if (!m_browser) {
        // TRN Config wizard page with a log in page.
        wxStaticText* fail_text = new wxStaticText(this, wxID_ANY, _L("Failed to load a web browser. Logging in is not possible in the moment."));
        append(fail_text);
        return;
    }
    if (logged) {
        // TRN Config wizard page with a log in web.
        m_text = new wxStaticText(this, wxID_ANY, format_wxstr(_L("You are logged as %1%."), p_user_account->get_username()));       
    } else {
        // TRN Config wizard page with a log in web. first line of text.
        m_text = new wxStaticText(this, wxID_ANY, _L("Log in to control your printers remotely through the built-in Connect in QIDISlicer."));
        // TRN Config wizard page with a log in web. second line of text.
    }
    append(m_text);
    m_browser_sizer->Add(m_browser, 1, wxEXPAND);
    append(m_browser_sizer, 1, wxEXPAND);

    m_browser_sizer->Show(true);

    this->Layout();
    // Connect the webview events
    // Bind(wxEVT_WEBVIEW_ERROR, &ConfigWizardWebViewPage::on_error, this, m_browser->GetId());
    // Bind(wxEVT_WEBVIEW_NAVIGATED, &ConfigWizardWebViewPage::on_navigation_request, this, m_browser->GetId());
    // Bind(wxEVT_IDLE, &ConfigWizardWebViewPage::on_idle, this);
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &ConfigWizardWebViewPage::is_login, this);
}

bool ConfigWizardWebViewPage::login_changed()
{
    assert(p_user_account && m_browser_sizer && m_text);
    bool logged = p_user_account->is_logged();
    m_browser_sizer->Show(!logged);
    if (logged) {
        // TRN Config wizard page with a log in web.
        m_text->SetLabel(format_wxstr(_L("You are logged as %1%."), p_user_account->get_username()));
    } else {
        // TRN Config wizard page with a log in web. first line of text.
        m_text->SetLabel(_L("Log in to control your printers remotely through the built-in Connect in QIDISlicer."));
    }
    return logged;
}

void ConfigWizardWebViewPage::on_error(wxWebViewEvent &evt) 
{
#define WX_ERROR_CASE(type) \
case type: \
    category = #type; \
    break;

    wxString category;
    switch (evt.GetInt())
    {
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CONNECTION);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CERTIFICATE);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_AUTH);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_SECURITY);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_NOT_FOUND);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_REQUEST);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_USER_CANCELLED);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_OTHER);
    }

    BOOST_LOG_TRIVIAL(error) << "ConfigWizardWebViewPage error: " << category;
    load_error_page();
}

void ConfigWizardWebViewPage::load_error_page() {
    if (!m_browser)
        return;
    if (m_vetoed)
        return;
    m_browser->Stop();
    m_load_error_page = true;
}

void ConfigWizardWebViewPage::on_idle(wxIdleEvent &WXUNUSED(evt)) {
    if (!m_browser)
        return;
    if (m_browser->IsBusy()) {
        wxSetCursor(wxCURSOR_ARROWWAIT);
    } else {
        wxSetCursor(wxNullCursor);

        if (!m_vetoed && m_load_error_page) {
            m_load_error_page = false;
            m_browser->LoadURL(GUI::format_wxstr(
                "file://%1%/web/connection_failed.html",
                boost::filesystem::path(resources_dir()).generic_string()
            ));
        }
    }
}


void ConfigWizardWebViewPage::on_navigation_request(wxWebViewEvent &evt) 
{
    wxString url = evt.GetURL();
    if (url.starts_with(L"qidislicer")) {
        delete_cookies(m_browser, "https://account.qidi3d.com");
        delete_cookies(m_browser, "https://accounts.google.com");
        delete_cookies(m_browser, "https://appleid.apple.com");
        delete_cookies(m_browser, "https://facebook.com");
        evt.Veto();
        m_vetoed = true;
        wxPostEvent(wxGetApp().plater(), Event<std::string>(EVT_LOGIN_VIA_WIZARD, into_u8(url)));	
    } else if (url.Find("accounts.google.com") != wxString::npos 
        || url.Find("appleid.apple.com") != wxString::npos 
        || url.Find("facebook.com") != wxString::npos) 
    {
        auto& sc = Utils::ServiceConfig::instance();
        if (!m_evt_sent && !url.starts_with(GUI::from_u8(sc.account_url()))) {
            wxCommandEvent evt(EVT_OPEN_EXTERNAL_LOGIN_WIZARD);
            evt.SetString(url);
            wxPostEvent(wxGetApp().plater(), evt);
            m_evt_sent = true;
        }
    }
}

void ConfigWizardWebViewPage::is_login(wxWebViewEvent& evt)
{
    wxString str_input = evt.GetString();
    BOOST_LOG_TRIVIAL(error) << evt.GetString();
    std::string token;
    try {
        json j = json::parse(into_u8(str_input));
        token = j["data"]["token"];
    }
    catch (std::exception& e) {
        wxMessageBox(e.what(), "parse json failed", wxICON_WARNING);
    }
    BOOST_LOG_TRIVIAL(error) << token;
    wxGetApp().app_config->set("user_token", token);
}

}} // namespace Slic3r::GUI