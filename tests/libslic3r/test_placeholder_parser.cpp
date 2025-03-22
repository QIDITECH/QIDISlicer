#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/PlaceholderParser.hpp"
#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;
using namespace Catch;

SCENARIO("Placeholder parser scripting", "[PlaceholderParser]") {
	PlaceholderParser 	parser;
	auto 				config = DynamicPrintConfig::full_print_config();

	config.set_deserialize_strict( {
		{ "printer_notes", "  PRINTER_VENDOR_QIDI3D  PRINTER_MODEL_MK2  " },
	    { "nozzle_diameter", "0.6;0.6;0.6;0.6" },
	    { "temperature", "357;359;363;378" }
	});
    // To test the "first_layer_extrusion_width" over "first_layer_heigth".
    // "first_layer_heigth" over "layer_height" is no more supported after first_layer_height was moved from PrintObjectConfig to PrintConfig.
//  config.option<ConfigOptionFloatOrPercent>("first_layer_height")->value = 150.;
//  config.option<ConfigOptionFloatOrPercent>("first_layer_height")->percent = true;
    config.option<ConfigOptionFloatOrPercent>("first_layer_height")->value = 1.5 * config.opt_float("layer_height");
    config.option<ConfigOptionFloatOrPercent>("first_layer_height")->percent = false;
    // To let the PlaceholderParser throw when referencing first_layer_speed if it is set to percent, as the PlaceholderParser does not know
    // a percent to what.
    config.option<ConfigOptionFloatOrPercent>("first_layer_speed")->value = 50.;
    config.option<ConfigOptionFloatOrPercent>("first_layer_speed")->percent = true;
    ConfigOptionFloatsNullable *opt_filament_retract_length = config.option<ConfigOptionFloatsNullable>("filament_retract_length", true);
    opt_filament_retract_length->values = { 5., ConfigOptionFloatsNullable::nil_value(), 3. };

    parser.apply_config(config);
	parser.set("foo", 0);
	parser.set("bar", 2);
	parser.set("num_extruders", 4);
    parser.set("gcode_flavor", "marlin");

    SECTION("nested config options (legacy syntax)") { REQUIRE(parser.process("[temperature_[foo]]") == "357"); }
    SECTION("array reference") { REQUIRE(parser.process("{temperature[foo]}") == "357"); }
    SECTION("whitespaces and newlines are maintained") { REQUIRE(parser.process("test [ temperature_ [foo] ] \n hu") == "test 357 \n hu"); }
    SECTION("nullable is not null") { REQUIRE(parser.process("{is_nil(filament_retract_length[0])}") == "false"); }
    SECTION("nullable is null") { REQUIRE(parser.process("{is_nil(filament_retract_length[1])}") == "true"); }
    SECTION("nullable is not null 2") { REQUIRE(parser.process("{is_nil(filament_retract_length[2])}") == "false"); }
    SECTION("multiple expressions") { REQUIRE(parser.process("{temperature[foo];temperature[foo]}") == "357357"); }
    SECTION("multiple expressions with semicolons") { REQUIRE(parser.process("{temperature[foo];;;temperature[foo];}") == "357357"); }
    SECTION("multiple expressions with semicolons 2") { REQUIRE(parser.process("{temperature[foo];;temperature[foo];}") == "357357"); }
    SECTION("multiple expressions with semicolons 3") { REQUIRE(parser.process("{temperature[foo];;;temperature[foo];;}") == "357357"); }

    SECTION("parsing string with escaped characters") { REQUIRE(parser.process("{\"hu\\nha\\\\\\\"ha\\\"\"}") == "hu\nha\\\"ha\""); }

    WHEN("An UTF-8 character is used inside the code block") {
        THEN("A std::runtime_error exception is thrown.") {
            // full-width plus sign instead of plain +
            REQUIRE_THROWS_AS(parser.process("{1\xEF\xBC\x8B 3}"), std::runtime_error);
        }
    }
    WHEN("An UTF-8 character is used inside a string") {
        THEN("UTF-8 sequence is processed correctly when quoted") {
            // japanese "cool" or "stylish"
            REQUIRE(parser.process("{1+\"\xE3\x81\x8B\xE3\x81\xA3\xE3\x81\x93\xE3\x81\x84\xE3\x81\x84\"+\" \"+3}") == "1\xE3\x81\x8B\xE3\x81\xA3\xE3\x81\x93\xE3\x81\x84\xE3\x81\x84 3");
        }
    }
    WHEN("An UTF-8 character is used inside a string") {
        THEN("UTF-8 sequence is processed correctly outside of code blocks") {
            // japanese "cool" or "stylish"
            REQUIRE(parser.process("{1+3}\xE3\x81\x8B\xE3\x81\xA3\xE3\x81\x93\xE3\x81\x84\xE3\x81\x84") == "4\xE3\x81\x8B\xE3\x81\xA3\xE3\x81\x93\xE3\x81\x84\xE3\x81\x84");
        }
    }

    // Test the math expressions.
    SECTION("math: 2*3") { REQUIRE(parser.process("{2*3}") == "6"); }
    SECTION("math: 2*3/6") { REQUIRE(parser.process("{2*3/6}") == "1"); }
    SECTION("math: 2*3/12") { REQUIRE(parser.process("{2*3/12}") == "0"); }
    SECTION("math: 2.*3/12") { REQUIRE(std::stod(parser.process("{2.*3/12}")) == Approx(0.5)); }
    SECTION("math: 10 % 2.5") { REQUIRE(std::stod(parser.process("{10%2.5}")) == Approx(0.)); }
    SECTION("math: 11 % 2.5") { REQUIRE(std::stod(parser.process("{11%2.5}")) == Approx(1.)); }
    SECTION("math: 2*(3-12)") { REQUIRE(parser.process("{2*(3-12)}") == "-18"); }
    SECTION("math: 2*foo*(3-12)") { REQUIRE(parser.process("{2*foo*(3-12)}") == "0"); }
    SECTION("math: 2*bar*(3-12)") { REQUIRE(parser.process("{2*bar*(3-12)}") == "-36"); }
    SECTION("math: 2.5*bar*(3-12)") { REQUIRE(std::stod(parser.process("{2.5*bar*(3-12)}")) == Approx(-45)); }
    SECTION("math: min(12, 14)") { REQUIRE(parser.process("{min(12, 14)}") == "12"); }
    SECTION("math: max(12, 14)") { REQUIRE(parser.process("{max(12, 14)}") == "14"); }
    SECTION("math: min(13.4, -1238.1)") { REQUIRE(std::stod(parser.process("{min(13.4, -1238.1)}")) == Approx(-1238.1)); }
    SECTION("math: max(13.4, -1238.1)") { REQUIRE(std::stod(parser.process("{max(13.4, -1238.1)}")) == Approx(13.4)); }
    SECTION("math: int(13.4)") { REQUIRE(parser.process("{int(13.4)}") == "13"); }
    SECTION("math: int(-13.4)") { REQUIRE(parser.process("{int(-13.4)}") == "-13"); }
    SECTION("math: round(13.4)") { REQUIRE(parser.process("{round(13.4)}") == "13"); }
    SECTION("math: round(-13.4)") { REQUIRE(parser.process("{round(-13.4)}") == "-13"); }
    SECTION("math: round(13.6)") { REQUIRE(parser.process("{round(13.6)}") == "14"); }
    SECTION("math: round(-13.6)") { REQUIRE(parser.process("{round(-13.6)}") == "-14"); }
    SECTION("math: digits(5, 15)") { REQUIRE(parser.process("{digits(5, 15)}") == "              5"); }
    SECTION("math: digits(5., 15)") { REQUIRE(parser.process("{digits(5., 15)}") == "              5"); }
    SECTION("math: zdigits(5, 15)") { REQUIRE(parser.process("{zdigits(5, 15)}") == "000000000000005"); }
    SECTION("math: zdigits(5., 15)") { REQUIRE(parser.process("{zdigits(5., 15)}") == "000000000000005"); }
    SECTION("math: digits(5, 15, 8)") { REQUIRE(parser.process("{digits(5, 15, 8)}") == "     5.00000000"); }
    SECTION("math: digits(5., 15, 8)") { REQUIRE(parser.process("{digits(5, 15, 8)}") == "     5.00000000"); }
    SECTION("math: zdigits(5, 15, 8)") { REQUIRE(parser.process("{zdigits(5, 15, 8)}") == "000005.00000000"); }
    SECTION("math: zdigits(5., 15, 8)") { REQUIRE(parser.process("{zdigits(5, 15, 8)}") == "000005.00000000"); }
    SECTION("math: digits(13.84375892476, 15, 8)") { REQUIRE(parser.process("{digits(13.84375892476, 15, 8)}") == "    13.84375892"); }
    SECTION("math: zdigits(13.84375892476, 15, 8)") { REQUIRE(parser.process("{zdigits(13.84375892476, 15, 8)}") == "000013.84375892"); }
    SECTION("math: ternary1") { REQUIRE(parser.process("{12 == 12 ? 1 - 3 : 2 * 2 * unknown_symbol}") == "-2"); }
    SECTION("math: ternary2") { REQUIRE(parser.process("{12 == 21/2 ? 1 - 1 - unknown_symbol : 2 * 2}") == "4"); }
    SECTION("math: ternary3") { REQUIRE(parser.process("{12 == 13 ? 1 - 1 * unknown_symbol : 2 * 2}") == "4"); }
    SECTION("math: ternary4") { REQUIRE(parser.process("{12 == 2 * 6 ? 1 - 1 : 2 * unknown_symbol}") == "0"); }
    SECTION("math: ternary nested") { REQUIRE(parser.process("{12 == 2 * 6 ? 3 - 1 != 2 ? does_not_exist : 0 * 0 - 0 / 1 + 12345 : bull ? 3 - cokoo : 2 * unknown_symbol}") == "12345"); }
    SECTION("math: interpolate_table(13.84375892476, (0, 0), (20, 20))") { REQUIRE(std::stod(parser.process("{interpolate_table(13.84375892476, (0, 0), (20, 20))}")) == Approx(13.84375892476)); }
    SECTION("math: interpolate_table(13, (0, 0), (20, 20), (30, 20))") { REQUIRE(std::stod(parser.process("{interpolate_table(13, (0, 0), (20, 20), (30, 20))}")) == Approx(13.)); }
    SECTION("math: interpolate_table(25, (0, 0), (20, 20), (30, 20))") { REQUIRE(std::stod(parser.process("{interpolate_table(25, (0, 0), (20, 20), (30, 20))}")) == Approx(20.)); }

    // Test the "coFloatOrPercent" and "xxx_extrusion_width" substitutions.
    // first_layer_extrusion_width ratio_over first_layer_heigth.
    SECTION("perimeter_extrusion_width") { REQUIRE(std::stod(parser.process("{perimeter_extrusion_width}")) == Approx(0.67500001192092896)); }
    SECTION("first_layer_extrusion_width") { REQUIRE(std::stod(parser.process("{first_layer_extrusion_width}")) == Approx(0.9)); }
    SECTION("support_material_xy_spacing") { REQUIRE(std::stod(parser.process("{support_material_xy_spacing}")) == Approx(0.3375)); }
    // external_perimeter_speed over perimeter_speed
    SECTION("external_perimeter_speed") { REQUIRE(std::stod(parser.process("{external_perimeter_speed}")) == Approx(30.)); }
    // infill_overlap over perimeter_extrusion_width
    SECTION("infill_overlap") { REQUIRE(std::stod(parser.process("{infill_overlap}")) == Approx(0.16875)); }
    // If first_layer_speed is set to percent, then it is applied over respective extrusion types by overriding their respective speeds.
    // The PlaceholderParser has no way to know which extrusion type the caller has in mind, therefore it throws.
    SECTION("first_layer_speed") { REQUIRE_THROWS(parser.process("{first_layer_speed}")); }

    // Test the boolean expression parser.
    auto boolean_expression = [&parser](const std::string& templ) { return parser.evaluate_boolean_expression(templ, parser.config()); };

    SECTION("boolean expression parser: 12 == 12") { REQUIRE(boolean_expression("12 == 12")); }
    SECTION("boolean expression parser: 12 != 12") { REQUIRE(! boolean_expression("12 != 12")); }
    SECTION("boolean expression parser: regex matches") { REQUIRE(boolean_expression("\"has some PATTERN embedded\" =~ /.*PATTERN.*/")); }
    SECTION("boolean expression parser: regex does not match") { REQUIRE(! boolean_expression("\"has some PATTERN embedded\" =~ /.*PTRN.*/")); }
    SECTION("boolean expression parser: accessing variables, equal") { REQUIRE(boolean_expression("foo + 2 == bar")); }
    SECTION("boolean expression parser: accessing variables, not equal") { REQUIRE(! boolean_expression("foo + 3 == bar")); }
    SECTION("boolean expression parser: (12 == 12) and (13 != 14)") { REQUIRE(boolean_expression("(12 == 12) and (13 != 14)")); }
    SECTION("boolean expression parser: (12 == 12) && (13 != 14)") { REQUIRE(boolean_expression("(12 == 12) && (13 != 14)")); }
    SECTION("boolean expression parser: (12 == 12) or (13 == 14)") { REQUIRE(boolean_expression("(12 == 12) or (13 == 14)")); }
    SECTION("boolean expression parser: (12 == 12) || (13 == 14)") { REQUIRE(boolean_expression("(12 == 12) || (13 == 14)")); }
    SECTION("boolean expression parser: (12 == 12) and not (13 == 14)") { REQUIRE(boolean_expression("(12 == 12) and not (13 == 14)")); }
    SECTION("boolean expression parser: ternary true") { REQUIRE(boolean_expression("(12 == 12) ? (1 - 1 == 0) : (2 * 2 == 3 * unknown_symbol)")); }
    SECTION("boolean expression parser: ternary false") { REQUIRE(! boolean_expression("(12 == 21/2) ? (1 - 1 == 0 - unknown_symbol) : (2 * 2 == 3)")); }
    SECTION("boolean expression parser: ternary false 2") { REQUIRE(boolean_expression("(12 == 13) ? (1 - 1 == 3 * unknown_symbol) : (2 * 2 == 4)")); }
    SECTION("boolean expression parser: ternary true 2") { REQUIRE(! boolean_expression("(12 == 2 * 6) ? (1 - 1 == 3) : (2 * 2 == 4 * unknown_symbol)")); }
    SECTION("boolean expression parser: lower than - false") { REQUIRE(! boolean_expression("12 < 3")); }
    SECTION("boolean expression parser: lower than - true") { REQUIRE(boolean_expression("12 < 22")); }
    SECTION("boolean expression parser: greater than - true") { REQUIRE(boolean_expression("12 > 3")); }
    SECTION("boolean expression parser: greater than - false") { REQUIRE(! boolean_expression("12 > 22")); }
    SECTION("boolean expression parser: lower than or equal- false") { REQUIRE(! boolean_expression("12 <= 3")); }
    SECTION("boolean expression parser: lower than or equal - true") { REQUIRE(boolean_expression("12 <= 22")); }
    SECTION("boolean expression parser: greater than or equal - true") { REQUIRE(boolean_expression("12 >= 3")); }
    SECTION("boolean expression parser: greater than or equal - false") { REQUIRE(! boolean_expression("12 >= 22")); }
    SECTION("boolean expression parser: lower than or equal (same values) - true") { REQUIRE(boolean_expression("12 <= 12")); }
    SECTION("boolean expression parser: greater than or equal (same values) - true") { REQUIRE(boolean_expression("12 >= 12")); }
    SECTION("boolean expression parser: one_of(\"a\", \"a\", \"b\", \"c\")") { REQUIRE(boolean_expression("one_of(\"a\", \"a\", \"b\", \"c\")")); }
    SECTION("boolean expression parser: one_of(\"b\", \"a\", \"b\", \"c\")") { REQUIRE(boolean_expression("one_of(\"b\", \"a\", \"b\", \"c\")")); }
    SECTION("boolean expression parser: one_of(\"c\", \"a\", \"b\", \"c\")") { REQUIRE(boolean_expression("one_of(\"c\", \"a\", \"b\", \"c\")")); }
    SECTION("boolean expression parser: one_of(\"d\", \"a\", \"b\", \"c\")") { REQUIRE(! boolean_expression("one_of(\"d\", \"a\", \"b\", \"c\")")); }
    SECTION("boolean expression parser: one_of(\"a\")") { REQUIRE(! boolean_expression("one_of(\"a\")")); }
    SECTION("boolean expression parser: one_of(\"a\", \"a\")") { REQUIRE(boolean_expression("one_of(\"a\", \"a\")")); }
    SECTION("boolean expression parser: one_of(\"b\", \"a\")") { REQUIRE(! boolean_expression("one_of(\"b\", \"a\")")); }
    SECTION("boolean expression parser: one_of(\"abcdef\", /.*c.*/)") { REQUIRE(boolean_expression("one_of(\"abcdef\", /.*c.*/)")); }
    SECTION("boolean expression parser: one_of(\"abcdef\", /.*f.*/, /.*c.*/)") { REQUIRE(boolean_expression("one_of(\"abcdef\", /.*f.*/, /.*c.*/)")); }
    SECTION("boolean expression parser: one_of(\"abcdef\", ~\".*f.*\", ~\".*c.*\")") { REQUIRE(boolean_expression("one_of(\"abcdef\", ~\".*f.*\", ~\".*c.*\")")); }
    SECTION("boolean expression parser: one_of(\"ghij\", /.*f.*/, /.*c.*/)") { REQUIRE(! boolean_expression("one_of(\"ghij\", /.*f.*/, /.*c.*/)")); }
    SECTION("boolean expression parser: one_of(\"ghij\", ~\".*f.*\", ~\".*c.*\")") { REQUIRE(! boolean_expression("one_of(\"ghij\", ~\".*f.*\", ~\".*c.*\")")); }
    SECTION("complex expression") { REQUIRE(boolean_expression("printer_notes=~/.*PRINTER_VENDOR_QIDI3D.*/ and printer_notes=~/.*PRINTER_MODEL_MK2.*/ and nozzle_diameter[0]==0.6 and num_extruders>1")); }
    SECTION("complex expression2") { REQUIRE(boolean_expression("printer_notes=~/.*PRINTER_VEwerfNDOR_QIDI3D.*/ or printer_notes=~/.*PRINTertER_MODEL_MK2.*/ or (nozzle_diameter[0]==0.6 and num_extruders>1)")); }
    SECTION("complex expression3") { REQUIRE(! boolean_expression("printer_notes=~/.*PRINTER_VEwerfNDOR_QIDI3D.*/ or printer_notes=~/.*PRINTertER_MODEL_MK2.*/ or (nozzle_diameter[0]==0.3 and num_extruders>1)")); }
    SECTION("enum expression") { REQUIRE(boolean_expression("gcode_flavor == \"marlin\"")); }

    SECTION("write to a scalar variable") {
        DynamicConfig config_outputs;
        config_outputs.set_key_value("writable_string", new ConfigOptionString());
        parser.process("{writable_string = \"Written\"}", 0, nullptr, &config_outputs, nullptr);
        REQUIRE(parser.process("{writable_string}", 0, nullptr, &config_outputs, nullptr) == "Written");
    }
    SECTION("write to a vector variable") {
        DynamicConfig config_outputs;
        config_outputs.set_key_value("writable_floats", new ConfigOptionFloats({ 0., 0., 0. }));
        parser.process("{writable_floats[1] = 33}", 0, nullptr, &config_outputs, nullptr);
        REQUIRE(config_outputs.opt_float("writable_floats", 1) == Approx(33.));
    }
}

