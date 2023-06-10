#include "EmbossStylesSerializable.hpp"

#include <libslic3r/AppConfig.hpp>
#include "WxFontUtils.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI;

const std::string EmbossStylesSerializable::APP_CONFIG_FONT_NAME        = "name";
const std::string EmbossStylesSerializable::APP_CONFIG_FONT_DESCRIPTOR  = "descriptor";
const std::string EmbossStylesSerializable::APP_CONFIG_FONT_LINE_HEIGHT = "line_height";
const std::string EmbossStylesSerializable::APP_CONFIG_FONT_DEPTH       = "depth";
const std::string EmbossStylesSerializable::APP_CONFIG_FONT_USE_SURFACE = "use_surface";
const std::string EmbossStylesSerializable::APP_CONFIG_FONT_BOLDNESS    = "boldness";
const std::string EmbossStylesSerializable::APP_CONFIG_FONT_SKEW        = "skew";
const std::string EmbossStylesSerializable::APP_CONFIG_FONT_DISTANCE    = "distance";
const std::string EmbossStylesSerializable::APP_CONFIG_FONT_ANGLE       = "angle";
const std::string EmbossStylesSerializable::APP_CONFIG_FONT_COLLECTION  = "collection";
const std::string EmbossStylesSerializable::APP_CONFIG_FONT_CHAR_GAP    = "char_gap";
const std::string EmbossStylesSerializable::APP_CONFIG_FONT_LINE_GAP    = "line_gap";

const std::string EmbossStylesSerializable::APP_CONFIG_ACTIVE_FONT      = "active_font";

std::string EmbossStylesSerializable::create_section_name(unsigned index)
{
    return AppConfig::SECTION_EMBOSS_STYLE + ':' + std::to_string(index);
}

// check only existence of flag
bool EmbossStylesSerializable::read(const std::map<std::string, std::string>& section, const std::string& key, bool& value){
    auto item = section.find(key);
    if (item == section.end()) return false;

    value = true;
    return true;
}

#include "fast_float/fast_float.h"
bool EmbossStylesSerializable::read(const std::map<std::string, std::string>& section, const std::string& key, float& value){
    auto item = section.find(key);
    if (item == section.end()) return false;
    const std::string &data = item->second;
    if (data.empty()) return false;
    float value_;
    fast_float::from_chars(data.c_str(), data.c_str() + data.length(), value_);
    // read only non zero value
    if (fabs(value_) <= std::numeric_limits<float>::epsilon()) return false;

    value = value_;
    return true;
}

bool EmbossStylesSerializable::read(const std::map<std::string, std::string>& section, const std::string& key, std::optional<int>& value){
    auto item = section.find(key);
    if (item == section.end()) return false;
    const std::string &data = item->second;
    if (data.empty()) return false;
    int value_ = std::atoi(data.c_str());
    if (value_ == 0) return false;

    value = value_;
    return true;
}

bool EmbossStylesSerializable::read(const std::map<std::string, std::string>& section, const std::string& key, std::optional<unsigned int>& value){
    auto item = section.find(key);
    if (item == section.end()) return false;
    const std::string &data = item->second;
    if (data.empty()) return false;
    int value_ = std::atoi(data.c_str());
    if (value_ <= 0) return false;

    value = static_cast<unsigned int>(value_);
    return true;
}

bool EmbossStylesSerializable::read(const std::map<std::string, std::string>& section, const std::string& key, std::optional<float>& value){
    auto item = section.find(key);
    if (item == section.end()) return false;
    const std::string &data = item->second;
    if (data.empty()) return false;
    float value_;
    fast_float::from_chars(data.c_str(), data.c_str() + data.length(), value_);
    // read only non zero value
    if (fabs(value_) <= std::numeric_limits<float>::epsilon()) return false;

    value = value_;
    return true;
}

std::optional<EmbossStyle> EmbossStylesSerializable::load_style(
    const std::map<std::string, std::string> &app_cfg_section)
{
    auto path_it = app_cfg_section.find(APP_CONFIG_FONT_DESCRIPTOR);
    if (path_it == app_cfg_section.end()) return {};
    const std::string &path = path_it->second;

    auto name_it = app_cfg_section.find(APP_CONFIG_FONT_NAME);
    const std::string default_name = "font_name";
    const std::string &name = 
        (name_it == app_cfg_section.end()) ?
        default_name : name_it->second;
        
    FontProp fp;
    read(app_cfg_section, APP_CONFIG_FONT_LINE_HEIGHT, fp.size_in_mm);
    read(app_cfg_section, APP_CONFIG_FONT_DEPTH, fp.emboss);
    read(app_cfg_section, APP_CONFIG_FONT_USE_SURFACE, fp.use_surface);
    read(app_cfg_section, APP_CONFIG_FONT_BOLDNESS, fp.boldness);
    read(app_cfg_section, APP_CONFIG_FONT_SKEW, fp.skew);
    read(app_cfg_section, APP_CONFIG_FONT_DISTANCE, fp.distance);
    read(app_cfg_section, APP_CONFIG_FONT_ANGLE, fp.angle);
    read(app_cfg_section, APP_CONFIG_FONT_COLLECTION, fp.collection_number);
    read(app_cfg_section, APP_CONFIG_FONT_CHAR_GAP, fp.char_gap);
    read(app_cfg_section, APP_CONFIG_FONT_LINE_GAP, fp.line_gap);

    EmbossStyle::Type type = WxFontUtils::get_actual_type();
    return EmbossStyle{ name, path, type, fp };
}

