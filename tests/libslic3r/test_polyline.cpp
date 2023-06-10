#include <catch2/catch.hpp>

#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"

using namespace Slic3r;

SCENARIO("Simplify polyline", "[Polyline]")
{
    GIVEN("polyline 1") {
        auto polyline = Polyline{ {0,0},{1,0},{2,0},{2,1},{2,2},{1,2},{0,2},{0,1},{0,0} };
        WHEN("simplified with Douglas-Peucker") {
            polyline.simplify(1.);
            THEN("simplified correctly") {
                REQUIRE(polyline == Polyline{ {0,0}, {2,0}, {2,2}, {0,2}, {0,0} });
            }
        }
    }
    GIVEN("polyline 2") {
        auto polyline = Polyline{ {0,0}, {50,50}, {100,0}, {125,-25}, {150,50} };
        WHEN("simplified with Douglas-Peucker") {
            polyline.simplify(25.);
            THEN("not simplified") {
                REQUIRE(polyline == Polyline{ {0,0}, {50,50}, {125,-25}, {150,50} });
            }
        }
    }
}
