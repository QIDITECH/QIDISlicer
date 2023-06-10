#include "IconManager.hpp"
#include <cmath>
#include <boost/log/trivial.hpp>

using namespace Slic3r::GUI;

namespace priv {
// set shared pointer to point on bad texture
static void clear(IconManager::Icons &icons);
static const std::vector<std::pair<int, bool>>& get_states(IconManager::RasterType type);
static void draw_transparent_icon(const IconManager::Icon &icon); // only help function
}

IconManager::~IconManager() {
	priv::clear(m_icons);
	// release opengl texture is made in ~GLTexture()
}

std::vector<IconManager::Icons> IconManager::init(const InitTypes &input) 
{
    BOOST_LOG_TRIVIAL(error) << "Not implemented yet";
    return {};
}

std::vector<IconManager::Icons> IconManager::init(const std::vector<std::string> &file_paths, const ImVec2 &size, RasterType type)
{
    // TODO: remove in future
    if (!m_icons.empty()) {
        // not first initialization
        priv::clear(m_icons);
        m_icons.clear();
        m_icons_texture.reset();
    }

    // only rectangle are supported
    assert(size.x == size.y);
    // no subpixel supported
    unsigned int width = static_cast<unsigned int>(std::abs(std::round(size.x)));
    assert(size.x == static_cast<float>(width));

    // state order has to match the enum IconState
    const auto& states = priv::get_states(type);
        
    bool compress  = false;
    bool is_loaded = m_icons_texture.load_from_svg_files_as_sprites_array(file_paths, states, width, compress);
    if (!is_loaded || (size_t) m_icons_texture.get_width() < (states.size() * width) ||
        (size_t) m_icons_texture.get_height() < (file_paths.size() * width)) {
        // bad load of icons, but all usage of m_icons_texture check that texture is initialized
        assert(false);
        m_icons_texture.reset();
        return {};
    }

    unsigned count_files = file_paths.size();
    // count icons per file
    unsigned count = states.size();
    // create result
    std::vector<Icons> result;
    result.reserve(count_files);

    Icon def_icon;
    def_icon.tex_id = m_icons_texture.get_id();
    def_icon.size   = size;

    // float beacouse of dividing
    float tex_height = static_cast<float>(m_icons_texture.get_height());
    float tex_width = static_cast<float>(m_icons_texture.get_width());

    //for (const auto &f: file_paths) {
    for (unsigned f = 0; f < count_files; ++f) {
        // NOTE: there are space between icons
        unsigned start_y = static_cast<unsigned>(f) * (width + 1) + 1;
        float y1 = start_y / tex_height;
        float y2 = (start_y + width) / tex_height;
        Icons file_icons;
        file_icons.reserve(count);
        //for (const auto &s : states) {
        for (unsigned j = 0; j < count; ++j) {
            auto icon = std::make_shared<Icon>(def_icon);
            // NOTE: there are space between icons
            unsigned start_x = static_cast<unsigned>(j) * (width + 1) + 1;
            float x1 = start_x / tex_width;
            float x2 = (start_x + width) / tex_width;
            icon->tl = ImVec2(x1, y1);
            icon->br = ImVec2(x2, y2);
            file_icons.push_back(icon);
            m_icons.push_back(std::move(icon));
        }
        result.emplace_back(std::move(file_icons));
    }
	return result;
}

void IconManager::release() {
	BOOST_LOG_TRIVIAL(error) << "Not implemented yet";
}

void priv::clear(IconManager::Icons &icons) {
    std::string message;
	for (auto &icon : icons) {
		// Exist more than this instance of shared ptr?
        long count = icon.use_count();
        if (count != 1) {
			// in existing icon change texture to non existing one
            icon->tex_id = 0;

            std::string descr = 
				((count > 2) ? (std::to_string(count - 1) + "x") : "") + // count
				std::to_string(icon->size.x) + "x" + std::to_string(icon->size.y); // resolution
            if (message.empty())
                message = descr;
            else
                message += ", " + descr;
		}
	}

    if (!message.empty())
		BOOST_LOG_TRIVIAL(warning) << "There is still used icons(" << message << ").";
}

const std::vector<std::pair<int, bool>> &priv::get_states(IconManager::RasterType type) {
    static std::vector<std::pair<int, bool>> color = {std::make_pair(0, false)};
    static std::vector<std::pair<int, bool>> white = {std::make_pair(1, false)};
    static std::vector<std::pair<int, bool>> gray = {std::make_pair(2, false)};
    static std::vector<std::pair<int, bool>> color_wite_gray = {
        std::make_pair(1, false), // Activable
        std::make_pair(0, false), // Hovered
        std::make_pair(2, false)  // Disabled
    };

    switch (type) {
    case IconManager::RasterType::color: return color;
    case IconManager::RasterType::white_only_data: return white;
    case IconManager::RasterType::gray_only_data: return gray;
    case IconManager::RasterType::color_wite_gray: return color_wite_gray;
    default: return color;
    }
}

void priv::draw_transparent_icon(const IconManager::Icon &icon)
{
    // Check input
    if (!icon.is_valid()) {
        assert(false);
        BOOST_LOG_TRIVIAL(warning) << "Drawing invalid Icon.";
        ImGui::Text("?");
        return;
    }

    // size UV texture coors [in texture ratio]
    ImVec2 size_uv(icon.br.x - icon.tl.x, icon.br.y - icon.tl.y);
    ImVec2 one_px(size_uv.x / icon.size.x, size_uv.y / icon.size.y);

    // use top left corner of first icon
    IconManager::Icon icon_px = icon; // copy
    // reduce uv coors to one pixel
    icon_px.tl = ImVec2(0, 0);
    icon_px.br = one_px;
    draw(icon_px);
}

namespace Slic3r::GUI {

void draw(const IconManager::Icon &icon, const ImVec2 &size, const ImVec4 &tint_col, const ImVec4 &border_col)
{
    // Check input
    if (!icon.is_valid()) {
        assert(false);
        BOOST_LOG_TRIVIAL(warning) << "Drawing invalid Icon.";
        ImGui::Text("?");
        return;
    }

    ImTextureID id = (void *)static_cast<intptr_t>(icon.tex_id);
    const ImVec2 &s  = (size.x < 1 || size.y < 1) ? icon.size : size;
    ImGui::Image(id, s, icon.tl, icon.br, tint_col, border_col);
}

bool clickable(const IconManager::Icon &icon, const IconManager::Icon &icon_hover)
{
    // check of hover
    float cursor_x = ImGui::GetCursorPosX();
    priv::draw_transparent_icon(icon);
    ImGui::SameLine(cursor_x);
    if (ImGui::IsItemHovered()) {
        // redraw image
        draw(icon_hover);
    } else {
        // redraw normal image
        draw(icon);
    }
    return ImGui::IsItemClicked();
}

bool button(const IconManager::Icon &activ, const IconManager::Icon &hover, const IconManager::Icon &disable, bool disabled)
{
    if (disabled) {
        draw(disable);
        return false;
    }
    return clickable(activ, hover);
}

} // namespace Slic3r::GUI
