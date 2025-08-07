#include "Moonraker.hpp"

#include <algorithm>
#include <sstream>
#include <exception>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/nowide/convert.hpp>
#include <curl/curl.h>

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/format.hpp"
#include "libslic3r/AppConfig.hpp"
#include "Http.hpp"

namespace fs = boost::filesystem;
namespace pt = boost::property_tree;
namespace Slic3r {

//y21
bool Moonraker::m_isStop = false;
double Moonraker::progress_percentage = 0;

namespace {
#ifdef WIN32
// Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
std::string substitute_host(const std::string& orig_addr, std::string sub_addr)
{
    // put ipv6 into [] brackets 
    if (sub_addr.find(':') != std::string::npos && sub_addr.at(0) != '[')
        sub_addr = "[" + sub_addr + "]";
    // Using the new CURL API for handling URL. https://everything.curl.dev/libcurl/url
    // If anything fails, return the input unchanged.
    std::string out = orig_addr;
    CURLU* hurl = curl_url();
    if (hurl) {
        // Parse the input URL.
        CURLUcode rc = curl_url_set(hurl, CURLUPART_URL, orig_addr.c_str(), 0);
        if (rc == CURLUE_OK) {
            // Replace the address.
            rc = curl_url_set(hurl, CURLUPART_HOST, sub_addr.c_str(), 0);
            if (rc == CURLUE_OK) {
                // Extract a string fromt the CURL URL handle.
                char* url;
                rc = curl_url_get(hurl, CURLUPART_URL, &url, 0);
                if (rc == CURLUE_OK) {
                    out = url;
                    curl_free(url);
                }
                else
                    BOOST_LOG_TRIVIAL(error) << "OctoPrint substitute_host: failed to extract the URL after substitution";
            }
            else
                BOOST_LOG_TRIVIAL(error) << "OctoPrint substitute_host: failed to substitute host " << sub_addr << " in URL " << orig_addr;
        }
        else
            BOOST_LOG_TRIVIAL(error) << "OctoPrint substitute_host: failed to parse URL " << orig_addr;
        curl_url_cleanup(hurl);
    }
    else
        BOOST_LOG_TRIVIAL(error) << "OctoPrint substitute_host: failed to allocate curl_url";
    return out;
}
#endif
}
//B55
Moonraker::Moonraker(DynamicPrintConfig *config, bool add_port) :
    m_host(add_port ? config->opt_string("print_host").find(":") == std::string::npos ? config->opt_string("print_host") + ":10088" :
                                                                                             config->opt_string("print_host") :
                        config->opt_string("print_host")),
    m_apikey(config->opt_string("printhost_apikey")),
    m_cafile(config->opt_string("printhost_cafile")),
    m_ssl_revoke_best_effort(config->opt_bool("printhost_ssl_ignore_revoke"))
{}
//B64
Moonraker::Moonraker(std::string host, std::string local_ip)
    : m_host(host), m_show_ip(local_ip) {}

const char* Moonraker::get_name() const { return "Moonraker"; }

wxString Moonraker::get_test_ok_msg () const
{
    return _(L("Connection to Moonraker works correctly."));
}

wxString Moonraker::get_test_failed_msg (wxString &msg) const
{
    return GUI::format_wxstr("%s: %s"
        , _L("Could not connect to Moonraker")
        , msg);
}

bool Moonraker::test(wxString& msg) const
{
    // GET /server/info

    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure
    const char* name = get_name();

    bool res = true;
    auto url = make_url("server/info");

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Get version at: %2%") % name % url;

    auto http = Http::get(std::move(url));
    set_auth(http);
    http.on_error([&](std::string body, std::string error, unsigned status) {
        BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting version: %2%, HTTP %3%, body: `%4%`") % name % error % status % body;
        res = false;
        msg = format_error(body, error, status);
    })
    .on_complete([&](std::string body, unsigned) {
        BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: Got server/info: %2%") % name % body;
            
        try {
            // All successful HTTP requests will return a json encoded object in the form of :
            // {result: <response data>}
            std::stringstream ss(body);
            pt::ptree ptree;
            pt::read_json(ss, ptree);
            if (ptree.front().first != "result") {
                msg = "Could not parse server response";
                res = false;
                return;
            }
            if (!ptree.front().second.get_optional<std::string>("moonraker_version")) {
                msg = "Could not parse server response";
                res = false;
                return;
            }
            BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Got version: %2%") % name % ptree.front().second.get_optional<std::string>("moonraker_version");
        } catch (const std::exception&) {
            res = false;
            msg = "Could not parse server response";
        }
    })
#ifdef _WIN32
    .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
    .on_ip_resolve([&](std::string address) {
        // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
        // Remember resolved address to be reused at successive REST API call.
        msg = GUI::from_u8(address);
    })
#endif // _WIN32
    .perform_sync();

    return res;
}

//B45
std::string Moonraker::get_status(wxString &msg) const
{
    // GET /server/info

    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure
    const char *name = get_name();

    bool res = true;
    std::string print_state = "standby";
    auto url = make_url("printer/objects/query?print_stats=state");

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Get version at: %2%") % name % url;

    auto http = Http::get(std::move(url));
    set_auth(http);
    //B64 //y6
    http.timeout_connect(4)
    .on_error([&](std::string body, std::string error, unsigned status) {
        // y1
        if (status == 404)
        {
            body = ("Network connection fails.");
            if (body.find("AWS") != std::string::npos)
                body += ("Unable to get required resources from AWS server, please check your network settings.");
            else
                body += ("Unable to get required resources from Aliyun server, please check your network settings.");
        }
        BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting version: %2%, HTTP %3%, body: `%4%`") % name % error % status %
            body;
        print_state = "offline";
        msg = format_error(body, error, status);
    })
    .on_complete([&](std::string body, unsigned) {
        BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: Got print_stats: %2%") % name % body;

        try {
            // All successful HTTP requests will return a json encoded object in the form of :
            // {result: <response data>}
            std::stringstream ss(body);
            pt::ptree         ptree;
            pt::read_json(ss, ptree);
            if (ptree.front().first != "result") {
                msg = "Could not parse server response";
                print_state = "offline";
                return;
            }
            if (!ptree.front().second.get_optional<std::string>("status")) {
                msg = "Could not parse server response";
                print_state = "offline";
                return;
            }
            print_state = ptree.get<std::string>("result.status.print_stats.state");
            BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Got state: %2%") % name % print_state;
            ;
        } catch (const std::exception &) {
            print_state = "offline";
            msg         = "Could not parse server response";
        }
    })
#ifdef _WIN32
    .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
    .on_ip_resolve([&](std::string address) {
        // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
        // Remember resolved address to be reused at successive REST API call.
        msg = GUI::from_u8(address);
    })
#endif // _WIN32
    .perform_sync();

    return print_state;
}

float Moonraker::get_progress(wxString &msg) const
{
    // GET /server/info

    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure
    const char *name = get_name();

    bool res = true;
    auto url = make_url("printer/objects/query?display_status=progress");
    float  process = 0;
    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Get version at: %2%") % name % url;

    auto http = Http::get(std::move(url));
    set_auth(http);
    http.on_error([&](std::string body, std::string error, unsigned status) {
        // y1
        if (status == 404)
        {
            body = ("Network connection fails.");
            if (body.find("AWS") != std::string::npos)
                body += ("Unable to get required resources from AWS server, please check your network settings.");
            else
                body += ("Unable to get required resources from Aliyun server, please check your network settings.");
        }
        BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting version: %2%, HTTP %3%, body: `%4%`") % name % error % status %
            body;
        res = false;
        msg = format_error(body, error, status);
    })
    .on_complete([&](std::string body, unsigned) {
        BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: Got display_status: %2%") % name % body;

        try {
            // All successful HTTP requests will return a json encoded object in the form of :
            // {result: <response data>}
            std::stringstream ss(body);
            pt::ptree         ptree;
            pt::read_json(ss, ptree);
            if (ptree.front().first != "result") {
                msg = "Could not parse server response";
                res = false;
                return;
            }
            if (!ptree.front().second.get_optional<std::string>("status")) {
                msg = "Could not parse server response";
                res = false;
                return;
            }
            process = std::stof(ptree.get<std::string>("result.status.display_status.progress"));
            BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Got state: %2%") % name % process;
        } catch (const std::exception &) {
            res = false;
            msg = "Could not parse server response";
        }
    })
#ifdef _WIN32
    .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
    .on_ip_resolve([&](std::string address) {
        // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
        // Remember resolved address to be reused at successive REST API call.
        msg = GUI::from_u8(address);
    })
#endif // _WIN32
    .perform_sync();

    return process;
}

std::pair<std::string, float> Moonraker::get_status_progress(wxString &msg) const 
{
    // GET /server/info

    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure
    const char *name = get_name();

    bool        res         = true;
    std::string print_state = "standby";
    float       process     = 0;
    auto        url         = make_url("printer/objects/query?print_stats=state&display_status=progress");

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Get version at: %2%") % name % url;

    auto http = Http::get(std::move(url));
    set_auth(http);
    // B64 //y6
    http.timeout_connect(4)
        .on_error([&](std::string body, std::string error, unsigned status) {
            // y1
            if (status == 404) {
                body = ("Network connection fails.");
                if (body.find("AWS") != std::string::npos)
                    body += ("Unable to get required resources from AWS server, please check your network settings.");
                else
                    body += ("Unable to get required resources from Aliyun server, please check your network settings.");
            }
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting version: %2%, HTTP %3%, body: `%4%`") % name % error % status %
                                            body;
            print_state = "offline";
            msg         = format_error(body, error, status);
        })
        .on_complete([&](std::string body, unsigned) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: Got print_stats and process: %2%") % name % body;
            try {
                // All successful HTTP requests will return a json encoded object in the form of :
                // {result: <response data>}
                std::stringstream ss(body);
                pt::ptree         ptree;
                pt::read_json(ss, ptree);
                if (ptree.front().first != "result") {
                    msg         = "Could not parse server response";
                    print_state = "offline";
                    process     = 0;
                    return;
                }
                if (!ptree.front().second.get_optional<std::string>("status")) {
                    msg         = "Could not parse server response";
                    print_state = "offline";
                    process     = 0;
                    return;
                }
                print_state = ptree.get<std::string>("result.status.print_stats.state");
                process     = std::stof(ptree.get<std::string>("result.status.display_status.progress"));
                BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Got state: %2%") % name % print_state;
                ;
            } catch (const std::exception &) {
                print_state = "offline";
                process     = 0;
                msg         = "Could not parse server response";
            }
        })
#ifdef _WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
        .on_ip_resolve([&](std::string address) {
            // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
            // Remember resolved address to be reused at successive REST API call.
            msg = GUI::from_u8(address);
        })
#endif // _WIN32
        .perform_sync();

