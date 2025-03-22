#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <numeric>
#include <iostream>
#include <boost/filesystem.hpp>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/SVG.hpp"

using namespace Slic3r;
using namespace Catch;

SCENARIO("Various Clipper operations - xs/t/11_clipper.t", "[ClipperUtils]") {
	// CCW oriented contour
	Slic3r::Polygon   square{ { 200, 100 }, {200, 200}, {100, 200}, {100, 100} };
	// CW oriented contour
	Slic3r::Polygon   hole_in_square{ { 160, 140 }, { 140, 140 }, { 140, 160 }, { 160, 160 } };
	Slic3r::ExPolygon square_with_hole(square, hole_in_square);
	GIVEN("square_with_hole") {
        WHEN("offset") {
            Polygons result = Slic3r::offset(square_with_hole, 5.f);
            THEN("offset matches") {
                REQUIRE(result == Polygons { 
                    { { 205, 205 }, { 95, 205 }, { 95, 95 }, { 205, 95 }, },
                    { { 155, 145 }, { 145, 145 }, { 145, 155 }, { 155, 155 } } });
            }
        }
        WHEN("offset_ex") {
            ExPolygons result = Slic3r::offset_ex(square_with_hole, 5.f);
            THEN("offset matches") {
                REQUIRE(result == ExPolygons { { 
                    { { 205, 205 }, { 95, 205 }, { 95, 95 }, { 205, 95 }, },
                    { { 145, 145 }, { 145, 155 }, { 155, 155 }, { 155, 145 } } } } );
            }
        }
        WHEN("offset2_ex") {
            ExPolygons result = Slic3r::offset2_ex({ square_with_hole }, 5.f, -2.f);
            THEN("offset matches") {
                REQUIRE(result == ExPolygons { {
                    { { 203, 203 }, { 97, 203 }, { 97, 97 }, { 203, 97 } },
                    { { 143, 143 }, { 143, 157 }, { 157, 157 }, { 157, 143 } } } } );
            }
        }
    }
    GIVEN("square_with_hole 2") {
        Slic3r::ExPolygon square_with_hole(
            { { 20000000, 20000000 }, { 0, 20000000 }, { 0, 0 }, { 20000000, 0 } },
            { { 5000000, 15000000 }, { 15000000, 15000000 }, { 15000000, 5000000 }, { 5000000, 5000000 } });
        WHEN("offset2_ex") {
            Slic3r::ExPolygons result = Slic3r::offset2_ex(ExPolygons { square_with_hole }, -1.f, 1.f);
            THEN("offset matches") {
                REQUIRE(result.size() == 1);
                REQUIRE(square_with_hole.area() == result.front().area());
            }
        }
    }
    GIVEN("square and hole") {
        WHEN("diff_ex") {
            ExPolygons result = Slic3r::diff_ex(Polygons{ square }, Polygons{ hole_in_square });
            THEN("hole is created") {
                REQUIRE(result.size() == 1);
                REQUIRE(square_with_hole.area() == result.front().area());
            }
        }
    }
    GIVEN("polyline") {
        Polyline polyline { { 50, 150 }, { 300, 150 } };
        WHEN("intersection_pl") {
            Polylines result = Slic3r::intersection_pl(polyline, ExPolygon{ square, hole_in_square });
            THEN("correct number of result lines") {
                REQUIRE(result.size() == 2);
            }
            THEN("result lines have correct length") {
                // results are in no particular order
                REQUIRE(result[0].length() == 40);
                REQUIRE(result[1].length() == 40);
            }
        }
        WHEN("diff_pl") {
            Polylines result = Slic3r::diff_pl({ polyline }, Polygons{ square, hole_in_square });
            THEN("correct number of result lines") {
                REQUIRE(result.size() == 3);
            }
            // results are in no particular order
            THEN("the left result line has correct length") {
                REQUIRE(std::count_if(result.begin(), result.end(), [](const Polyline &pl) { return pl.length() == 50; }) == 1);
            }
            THEN("the right result line has correct length") {
                REQUIRE(std::count_if(result.begin(), result.end(), [](const Polyline &pl) { return pl.length() == 100; }) == 1);
            }
            THEN("the central result line has correct length") {
                REQUIRE(std::count_if(result.begin(), result.end(), [](const Polyline &pl) { return pl.length() == 20; }) == 1);
            }
        }
    }
	GIVEN("Clipper bug #96 / Slic3r issue #2028") {
		Slic3r::Polyline subject{
			{ 44735000, 31936670 }, { 55270000, 31936670 }, { 55270000, 25270000 }, { 74730000, 25270000 }, { 74730000, 44730000 }, { 68063296, 44730000 }, { 68063296, 55270000 }, { 74730000, 55270000 },
			{ 74730000, 74730000 }, { 55270000, 74730000 }, { 55270000, 68063296 }, { 44730000, 68063296 }, { 44730000, 74730000 }, { 25270000, 74730000 }, { 25270000, 55270000 }, { 31936670, 55270000 },
			{ 31936670, 44730000 }, { 25270000, 44730000 }, { 25270000, 25270000 }, { 44730000, 25270000 }, { 44730000, 31936670 } };
		Slic3r::Polygon clip { {75200000, 45200000}, {54800000, 45200000}, {54800000, 24800000}, {75200000, 24800000} };
        Slic3r::Polylines result = Slic3r::intersection_pl(subject, ExPolygon{ clip });
		THEN("intersection_pl - result is not empty") {
			REQUIRE(result.size() == 1);
		}
	}
	GIVEN("Clipper bug #122") {
		Slic3r::Polyline subject { { 1975, 1975 }, { 25, 1975 }, { 25, 25 }, { 1975, 25 }, { 1975, 1975 } };
		Slic3r::Polygons clip { { { 2025, 2025 }, { -25, 2025 } , { -25, -25 }, { 2025, -25 } },
								{ { 525, 525 }, { 525, 1475 }, { 1475, 1475 }, { 1475, 525 } } };
		Slic3r::Polylines result = Slic3r::intersection_pl({ subject }, clip);
		THEN("intersection_pl - result is not empty") {
			REQUIRE(result.size() == 1);
			REQUIRE(result.front().points.size() == 5);
		}
	}
	GIVEN("Clipper bug #126") {
		Slic3r::Polyline subject { { 200000, 19799999 }, { 200000, 200000 }, { 24304692, 200000 }, { 15102879, 17506106 }, { 13883200, 19799999 }, { 200000, 19799999 } };
		Slic3r::Polygon clip { { 15257205, 18493894 }, { 14350057, 20200000 }, { -200000, 20200000 }, { -200000, -200000 }, { 25196917, -200000 } };
		Slic3r::Polylines result = Slic3r::intersection_pl(subject, ExPolygon{ clip });
		THEN("intersection_pl - result is not empty") {
			REQUIRE(result.size() == 1);
		}
		THEN("intersection_pl - result has same length as subject polyline") {
			REQUIRE(result.front().length() == Approx(subject.length()));
		}
	}

#if 0
	{
		# Clipper does not preserve polyline orientation
		my $polyline = Slic3r::Polyline->new([50, 150], [300, 150]);
		my $result = Slic3r::Geometry::Clipper::intersection_pl([$polyline], [$square]);
		is scalar(@$result), 1, 'intersection_pl - correct number of result lines';
		is_deeply $result->[0]->pp, [[100, 150], [200, 150]], 'clipped line orientation is preserved';
	}
	{
		# Clipper does not preserve polyline orientation
		my $polyline = Slic3r::Polyline->new([300, 150], [50, 150]);
		my $result = Slic3r::Geometry::Clipper::intersection_pl([$polyline], [$square]);
		is scalar(@$result), 1, 'intersection_pl - correct number of result lines';
		is_deeply $result->[0]->pp, [[200, 150], [100, 150]], 'clipped line orientation is preserved';
	}
	{
		# Disabled until Clipper bug #127 is fixed
		my $subject = [
			Slic3r::Polyline->new([-90000000, -100000000], [-90000000, 100000000]), # vertical
				Slic3r::Polyline->new([-100000000, -10000000], [100000000, -10000000]), # horizontal
				Slic3r::Polyline->new([-100000000, 0], [100000000, 0]), # horizontal
				Slic3r::Polyline->new([-100000000, 10000000], [100000000, 10000000]), # horizontal
		];
		my $clip = Slic3r::Polygon->new(# a circular, convex, polygon
			[99452190, 10452846], [97814760, 20791169], [95105652, 30901699], [91354546, 40673664], [86602540, 50000000],
			[80901699, 58778525], [74314483, 66913061], [66913061, 74314483], [58778525, 80901699], [50000000, 86602540],
			[40673664, 91354546], [30901699, 95105652], [20791169, 97814760], [10452846, 99452190], [0, 100000000],
			[-10452846, 99452190], [-20791169, 97814760], [-30901699, 95105652], [-40673664, 91354546],
			[-50000000, 86602540], [-58778525, 80901699], [-66913061, 74314483], [-74314483, 66913061],
			[-80901699, 58778525], [-86602540, 50000000], [-91354546, 40673664], [-95105652, 30901699],
			[-97814760, 20791169], [-99452190, 10452846], [-100000000, 0], [-99452190, -10452846],
			[-97814760, -20791169], [-95105652, -30901699], [-91354546, -40673664], [-86602540, -50000000],
			[-80901699, -58778525], [-74314483, -66913061], [-66913061, -74314483], [-58778525, -80901699],
			[-50000000, -86602540], [-40673664, -91354546], [-30901699, -95105652], [-20791169, -97814760],
			[-10452846, -99452190], [0, -100000000], [10452846, -99452190], [20791169, -97814760],
			[30901699, -95105652], [40673664, -91354546], [50000000, -86602540], [58778525, -80901699],
			[66913061, -74314483], [74314483, -66913061], [80901699, -58778525], [86602540, -50000000],
			[91354546, -40673664], [95105652, -30901699], [97814760, -20791169], [99452190, -10452846], [100000000, 0]
			);
		my $result = Slic3r::Geometry::Clipper::intersection_pl($subject, [$clip]);
		is scalar(@$result), scalar(@$subject), 'intersection_pl - expected number of polylines';
		is sum(map scalar(@$_), @$result), scalar(@$subject) * 2, 'intersection_pl - expected number of points in polylines';
	}
#endif
}