void EmbossStylesSerializable::store_style(AppConfig &     cfg,
                                           const EmbossStyle &fi,
                                           unsigned        index)
{
    std::map<std::string, std::string> data;
    data[APP_CONFIG_FONT_NAME] = fi.name;
    data[APP_CONFIG_FONT_DESCRIPTOR] = fi.path;
    const FontProp &fp = fi.prop;
    data[APP_CONFIG_FONT_LINE_HEIGHT] = std::to_string(fp.size_in_mm);
    data[APP_CONFIG_FONT_DEPTH] = std::to_string(fp.emboss);
    if (fp.use_surface)
        data[APP_CONFIG_FONT_USE_SURFACE] = "true";
    if (fp.boldness.has_value())
        data[APP_CONFIG_FONT_BOLDNESS] = std::to_string(*fp.boldness);
    if (fp.skew.has_value())
        data[APP_CONFIG_FONT_SKEW] = std::to_string(*fp.skew);
    if (fp.distance.has_value())
        data[APP_CONFIG_FONT_DISTANCE] = std::to_string(*fp.distance);
    if (fp.angle.has_value())
        data[APP_CONFIG_FONT_ANGLE] = std::to_string(*fp.angle);
    if (fp.collection_number.has_value())
        data[APP_CONFIG_FONT_COLLECTION] = std::to_string(*fp.collection_number);
    if (fp.char_gap.has_value())
        data[APP_CONFIG_FONT_CHAR_GAP] = std::to_string(*fp.char_gap);
    if (fp.line_gap.has_value())
        data[APP_CONFIG_FONT_LINE_GAP] = std::to_string(*fp.line_gap);
    cfg.set_section(create_section_name(index), std::move(data));
}

void EmbossStylesSerializable::store_style_index(AppConfig &cfg, unsigned index) {
    // store actual font index
    // active font first index is +1 to correspond with section name
    std::map<std::string, std::string> data;
    data[APP_CONFIG_ACTIVE_FONT] = std::to_string(index);
    cfg.set_section(AppConfig::SECTION_EMBOSS_STYLE, std::move(data));
}

std::optional<size_t> EmbossStylesSerializable::load_style_index(const AppConfig &cfg)
{
    if (!cfg.has_section(AppConfig::SECTION_EMBOSS_STYLE)) return {};

    auto section = cfg.get_section(AppConfig::SECTION_EMBOSS_STYLE);
    auto it      = section.find(APP_CONFIG_ACTIVE_FONT);
    if (it == section.end()) return {};

    size_t active_font = static_cast<size_t>(std::atoi(it->second.c_str()));
    // order in config starts with number 1
    return active_font - 1;
}

EmbossStyles EmbossStylesSerializable::load_styles(const AppConfig &cfg)
{
    EmbossStyles result;
    // human readable index inside of config starts from 1 !!
    unsigned    index        = 1;
    std::string section_name = create_section_name(index);
    while (cfg.has_section(section_name)) {
        std::optional<EmbossStyle> style_opt = load_style(cfg.get_section(section_name));
        if (style_opt.has_value()) result.emplace_back(*style_opt);
        section_name = create_section_name(++index);
    }
    return result;
}

void EmbossStylesSerializable::store_styles(AppConfig &cfg, const EmbossStyles& styles)
{
    // store styles
    unsigned index = 1;
    for (const EmbossStyle &style : styles) {
        // skip file paths + fonts from other OS(loaded from .3mf)
        assert(style.type == WxFontUtils::get_actual_type());
        // if (style_opt.type != WxFontUtils::get_actual_type()) continue;
        store_style(cfg, style, index++);
    }

    // remove rest of font sections (after deletation)
    std::string section_name = create_section_name(index);
    while (cfg.has_section(section_name)) {
        cfg.clear_section(section_name);
        section_name = create_section_name(++index);
    }
}