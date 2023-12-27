#ifndef slic3r_GCode_LabelObjects_hpp_
#define slic3r_GCode_LabelObjects_hpp_

#include <string>
#include <unordered_map>

namespace Slic3r {

enum GCodeFlavor : unsigned char;
enum class LabelObjectsStyle;
struct PrintInstance;
class Print;


namespace GCode {


class LabelObjects {
public:
    enum class IncludeName {
        No,
        Yes
    };
    void init(const Print& print);
    std::string all_objects_header() const;
    std::string start_object(const PrintInstance& print_instance, IncludeName include_name) const;
    std::string stop_object(const PrintInstance& print_instance) const;

private:
    struct LabelData {
        std::string name;
        int unique_id;
    };

    LabelObjectsStyle m_label_objects_style;
    GCodeFlavor       m_flavor;
    std::unordered_map<const PrintInstance*, LabelData> m_label_data;

};


} // namespace GCode
} // namespace Slic3r

#endif // slic3r_GCode_LabelObjects_hpp_