SCENARIO("Various Clipper operations - t/clipper.t", "[ClipperUtils]") {
    GIVEN("square with hole") {
        // CCW oriented contour
        Slic3r::Polygon   square { { 10, 10 }, { 20, 10 }, { 20, 20 }, { 10, 20 } };
        Slic3r::Polygon   square2 { { 5, 12 }, { 25, 12 }, { 25, 18 }, { 5, 18 } };
        // CW oriented contour
        Slic3r::Polygon   hole_in_square { { 14, 14 }, { 14, 16 }, { 16, 16 }, { 16, 14 } };
        WHEN("intersection_ex with another square") {
            ExPolygons intersection = Slic3r::intersection_ex(Polygons{ square, hole_in_square }, Polygons{ square2 });
            THEN("intersection area matches (hole is preserved)") {
                ExPolygon match({ { 20, 18 }, { 10, 18 }, { 10, 12 }, { 20, 12 } },
                                { { 14, 16 }, { 16, 16 }, { 16, 14 }, { 14, 14 } });
                REQUIRE(intersection.size() == 1);
                REQUIRE(intersection.front().area() == Approx(match.area()));
            }
        }

        ExPolygons expolygons { ExPolygon { square, hole_in_square } };
        WHEN("Clipping line 1") {
            Polylines intersection = intersection_pl({ Polyline { { 15, 18 }, { 15, 15 } } }, expolygons);
            THEN("line is clipped to square with hole") {
                REQUIRE((Vec2f(15, 18) - Vec2f(15, 16)).norm() == Approx(intersection.front().length()));
            }
        }
        WHEN("Clipping line 2") {
            Polylines intersection = intersection_pl({ Polyline { { 15, 15 }, { 15, 12 } } }, expolygons);
            THEN("line is clipped to square with hole") {
                REQUIRE((Vec2f(15, 14) - Vec2f(15, 12)).norm() == Approx(intersection.front().length()));
            }
        }
        WHEN("Clipping line 3") {
            Polylines intersection = intersection_pl({ Polyline { { 12, 18 }, { 18, 18 } } }, expolygons);
            THEN("line is clipped to square with hole") {
                REQUIRE((Vec2f(18, 18) - Vec2f(12, 18)).norm() == Approx(intersection.front().length()));
            }
        }
        WHEN("Clipping line 4") {
            Polylines intersection = intersection_pl({ Polyline { { 5, 15 }, { 30, 15 } } }, expolygons);
            THEN("line is clipped to square with hole") {
                REQUIRE((Vec2f(14, 15) - Vec2f(10, 15)).norm() == Approx(intersection.front().length()));
                REQUIRE((Vec2f(20, 15) - Vec2f(16, 15)).norm() == Approx(intersection[1].length()));
            }
        }
        WHEN("Clipping line 5") {
            Polylines intersection = intersection_pl({ Polyline { { 30, 15 }, { 5, 15 } } }, expolygons);
            THEN("reverse line is clipped to square with hole") {
                REQUIRE((Vec2f(20, 15) - Vec2f(16, 15)).norm() == Approx(intersection.front().length()));
                REQUIRE((Vec2f(14, 15) - Vec2f(10, 15)).norm() == Approx(intersection[1].length()));
            }
        }
        WHEN("Clipping line 6") {
            Polylines intersection = intersection_pl({ Polyline { { 10, 18 }, { 20, 18 } } }, expolygons);
            THEN("tangent line is clipped to square with hole") {
                REQUIRE((Vec2f(20, 18) - Vec2f(10, 18)).norm() == Approx(intersection.front().length()));
            }
        }
    }
    GIVEN("square with hole 2") {
        // CCW oriented contour
        Slic3r::Polygon   square { { 0, 0 }, { 40, 0 }, { 40, 40 }, { 0, 40 } };
        Slic3r::Polygon   square2 { { 10, 10 }, { 30, 10 }, { 30, 30 }, { 10, 30 } };
        // CW oriented contour
        Slic3r::Polygon   hole { { 15, 15 }, { 15, 25 }, { 25, 25 }, {25, 15 } };
        WHEN("union_ex with another square") {
            ExPolygons union_ = Slic3r::union_ex({ square, square2, hole });
            THEN("union of two ccw and one cw is a contour with no holes") {
                REQUIRE(union_.size() == 1);
                REQUIRE(union_.front() == ExPolygon { { 40, 40 }, { 0, 40 }, { 0, 0 }, { 40, 0 } } );
            }
        }
        WHEN("diff_ex with another square") {
			ExPolygons diff = Slic3r::diff_ex(Polygons{ square, square2 }, Polygons{ hole });
            THEN("difference of a cw from two ccw is a contour with one hole") {
                REQUIRE(diff.size() == 1);
                REQUIRE(diff.front().area() == Approx(ExPolygon({ {40, 40}, {0, 40}, {0, 0}, {40, 0} }, { {15, 25}, {25, 25}, {25, 15}, {15, 15} }).area()));
            }
        }
    }
    GIVEN("yet another square") {
        Slic3r::Polygon  square { { 10, 10 }, { 20, 10 }, { 20, 20 }, { 10, 20 } };
        Slic3r::Polyline square_pl = square.split_at_first_point();
        WHEN("no-op diff_pl") {
            Slic3r::Polylines res = Slic3r::diff_pl({ square_pl }, Polygons{});
            THEN("returns the right number of polylines") {
                REQUIRE(res.size() == 1);
            }
            THEN("returns the unmodified input polyline") {
                REQUIRE(res.front().points.size() == square_pl.points.size());
            }
        }
    }
    GIVEN("circle") {
        Slic3r::ExPolygon circle_with_hole { Polygon::new_scale({
                { 151.8639,288.1192 }, {133.2778,284.6011}, { 115.0091,279.6997 }, { 98.2859,270.8606 }, { 82.2734,260.7933 }, 
                { 68.8974,247.4181 }, { 56.5622,233.0777 }, { 47.7228,216.3558 }, { 40.1617,199.0172 }, { 36.6431,180.4328 }, 
                { 34.932,165.2312 }, { 37.5567,165.1101 }, { 41.0547,142.9903 }, { 36.9056,141.4295 }, { 40.199,124.1277 }, 
                { 47.7776,106.7972 }, { 56.6335,90.084 }, { 68.9831,75.7557 }, { 82.3712,62.3948 }, { 98.395,52.3429 }, 
                { 115.1281,43.5199 }, { 133.4004,38.6374 }, { 151.9884,35.1378 }, { 170.8905,35.8571 }, { 189.6847,37.991 }, 
                { 207.5349,44.2488 }, { 224.8662,51.8273 }, { 240.0786,63.067 }, { 254.407,75.4169 }, { 265.6311,90.6406 }, 
                { 275.6832,106.6636 }, { 281.9225,124.52 }, { 286.8064,142.795 }, { 287.5061,161.696 }, { 286.7874,180.5972 }, 
                { 281.8856,198.8664 }, { 275.6283,216.7169 }, { 265.5604,232.7294 }, { 254.3211,247.942 }, { 239.9802,260.2776 }, 
                { 224.757,271.5022 }, { 207.4179,279.0635 }, { 189.5605,285.3035 }, { 170.7649,287.4188 }
            }) };
        circle_with_hole.holes = { Polygon::new_scale({
                { 158.227,215.9007 }, { 164.5136,215.9007 }, { 175.15,214.5007 }, { 184.5576,210.6044 }, { 190.2268,207.8743 }, 
                { 199.1462,201.0306 }, { 209.0146,188.346 }, { 213.5135,177.4829 }, { 214.6979,168.4866 }, { 216.1025,162.3325 }, 
                { 214.6463,151.2703 }, { 213.2471,145.1399 }, { 209.0146,134.9203 }, { 199.1462,122.2357 }, { 189.8944,115.1366 }, 
                { 181.2504,111.5567 }, { 175.5684,108.8205 }, { 164.5136,107.3655 }, { 158.2269,107.3655 }, { 147.5907,108.7656 }, 
                { 138.183,112.6616 }, { 132.5135,115.3919 }, { 123.5943,122.2357 }, { 113.7259,134.92 }, { 109.2269,145.7834 }, 
                { 108.0426,154.7799 }, { 106.638,160.9339 }, { 108.0941,171.9957 }, { 109.4933,178.1264 }, { 113.7259,188.3463 }, 
                { 123.5943,201.0306 }, { 132.8461,208.1296 }, { 141.4901,211.7094 }, { 147.172,214.4458 }
            }) };
        THEN("contour is counter-clockwise") {
            REQUIRE(circle_with_hole.contour.is_counter_clockwise());
        }
        THEN("hole is counter-clockwise") {
            REQUIRE(circle_with_hole.holes.size() == 1);
            REQUIRE(circle_with_hole.holes.front().is_clockwise());
        }
    
        WHEN("clipping a line") {
            auto line = Polyline::new_scale({ { 152.742,288.086671142818 }, { 152.742,34.166466971035 } });    
            Polylines intersection = intersection_pl(line, to_polygons(circle_with_hole));
            THEN("clipped to two pieces") {
                REQUIRE(intersection.front().length() == Approx((Vec2d(152742000, 215178843) - Vec2d(152742000, 288086661)).norm()));
                REQUIRE(intersection[1].length() == Approx((Vec2d(152742000, 35166477) - Vec2d(152742000, 108087507)).norm()));
            }
        }
    }
    GIVEN("line") {
        THEN("expand by 5") {
            REQUIRE(offset(Polyline({10,10}, {20,10}), 5).front().area() == Polygon({ {10,5}, {20,5}, {20,15}, {10,15} }).area());
        }
    }
}

