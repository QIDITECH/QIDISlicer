#ifndef slic3r_EmbossStylesSerializable_hpp_
#define slic3r_EmbossStylesSerializable_hpp_

#include <string>
#include <map>
#include <optional>
#include <libslic3r/TextConfiguration.hpp> // EmbossStyles+EmbossStyle

namespace Slic3r {
class AppConfig;
}

namespace Slic3r::GUI {

/// <summary>
/// For store/load font list to/from AppConfig
/// </summary>
class EmbossStylesSerializable
{
    static const std::string APP_CONFIG_FONT_NAME;
    static const std::string APP_CONFIG_FONT_DESCRIPTOR;
    static const std::string APP_CONFIG_FONT_LINE_HEIGHT;
    static const std::string APP_CONFIG_FONT_DEPTH;
    static const std::string APP_CONFIG_FONT_USE_SURFACE;
    static const std::string APP_CONFIG_FONT_BOLDNESS;
    static const std::string APP_CONFIG_FONT_SKEW;
    static const std::string APP_CONFIG_FONT_DISTANCE;
    static const std::string APP_CONFIG_FONT_ANGLE;
    static const std::string APP_CONFIG_FONT_COLLECTION;
    static const std::string APP_CONFIG_FONT_CHAR_GAP;
    static const std::string APP_CONFIG_FONT_LINE_GAP;

    static const std::string APP_CONFIG_ACTIVE_FONT;
public:
    EmbossStylesSerializable() = delete;

    static void store_style_index(AppConfig &cfg, unsigned index);
    static std::optional<size_t> load_style_index(const AppConfig &cfg);

    static EmbossStyles load_styles(const AppConfig &cfg);
    static void     store_styles(AppConfig &cfg, const EmbossStyles& styles);

private:
    static std::string create_section_name(unsigned index);
    static std::optional<EmbossStyle> load_style(const std::map<std::string, std::string> &app_cfg_section);
    static void store_style(AppConfig &cfg, const EmbossStyle &style, unsigned index);

    // TODO: move to app config like read from section
    static bool read(const std::map<std::string, std::string>& section, const std::string& key, bool& value);
    static bool read(const std::map<std::string, std::string>& section, const std::string& key, float& value);
    static bool read(const std::map<std::string, std::string>& section, const std::string& key, std::optional<int>& value);
    static bool read(const std::map<std::string, std::string>& section, const std::string& key, std::optional<unsigned int>& value);
    static bool read(const std::map<std::string, std::string>& section, const std::string& key, std::optional<float>& value);    
};
} // namespace Slic3r

#endif // #define slic3r_EmbossStylesSerializable_hpp_