SCENARIO("Placeholder parser variables", "[PlaceholderParser]") {
    PlaceholderParser 	parser;
    auto 				config = DynamicPrintConfig::full_print_config();

    config.set_deserialize_strict({
        { "filament_notes", "testnotes" },
        { "enable_dynamic_fan_speeds", "1" },
        { "nozzle_diameter", "0.6;0.6;0.6;0.6" },
        { "temperature", "357;359;363;378" }
        });

    PlaceholderParser::ContextData context_with_global_dict;
    context_with_global_dict.global_config = std::make_unique<DynamicConfig>();

    SECTION("create an int local variable") { REQUIRE(parser.process("{local myint = 33+2}{myint}", 0, nullptr, nullptr, nullptr) == "35"); }
    SECTION("create a string local variable") { REQUIRE(parser.process("{local mystr = \"mine\" + \"only\" + \"mine\"}{mystr}", 0, nullptr, nullptr, nullptr) == "mineonlymine"); }
    SECTION("create a bool local variable") { REQUIRE(parser.process("{local mybool = 1 + 1 == 2}{mybool}", 0, nullptr, nullptr, nullptr) == "true"); }
    SECTION("create an int global variable") { REQUIRE(parser.process("{global myint = 33+2}{myint}", 0, nullptr, nullptr, &context_with_global_dict) == "35"); }
    SECTION("create a string global variable") { REQUIRE(parser.process("{global mystr = \"mine\" + \"only\" + \"mine\"}{mystr}", 0, nullptr, nullptr, &context_with_global_dict) == "mineonlymine"); }
    SECTION("create a bool global variable") { REQUIRE(parser.process("{global mybool = 1 + 1 == 2}{mybool}", 0, nullptr, nullptr, &context_with_global_dict) == "true"); }

    SECTION("create an int local variable and overwrite it") { REQUIRE(parser.process("{local myint = 33+2}{myint = 12}{myint}", 0, nullptr, nullptr, nullptr) == "12"); }
    SECTION("create a string local variable and overwrite it") { REQUIRE(parser.process("{local mystr = \"mine\" + \"only\" + \"mine\"}{mystr = \"yours\"}{mystr}", 0, nullptr, nullptr, nullptr) == "yours"); }
    SECTION("create a bool local variable and overwrite it") { REQUIRE(parser.process("{local mybool = 1 + 1 == 2}{mybool = false}{mybool}", 0, nullptr, nullptr, nullptr) == "false"); }
    SECTION("create an int global variable and overwrite it") { REQUIRE(parser.process("{global myint = 33+2}{myint = 12}{myint}", 0, nullptr, nullptr, &context_with_global_dict) == "12"); }
    SECTION("create a string global variable and overwrite it") { REQUIRE(parser.process("{global mystr = \"mine\" + \"only\" + \"mine\"}{mystr = \"yours\"}{mystr}", 0, nullptr, nullptr, &context_with_global_dict) == "yours"); }
    SECTION("create a bool global variable and overwrite it") { REQUIRE(parser.process("{global mybool = 1 + 1 == 2}{mybool = false}{mybool}", 0, nullptr, nullptr, &context_with_global_dict) == "false"); }

    SECTION("create an int local variable and redefine it") { REQUIRE(parser.process("{local myint = 33+2}{local myint = 12}{myint}", 0, nullptr, nullptr, nullptr) == "12"); }
    SECTION("create a string local variable and redefine it") { REQUIRE(parser.process("{local mystr = \"mine\" + \"only\" + \"mine\"}{local mystr = \"yours\"}{mystr}", 0, nullptr, nullptr, nullptr) == "yours"); }
    SECTION("create a bool local variable and redefine it") { REQUIRE(parser.process("{local mybool = 1 + 1 == 2}{local mybool = false}{mybool}", 0, nullptr, nullptr, nullptr) == "false"); }
    SECTION("create an int global variable and redefine it") { REQUIRE(parser.process("{global myint = 33+2}{global myint = 12}{myint}", 0, nullptr, nullptr, &context_with_global_dict) == "12"); }
    SECTION("create a string global variable and redefine it") { REQUIRE(parser.process("{global mystr = \"mine\" + \"only\" + \"mine\"}{global mystr = \"yours\"}{mystr}", 0, nullptr, nullptr, &context_with_global_dict) == "yours"); }
    SECTION("create a bool global variable and redefine it") { REQUIRE(parser.process("{global mybool = 1 + 1 == 2}{global mybool = false}{mybool}", 0, nullptr, nullptr, &context_with_global_dict) == "false"); }

    SECTION("create an ints local variable with repeat()") { REQUIRE(parser.process("{local myint = repeat(2*3, 4*6)}{myint[5]}", 0, nullptr, nullptr, nullptr) == "24"); }
    SECTION("create a strings local variable with repeat()") { REQUIRE(parser.process("{local mystr = repeat(2*3, \"mine\" + \"only\" + \"mine\")}{mystr[5]}", 0, nullptr, nullptr, nullptr) == "mineonlymine"); }
    SECTION("create a bools local variable with repeat()") { REQUIRE(parser.process("{local mybool = repeat(5, 1 + 1 == 2)}{mybool[4]}", 0, nullptr, nullptr, nullptr) == "true"); }
    SECTION("create an ints global variable with repeat()") { REQUIRE(parser.process("{global myint = repeat(2*3, 4*6)}{myint[5]}", 0, nullptr, nullptr, &context_with_global_dict) == "24"); }
    SECTION("create a strings global variable with repeat()") { REQUIRE(parser.process("{global mystr = repeat(2*3, \"mine\" + \"only\" + \"mine\")}{mystr[5]}", 0, nullptr, nullptr, &context_with_global_dict) == "mineonlymine"); }
    SECTION("create a bools global variable with repeat()") { REQUIRE(parser.process("{global mybool = repeat(5, 1 + 1 == 2)}{mybool[4]}", 0, nullptr, nullptr, &context_with_global_dict) == "true"); }

    SECTION("create an ints local variable with initializer list") { REQUIRE(parser.process("{local myint = (2*3, 4*6, 5*5)}{myint[1]}", 0, nullptr, nullptr, nullptr) == "24"); }
    SECTION("create a strings local variable with initializer list") { REQUIRE(parser.process("{local mystr = (2*3, \"mine\" + \"only\" + \"mine\", 8)}{mystr[1]}", 0, nullptr, nullptr, nullptr) == "mineonlymine"); }
    SECTION("create a bools local variable with initializer list") { REQUIRE(parser.process("{local mybool = (3*3 == 8, 1 + 1 == 2)}{mybool[1]}", 0, nullptr, nullptr, nullptr) == "true"); }
    SECTION("create an ints global variable with initializer list") { REQUIRE(parser.process("{global myint = (2*3, 4*6, 5*5)}{myint[1]}", 0, nullptr, nullptr, &context_with_global_dict) == "24"); }
    SECTION("create a strings global variable with initializer list") { REQUIRE(parser.process("{global mystr = (2*3, \"mine\" + \"only\" + \"mine\", 8)}{mystr[1]}", 0, nullptr, nullptr, &context_with_global_dict) == "mineonlymine"); }
    SECTION("create a bools global variable with initializer list") { REQUIRE(parser.process("{global mybool = (2*3 == 8, 1 + 1 == 2, 5*5 != 33)}{mybool[1]}", 0, nullptr, nullptr, &context_with_global_dict) == "true"); }

    SECTION("create an ints local variable by a copy") { REQUIRE(parser.process("{local myint = temperature}{myint[0]}", 0, &config, nullptr, nullptr) == "357"); }
    SECTION("create a strings local variable by a copy") { REQUIRE(parser.process("{local mystr = filament_notes}{mystr[0]}", 0, &config, nullptr, nullptr) == "testnotes"); }
    SECTION("create a bools local variable by a copy") { REQUIRE(parser.process("{local mybool = enable_dynamic_fan_speeds}{mybool[0]}", 0, &config, nullptr, nullptr) == "true"); }
    SECTION("create an ints global variable by a copy") { REQUIRE(parser.process("{global myint = temperature}{myint[0]}", 0, &config, nullptr, &context_with_global_dict) == "357"); }
    SECTION("create a strings global variable by a copy") { REQUIRE(parser.process("{global mystr = filament_notes}{mystr[0]}", 0, &config, nullptr, &context_with_global_dict) == "testnotes"); }
    SECTION("create a bools global variable by a copy") { REQUIRE(parser.process("{global mybool = enable_dynamic_fan_speeds}{mybool[0]}", 0, &config, nullptr, &context_with_global_dict) == "true"); }

    SECTION("create an ints local variable by a copy and overwrite it") {
        REQUIRE(parser.process("{local myint = temperature}{myint = repeat(2*3, 4*6)}{myint[5]}", 0, &config, nullptr, nullptr) == "24");
        REQUIRE(parser.process("{local myint = temperature}{myint = (2*3, 4*6)}{myint[1]}", 0, &config, nullptr, nullptr) == "24");
        REQUIRE(parser.process("{local myint = temperature}{myint = (1)}{myint = temperature}{myint[0]}", 0, &config, nullptr, nullptr) == "357");
    }
    SECTION("create a strings local variable by a copy and overwrite it") {
        REQUIRE(parser.process("{local mystr = filament_notes}{mystr = repeat(2*3, \"mine\" + \"only\" + \"mine\")}{mystr[5]}", 0, &config, nullptr, nullptr) == "mineonlymine");
        REQUIRE(parser.process("{local mystr = filament_notes}{mystr = (2*3, \"mine\" + \"only\" + \"mine\")}{mystr[1]}", 0, &config, nullptr, nullptr) == "mineonlymine");
        REQUIRE(parser.process("{local mystr = filament_notes}{mystr = (2*3, \"mine\" + \"only\" + \"mine\")}{mystr = filament_notes}{mystr[0]}", 0, &config, nullptr, nullptr) == "testnotes");
    }
    SECTION("create a bools local variable by a copy and overwrite it") {
        REQUIRE(parser.process("{local mybool = enable_dynamic_fan_speeds}{mybool = repeat(2*3, true)}{mybool[5]}", 0, &config, nullptr, nullptr) == "true");
        REQUIRE(parser.process("{local mybool = enable_dynamic_fan_speeds}{mybool = (false, true)}{mybool[1]}", 0, &config, nullptr, nullptr) == "true");
        REQUIRE(parser.process("{local mybool = enable_dynamic_fan_speeds}{mybool = (false, false)}{mybool = enable_dynamic_fan_speeds}{mybool[0]}", 0, &config, nullptr, nullptr) == "true");
    }

    SECTION("size() of a non-empty vector returns the right size") { REQUIRE(parser.process("{local myint = (0, 1, 2, 3)}{size(myint)}", 0, nullptr, nullptr, nullptr) == "4"); }
    SECTION("size() of a an empty vector returns the right size") { REQUIRE(parser.process("{local myint = (0);myint=();size(myint)}", 0, nullptr, nullptr, nullptr) == "0"); }
    SECTION("empty() of a non-empty vector returns false") { REQUIRE(parser.process("{local myint = (0, 1, 2, 3)}{empty(myint)}", 0, nullptr, nullptr, nullptr) == "false"); }
    SECTION("empty() of a an empty vector returns true") { REQUIRE(parser.process("{local myint = (0);myint=();empty(myint)}", 0, nullptr, nullptr, nullptr) == "true"); }

    SECTION("nested if with new variables") {
        std::string script =
            "{if 1 == 1}{local myints = (5, 4, 3, 2, 1)}{else}{local myfloats = (1., 2., 3., 4., 5., 6., 7.)}{endif}"
            "{myints[1]},{size(myints)}";
        REQUIRE(parser.process(script, 0, nullptr, nullptr, nullptr) == "4,5");
    }
    SECTION("nested if with new variables 2") {
        std::string script =
            "{if 1 == 0}{local myints = (5, 4, 3, 2, 1)}{else}{local myfloats = (1., 2., 3., 4., 5., 6., 7.)}{endif}"
            "{size(myfloats)}";
        REQUIRE(parser.process(script, 0, nullptr, nullptr, nullptr) == "7");
    }
    SECTION("nested if with new variables 2, mixing }{ with ;") {
        std::string script =
            "{if 1 == 0 then local myints = (5, 4, 3, 2, 1);else;local myfloats = (1., 2., 3., 4., 5., 6., 7.);endif}"
            "{size(myfloats)}";
        REQUIRE(parser.process(script, 0, nullptr, nullptr, nullptr) == "7");
    }
    SECTION("nested if with new variables, two level") {
        std::string script =
            "{if 1 == 1}{if 2 == 3}{nejaka / haluz}{else}{local myints = (6, 5, 4, 3, 2, 1)}{endif}{else}{if zase * haluz}{else}{local myfloats = (1., 2., 3., 4., 5., 6., 7.)}{endif}{endif}"
            "{size(myints)}";
        REQUIRE(parser.process(script, 0, nullptr, nullptr, nullptr) == "6");
    }
    SECTION("if with empty block and ;") {
        std::string script =
            "{if false then else;local myfloats = (1., 2., 3., 4., 5., 6., 7.);endif}"
            "{size(myfloats)}";
        REQUIRE(parser.process(script, 0, nullptr, nullptr, nullptr) == "7");
    }
    SECTION("nested if with new variables, two level, mixing }{ with ;") {
        std::string script =
            "{if 1 == 1 then if 2 == 3}nejaka / haluz{else local myints = (6, 5, 4, 3, 2, 1) endif else if zase * haluz then else local myfloats = (1., 2., 3., 4., 5., 6., 7.) endif endif}"
            "{size(myints)}";
        REQUIRE(parser.process(script, 0, nullptr, nullptr, nullptr) == "6");
    }
    SECTION("nested if with new variables, two level, mixing }{ with ; 2") {
        std::string script =
            "{if 1 == 1 then if 2 == 3 then nejaka / haluz else}{local myints = (6, 5, 4, 3, 2, 1)}{endif else if zase * haluz then else local myfloats = (1., 2., 3., 4., 5., 6., 7.) endif endif}"
            "{size(myints)}";
        REQUIRE(parser.process(script, 0, nullptr, nullptr, nullptr) == "6");
    }
    SECTION("nested if with new variables, two level, mixing }{ with ; 3") {
        std::string script =
            "{if 1 == 1 then if 2 == 3 then nejaka / haluz else}{local myints = (6, 5, 4, 3, 2, 1)}{endif else}{if zase * haluz}{else local myfloats = (1., 2., 3., 4., 5., 6., 7.) endif}{endif}"
            "{size(myints)}";
        REQUIRE(parser.process(script, 0, nullptr, nullptr, nullptr) == "6");
    }
    SECTION("if else completely empty") { REQUIRE(parser.process("{if false then elsif false then else endif}", 0, nullptr, nullptr, nullptr) == ""); }
}
