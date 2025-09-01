// Configuration store of Slic3r.
//
// The configuration store is either static or dynamic.
// DynamicPrintConfig is used mainly at the user interface. while the StaticPrintConfig is used
// during the slicing and the g-code generation.
//
// The classes derived from StaticPrintConfig form a following hierarchy.
//
// FullPrintConfig
//    PrintObjectConfig
//    PrintRegionConfig
//    PrintConfig
//        GCodeConfig
//

#ifndef slic3r_PrintConfig_hpp_
#define slic3r_PrintConfig_hpp_

#include <boost/preprocessor/facilities/empty.hpp>
#include <boost/preprocessor/punctuation/comma_if.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/for_each_i.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <boost/container_hash/hash.hpp>
#include <cereal/cereal.hpp>
#include <algorithm>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "libslic3r.h"
#include "Config.hpp"
#include "SLA/SupportTreeStrategies.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {
class FullPrintConfig;
class GCodeConfig;
class MachineEnvelopeConfig;
class PrintConfig;
class PrintObjectConfig;
class PrintRegionConfig;
class SLAFullPrintConfig;
class SLAMaterialConfig;
class SLAPrintConfig;
class SLAPrintObjectConfig;
class SLAPrinterConfig;

enum class ArcFittingType {
    Disabled,
    EmitCenter
};

enum GCodeFlavor : unsigned char {
    gcfRepRapSprinter, gcfRepRapFirmware, gcfRepetier, gcfTeacup, gcfMakerWare, gcfMarlinLegacy, gcfMarlinFirmware, gcfKlipper, gcfSailfish, gcfMach3, gcfMachinekit,
    gcfSmoothie, gcfNoExtrusion,
};

enum class MachineLimitsUsage {
    EmitToGCode,
    TimeEstimateOnly,
    Ignore,
    Count,
};

//B55
enum PrintHostType {
    htQIDILink, htQIDIConnect, htOctoPrint, htMoonraker, htMoonraker2,htDuet, htFlashAir, htAstroBox, htRepetier, htMKS, htQIDIConnectNew
};

enum AuthorizationType {
    atKeyPassword, atUserPassword
};

enum class FuzzySkinType {
    None,
    External,
    All,
};

enum InfillPattern : int {
    ipRectilinear, ipMonotonic, ipMonotonicLines, ipAlignedRectilinear, ipGrid, ipTriangles, ipStars, ipCubic, ipLine, ipConcentric, ipHoneycomb, ip3DHoneycomb,
    ipGyroid, ipHilbertCurve, ipArchimedeanChords, ipOctagramSpiral, ipAdaptiveCubic, ipSupportCubic, ipSupportBase,
    ipLightning,
    ipEnsuring,
    ipZigZag,
    ipCount,
    //w14
    ipConcentricInternal,
    //w32
    ipCrossHatch,
};

enum class IroningType {
    TopSurfaces,
    TopmostOnly,
    AllSolid,
    Count,
};

enum class SlicingMode
{
    // Regular, applying ClipperLib::pftNonZero rule when creating ExPolygons.
    Regular,
    // Compatible with 3DLabPrint models, applying ClipperLib::pftEvenOdd rule when creating ExPolygons.
    EvenOdd,
    // Orienting all contours CCW, thus closing all holes.
    CloseHoles,
};

enum SupportMaterialPattern {
    smpRectilinear, smpRectilinearGrid, smpHoneycomb,
};

enum SupportMaterialStyle {
    smsGrid, smsSnug, smsTree, smsOrganic,
};

enum SupportMaterialInterfacePattern {
    smipAuto, smipRectilinear, smipConcentric,
};

enum SeamPosition {
    spRandom, spNearest, spAligned, spRear
};

enum class ScarfSeamPlacement {
    nowhere,
    countours,
    everywhere
};

enum SLAMaterial {
    slamTough,
    slamFlex,
    slamCasting,
    slamDental,
    slamHeatResistant,
};

enum SLADisplayOrientation {
    sladoLandscape,
    sladoPortrait
};

using SLASupportTreeType = sla::SupportTreeType;
using SLAPillarConnectionMode = sla::PillarConnectionMode;

enum BrimType {
    btNoBrim,
    btOuterOnly,
    btInnerOnly,
    btOuterAndInner,
};

enum DraftShield {
    dsDisabled, dsLimited, dsEnabled
};

enum class LabelObjectsStyle {
    Disabled, Octoprint, Firmware
};

enum class PerimeterGeneratorType
{
    // Classic perimeter generator using Clipper offsets with constant extrusion width.
    Classic,
    // Perimeter generator with variable extrusion width based on the paper
    // "A framework for adaptive width control of dense contour-parallel toolpaths in fused deposition modeling" ported from Cura.
    Arachne
};

enum class TopOnePerimeterType
{
    None,
    TopSurfaces,
    TopmostOnly,
    Count
};
//B3
enum class GCodeThumbnailsFormat {
    QIDI,PNG, JPG, QOI
};

enum TowerSpeeds : int {
    tsLayer1,
    tsLayer2,
    tsLayer3,
    tsLayer4,
    tsLayer5,
    tsLayer8,
    tsLayer11,
    tsLayer14,
    tsLayer18, 
    tsLayer22,
    tsLayer24,
};

enum TiltSpeeds : int {
    tsMove120,
    tsLayer200,
    tsMove300,
    tsLayer400,
    tsLayer600,
    tsLayer800,
    tsLayer1000,
    tsLayer1250,
    tsLayer1500,
    tsLayer1750,
    tsLayer2000,
    tsLayer2250,
    tsMove5120,
    tsMove8000,
};

enum class EnsureVerticalShellThickness {
    Disabled,
    Partial,
    Enabled,
};

#define CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(NAME) \
    template<> const t_config_enum_names& ConfigOptionEnum<NAME>::get_enum_names(); \
    template<> const t_config_enum_values& ConfigOptionEnum<NAME>::get_enum_values();

CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(ArcFittingType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(PrinterTechnology)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(GCodeFlavor)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(MachineLimitsUsage)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(PrintHostType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(AuthorizationType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(FuzzySkinType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(InfillPattern)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(IroningType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SlicingMode)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SupportMaterialPattern)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SupportMaterialStyle)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SupportMaterialInterfacePattern)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SeamPosition)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(ScarfSeamPlacement)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SLADisplayOrientation)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SLAPillarConnectionMode)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SLASupportTreeType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(BrimType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(DraftShield)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(LabelObjectsStyle)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(GCodeThumbnailsFormat)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(ForwardCompatibilitySubstitutionRule)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(PerimeterGeneratorType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(TopOnePerimeterType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(EnsureVerticalShellThickness)

#undef CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS

class DynamicPrintConfig;

// Defines each and every confiuration option of Slic3r, including the properties of the GUI dialogs.
// Does not store the actual values, but defines default values.
class PrintConfigDef : public ConfigDef
{
public:
    PrintConfigDef();

    static void handle_legacy(t_config_option_key &opt_key, std::string &value);
    static void handle_legacy_composite(DynamicPrintConfig &config);

    // Array options growing with the number of extruders
    const std::vector<std::string>& extruder_option_keys() const { return m_extruder_option_keys; }
    // Options defining the extruder retract properties. These keys are sorted lexicographically.
    // The extruder retract keys could be overidden by the same values defined at the Filament level
    // (then the key is further prefixed with the "filament_" prefix).
    const std::vector<std::string>& extruder_retract_keys() const { return m_extruder_retract_keys; }

private:
    void init_common_params();
    void init_fff_params();
    void init_extruder_option_keys();
    void init_sla_params();
    void init_sla_tilt_params();
    void init_sla_support_params(const std::string &method_prefix);

    std::vector<std::string>    m_extruder_option_keys;
    std::vector<std::string>    m_extruder_retract_keys;
};

// The one and only global definition of SLic3r configuration options.
// This definition is constant.
extern const PrintConfigDef print_config_def;

class StaticPrintConfig;

// Minimum object distance for arrangement, based on printer technology.
double min_object_distance(const ConfigBase &cfg);

// Slic3r dynamic configuration, used to override the configuration
// per object, per modification volume or per printing material.
// The dynamic configuration is also used to store user modifications of the print global parameters,
// so the modified configuration values may be diffed against the active configuration
// to invalidate the proper slicing resp. g-code generation processing steps.
class DynamicPrintConfig : public DynamicConfig
{
public:
    DynamicPrintConfig() {}
    DynamicPrintConfig(const DynamicPrintConfig &rhs) : DynamicConfig(rhs) {}
    DynamicPrintConfig(DynamicPrintConfig &&rhs) noexcept : DynamicConfig(std::move(rhs)) {}
    explicit DynamicPrintConfig(const StaticPrintConfig &rhs);
    explicit DynamicPrintConfig(const ConfigBase &rhs) : DynamicConfig(rhs) {}

    DynamicPrintConfig& operator=(const DynamicPrintConfig &rhs) { DynamicConfig::operator=(rhs); return *this; }
    DynamicPrintConfig& operator=(DynamicPrintConfig &&rhs) noexcept { DynamicConfig::operator=(std::move(rhs)); return *this; }

    static DynamicPrintConfig  full_print_config();
    static DynamicPrintConfig  full_print_config_with(const t_config_option_key &opt_key, const std::string &str, bool append = false) {
        auto config = DynamicPrintConfig::full_print_config();
        config.set_deserialize_strict(opt_key, str, append);
        return config;
    }
    static DynamicPrintConfig  full_print_config_with(std::initializer_list<SetDeserializeItem> items) {
        auto config = DynamicPrintConfig::full_print_config();
        config.set_deserialize_strict(items);
        return config;
    }
    static DynamicPrintConfig  new_with(const t_config_option_key &opt_key, const std::string &str, bool append = false) {
        DynamicPrintConfig config;
        config.set_deserialize_strict(opt_key, str, append);
        return config;
    }
    static DynamicPrintConfig  new_with(std::initializer_list<SetDeserializeItem> items) {
        DynamicPrintConfig config;
        config.set_deserialize_strict(items);
        return config;
    }
    static DynamicPrintConfig* new_from_defaults_keys(const std::vector<std::string> &keys);

    // Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here.
    const ConfigDef*    def() const override { return &print_config_def; }

    void                normalize_fdm();

    void                set_num_extruders(unsigned int num_extruders);

    // Validate the PrintConfig. Returns an empty string on success, otherwise an error message is returned.
    std::string         validate();

    // Verify whether the opt_key has not been obsoleted or renamed.
    // Both opt_key and value may be modified by handle_legacy().
    // If the opt_key is no more valid in this version of Slic3r, opt_key is cleared by handle_legacy().
    // handle_legacy() is called internally by set_deserialize().
    void                handle_legacy(t_config_option_key &opt_key, std::string &value) const override
        { PrintConfigDef::handle_legacy(opt_key, value); }

    // Called after a config is loaded as a whole.
    // Perform composite conversions, for example merging multiple keys into one key.
    // For conversion of single options, the handle_legacy() method above is called.
    void                handle_legacy_composite() override
        { PrintConfigDef::handle_legacy_composite(*this); }
};

// This vector containes list of parameters for preview of tilt profiles
const std::vector<std::string>& tilt_options();

void update_tilts_by_mode(DynamicPrintConfig& config, int tilt_mode, bool is_sl1_model);
void handle_legacy_sla(DynamicPrintConfig &config);

class StaticPrintConfig : public StaticConfig
{
public:
    StaticPrintConfig() {}

    // Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here.
    const ConfigDef*    def() const override { return &print_config_def; }
    // Reference to the cached list of keys.
    virtual const t_config_option_keys& keys_ref() const = 0;

protected:
    // Verify whether the opt_key has not been obsoleted or renamed.
    // Both opt_key and value may be modified by handle_legacy().
    // If the opt_key is no more valid in this version of Slic3r, opt_key is cleared by handle_legacy().
    // handle_legacy() is called internally by set_deserialize().
    void                handle_legacy(t_config_option_key &opt_key, std::string &value) const override
        { PrintConfigDef::handle_legacy(opt_key, value); }

    // Internal class for keeping a dynamic map to static options.
    class StaticCacheBase
    {
    public:
        // To be called during the StaticCache setup.
        // Add one ConfigOption into m_map_name_to_offset.
        template<typename T>
        void                opt_add(const std::string &name, const char *base_ptr, const T &opt)
        {
            assert(m_map_name_to_offset.find(name) == m_map_name_to_offset.end());
            m_map_name_to_offset[name] = (const char*)&opt - base_ptr;
        }

    protected:
        std::map<std::string, ptrdiff_t>    m_map_name_to_offset;
    };

    // Parametrized by the type of the topmost class owning the options.
    template<typename T>
    class StaticCache : public StaticCacheBase
    {
    public:
        // Calling the constructor of m_defaults with 0 forces m_defaults to not run the initialization.
        StaticCache() : m_defaults(nullptr) {}
        ~StaticCache() { delete m_defaults; m_defaults = nullptr; }

        bool                initialized() const { return ! m_keys.empty(); }

        ConfigOption*       optptr(const std::string &name, T *owner) const
        {
            const auto it = m_map_name_to_offset.find(name);
            return (it == m_map_name_to_offset.end()) ? nullptr : reinterpret_cast<ConfigOption*>((char*)owner + it->second);
        }

        const ConfigOption* optptr(const std::string &name, const T *owner) const
        {
            const auto it = m_map_name_to_offset.find(name);
            return (it == m_map_name_to_offset.end()) ? nullptr : reinterpret_cast<const ConfigOption*>((const char*)owner + it->second);
        }

        const std::vector<std::string>& keys()      const { return m_keys; }
        const T&                        defaults()  const { return *m_defaults; }

        // To be called during the StaticCache setup.
        // Collect option keys from m_map_name_to_offset,
        // assign default values to m_defaults.
        void                finalize(T *defaults, const ConfigDef *defs)
        {
            assert(defs != nullptr);
            m_defaults = defaults;
            m_keys.clear();
            m_keys.reserve(m_map_name_to_offset.size());
            for (const auto &kvp : defs->options) {
                // Find the option given the option name kvp.first by an offset from (char*)m_defaults.
                ConfigOption *opt = this->optptr(kvp.first, m_defaults);
                if (opt == nullptr)
                    // This option is not defined by the ConfigBase of type T.
                    continue;
                m_keys.emplace_back(kvp.first);
                const ConfigOptionDef *def = defs->get(kvp.first);
                assert(def != nullptr);
                if (def->default_value)
                    opt->set(def->default_value.get());
            }
        }

    private:
        T                                  *m_defaults;
        std::vector<std::string>            m_keys;
    };
};

#define STATIC_PRINT_CONFIG_CACHE_BASE(CLASS_NAME) \
public: \
    /* Overrides ConfigBase::optptr(). Find ando/or create a ConfigOption instance for a given name. */ \
    const ConfigOption*      optptr(const t_config_option_key &opt_key) const override \
        { return s_cache_##CLASS_NAME.optptr(opt_key, this); } \
    /* Overrides ConfigBase::optptr(). Find ando/or create a ConfigOption instance for a given name. */ \
    ConfigOption*            optptr(const t_config_option_key &opt_key, bool create = false) override \
        { return s_cache_##CLASS_NAME.optptr(opt_key, this); } \
    /* Overrides ConfigBase::keys(). Collect names of all configuration values maintained by this configuration store. */ \
    t_config_option_keys     keys() const override { return s_cache_##CLASS_NAME.keys(); } \
    const t_config_option_keys& keys_ref() const override { return s_cache_##CLASS_NAME.keys(); } \
    static const CLASS_NAME& defaults() { assert(s_cache_##CLASS_NAME.initialized()); return s_cache_##CLASS_NAME.defaults(); } \
private: \
    friend int print_config_static_initializer(); \
    static void initialize_cache() \
    { \
        assert(! s_cache_##CLASS_NAME.initialized()); \
        if (! s_cache_##CLASS_NAME.initialized()) { \
            CLASS_NAME *inst = new CLASS_NAME(1); \
            inst->initialize(s_cache_##CLASS_NAME, (const char*)inst); \
            s_cache_##CLASS_NAME.finalize(inst, inst->def()); \
        } \
    } \
    /* Cache object holding a key/option map, a list of option keys and a copy of this static config initialized with the defaults. */ \
    static StaticPrintConfig::StaticCache<CLASS_NAME> s_cache_##CLASS_NAME;

#define STATIC_PRINT_CONFIG_CACHE(CLASS_NAME) \
    STATIC_PRINT_CONFIG_CACHE_BASE(CLASS_NAME) \
public: \
    /* Public default constructor will initialize the key/option cache and the default object copy if needed. */ \
    CLASS_NAME() { assert(s_cache_##CLASS_NAME.initialized()); *this = s_cache_##CLASS_NAME.defaults(); } \
protected: \
    /* Protected constructor to be called when compounded. */ \
    CLASS_NAME(int) {}

#define STATIC_PRINT_CONFIG_CACHE_DERIVED(CLASS_NAME) \
    STATIC_PRINT_CONFIG_CACHE_BASE(CLASS_NAME) \
public: \
    /* Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here. */ \
    const ConfigDef*    def() const override { return &print_config_def; } \
    /* Handle legacy and obsoleted config keys */ \
    void                handle_legacy(t_config_option_key &opt_key, std::string &value) const override \
        { PrintConfigDef::handle_legacy(opt_key, value); }

#define PRINT_CONFIG_CLASS_ELEMENT_DEFINITION(r, data, elem) BOOST_PP_TUPLE_ELEM(0, elem) BOOST_PP_TUPLE_ELEM(1, elem);
#define PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION2(KEY) cache.opt_add(BOOST_PP_STRINGIZE(KEY), base_ptr, this->KEY);
#define PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION(r, data, elem) PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION2(BOOST_PP_TUPLE_ELEM(1, elem))
#define PRINT_CONFIG_CLASS_ELEMENT_HASH(r, data, elem) boost::hash_combine(seed, BOOST_PP_TUPLE_ELEM(1, elem).hash());
#define PRINT_CONFIG_CLASS_ELEMENT_EQUAL(r, data, elem) if (! (BOOST_PP_TUPLE_ELEM(1, elem) == rhs.BOOST_PP_TUPLE_ELEM(1, elem))) return false;
#define PRINT_CONFIG_CLASS_ELEMENT_LOWER(r, data, elem) \
        if (BOOST_PP_TUPLE_ELEM(1, elem) < rhs.BOOST_PP_TUPLE_ELEM(1, elem)) return true; \
        if (! (BOOST_PP_TUPLE_ELEM(1, elem) == rhs.BOOST_PP_TUPLE_ELEM(1, elem))) return false;

#define PRINT_CONFIG_CLASS_DEFINE(CLASS_NAME, PARAMETER_DEFINITION_SEQ) \
class CLASS_NAME : public StaticPrintConfig { \
    STATIC_PRINT_CONFIG_CACHE(CLASS_NAME) \
public: \
    BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_DEFINITION, _, PARAMETER_DEFINITION_SEQ) \
    size_t hash() const throw() \
    { \
        size_t seed = 0; \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_HASH, _, PARAMETER_DEFINITION_SEQ) \
        return seed; \
    } \
    bool operator==(const CLASS_NAME &rhs) const throw() \
    { \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_EQUAL, _, PARAMETER_DEFINITION_SEQ) \
        return true; \
    } \
    bool operator!=(const CLASS_NAME &rhs) const throw() { return ! (*this == rhs); } \
    bool operator<(const CLASS_NAME &rhs) const throw() \
    { \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_LOWER, _, PARAMETER_DEFINITION_SEQ) \
        return false; \
    } \
protected: \
    void initialize(StaticCacheBase &cache, const char *base_ptr) \
    { \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION, _, PARAMETER_DEFINITION_SEQ) \
    } \
};

#define PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST_ITEM(r, data, i, elem) BOOST_PP_COMMA_IF(i) public elem
#define PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST(CLASSES_PARENTS_TUPLE) BOOST_PP_SEQ_FOR_EACH_I(PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST_ITEM, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE))
#define PRINT_CONFIG_CLASS_DERIVED_INITIALIZER_ITEM(r, VALUE, i, elem) BOOST_PP_COMMA_IF(i) elem(VALUE)
#define PRINT_CONFIG_CLASS_DERIVED_INITIALIZER(CLASSES_PARENTS_TUPLE, VALUE) BOOST_PP_SEQ_FOR_EACH_I(PRINT_CONFIG_CLASS_DERIVED_INITIALIZER_ITEM, VALUE, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE))
#define PRINT_CONFIG_CLASS_DERIVED_INITCACHE_ITEM(r, data, elem) this->elem::initialize(cache, base_ptr);
#define PRINT_CONFIG_CLASS_DERIVED_INITCACHE(CLASSES_PARENTS_TUPLE) BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_DERIVED_INITCACHE_ITEM, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE))
#define PRINT_CONFIG_CLASS_DERIVED_HASH(r, data, elem) boost::hash_combine(seed, static_cast<const elem*>(this)->hash());
#define PRINT_CONFIG_CLASS_DERIVED_EQUAL(r, data, elem) \
    if (! (*static_cast<const elem*>(this) == static_cast<const elem&>(rhs))) return false;

// Generic version, with or without new parameters. Don't use this directly.
#define PRINT_CONFIG_CLASS_DERIVED_DEFINE1(CLASS_NAME, CLASSES_PARENTS_TUPLE, PARAMETER_DEFINITION, PARAMETER_REGISTRATION, PARAMETER_HASHES, PARAMETER_EQUALS) \
class CLASS_NAME : PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST(CLASSES_PARENTS_TUPLE) { \
    STATIC_PRINT_CONFIG_CACHE_DERIVED(CLASS_NAME) \
    CLASS_NAME() : PRINT_CONFIG_CLASS_DERIVED_INITIALIZER(CLASSES_PARENTS_TUPLE, 0) { assert(s_cache_##CLASS_NAME.initialized()); *this = s_cache_##CLASS_NAME.defaults(); } \
public: \
    PARAMETER_DEFINITION \
    size_t hash() const throw() \
    { \
        size_t seed = 0; \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_DERIVED_HASH, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE)) \
        PARAMETER_HASHES \
        return seed; \
    } \
    bool operator==(const CLASS_NAME &rhs) const throw() \
    { \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_DERIVED_EQUAL, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE)) \
        PARAMETER_EQUALS \
        return true; \
    } \
    bool operator!=(const CLASS_NAME &rhs) const throw() { return ! (*this == rhs); } \
protected: \
    CLASS_NAME(int) : PRINT_CONFIG_CLASS_DERIVED_INITIALIZER(CLASSES_PARENTS_TUPLE, 1) {} \
    void initialize(StaticCacheBase &cache, const char* base_ptr) { \
        PRINT_CONFIG_CLASS_DERIVED_INITCACHE(CLASSES_PARENTS_TUPLE) \
        PARAMETER_REGISTRATION \
    } \
};
// Variant without adding new parameters.
#define PRINT_CONFIG_CLASS_DERIVED_DEFINE0(CLASS_NAME, CLASSES_PARENTS_TUPLE) \
    PRINT_CONFIG_CLASS_DERIVED_DEFINE1(CLASS_NAME, CLASSES_PARENTS_TUPLE, BOOST_PP_EMPTY(), BOOST_PP_EMPTY(), BOOST_PP_EMPTY(), BOOST_PP_EMPTY())
// Variant with adding new parameters.
#define PRINT_CONFIG_CLASS_DERIVED_DEFINE(CLASS_NAME, CLASSES_PARENTS_TUPLE, PARAMETER_DEFINITION_SEQ) \
    PRINT_CONFIG_CLASS_DERIVED_DEFINE1(CLASS_NAME, CLASSES_PARENTS_TUPLE, \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_DEFINITION, _, PARAMETER_DEFINITION_SEQ), \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION, _, PARAMETER_DEFINITION_SEQ), \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_HASH, _, PARAMETER_DEFINITION_SEQ), \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_EQUAL, _, PARAMETER_DEFINITION_SEQ))

PRINT_CONFIG_CLASS_DEFINE(
    PrintObjectConfig,

    ((ConfigOptionFloat,               brim_separation))
    ((ConfigOptionEnum<BrimType>,      brim_type))
    ((ConfigOptionFloat,               brim_width))
    ((ConfigOptionBool,                dont_support_bridges))
    //w28
    ((ConfigOptionFloat,                max_bridge_length))
    ((ConfigOptionFloat,               elefant_foot_compensation))
    //w26
    ((ConfigOptionInt,                 elefant_foot_compensation_layers))
    ((ConfigOptionFloatOrPercent,      extrusion_width))
    ((ConfigOptionFloat,               first_layer_acceleration_over_raft))
    ((ConfigOptionFloatOrPercent,      first_layer_speed_over_raft))
    // ((ConfigOptionBool,                infill_only_where_needed))
    // Force the generation of solid shells between adjacent materials/volumes.
    ((ConfigOptionBool,                interface_shells))
    ((ConfigOptionFloat,               layer_height))
    ((ConfigOptionFloat,               mmu_segmented_region_max_width))
    ((ConfigOptionFloat,               mmu_segmented_region_interlocking_depth))
    ((ConfigOptionFloat,               raft_contact_distance))
    ((ConfigOptionFloat,               raft_expansion))
    ((ConfigOptionPercent,             raft_first_layer_density))
    ((ConfigOptionFloat,               raft_first_layer_expansion))
    ((ConfigOptionInt,                 raft_layers))
    ((ConfigOptionEnum<SeamPosition>,  seam_position))
    ((ConfigOptionBool,                staggered_inner_seams))
//  ((ConfigOptionFloat,               seam_preferred_direction))
//  ((ConfigOptionFloat,               seam_preferred_direction_jitter))
    ((ConfigOptionFloat,               slice_closing_radius))
    ((ConfigOptionEnum<SlicingMode>,   slicing_mode))
    ((ConfigOptionEnum<PerimeterGeneratorType>, perimeter_generator))
    ((ConfigOptionFloatOrPercent,      wall_transition_length))
    ((ConfigOptionFloatOrPercent,      wall_transition_filter_deviation))
    ((ConfigOptionFloat,               wall_transition_angle))
    ((ConfigOptionInt,                 wall_distribution_count))
    ((ConfigOptionFloatOrPercent,      min_feature_size))
    ((ConfigOptionFloatOrPercent,      min_bead_width))
    ((ConfigOptionBool,                support_material))
    // Automatic supports (generated based fdm support point generator).
    ((ConfigOptionBool,                support_material_auto))
    // Direction of the support pattern (in XY plane).`
    ((ConfigOptionFloat,               support_material_angle))
    ((ConfigOptionBool,                support_material_buildplate_only))
    ((ConfigOptionFloat,               support_material_contact_distance))
    ((ConfigOptionFloat,               support_material_bottom_contact_distance))
    ((ConfigOptionInt,                 support_material_enforce_layers))
    ((ConfigOptionInt,                 support_material_extruder))
    ((ConfigOptionFloatOrPercent,      support_material_extrusion_width))
    ((ConfigOptionBool,                support_material_interface_contact_loops))
    ((ConfigOptionInt,                 support_material_interface_extruder))
    ((ConfigOptionInt,                 support_material_interface_layers))
    ((ConfigOptionInt,                 support_material_bottom_interface_layers))
    // Spacing between interface lines (the hatching distance). Set zero to get a solid interface.
    ((ConfigOptionFloat,               support_material_interface_spacing))
    ((ConfigOptionFloatOrPercent,      support_material_interface_speed))
    ((ConfigOptionEnum<SupportMaterialPattern>, support_material_pattern))
    ((ConfigOptionEnum<SupportMaterialInterfacePattern>, support_material_interface_pattern))
    // Morphological closing of support areas. Only used for "sung" supports.
    ((ConfigOptionFloat,               support_material_closing_radius))
    // Spacing between support material lines (the hatching distance).
    ((ConfigOptionFloat,               support_material_spacing))
    ((ConfigOptionFloat,               support_material_speed))
    ((ConfigOptionEnum<SupportMaterialStyle>, support_material_style))
    ((ConfigOptionBool,                support_material_synchronize_layers))
    // Overhang angle threshold.
    ((ConfigOptionInt,                 support_material_threshold))
    ((ConfigOptionBool,                support_material_with_sheath))
    ((ConfigOptionFloatOrPercent,      support_material_xy_spacing))
    // Tree supports
    ((ConfigOptionFloat,               support_tree_angle))
    ((ConfigOptionFloat,               support_tree_angle_slow))
    ((ConfigOptionFloat,               support_tree_branch_diameter))
    ((ConfigOptionFloat,               support_tree_branch_diameter_angle))
    ((ConfigOptionFloat,               support_tree_branch_diameter_double_wall))
    ((ConfigOptionPercent,             support_tree_top_rate))
    ((ConfigOptionFloat,               support_tree_branch_distance))
    ((ConfigOptionFloat,               support_tree_tip_diameter))
    // The rest
    ((ConfigOptionBool,                thick_bridges))
    ((ConfigOptionFloat,               xy_size_compensation))
    //w12
    ((ConfigOptionFloat, xy_hole_compensation))
    ((ConfigOptionFloat, xy_contour_compensation))
    ((ConfigOptionBool,                wipe_into_objects))

    ((ConfigOptionBool,                interlocking_beam))
    ((ConfigOptionFloat,               interlocking_beam_width))
    ((ConfigOptionFloat,               interlocking_orientation))
    ((ConfigOptionInt,                 interlocking_beam_layer_count))
    ((ConfigOptionInt,                 interlocking_depth))
    ((ConfigOptionInt,                 interlocking_boundary_avoidance))
    //w11
    ((ConfigOptionBool,                detect_narrow_internal_solid_infill))
    //w21
    ((ConfigOptionFloat,               filter_top_gap_infill))
    //w27
    ((ConfigOptionBool, precise_z_height))
)

PRINT_CONFIG_CLASS_DEFINE(
    PrintRegionConfig,

    ((ConfigOptionBool,                 automatic_infill_combination))
    ((ConfigOptionFloatOrPercent,       automatic_infill_combination_max_layer_height))
    ((ConfigOptionFloat,                bridge_angle))
    ((ConfigOptionInt,                  bottom_solid_layers))
    ((ConfigOptionFloat,                bottom_solid_min_thickness))
    ((ConfigOptionFloat,                bridge_flow_ratio))
    ((ConfigOptionFloat,                bridge_speed))
    ((ConfigOptionEnum<EnsureVerticalShellThickness>, ensure_vertical_shell_thickness))
    ((ConfigOptionFloatOrPercent,       over_bridge_speed))
    ((ConfigOptionEnum<InfillPattern>,  top_fill_pattern))
    ((ConfigOptionEnum<InfillPattern>,  bottom_fill_pattern))
    ((ConfigOptionFloatOrPercent,       external_perimeter_extrusion_width))
    ((ConfigOptionFloatOrPercent,       external_perimeter_speed))
    ((ConfigOptionBool,                 enable_dynamic_overhang_speeds))
    ((ConfigOptionFloatOrPercent,       overhang_speed_0))
    ((ConfigOptionFloatOrPercent,       overhang_speed_1))
    ((ConfigOptionFloatOrPercent,       overhang_speed_2))
    ((ConfigOptionFloatOrPercent,       overhang_speed_3))
    //Y27
    ((ConfigOptionBool,                resonance_avoidance))
    ((ConfigOptionFloat,               min_resonance_avoidance_speed))
    ((ConfigOptionFloat,               max_resonance_avoidance_speed))
    ((ConfigOptionBool,                 external_perimeters_first))
    ((ConfigOptionBool,                 extra_perimeters))
    ((ConfigOptionBool,                 extra_perimeters_on_overhangs))
    ((ConfigOptionFloat,                fill_angle))
    ((ConfigOptionPercent,              fill_density))
    ((ConfigOptionEnum<InfillPattern>,  fill_pattern))
    ((ConfigOptionEnum<FuzzySkinType>,  fuzzy_skin))
    ((ConfigOptionFloat,                fuzzy_skin_thickness))
    ((ConfigOptionFloat,                fuzzy_skin_point_dist))
    ((ConfigOptionBool,                 gap_fill_enabled))
    ((ConfigOptionFloat,                gap_fill_speed))
    ((ConfigOptionFloatOrPercent,       infill_anchor))
    ((ConfigOptionFloatOrPercent,       infill_anchor_max))
    ((ConfigOptionInt,                  infill_extruder))
    ((ConfigOptionFloatOrPercent,       infill_extrusion_width))
    ((ConfigOptionInt,                  infill_every_layers))
    ((ConfigOptionFloatOrPercent,       infill_overlap))
    ((ConfigOptionFloat,                infill_speed))
    // Ironing options
    ((ConfigOptionBool,                 ironing))
    ((ConfigOptionEnum<IroningType>,    ironing_type))
    //w33
    ((ConfigOptionEnum<InfillPattern>,  ironing_pattern))
    ((ConfigOptionPercent,              ironing_flowrate))
    ((ConfigOptionFloat,                ironing_spacing))
    ((ConfigOptionFloat,                ironing_speed))
    // Detect bridging perimeters
    ((ConfigOptionBool,                 overhangs))
    ((ConfigOptionInt,                  perimeter_extruder))
    ((ConfigOptionFloatOrPercent,       perimeter_extrusion_width))
    ((ConfigOptionFloat,                perimeter_speed))
    // Total number of perimeters.
    ((ConfigOptionInt,                  perimeters))
    ((ConfigOptionFloatOrPercent,       small_perimeter_speed))
    ((ConfigOptionFloat,                solid_infill_below_area))
    ((ConfigOptionInt,                  solid_infill_extruder))
    ((ConfigOptionFloatOrPercent,       solid_infill_extrusion_width))
    ((ConfigOptionInt,                  solid_infill_every_layers))
    ((ConfigOptionFloatOrPercent,       solid_infill_speed))
    // Detect thin walls.
    ((ConfigOptionBool,                 thin_walls))
    ((ConfigOptionFloatOrPercent,       top_infill_extrusion_width))
    ((ConfigOptionInt,                  top_solid_layers))
    ((ConfigOptionFloat,                top_solid_min_thickness))
    ((ConfigOptionFloatOrPercent,       top_solid_infill_speed))
    ((ConfigOptionBool,                 wipe_into_infill))
    //w30
    ((ConfigOptionFloat, top_solid_infill_flow_ratio))
    ((ConfigOptionFloat, bottom_solid_infill_flow_ratio))

    //w39
    ((ConfigOptionBool, precise_outer_wall))
    // Single perimeter.
    ((ConfigOptionEnum<TopOnePerimeterType>, top_one_perimeter_type))
    ((ConfigOptionBool,                 only_one_perimeter_first_layer))

    ((ConfigOptionEnum<ScarfSeamPlacement>, scarf_seam_placement))
    ((ConfigOptionBool,                     scarf_seam_only_on_smooth))
    ((ConfigOptionPercent,                  scarf_seam_start_height))
    ((ConfigOptionBool,                     scarf_seam_entire_loop))
    ((ConfigOptionFloat,                    scarf_seam_length))
    ((ConfigOptionFloat,                    scarf_seam_max_segment_length))
    ((ConfigOptionBool,                     scarf_seam_on_inner_perimeters))
)

PRINT_CONFIG_CLASS_DEFINE(
    MachineEnvelopeConfig,

    // Allowing the machine limits to be completely ignored or used just for time estimator.
    ((ConfigOptionEnum<MachineLimitsUsage>, machine_limits_usage))
    // M201 X... Y... Z... E... [mm/sec^2]
    ((ConfigOptionFloats,               machine_max_acceleration_x))
    ((ConfigOptionFloats,               machine_max_acceleration_y))
    ((ConfigOptionFloats,               machine_max_acceleration_z))
    ((ConfigOptionFloats,               machine_max_acceleration_e))
    // M203 X... Y... Z... E... [mm/sec]
    ((ConfigOptionFloats,               machine_max_feedrate_x))
    ((ConfigOptionFloats,               machine_max_feedrate_y))
    ((ConfigOptionFloats,               machine_max_feedrate_z))
    ((ConfigOptionFloats,               machine_max_feedrate_e))

    // M204 P... R... T...[mm/sec^2]
    ((ConfigOptionFloats,               machine_max_acceleration_extruding))
    ((ConfigOptionFloats,               machine_max_acceleration_retracting))
    ((ConfigOptionFloats,               machine_max_acceleration_travel))

    // M205 X... Y... Z... E... [mm/sec]
    ((ConfigOptionFloats,               machine_max_jerk_x))
    ((ConfigOptionFloats,               machine_max_jerk_y))
    ((ConfigOptionFloats,               machine_max_jerk_z))
    ((ConfigOptionFloats,               machine_max_jerk_e))
    // M205 T... [mm/sec]
    ((ConfigOptionFloats,               machine_min_travel_rate))
    // M205 S... [mm/sec]
    ((ConfigOptionFloats,               machine_min_extruding_rate))
)

PRINT_CONFIG_CLASS_DEFINE(
    GCodeConfig,

    ((ConfigOptionEnum<ArcFittingType>, arc_fitting))
    ((ConfigOptionBool,                autoemit_temperature_commands))
    ((ConfigOptionInt,                 bed_temperature_extruder))
    ((ConfigOptionString,              before_layer_gcode))
    ((ConfigOptionString,              between_objects_gcode))
    ((ConfigOptionBool,                binary_gcode))
    ((ConfigOptionFloats,              deretract_speed))
    ((ConfigOptionString,              end_gcode))
    ((ConfigOptionStrings,             end_filament_gcode))
    ((ConfigOptionString,              extrusion_axis))
    ((ConfigOptionFloats,              extrusion_multiplier))
    ((ConfigOptionFloats,              filament_diameter))
    ((ConfigOptionFloats,              filament_density))
    ((ConfigOptionStrings,             filament_type))
    ((ConfigOptionBools,               filament_soluble))
    ((ConfigOptionBools,               filament_abrasive))
    ((ConfigOptionFloats,              filament_cost))
    ((ConfigOptionFloats,              filament_spool_weight))
    ((ConfigOptionFloats,              filament_max_volumetric_speed))
    ((ConfigOptionFloats,              filament_infill_max_speed))
    ((ConfigOptionFloats,              filament_infill_max_crossing_speed))
    ((ConfigOptionFloats,              filament_loading_speed))
    ((ConfigOptionFloats,              filament_loading_speed_start))
    ((ConfigOptionFloats,              filament_load_time))
    ((ConfigOptionFloats,              filament_unloading_speed))
    ((ConfigOptionFloats,              filament_unloading_speed_start))
    ((ConfigOptionFloats,              filament_toolchange_delay))
    ((ConfigOptionFloats,              filament_unload_time))
    ((ConfigOptionInts,                filament_cooling_moves))
    ((ConfigOptionFloats,              filament_cooling_initial_speed))
    ((ConfigOptionFloats,              filament_minimal_purge_on_wipe_tower))
    ((ConfigOptionFloats,              filament_cooling_final_speed))
    ((ConfigOptionPercents,            filament_purge_multiplier))
    ((ConfigOptionStrings,             filament_ramming_parameters))
    ((ConfigOptionBools,               filament_multitool_ramming))
    ((ConfigOptionFloats,              filament_multitool_ramming_volume))
    ((ConfigOptionFloats,              filament_multitool_ramming_flow))
    ((ConfigOptionFloats,              filament_stamping_loading_speed))
    ((ConfigOptionFloats,              filament_stamping_distance))
    ((ConfigOptionFloatsOrPercentsNullable, filament_seam_gap_distance))
    ((ConfigOptionPercents,            filament_shrinkage_compensation_xy))
    ((ConfigOptionPercents,            filament_shrinkage_compensation_z))
    ((ConfigOptionBool,                gcode_comments))
    ((ConfigOptionEnum<GCodeFlavor>,   gcode_flavor))
    ((ConfigOptionEnum<LabelObjectsStyle>,  gcode_label_objects))
    // Triples of strings: "search pattern", "replace with pattern", "attribs"
    // where "attribs" are one of:
    //      r - regular expression
    //      i - case insensitive
    //      w - whole word
    ((ConfigOptionStrings,             gcode_substitutions))
    ((ConfigOptionString,              layer_gcode))
    ((ConfigOptionFloat,               max_print_speed))
    ((ConfigOptionFloat,               max_volumetric_speed))
    ((ConfigOptionFloat,               max_volumetric_extrusion_rate_slope_positive))
    ((ConfigOptionFloat,               max_volumetric_extrusion_rate_slope_negative))
    ((ConfigOptionBools,               travel_ramping_lift))
    ((ConfigOptionFloats,              travel_max_lift))
    ((ConfigOptionFloats,              travel_slope))
    ((ConfigOptionBools,               travel_lift_before_obstacle))
    ((ConfigOptionBools,               nozzle_high_flow))
    ((ConfigOptionPercents,            retract_before_wipe))
    ((ConfigOptionFloats,              retract_length))
    ((ConfigOptionFloats,              retract_length_toolchange))
    ((ConfigOptionFloats,              retract_lift))
    ((ConfigOptionFloats,              retract_lift_above))
    ((ConfigOptionFloats,              retract_lift_below))
    ((ConfigOptionFloats,              retract_restart_extra))
    ((ConfigOptionFloats,              retract_restart_extra_toolchange))
    ((ConfigOptionFloats,              retract_speed))
    ((ConfigOptionFloatOrPercent,      seam_gap_distance))
    ((ConfigOptionString,              start_gcode))
    ((ConfigOptionStrings,             start_filament_gcode))
    ((ConfigOptionBool,                single_extruder_multi_material))
    //Y25
    ((ConfigOptionBool,                wipe_device))
    ((ConfigOptionBool,                single_extruder_multi_material_priming))
    ((ConfigOptionBool,                wipe_tower_no_sparse_layers))
    ((ConfigOptionString,              toolchange_gcode))
    ((ConfigOptionFloat,               travel_speed))

    //B36
    ((ConfigOptionFloat, first_layer_travel_speed))

    ((ConfigOptionFloat,               travel_speed_z))
    ((ConfigOptionBool,                use_firmware_retraction))
    ((ConfigOptionBool,                use_relative_e_distances))
    ((ConfigOptionBool,                use_volumetric_e))
    ((ConfigOptionBool,                variable_layer_height))
    ((ConfigOptionFloat,               cooling_tube_retraction))
    ((ConfigOptionFloat,               cooling_tube_length))
    ((ConfigOptionBool,                high_current_on_filament_swap))
    ((ConfigOptionFloat,               parking_pos_retraction))
    ((ConfigOptionBool,                remaining_times))
    ((ConfigOptionBool,                silent_mode))
    ((ConfigOptionFloat,               extra_loading_move))
    ((ConfigOptionFloat,               multimaterial_purging))
    ((ConfigOptionString,              color_change_gcode))
    ((ConfigOptionString,              pause_print_gcode))
    ((ConfigOptionString,              template_custom_gcode))
    //w15
    ((ConfigOptionFloats,              wipe_distance))
    //y25
    ((ConfigOptionStrings,             filament_id))
)

static inline std::string get_extrusion_axis(const GCodeConfig &cfg)
{
    return
        ((cfg.gcode_flavor.value == gcfMach3) || (cfg.gcode_flavor.value == gcfMachinekit)) ? "A" :
        (cfg.gcode_flavor.value == gcfNoExtrusion) ? "" : cfg.extrusion_axis.value;
}

PRINT_CONFIG_CLASS_DERIVED_DEFINE(
    PrintConfig, 
    (MachineEnvelopeConfig, GCodeConfig),

    ((ConfigOptionBool,               automatic_extrusion_widths))
    ((ConfigOptionBool,               avoid_crossing_curled_overhangs))
    ((ConfigOptionBool,               avoid_crossing_perimeters))
    ((ConfigOptionFloatOrPercent,     avoid_crossing_perimeters_max_detour))
    ((ConfigOptionPoints,             bed_shape))
    //Y20 //B52
    ((ConfigOptionPoints,             bed_exclude_area))
    ((ConfigOptionInts,               bed_temperature))
    //Y16
    ((ConfigOptionBool,               chamber_temperature_control))
    //Y26
    ((ConfigOptionBool,               seal_print))
    ((ConfigOptionFloat,              bridge_acceleration))
    ((ConfigOptionInts,               bridge_fan_speed))
    ((ConfigOptionBools,              enable_dynamic_fan_speeds))
    ((ConfigOptionInts,               overhang_fan_speed_0))
    ((ConfigOptionInts,               overhang_fan_speed_1))
    ((ConfigOptionInts,               overhang_fan_speed_2))
    ((ConfigOptionInts,               overhang_fan_speed_3))
    ((ConfigOptionInts,               chamber_temperature))
    ((ConfigOptionInts,               chamber_minimal_temperature))
    //Y30
    ((ConfigOptionInts,               box_temperature))
    ((ConfigOptionBool,               box_temperature_control))
    ((ConfigOptionBool,               complete_objects))
    ((ConfigOptionFloats,             colorprint_heights))
    ((ConfigOptionBools,              cooling))
    //Y16
    ((ConfigOptionBool,               auxiliary_fan))
    ((ConfigOptionBool,               chamber_fan))
    //B25
    ((ConfigOptionInts,               enable_volume_fan))
    //B15
    ((ConfigOptionInts,               enable_auxiliary_fan))
    //Y26
    ((ConfigOptionInts,               enable_auxiliary_fan_unseal))
    ((ConfigOptionFloat,              default_acceleration))
    ((ConfigOptionInts,               disable_fan_first_layers))
    //B39
    ((ConfigOptionInts,               disable_rapid_cooling_fan_first_layers))
    ((ConfigOptionEnum<DraftShield>,  draft_shield))
    ((ConfigOptionFloat,              duplicate_distance))
    ((ConfigOptionFloat,              external_perimeter_acceleration))
    ((ConfigOptionFloat,              extruder_clearance_height))
    ((ConfigOptionFloat,              extruder_clearance_radius))
    ((ConfigOptionStrings,            extruder_colour))
    ((ConfigOptionPoints,             extruder_offset))
    ((ConfigOptionBools,              fan_always_on))
    ((ConfigOptionInts,               fan_below_layer_time))
    ((ConfigOptionStrings,            filament_colour))
    ((ConfigOptionStrings,            filament_notes))
    ((ConfigOptionFloat,              first_layer_acceleration))
    ((ConfigOptionInts,               first_layer_bed_temperature))
    ((ConfigOptionFloatOrPercent,     first_layer_extrusion_width))
    ((ConfigOptionFloatOrPercent,     first_layer_height))
    ((ConfigOptionFloatOrPercent,     first_layer_speed))
    ((ConfigOptionFloatOrPercent,     first_layer_infill_speed))

    //w25
    ((ConfigOptionInt,                slow_down_layers))
    ((ConfigOptionInts,               first_layer_temperature))
    //Y30
    ((ConfigOptionInts,               filament_flush_temp))
    ((ConfigOptionIntsNullable,       idle_temperature))
    //B26
    ((ConfigOptionBools,              enable_advance_pressure))
    ((ConfigOptionFloats,             advance_pressure))
    ((ConfigOptionFloats,             smooth_time))
    ((ConfigOptionInts,               full_fan_speed_layer))
    ((ConfigOptionFloat,              infill_acceleration))
    ((ConfigOptionBool,               infill_first))
    ((ConfigOptionInts,               max_fan_speed))
    ((ConfigOptionFloats,             max_layer_height))
    ((ConfigOptionInts,               min_fan_speed))
    ((ConfigOptionFloats,             min_layer_height))
    ((ConfigOptionFloat,              max_print_height))
    //Y28
    ((ConfigOptionBools,              dont_slow_down_outer_wall))
    ((ConfigOptionFloats,             min_print_speed))
    ((ConfigOptionFloat,              min_skirt_length))
    ((ConfigOptionString,             notes))
    ((ConfigOptionFloats,             nozzle_diameter))
    ((ConfigOptionBool,               only_retract_when_crossing_perimeters))
    ((ConfigOptionBool,               ooze_prevention))
    ((ConfigOptionString,             output_filename_format))
    ((ConfigOptionFloat,              perimeter_acceleration))
    ((ConfigOptionStrings,            post_process))
    ((ConfigOptionBool,               prefer_clockwise_movements))
    ((ConfigOptionString,             printer_model))
    ((ConfigOptionString,             printer_notes))
    ((ConfigOptionFloat,              resolution))
    ((ConfigOptionFloat,              gcode_resolution))
    ((ConfigOptionFloats,             retract_before_travel))
    ((ConfigOptionBools,              retract_layer_change))
    ((ConfigOptionFloat,              skirt_distance))
    ((ConfigOptionInt,                skirt_height))
    ((ConfigOptionInt,                skirts))
    ((ConfigOptionInts,               slowdown_below_layer_time))
    ((ConfigOptionFloat,              solid_infill_acceleration))
    ((ConfigOptionBool,               spiral_vase))
    ((ConfigOptionInt,                standby_temperature_delta))
    ((ConfigOptionInts,               temperature))
    ((ConfigOptionInt,                threads))
    ((ConfigOptionString,             thumbnails))
    ((ConfigOptionEnum<GCodeThumbnailsFormat>,  thumbnails_format))
    ((ConfigOptionFloat,              top_solid_infill_acceleration))
    ((ConfigOptionFloat,              travel_acceleration))
    ((ConfigOptionBools,              wipe))
    ((ConfigOptionBool,               wipe_tower))
    ((ConfigOptionFloat,              wipe_tower_acceleration))
    ((ConfigOptionFloat,              wipe_tower_width))
    ((ConfigOptionFloat,              wipe_tower_per_color_wipe))
    ((ConfigOptionFloat,              wipe_tower_brim_width))
    ((ConfigOptionFloat,              wipe_tower_cone_angle))
    ((ConfigOptionPercent,            wipe_tower_extra_spacing))
    ((ConfigOptionPercent,            wipe_tower_extra_flow))
    ((ConfigOptionFloat,              wipe_tower_bridging))
    ((ConfigOptionInt,                wipe_tower_extruder))
    ((ConfigOptionFloats,             wiping_volumes_matrix))
    ((ConfigOptionBool,               wiping_volumes_use_custom_matrix))
    ((ConfigOptionFloat,              z_offset))
    //y25
    ((ConfigOptionString,             box_id))
    //y29
    ((ConfigOptionBool,               is_support_3mf))
)

PRINT_CONFIG_CLASS_DERIVED_DEFINE0(
    FullPrintConfig,
    (PrintObjectConfig, PrintRegionConfig, PrintConfig)
)

// Validate the FullPrintConfig. Returns an empty string on success, otherwise an error message is returned.
std::string validate(const FullPrintConfig &config);

PRINT_CONFIG_CLASS_DEFINE(
    SLAPrintConfig,
    ((ConfigOptionString,     output_filename_format))
)

PRINT_CONFIG_CLASS_DEFINE(
    SLAPrintObjectConfig,

    ((ConfigOptionFloat, layer_height))

    //Number of the layers needed for the exposure time fade [3;20]
    ((ConfigOptionInt,  faded_layers))/*= 10*/

    ((ConfigOptionFloat, slice_closing_radius))
    ((ConfigOptionEnum<SlicingMode>, slicing_mode))

    // Enabling or disabling support creation
    ((ConfigOptionBool,  supports_enable))

    ((ConfigOptionEnum<sla::SupportTreeType>, support_tree_type))

    // Diameter in mm of the pointing side of the head.
    ((ConfigOptionFloat, support_head_front_diameter))/*= 0.2*/

    // How much the pinhead has to penetrate the model surface
    ((ConfigOptionFloat, support_head_penetration))/*= 0.2*/

    // Width in mm from the back sphere center to the front sphere center.
    ((ConfigOptionFloat, support_head_width))/*= 1.0*/

    // Radius in mm of the support pillars.
    ((ConfigOptionFloat, support_pillar_diameter))/*= 0.8*/

    // The percentage of smaller pillars compared to the normal pillar diameter
    // which are used in problematic areas where a normal pilla cannot fit.
    ((ConfigOptionPercent, support_small_pillar_diameter_percent))

    // How much bridge (supporting another pinhead) can be placed on a pillar.
    ((ConfigOptionInt,   support_max_bridges_on_pillar))

    // How the pillars are bridged together
    ((ConfigOptionEnum<SLAPillarConnectionMode>, support_pillar_connection_mode))

    // Generate only ground facing supports
    ((ConfigOptionBool, support_buildplate_only))

    ((ConfigOptionFloat, support_max_weight_on_model))

    // Generate only ground facing supports
    ((ConfigOptionBool, support_enforcers_only))

    // TODO: unimplemented at the moment. This coefficient will have an impact
    // when bridges and pillars are merged. The resulting pillar should be a bit
    // thicker than the ones merging into it. How much thicker? I don't know
    // but it will be derived from this value.
    ((ConfigOptionFloat, support_pillar_widening_factor))

    // Radius in mm of the pillar base.
    ((ConfigOptionFloat, support_base_diameter))/*= 2.0*/

    // The height of the pillar base cone in mm.
    ((ConfigOptionFloat, support_base_height))/*= 1.0*/

    // The minimum distance of the pillar base from the model in mm.
    ((ConfigOptionFloat, support_base_safety_distance)) /*= 1.0*/

    // The default angle for connecting support sticks and junctions.
    ((ConfigOptionFloat, support_critical_angle))/*= 45*/

    // The max length of a bridge in mm
    ((ConfigOptionFloat, support_max_bridge_length))/*= 15.0*/

    // The max distance of two pillars to get cross linked.
    ((ConfigOptionFloat, support_max_pillar_link_distance))

    // The elevation in Z direction upwards. This is the space between the pad
    // and the model object's bounding box bottom. Units in mm.
    ((ConfigOptionFloat, support_object_elevation))/*= 5.0*/


    // Branching tree

    // Diameter in mm of the pointing side of the head.
    ((ConfigOptionFloat, branchingsupport_head_front_diameter))/*= 0.2*/

    // How much the pinhead has to penetrate the model surface
    ((ConfigOptionFloat, branchingsupport_head_penetration))/*= 0.2*/

    // Width in mm from the back sphere center to the front sphere center.
    ((ConfigOptionFloat, branchingsupport_head_width))/*= 1.0*/

    // Radius in mm of the support pillars.
    ((ConfigOptionFloat, branchingsupport_pillar_diameter))/*= 0.8*/

    // The percentage of smaller pillars compared to the normal pillar diameter
    // which are used in problematic areas where a normal pilla cannot fit.
    ((ConfigOptionPercent, branchingsupport_small_pillar_diameter_percent))

    // How much bridge (supporting another pinhead) can be placed on a pillar.
    ((ConfigOptionInt,   branchingsupport_max_bridges_on_pillar))

    // How the pillars are bridged together
    ((ConfigOptionEnum<SLAPillarConnectionMode>, branchingsupport_pillar_connection_mode))

    // Generate only ground facing supports
    ((ConfigOptionBool, branchingsupport_buildplate_only))

    ((ConfigOptionFloat, branchingsupport_max_weight_on_model))

    ((ConfigOptionFloat, branchingsupport_pillar_widening_factor))

    // Radius in mm of the pillar base.
    ((ConfigOptionFloat, branchingsupport_base_diameter))/*= 2.0*/

    // The height of the pillar base cone in mm.
    ((ConfigOptionFloat, branchingsupport_base_height))/*= 1.0*/

    // The minimum distance of the pillar base from the model in mm.
    ((ConfigOptionFloat, branchingsupport_base_safety_distance)) /*= 1.0*/

    // The default angle for connecting support sticks and junctions.
    ((ConfigOptionFloat, branchingsupport_critical_angle))/*= 45*/

    // The max length of a bridge in mm
    ((ConfigOptionFloat, branchingsupport_max_bridge_length))/*= 15.0*/

    // The max distance of two pillars to get cross linked.
    ((ConfigOptionFloat, branchingsupport_max_pillar_link_distance))

    // The elevation in Z direction upwards. This is the space between the pad
    // and the model object's bounding box bottom. Units in mm.
    ((ConfigOptionFloat, branchingsupport_object_elevation))/*= 5.0*/

    /////// Following options influence automatic support points placement:
    ((ConfigOptionInt, support_points_density_relative))

    // Now for the base pool (pad) /////////////////////////////////////////////

    // Enabling or disabling support creation
    ((ConfigOptionBool,  pad_enable))

    // The thickness of the pad walls
    ((ConfigOptionFloat, pad_wall_thickness))/*= 2*/

    // The height of the pad from the bottom to the top not considering the pit
    ((ConfigOptionFloat, pad_wall_height))/*= 5*/

    // How far should the pad extend around the contained geometry
    ((ConfigOptionFloat, pad_brim_size))

    // The greatest distance where two individual pads are merged into one. The
    // distance is measured roughly from the centroids of the pads.
    ((ConfigOptionFloat, pad_max_merge_distance))/*= 50*/

    // The smoothing radius of the pad edges
    // ((ConfigOptionFloat, pad_edge_radius))/*= 1*/;

    // The slope of the pad wall...
    ((ConfigOptionFloat, pad_wall_slope))

    // /////////////////////////////////////////////////////////////////////////
    // Zero elevation mode parameters:
    //    - The object pad will be derived from the model geometry.
    //    - There will be a gap between the object pad and the generated pad
    //      according to the support_base_safety_distance parameter.
    //    - The two pads will be connected with tiny connector sticks
    // /////////////////////////////////////////////////////////////////////////

    // Disable the elevation (ignore its value) and use the zero elevation mode
    ((ConfigOptionBool, pad_around_object))

    ((ConfigOptionBool, pad_around_object_everywhere))

    // This is the gap between the object bottom and the generated pad
    ((ConfigOptionFloat, pad_object_gap))

    // How far to place the connector sticks on the object pad perimeter
    ((ConfigOptionFloat, pad_object_connector_stride))

    // The width of the connectors sticks
    ((ConfigOptionFloat, pad_object_connector_width))

    // How much should the tiny connectors penetrate into the model body
    ((ConfigOptionFloat, pad_object_connector_penetration))

    // /////////////////////////////////////////////////////////////////////////
    // Model hollowing parameters:
    //   - Models can be hollowed out as part of the SLA print process
    //   - Thickness of the hollowed model walls can be adjusted
    //   -
    //   - Additional holes will be drilled into the hollow model to allow for
    //   - resin removal.
    // /////////////////////////////////////////////////////////////////////////

    ((ConfigOptionBool, hollowing_enable))

    // The minimum thickness of the model walls to maintain. Note that the
    // resulting walls may be thicker due to smoothing out fine cavities where
    // resin could stuck.
    ((ConfigOptionFloat, hollowing_min_thickness))

    // Indirectly controls the voxel size (resolution) used by openvdb
    ((ConfigOptionFloat, hollowing_quality))

    // Indirectly controls the minimum size of created cavities.
    ((ConfigOptionFloat, hollowing_closing_distance))
)

enum SLAMaterialSpeed { slamsSlow, slamsFast, slamsHighViscosity };

PRINT_CONFIG_CLASS_DEFINE(
    SLAMaterialConfig,

    ((ConfigOptionFloat,                       initial_layer_height))
    ((ConfigOptionFloat,                       bottle_cost))
    ((ConfigOptionFloat,                       bottle_volume))
    ((ConfigOptionFloat,                       bottle_weight))
    ((ConfigOptionFloat,                       material_density))
    ((ConfigOptionFloat,                       exposure_time))
    ((ConfigOptionFloat,                       initial_exposure_time))
    ((ConfigOptionFloats,                      material_correction))
    ((ConfigOptionFloat,                       material_correction_x))
    ((ConfigOptionFloat,                       material_correction_y))
    ((ConfigOptionFloat,                       material_correction_z))
    ((ConfigOptionEnum<SLAMaterialSpeed>,      material_print_speed))
    ((ConfigOptionInt,                         zcorrection_layers))

    ((ConfigOptionFloatNullable,               material_ow_support_pillar_diameter))
    ((ConfigOptionFloatNullable,               material_ow_branchingsupport_pillar_diameter))
    ((ConfigOptionFloatNullable,               material_ow_support_head_front_diameter))
    ((ConfigOptionFloatNullable,               material_ow_branchingsupport_head_front_diameter))
    ((ConfigOptionFloatNullable,               material_ow_support_head_penetration))
    ((ConfigOptionFloatNullable,               material_ow_branchingsupport_head_penetration))
    ((ConfigOptionFloatNullable,               material_ow_support_head_width))
    ((ConfigOptionFloatNullable,               material_ow_branchingsupport_head_width))
    ((ConfigOptionIntNullable,                 material_ow_support_points_density_relative))
    ((ConfigOptionFloatNullable,               material_ow_elefant_foot_compensation))
    ((ConfigOptionFloatNullable,               material_ow_absolute_correction))
    ((ConfigOptionFloat,                       area_fill))

    //tilt params
    ((ConfigOptionFloats,                      delay_before_exposure))
    ((ConfigOptionFloats,                      delay_after_exposure))
    ((ConfigOptionFloats,                      tower_hop_height))
    ((ConfigOptionEnums<TowerSpeeds>,          tower_speed))
    ((ConfigOptionBools,                       use_tilt))
    ((ConfigOptionEnums<TiltSpeeds>,           tilt_down_initial_speed))
    ((ConfigOptionInts,                        tilt_down_offset_steps))
    ((ConfigOptionFloats,                      tilt_down_offset_delay))
    ((ConfigOptionEnums<TiltSpeeds>,           tilt_down_finish_speed))
    ((ConfigOptionInts,                        tilt_down_cycles))
    ((ConfigOptionFloats,                      tilt_down_delay))
    ((ConfigOptionEnums<TiltSpeeds>,           tilt_up_initial_speed))
    ((ConfigOptionInts,                        tilt_up_offset_steps))
    ((ConfigOptionFloats,                      tilt_up_offset_delay))
    ((ConfigOptionEnums<TiltSpeeds>,           tilt_up_finish_speed))
    ((ConfigOptionInts,                        tilt_up_cycles))
    ((ConfigOptionFloats,                      tilt_up_delay))
)

PRINT_CONFIG_CLASS_DEFINE(
    SLAPrinterConfig,

    ((ConfigOptionEnum<PrinterTechnology>,    printer_technology))
    ((ConfigOptionPoints,                     bed_shape))
    ((ConfigOptionFloat,                      max_print_height))
    ((ConfigOptionFloat,                      display_width))
    ((ConfigOptionFloat,                      display_height))
    ((ConfigOptionInt,                        display_pixels_x))
    ((ConfigOptionInt,                        display_pixels_y))
    ((ConfigOptionEnum<SLADisplayOrientation>,display_orientation))
    ((ConfigOptionBool,                       display_mirror_x))
    ((ConfigOptionBool,                       display_mirror_y))
    ((ConfigOptionFloats,                     relative_correction))
    ((ConfigOptionFloat,                      relative_correction_x))
    ((ConfigOptionFloat,                      relative_correction_y))
    ((ConfigOptionFloat,                      relative_correction_z))
    ((ConfigOptionFloat,                      absolute_correction))
    ((ConfigOptionFloat,                      elefant_foot_compensation))
    ((ConfigOptionFloat,                      elefant_foot_min_width))
    ((ConfigOptionFloat,                      gamma_correction))
    ((ConfigOptionFloat,                      fast_tilt_time))
    ((ConfigOptionFloat,                      slow_tilt_time))
    ((ConfigOptionFloat,                      high_viscosity_tilt_time))
//    ((ConfigOptionFloat,                      area_fill))
    ((ConfigOptionFloat,                      min_exposure_time))
    ((ConfigOptionFloat,                      max_exposure_time))
    ((ConfigOptionFloat,                      min_initial_exposure_time))
    ((ConfigOptionFloat,                      max_initial_exposure_time))
    ((ConfigOptionString,                     sla_archive_format))
    ((ConfigOptionFloat,                      sla_output_precision))
    ((ConfigOptionString,                     printer_model))
)

PRINT_CONFIG_CLASS_DERIVED_DEFINE0(
    SLAFullPrintConfig,
    (SLAPrinterConfig, SLAPrintConfig, SLAPrintObjectConfig, SLAMaterialConfig)
)

#undef STATIC_PRINT_CONFIG_CACHE
#undef STATIC_PRINT_CONFIG_CACHE_BASE
#undef STATIC_PRINT_CONFIG_CACHE_DERIVED
#undef PRINT_CONFIG_CLASS_ELEMENT_DEFINITION
#undef PRINT_CONFIG_CLASS_ELEMENT_EQUAL
#undef PRINT_CONFIG_CLASS_ELEMENT_LOWER
#undef PRINT_CONFIG_CLASS_ELEMENT_HASH
#undef PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION
#undef PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION2
#undef PRINT_CONFIG_CLASS_DEFINE
#undef PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST
#undef PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST_ITEM
#undef PRINT_CONFIG_CLASS_DERIVED_DEFINE
#undef PRINT_CONFIG_CLASS_DERIVED_DEFINE0
#undef PRINT_CONFIG_CLASS_DERIVED_DEFINE1
#undef PRINT_CONFIG_CLASS_DERIVED_HASH
#undef PRINT_CONFIG_CLASS_DERIVED_EQUAL
#undef PRINT_CONFIG_CLASS_DERIVED_INITCACHE_ITEM
#undef PRINT_CONFIG_CLASS_DERIVED_INITCACHE
#undef PRINT_CONFIG_CLASS_DERIVED_INITIALIZER
#undef PRINT_CONFIG_CLASS_DERIVED_INITIALIZER_ITEM

class CLIActionsConfigDef : public ConfigDef
{
public:
    CLIActionsConfigDef();
};

class CLITransformConfigDef : public ConfigDef
{
public:
    CLITransformConfigDef();
};

class CLIMiscConfigDef : public ConfigDef
{
public:
    CLIMiscConfigDef();
};

class CLIInputConfigDef : public ConfigDef
{
public:
    CLIInputConfigDef();
};

typedef std::string t_custom_gcode_key;
// This map containes list of specific placeholders for each custom G-code, if any exist
const std::map<t_custom_gcode_key, t_config_option_keys>& custom_gcode_specific_placeholders();

// Next classes define placeholders used by GUI::EditGCodeDialog.

class ReadOnlySlicingStatesConfigDef : public ConfigDef
{
public:
    ReadOnlySlicingStatesConfigDef();
};

class ReadWriteSlicingStatesConfigDef : public ConfigDef
{
public:
    ReadWriteSlicingStatesConfigDef();
};

class OtherSlicingStatesConfigDef : public ConfigDef
{
public:
    OtherSlicingStatesConfigDef();
};

class PrintStatisticsConfigDef : public ConfigDef
{
public:
    PrintStatisticsConfigDef();
};

class ObjectsInfoConfigDef : public ConfigDef
{
public:
    ObjectsInfoConfigDef();
};

class DimensionsConfigDef : public ConfigDef
{
public:
    DimensionsConfigDef();
};

class TimestampsConfigDef : public ConfigDef
{
public:
    TimestampsConfigDef();
};

class OtherPresetsConfigDef : public ConfigDef
{
public:
    OtherPresetsConfigDef();
};

// This classes defines all custom G-code specific placeholders.
class CustomGcodeSpecificConfigDef : public ConfigDef
{
public:
    CustomGcodeSpecificConfigDef();
};
extern const CustomGcodeSpecificConfigDef    custom_gcode_specific_config_def;

// This class defines the command line options representing actions including options representing profiles sharing commands.
extern const CLIActionsConfigDef    cli_actions_config_def;

// This class defines the command line options representing transforms.
extern const CLITransformConfigDef  cli_transform_config_def;

// This class defines all command line options that are not actions or transforms.
extern const CLIMiscConfigDef       cli_misc_config_def;

// This class defines the command line options representing commands for loading configuration from CLI
extern const CLIInputConfigDef  cli_input_config_def;

bool is_XL_printer(const DynamicPrintConfig &cfg);
bool is_XL_printer(const PrintConfig &cfg);

Points get_bed_shape(const DynamicPrintConfig &cfg);
Points get_bed_shape(const PrintConfig &cfg);
Points get_bed_shape(const SLAPrinterConfig &cfg);

std::string get_sla_suptree_prefix(const DynamicPrintConfig &config);

// ModelConfig is a wrapper around DynamicPrintConfig with an addition of a timestamp.
// Each change of ModelConfig is tracked by assigning a new timestamp from a global counter.
// The counter is used for faster synchronization of the background slicing thread
// with the front end by skipping synchronization of equal config dictionaries.
// The global counter is also used for avoiding unnecessary serialization of config
// dictionaries when taking an Undo snapshot.
//
// The global counter is NOT thread safe, therefore it is recommended to use ModelConfig from
// the main thread only.
//
// As there is a global counter and it is being increased with each change to any ModelConfig,
// if two ModelConfig dictionaries differ, they should differ with their timestamp as well.
// Therefore copying the ModelConfig including its timestamp is safe as there is no harm
// in having multiple ModelConfig with equal timestamps as long as their dictionaries are equal.
//
// The timestamp is used by the Undo/Redo stack. As zero timestamp means invalid timestamp
// to the Undo/Redo stack (zero timestamp means the Undo/Redo stack needs to serialize and
// compare serialized data for differences), zero timestamp shall never be used.
// Timestamp==1 shall only be used for empty dictionaries.
class ModelConfig
{
public:
    // Following method clears the config and increases its timestamp, so the deleted
    // state is considered changed from perspective of the undo/redo stack.
    void         reset() { m_data.clear(); touch(); }

    void         assign_config(const ModelConfig &rhs) {
        if (m_timestamp != rhs.m_timestamp) {
            m_data      = rhs.m_data;
            m_timestamp = rhs.m_timestamp;
        }
    }
    void         assign_config(ModelConfig &&rhs) {
        if (m_timestamp != rhs.m_timestamp) {
            m_data      = std::move(rhs.m_data);
            m_timestamp = rhs.m_timestamp;
            rhs.reset();
        }
    }

    // Modification of the ModelConfig is not thread safe due to the global timestamp counter!
    // Don't call modification methods from the back-end!
    // Assign methods don't assign if src==dst to not having to bump the timestamp in case they are equal.
    void         assign_config(const DynamicPrintConfig &rhs)  { if (m_data != rhs) { m_data = rhs; this->touch(); } }
    void         assign_config(DynamicPrintConfig &&rhs)       { if (m_data != rhs) { m_data = std::move(rhs); this->touch(); } }
    void         apply(const ModelConfig &other, bool ignore_nonexistent = false) { this->apply(other.get(), ignore_nonexistent); }
    void         apply(const ConfigBase &other, bool ignore_nonexistent = false) { m_data.apply_only(other, other.keys(), ignore_nonexistent); this->touch(); }
    void         apply_only(const ModelConfig &other, const t_config_option_keys &keys, bool ignore_nonexistent = false) { this->apply_only(other.get(), keys, ignore_nonexistent); }
    void         apply_only(const ConfigBase &other, const t_config_option_keys &keys, bool ignore_nonexistent = false) { m_data.apply_only(other, keys, ignore_nonexistent); this->touch(); }
    bool         set_key_value(const std::string &opt_key, ConfigOption *opt) { bool out = m_data.set_key_value(opt_key, opt); this->touch(); return out; }
    template<typename T>
    void         set(const std::string &opt_key, T value) { m_data.set(opt_key, value, true); this->touch(); }
    void         set_deserialize(const t_config_option_key &opt_key, const std::string &str, ConfigSubstitutionContext &substitution_context, bool append = false)
        { m_data.set_deserialize(opt_key, str, substitution_context, append); this->touch(); }
    void         set_deserialize_strict(const t_config_option_key &opt_key, const std::string &str, bool append = false)
        { m_data.set_deserialize_strict(opt_key, str, append); this->touch(); }
    bool         erase(const t_config_option_key &opt_key) { bool out = m_data.erase(opt_key); if (out) this->touch(); return out; }

    // Getters are thread safe.
    // The following implicit conversion breaks the Cereal serialization.
//    operator const DynamicPrintConfig&() const throw() { return this->get(); }
    const DynamicPrintConfig&   get() const throw() { return m_data; }
    bool                        empty() const throw() { return m_data.empty(); }
    size_t                      size() const throw() { return m_data.size(); }
    auto                        cbegin() const { return m_data.cbegin(); }
    auto                        cend() const { return m_data.cend(); }
    t_config_option_keys        keys() const { return m_data.keys(); }
    bool                        has(const t_config_option_key &opt_key) const { return m_data.has(opt_key); }
    const ConfigOption*         option(const t_config_option_key &opt_key) const { return m_data.option(opt_key); }
    int                         opt_int(const t_config_option_key &opt_key) const { return m_data.opt_int(opt_key); }
    int                         extruder() const { return opt_int("extruder"); }
    double                      opt_float(const t_config_option_key &opt_key) const { return m_data.opt_float(opt_key); }
    std::string                 opt_serialize(const t_config_option_key &opt_key) const { return m_data.opt_serialize(opt_key); }

    // Return an optional timestamp of this object.
    // If the timestamp returned is non-zero, then the serialization framework will
    // only save this object on the Undo/Redo stack if the timestamp is different
    // from the timestmap of the object at the top of the Undo / Redo stack.
    virtual uint64_t    timestamp() const throw() { return m_timestamp; }
    bool                timestamp_matches(const ModelConfig &rhs) const throw() { return m_timestamp == rhs.m_timestamp; }
    // Not thread safe! Should not be called from other than the main thread!
    void                touch() { m_timestamp = ++ s_last_timestamp; }

private:
    friend class cereal::access;
    template<class Archive> void serialize(Archive& ar) { ar(m_timestamp); ar(m_data); }

    uint64_t                    m_timestamp { 1 };
    DynamicPrintConfig          m_data;

    static uint64_t             s_last_timestamp;
};

} // namespace Slic3r

// Serialization through the Cereal library
namespace cereal {
    // Let cereal know that there are load / save non-member functions declared for DynamicPrintConfig, ignore serialize / load / save from parent class DynamicConfig.
    template <class Archive> struct specialize<Archive, Slic3r::DynamicPrintConfig, cereal::specialization::non_member_load_save> {};

    template<class Archive> void load(Archive& archive, Slic3r::DynamicPrintConfig &config)
    {
        size_t cnt;
        archive(cnt);
        config.clear();
        for (size_t i = 0; i < cnt; ++ i) {
            size_t serialization_key_ordinal;
            archive(serialization_key_ordinal);
            assert(serialization_key_ordinal > 0);
            auto it = Slic3r::print_config_def.by_serialization_key_ordinal.find(serialization_key_ordinal);
            assert(it != Slic3r::print_config_def.by_serialization_key_ordinal.end());
            config.set_key_value(it->second->opt_key, it->second->load_option_from_archive(archive));
        }
    }

    template<class Archive> void save(Archive& archive, const Slic3r::DynamicPrintConfig &config)
    {
        size_t cnt = config.size();
        archive(cnt);
        for (auto it = config.cbegin(); it != config.cend(); ++it) {
            const Slic3r::ConfigOptionDef* optdef = Slic3r::print_config_def.get(it->first);
            assert(optdef != nullptr);
            assert(optdef->serialization_key_ordinal > 0);
            archive(optdef->serialization_key_ordinal);
            optdef->save_option_to_archive(archive, it->second.get());
        }
    }
}

#endif
