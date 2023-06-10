#include "EmbossStyleManager.hpp"
#include <GL/glew.h> // Imgui texture
#include <imgui/imgui_internal.h> // ImTextCharFromUtf8
#include "WxFontUtils.hpp"
#include "libslic3r/Utils.hpp" // ScopeGuard

#include "slic3r/GUI/3DScene.hpp" // ::glsafe
#include "slic3r/GUI/Jobs/CreateFontStyleImagesJob.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp" // check of font ranges

#include "slic3r/Utils/EmbossStylesSerializable.hpp"

using namespace Slic3r;
using namespace Slic3r::Emboss;
using namespace Slic3r::GUI::Emboss;

StyleManager::StyleManager(const ImWchar *language_glyph_range, std::function<EmbossStyles()> create_default_styles)
    : m_imgui_init_glyph_range(language_glyph_range)
    , m_create_default_styles(create_default_styles)
    , m_exist_style_images(false)
    , m_temp_style_images(nullptr)
    , m_app_config(nullptr)
    , m_last_style_index(std::numeric_limits<size_t>::max())
{}

StyleManager::~StyleManager() { 
    clear_imgui_font();
    free_style_images();
}

void StyleManager::init(AppConfig *app_config)
{
    m_app_config = app_config;
    EmbossStyles styles = (app_config != nullptr) ? 
        EmbossStylesSerializable::load_styles(*app_config) : 
        EmbossStyles{};
    if (styles.empty()) 
        styles = m_create_default_styles();
    for (EmbossStyle &style : styles) {
        make_unique_name(style.name);
        m_style_items.push_back({style});
    }

    std::optional<size_t> active_index_opt = (app_config != nullptr) ? 
        EmbossStylesSerializable::load_style_index(*app_config) : 
        std::optional<size_t>{};

    size_t active_index = 0;
    if (active_index_opt.has_value()) active_index = *active_index_opt;    
    if (active_index >= m_style_items.size()) active_index = 0;
    
    // find valid font item
    if (load_style(active_index))
        return; // style is loaded

    // Try to fix that style can't be loaded
    m_style_items.erase(m_style_items.begin() + active_index);

    load_valid_style();
}

bool StyleManager::store_styles_to_app_config(bool use_modification,
                                                    bool store_active_index)
{
    assert(m_app_config != nullptr);
    if (m_app_config == nullptr) return false;
    if (use_modification) {
        if (exist_stored_style()) {
            // update stored item
            m_style_items[m_style_cache.style_index].style = m_style_cache.style;
        } else {
            // add new into stored list
            EmbossStyle &style = m_style_cache.style;
            make_unique_name(style.name);
            m_style_cache.truncated_name.clear();
            m_style_cache.style_index = m_style_items.size();
            m_style_items.push_back({style});
        }
        m_style_cache.stored_wx_font = m_style_cache.wx_font;
    }

    if (store_active_index)
    {
        size_t style_index = exist_stored_style() ?
                                 m_style_cache.style_index :
                                 m_last_style_index;
        EmbossStylesSerializable::store_style_index(*m_app_config, style_index);
    }

    EmbossStyles styles;
    styles.reserve(m_style_items.size());
    for (const Item &item : m_style_items) styles.push_back(item.style);
    EmbossStylesSerializable::store_styles(*m_app_config, styles);
    return true;
}

void StyleManager::add_style(const std::string &name) {
    EmbossStyle& style = m_style_cache.style;
    style.name = name;
    make_unique_name(style.name);
    m_style_cache.style_index = m_style_items.size();
    m_style_cache.stored_wx_font = m_style_cache.wx_font;
    m_style_cache.truncated_name.clear();
    m_style_items.push_back({style});
}

void StyleManager::swap(size_t i1, size_t i2) {
    if (i1 >= m_style_items.size() || 
        i2 >= m_style_items.size()) return;
    std::swap(m_style_items[i1], m_style_items[i2]);
    // fix selected index
    if (!exist_stored_style()) return;
    if (m_style_cache.style_index == i1) {
        m_style_cache.style_index = i2;
    } else if (m_style_cache.style_index == i2) {
        m_style_cache.style_index = i1;
    }
}

void StyleManager::discard_style_changes() {
    if (exist_stored_style()) {
        if (load_style(m_style_cache.style_index))
            return; // correct reload style
    } else {
        if(load_style(m_last_style_index))
            return; // correct load last used style
    }

    // try to save situation by load some font
    load_valid_style();
}

void StyleManager::erase(size_t index) {
    if (index >= m_style_items.size()) return;

    // fix selected index
    if (exist_stored_style()) {
        size_t &i = m_style_cache.style_index;
        if (index < i) --i;
        else if (index == i) i = std::numeric_limits<size_t>::max();
    }

    m_style_items.erase(m_style_items.begin() + index);
}

