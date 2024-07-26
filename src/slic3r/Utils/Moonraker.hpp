#ifndef slic3r_Moonraker_hpp_
#define slic3r_Moonraker_hpp_

#include <string>
#include <wx/string.h>
#include <boost/optional.hpp>
#include <boost/asio/ip/address.hpp>

#include "PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"


namespace Slic3r {

class DynamicPrintConfig;
class Http;

// https://moonraker.readthedocs.io/en/latest/web_api
class Moonraker : public PrintHost
{
public:
    //B55
    Moonraker(DynamicPrintConfig *config, bool add_port);
    //B64
    Moonraker(std::string host,std::string local_ip);
    ~Moonraker() override = default;

    const char* get_name() const override;

    virtual bool test(wxString &curl_msg) const override;

    //B45
    virtual std::string get_status(wxString &curl_msg) const override;
    virtual float       get_progress(wxString &curl_msg) const override;

    wxString get_test_ok_msg () const override;
    wxString get_test_failed_msg (wxString &msg) const override;
    bool upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn, InfoFn info_fn) const override;
    bool has_auto_discovery() const override { return true; }
    bool can_test() const override { return true; }
    PrintHostPostUploadActions get_post_upload_actions() const override { return PrintHostPostUploadAction::StartPrint; }
    std::string                get_host() const override { return m_show_ip != "" ? m_show_ip : m_host; }
    const std::string& get_apikey() const { return m_apikey; }
    const std::string& get_cafile() const { return m_cafile; }

protected:
/*
#ifdef WIN32
    virtual bool upload_inner_with_resolved_ip(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn, InfoFn info_fn, const boost::asio::ip::address& resolved_addr) const;
#endif
    virtual bool validate_version_text(const boost::optional<std::string> &version_text) const;
    virtual bool upload_inner_with_host(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn, InfoFn info_fn) const;
*/
    //B64
    std::string m_show_ip;
    std::string m_host;
    std::string m_apikey;
    std::string m_cafile;
    bool        m_ssl_revoke_best_effort;

    virtual void set_auth(Http &http) const;
    std::string make_url(const std::string &path) const;

private:
/*
#ifdef WIN32
    bool test_with_resolved_ip(wxString& curl_msg) const;
#endif
*/
};

}

#endif