    return std::make_pair(print_state, process);
}

bool Moonraker::upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    // POST /server/files/upload

    const char* name = get_name();
    const auto upload_filename = upload_data.upload_path.filename();
    const auto upload_parent_path = upload_data.upload_path.parent_path();

    // If test fails, test_msg_or_host_ip contains the error message.
    wxString test_msg_or_host_ip;
    if (!test(test_msg_or_host_ip)) {
        error_fn(std::move(test_msg_or_host_ip));
        return false;
    }

    std::string url;
    bool res = true;

    //B64
#ifdef WIN32
    // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
    if (m_host.find("https://") == 0 || test_msg_or_host_ip.empty() || !GUI::get_app_config()->get_bool("allow_ip_resolve") ||
        m_host.find("aws") != -1 || m_host.find("aliyun") != -1)
#endif // _WIN32
    {
        // If https is entered we assume signed ceritificate is being used
        // IP resolving will not happen - it could resolve into address not being specified in cert
        url = make_url("server/files/upload");
    }
#ifdef WIN32
    else {
        // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
        // Curl uses easy_getinfo to get ip address of last successful transaction.
        // If it got the address use it instead of the stored in "host" variable.
        // This new address returns in "test_msg_or_host_ip" variable.
        // Solves troubles of uploades failing with name address.
        // in original address (m_host) replace host for resolved ip 
        info_fn(L"resolve", test_msg_or_host_ip);
        url = substitute_host(make_url("server/files/upload"), GUI::into_u8(test_msg_or_host_ip));
        BOOST_LOG_TRIVIAL(info) << "Upload address after ip resolve: " << url;
    }