void StyleManager::rename(const std::string& name) {
    m_style_cache.style.name = name;
    m_style_cache.truncated_name.clear();
    if (exist_stored_style()) { 
        Item &it = m_style_items[m_style_cache.style_index];
        it.style.name = name;
        it.truncated_name.clear();
    }
}

void StyleManager::load_valid_style()
{
    // iterate over all known styles
    while (!m_style_items.empty()) {
        if (load_style(0))
            return;
        // can't load so erase it from list
        m_style_items.erase(m_style_items.begin());
    }

    // no one style is loadable
    // set up default font list
    EmbossStyles def_style = m_create_default_styles();
    for (EmbossStyle &style : def_style) {
        make_unique_name(style.name);
        m_style_items.push_back({std::move(style)});
    }

    // iterate over default styles
    // There have to be option to use build in font
    while (!m_style_items.empty()) {
        if (load_style(0))
            return;
        // can't load so erase it from list
        m_style_items.erase(m_style_items.begin());
    }

    // This OS doesn't have TTF as default font,
    // find some loadable font out of default list
    assert(false);
}

bool StyleManager::load_style(size_t style_index)
{
    if (style_index >= m_style_items.size()) return false;
    if (!load_style(m_style_items[style_index].style)) return false;
    m_style_cache.style_index    = style_index;
    m_style_cache.stored_wx_font = m_style_cache.wx_font; // copy
    m_last_style_index           = style_index;
    return true;
}

bool StyleManager::load_style(const EmbossStyle &style) {
    if (style.type == EmbossStyle::Type::file_path) {
        std::unique_ptr<FontFile> font_ptr =
            create_font_file(style.path.c_str());
        if (font_ptr == nullptr) return false;
        m_style_cache.wx_font = {};
        m_style_cache.font_file = 
            FontFileWithCache(std::move(font_ptr));
        m_style_cache.style          = style; // copy
        m_style_cache.style_index    = std::numeric_limits<size_t>::max();
        m_style_cache.stored_wx_font = {};
        return true;
    }
    if (style.type != WxFontUtils::get_actual_type()) return false;
    std::optional<wxFont> wx_font_opt = WxFontUtils::load_wxFont(style.path);
    if (!wx_font_opt.has_value()) return false;
    return load_style(style, *wx_font_opt);
}

bool StyleManager::load_style(const EmbossStyle &style, const wxFont &font)
{
    m_style_cache.style = style; // copy

    // wx font property has bigger priority to set
    // it must be after copy of the style
    if (!set_wx_font(font)) return false;

    m_style_cache.style_index = std::numeric_limits<size_t>::max();
    m_style_cache.stored_wx_font = {};
    m_style_cache.truncated_name.clear();
    return true;
}

bool StyleManager::is_font_changed() const
{
    const wxFont &wx_font = get_wx_font();
    if (!wx_font.IsOk())
        return false;
    if (!exist_stored_style())
        return false;
    const EmbossStyle *stored_style = get_stored_style();
    if (stored_style == nullptr)
        return false;

    const wxFont &wx_font_stored = get_stored_wx_font();
    if (!wx_font_stored.IsOk())
        return false;

    const FontProp &prop = get_style().prop;
    const FontProp &prop_stored = stored_style->prop;

    // Exist change in face name?
    if(wx_font_stored.GetFaceName() != wx_font.GetFaceName()) return true;

    const std::optional<float> &skew = prop.skew;
    bool is_italic = skew.has_value() || WxFontUtils::is_italic(wx_font);
    const std::optional<float> &skew_stored = prop_stored.skew;
    bool is_stored_italic = skew_stored.has_value() || WxFontUtils::is_italic(wx_font_stored);
    // is italic changed
    if (is_italic != is_stored_italic)
        return true;

    const std::optional<float> &boldness = prop.boldness;
    bool is_bold = boldness.has_value() || WxFontUtils::is_bold(wx_font);
    const std::optional<float> &boldness_stored = prop_stored.boldness;
    bool is_stored_bold = boldness_stored.has_value() || WxFontUtils::is_bold(wx_font_stored);
    // is bold changed
    return is_bold != is_stored_bold;
}

bool StyleManager::is_active_font() { return m_style_cache.font_file.has_value(); }

const EmbossStyle* StyleManager::get_stored_style() const
{
    if (m_style_cache.style_index >= m_style_items.size()) return nullptr;
    return &m_style_items[m_style_cache.style_index].style;
}

