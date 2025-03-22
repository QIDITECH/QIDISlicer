#ifndef slic3r_GUI_Init_hpp_
#define slic3r_GUI_Init_hpp_

#include <libslic3r/Preset.hpp>
#include <libslic3r/PrintConfig.hpp>

namespace Slic3r {

namespace GUI {

struct OpenGLVersions
{
	static const std::vector<std::pair<int, int>> core;
};

struct CLISelectedProfiles
{
    std::string                 print;
    std::string                 printer;
    std::vector<std::string>    materials;

    bool has_valid_data() { return !print.empty() && !printer.empty() && !materials.empty(); }
};

struct GUI_InitParams
{
	int		                    argc;
	char	                  **argv;

	// Substitutions of unknown configuration values done during loading of user presets.
	PresetsConfigSubstitutions  preset_substitutions;

    std::vector<std::string>    load_configs;
    DynamicPrintConfig          extra_config;
    std::vector<std::string>    input_files;
    CLISelectedProfiles         selected_presets;

    bool                        start_as_gcodeviewer            { false };
    bool                        start_downloader                { false };
    bool                        delete_after_load               { false };
    std::string                 download_url;
#if !SLIC3R_OPENGL_ES
    std::pair<int, int>         opengl_version                  { 0, 0 };
    bool                        opengl_debug                    { false };
    bool                        opengl_compatibility_profile    { false };
#endif // !SLIC3R_OPENGL_ES
    bool                        opengl_aa                       { false };
};

int GUI_Run(GUI_InitParams &params);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_Init_hpp_