#endif // _WIN32

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Uploading file %2% at %3%, filename: %4%, path: %5%, print: %6%")
        % name
        % upload_data.source_path
        % url
        % upload_filename.string()
        % upload_parent_path.string()
        % (upload_data.post_action == PrintHostPostUploadAction::StartPrint ? "true" : "false");
    /*
    The file must be uploaded in the request's body multipart/form-data (ie: <input type="file">). The following arguments may also be added to the form-data:
    root: The root location in which to upload the file.Currently this may be gcodes or config.If not specified the default is gcodes.
    path : This argument may contain a path(relative to the root) indicating a subdirectory to which the file is written.If a path is present the server will attempt to create any subdirectories that do not exist.
    checksum : A SHA256 hex digest calculated by the client for the uploaded file.If this argument is supplied the server will compare it to its own checksum calculation after the upload has completed.A checksum mismatch will result in a 422 error.
    Arguments available only for the gcodes root :
    print: If set to "true", Klippy will attempt to start the print after uploading.Note that this value should be a string type, not boolean.This provides compatibility with OctoPrint's upload API.
    */
    auto http = Http::post(std::move(url));
    set_auth(http);
    
    http.form_add("root", "gcodes");
    if (!upload_parent_path.empty())
        http.form_add("path", upload_parent_path.string());
    if (upload_data.post_action == PrintHostPostUploadAction::StartPrint)
        http.form_add("print", "true");
    progress_percentage = 0;
    http.form_add_file("file", upload_data.source_path.string(), upload_filename.string())
        .on_complete([&](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: File uploaded: HTTP %2%: %3%") % name % status % body;
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            // y21
            if (progress_percentage < 0.99) {
                if (status == 404)
                {
                    body = ("Network connection fails.");
                    if (body.find("AWS") != std::string::npos)
                        body += ("Unable to get required resources from AWS server, please check your network settings.");
                    else
                        body += ("Unable to get required resources from Aliyun server, please check your network settings.");
                }
                BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error uploading file: %2%, HTTP %3%, body: `%4%`") % name % error % status % body;
                error_fn(format_error(body, error, status));
                res = false;
            }
        })
        .on_progress([&](Http::Progress progress, bool& cancel) {
            prorgess_fn(std::move(progress), cancel);
            if (cancel) {
                // Upload was canceled
                BOOST_LOG_TRIVIAL(info) << name << ": Upload canceled";
                res = false;
            }
        })