void StyleManager::clear_glyphs_cache()
{
    FontFileWithCache &ff = m_style_cache.font_file;
    if (!ff.has_value()) return;
    ff.cache = std::make_shared<Glyphs>();
}

void StyleManager::clear_imgui_font() { m_style_cache.atlas.Clear(); }

ImFont *StyleManager::get_imgui_font()
{
    if (!is_active_font()) return nullptr;
    
    ImVector<ImFont *> &fonts = m_style_cache.atlas.Fonts;
    if (fonts.empty()) return nullptr;

    // check correct index
    int f_size = fonts.size();
    assert(f_size == 1);
    if (f_size != 1) return nullptr;
    ImFont *font = fonts.front();
    if (font == nullptr) return nullptr;
    return font;
}

const std::vector<StyleManager::Item> &StyleManager::get_styles() const{ return m_style_items; }

void StyleManager::make_unique_name(std::string &name)
{
    auto is_unique = [&](const std::string &name) -> bool {
        for (const Item &it : m_style_items)
            if (it.style.name == name) return false;
        return true;
    };

    // Style name can't be empty so default name is set
    if (name.empty()) name = "Text style";

    // When name is already unique, nothing need to be changed
    if (is_unique(name)) return;

    // when there is previous version of style name only find number
    const char *prefix = " (";
    const char  suffix  = ')';
    auto pos = name.find_last_of(prefix);
    if (name.c_str()[name.size() - 1] == suffix && 
        pos != std::string::npos) {
        // short name by ord number
        name = name.substr(0, pos);
    }

    int order = 1; // start with value 2 to represents same font name
    std::string new_name;
    do {
        new_name = name + prefix + std::to_string(++order) + suffix;
    } while (!is_unique(new_name));
    name = new_name;
}

void StyleManager::init_trunc_names(float max_width) { 
    for (auto &s : m_style_items)
        if (s.truncated_name.empty()) {
            std::string name = s.style.name;
            ImGuiWrapper::escape_double_hash(name);
            s.truncated_name = ImGuiWrapper::trunc(name, max_width);
        }
}

// for access to worker
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp" 

// for get DPI
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/GUI_ObjectManipulation.hpp"

void StyleManager::init_style_images(const Vec2i &max_size,
                                    const std::string &text)
{
    // check already initialized
    if (m_exist_style_images) return;

    // check is initializing
    if (m_temp_style_images != nullptr) {
        // is initialization finished
        if (!m_temp_style_images->styles.empty()) { 
            assert(m_temp_style_images->images.size() ==
                   m_temp_style_images->styles.size());
            // copy images into styles
            for (StyleManager::StyleImage &image : m_temp_style_images->images){
                size_t index = &image - &m_temp_style_images->images.front();
                StyleImagesData::Item &style = m_temp_style_images->styles[index];

                // find style in font list and copy to it
                for (auto &it : m_style_items) {
                    if (it.style.name != style.text ||
                        !(it.style.prop == style.prop))
                        continue;
                    it.image = image;
                    break;
                }
            }
            m_temp_style_images = nullptr;
            m_exist_style_images = true;
            return;
        }
        // in process of initialization inside of job
        return;
    }

    // create job for init images
    m_temp_style_images = std::make_shared<StyleImagesData::StyleImages>();
    StyleImagesData::Items styles;
    styles.reserve(m_style_items.size());
    for (const Item &item : m_style_items) {
        const EmbossStyle &style = item.style;
        std::optional<wxFont> wx_font_opt = WxFontUtils::load_wxFont(style.path);
        if (!wx_font_opt.has_value()) continue;
        std::unique_ptr<FontFile> font_file =
            WxFontUtils::create_font_file(*wx_font_opt);
        if (font_file == nullptr) continue;
        styles.push_back({
            FontFileWithCache(std::move(font_file)), 
            style.name,
            style.prop
        });
    }

    auto mf = wxGetApp().mainframe;
    // dot per inch for monitor
    int dpi = get_dpi_for_window(mf);
    // pixel per milimeter
    double ppm = dpi / ObjectManipulation::in_to_mm;

    auto &worker = wxGetApp().plater()->get_ui_job_worker();
    StyleImagesData data{std::move(styles), max_size, text, m_temp_style_images, ppm};
    queue_job(worker, std::make_unique<CreateFontStyleImagesJob>(std::move(data)));
}

void StyleManager::free_style_images() {
    if (!m_exist_style_images) return;
    GLuint tex_id = 0;
    for (Item &it : m_style_items) {
        if (tex_id == 0 && it.image.has_value())
            tex_id = (GLuint)(intptr_t) it.image->texture_id;
        it.image.reset();
    }
    if (tex_id != 0)
        glsafe(::glDeleteTextures(1, &tex_id));
    m_exist_style_images = false;
}