template<e_ordering o = e_ordering::OFF, class P, class P_Alloc, class Tree>
double polytree_area(const Tree &tree, std::vector<P, P_Alloc> *out)
{
    traverse_pt<o>(tree, out);
    
    return std::accumulate(out->begin(), out->end(), 0.0,
                           [](double a, const P &p) { return a + p.area(); });
}

size_t count_polys(const ExPolygons& expolys)
{
    size_t c = 0;
    for (auto &ep : expolys) c += ep.holes.size() + 1;
    
    return c;
}

TEST_CASE("Traversing Clipper PolyTree", "[ClipperUtils]") {
    // Create a polygon representing unit box
    Polygon unitbox;
    const auto UNIT = coord_t(1. / SCALING_FACTOR);
    unitbox.points = { Vec2crd{0, 0}, Vec2crd{UNIT, 0}, Vec2crd{UNIT, UNIT}, Vec2crd{0, UNIT}};
    
    Polygon box_frame = unitbox;
    box_frame.scale(20, 10);
    
    Polygon hole_left = unitbox;
    hole_left.scale(8);
    hole_left.translate(UNIT, UNIT);
    hole_left.reverse();
    
    Polygon hole_right = hole_left;
    hole_right.translate(UNIT * 10, 0);
    
    Polygon inner_left = unitbox;
    inner_left.scale(4);
    inner_left.translate(UNIT * 3, UNIT * 3);
    
    Polygon inner_right = inner_left;
    inner_right.translate(UNIT * 10, 0);
    
    Polygons reference = union_({box_frame, hole_left, hole_right, inner_left, inner_right});
    
    ClipperLib::PolyTree tree = union_pt(reference);
    double area_sum = box_frame.area() + hole_left.area() +
                      hole_right.area() + inner_left.area() +
                      inner_right.area();
    
    REQUIRE(area_sum > 0);

    SECTION("Traverse into Polygons WITHOUT spatial ordering") {
        Polygons output;
        REQUIRE(area_sum == Approx(polytree_area(tree.GetFirst(), &output)));
        REQUIRE(output.size() == reference.size());
    }
    
    SECTION("Traverse into ExPolygons WITHOUT spatial ordering") {
        ExPolygons output;
        REQUIRE(area_sum == Approx(polytree_area(tree.GetFirst(), &output)));
        REQUIRE(count_polys(output) == reference.size());
    }
    
    SECTION("Traverse into Polygons WITH spatial ordering") {
        Polygons output;
        REQUIRE(area_sum == Approx(polytree_area<e_ordering::ON>(tree.GetFirst(), &output)));
        REQUIRE(output.size() == reference.size());
    }
    
    SECTION("Traverse into ExPolygons WITH spatial ordering") {
        ExPolygons output;
        REQUIRE(area_sum == Approx(polytree_area<e_ordering::ON>(tree.GetFirst(), &output)));
        REQUIRE(count_polys(output) == reference.size());
    }
}
