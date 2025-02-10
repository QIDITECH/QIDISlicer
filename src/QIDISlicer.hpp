#ifndef SLIC3R_HPP
#define SLIC3R_HPP

#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r {

namespace IO {
	enum ExportFormat : int { 
        OBJ, 
        STL, 
        // SVG, 
        TMF, 
        Gcode
    };
}

class CLI {
public:
    int run(int argc, char **argv);

private:
    DynamicPrintAndCLIConfig    m_config;
    DynamicPrintConfig			m_print_config;
    DynamicPrintConfig          m_extra_config;
    std::vector<std::string>    m_input_files;
    std::vector<std::string>    m_actions;
    std::vector<std::string>    m_transforms;
    std::vector<std::string>    m_profiles_sharing;
    std::vector<Model>          m_models;

    bool setup(int argc, char **argv);
    
    /// Prints usage of the CLI.
    void print_help(bool include_print_options = false, PrinterTechnology printer_technology = ptAny) const;
    
    /// Exports loaded models to a file of the specified format, according to the options affecting output filename.
    bool export_models(IO::ExportFormat format);
    
    bool has_print_action() const { return m_config.opt_bool("export_gcode") || m_config.opt_bool("export_sla"); }
    bool processed_profiles_sharing();

    bool check_and_load_input_profiles(PrinterTechnology& printer_technology);
    
    std::string output_filepath(const Model &model, IO::ExportFormat format) const;
};

}

#endif