float StyleManager::min_imgui_font_size = 18.f;
float StyleManager::max_imgui_font_size = 60.f;
float StyleManager::get_imgui_font_size(const FontProp &prop, const FontFile &file, double scale)
{
    const auto  &cn = prop.collection_number;
    unsigned int font_index = (cn.has_value()) ? *cn : 0;
    const auto  &font_info  = file.infos[font_index];
    // coeficient for convert line height to font size
    float c1 = (font_info.ascent - font_info.descent + font_info.linegap) /
               (float) font_info.unit_per_em;

    // The point size is defined as 1/72 of the Anglo-Saxon inch (25.4 mm):
    // It is approximately 0.0139 inch or 352.8 um.
    return c1 * std::abs(prop.size_in_mm) / 0.3528f * scale;
}

ImFont *StyleManager::create_imgui_font(const std::string &text, double scale)
{
    // inspiration inside of ImGuiWrapper::init_font
    auto& ff = m_style_cache.font_file;
    if (!ff.has_value()) return nullptr;
    const FontFile &font_file = *ff.font_file;

    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(m_imgui_init_glyph_range);
    if (!text.empty())
        builder.AddText(text.c_str());

    ImVector<ImWchar> &ranges = m_style_cache.ranges;
    ranges.clear();
    builder.BuildRanges(&ranges);
        
    m_style_cache.atlas.Flags |= ImFontAtlasFlags_NoMouseCursors |
                                ImFontAtlasFlags_NoPowerOfTwoHeight;

    const FontProp &font_prop = m_style_cache.style.prop;
    float font_size = get_imgui_font_size(font_prop, font_file, scale);
    if (font_size < min_imgui_font_size)
        font_size = min_imgui_font_size;
    if (font_size > max_imgui_font_size)
        font_size = max_imgui_font_size;

    ImFontConfig font_config;
    // TODO: start using merge mode
    //font_config.MergeMode = true;

    unsigned int font_index = font_prop.collection_number.value_or(0);
    const auto  &font_info  = font_file.infos[font_index];
    if (font_prop.char_gap.has_value()) {
        float coef = font_size / (double) font_info.unit_per_em;
        font_config.GlyphExtraSpacing.x = coef * (*font_prop.char_gap);
    }
    if (font_prop.line_gap.has_value()) {
        float coef = font_size / (double) font_info.unit_per_em;
        font_config.GlyphExtraSpacing.y = coef * (*font_prop.line_gap);
    }

    font_config.FontDataOwnedByAtlas = false;

    const std::vector<unsigned char> &buffer = *font_file.data;
    ImFont * font = m_style_cache.atlas.AddFontFromMemoryTTF(
        (void *) buffer.data(), buffer.size(), font_size, &font_config, m_style_cache.ranges.Data);

    unsigned char *pixels;
    int            width, height;
    m_style_cache.atlas.GetTexDataAsRGBA32(&pixels, &width, &height);

    // Upload texture to graphics system
    GLint last_texture;
    glsafe(::glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture));
    ScopeGuard sg([last_texture]() {
        glsafe(::glBindTexture(GL_TEXTURE_2D, last_texture));
    });

    GLuint font_texture;
    glsafe(::glGenTextures(1, &font_texture));
    glsafe(::glBindTexture(GL_TEXTURE_2D, font_texture));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    glsafe(::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
    if (OpenGLManager::are_compressed_textures_supported())
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));
    else
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));

    // Store our identifier
    m_style_cache.atlas.TexID = (ImTextureID) (intptr_t) font_texture;
    assert(!m_style_cache.atlas.Fonts.empty());
    if (m_style_cache.atlas.Fonts.empty()) return nullptr;
    assert(font == m_style_cache.atlas.Fonts.back());
    if (!font->IsLoaded()) return nullptr;
    assert(font->IsLoaded());
    return font;
}

bool StyleManager::set_wx_font(const wxFont &wx_font) {
    std::unique_ptr<FontFile> font_file = 
        WxFontUtils::create_font_file(wx_font);
    return set_wx_font(wx_font, std::move(font_file));
}

bool StyleManager::set_wx_font(const wxFont &wx_font, std::unique_ptr<FontFile> font_file)
{
    if (font_file == nullptr) return false;
    m_style_cache.wx_font = wx_font; // copy
    m_style_cache.font_file = 
        FontFileWithCache(std::move(font_file));

    EmbossStyle &style = m_style_cache.style;
    style.type = WxFontUtils::get_actual_type();
    // update string path
    style.path = WxFontUtils::store_wxFont(wx_font);
    WxFontUtils::update_property(style.prop, wx_font);
    clear_imgui_font();
    return true;
}
