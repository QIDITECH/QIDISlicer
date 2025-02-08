#ifndef slic3r_Format_AMF_hpp_
#define slic3r_Format_AMF_hpp_

namespace Slic3r {

class Model;
class DynamicPrintConfig;

// Load the content of an amf file into the given model and configuration.
extern bool load_amf(const char* path, DynamicPrintConfig* config, ConfigSubstitutionContext* config_substitutions, Model* model, bool check_version);

// The option has been missing in the UI since 2.4.0 (except for CLI).

} // namespace Slic3r

#endif /* slic3r_Format_AMF_hpp_ */
