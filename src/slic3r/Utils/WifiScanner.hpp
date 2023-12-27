#ifndef slic3r_WifiScanner_hpp_
#define slic3r_WifiScanner_hpp_

#include <map>
#include <vector>
#include <string>
#include <wx/string.h>

namespace Slic3r {

typedef std::map<wxString, std::string> WifiSsidPskMap;

class WifiScanner
{
public:
    WifiScanner();
    ~WifiScanner();
    
    const WifiSsidPskMap& get_map() const { return m_map; }
    // returns psk for given ssid
    // used on APPLE where each psk query requires user to give their password
    std::string get_psk(const std::string& ssid);
    const std::string get_current_ssid() { return m_current_ssid; }
    // fills map with ssid psk couples (or only ssid if no psk)
    // on APPLE only ssid
    void scan();
private:
    WifiSsidPskMap m_map;
    std::string m_current_ssid;
#if __APPLE__
    void get_ssids_mac(std::vector<std::string>& ssids);
    std::string get_psk_mac(const std::string& ssid);
    std::string get_current_ssid_mac();
    void* m_impl_osx { nullptr };
#endif
};

} // Slic3r

#endif