#ifdef WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
        .perform_sync();

    return res;
}

void Moonraker::set_auth(Http &http) const
{
    if (!m_apikey.empty())
        http.header("X-Api-Key", m_apikey);
    if (!m_cafile.empty())
        http.ca_file(m_cafile);
}

std::string Moonraker::make_url(const std::string &path) const
{
    if (m_host.find("http://") == 0 || m_host.find("https://") == 0) {
        if (m_host.back() == '/') {
            return (boost::format("%1%%2%") % m_host % path).str();
        } else {
            return (boost::format("%1%/%2%") % m_host % path).str();
        }
    } else {
        return (boost::format("http://%1%/%2%") % m_host % path).str();
    }
}

//y25
bool Moonraker::send_command_to_printer(wxString& msg, wxString commond) const
{
    // printer_state: http://192.168.20.66/printer/objects/query?print_stats
    const char* name = get_name();
    std::string gcode = "G28";
    std::string commond_str = commond.ToStdString();
    std::string json_body = "{\"script\": \"" + commond_str + "\"}";

    auto url = make_url("printer/gcode/script");
    bool successful = false;
    Http http = Http::post(std::move(url));
    http.header("Content-Type", "application/json")
        .set_post_body(json_body)
        .timeout_connect(4)
        .on_error([&](std::string body, std::string error, unsigned status) {
        BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error sending G-code: %2%, HTTP %3%, body: %4%")
            % name % error % status % body;
            })
        .on_complete([&](std::string body, unsigned status) {
        BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: G-code sent successfully: %2%") % name % gcode;
        successful = true;
            })
#ifdef _WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif // _WIN32
        .perform_sync();

    return successful;
}

}
