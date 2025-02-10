#include <catch2/catch.hpp>
#include <test_utils.hpp>

#include <random>

#include <libslic3r/ExtrusionEntity.hpp>
#include <libslic3r/GCode/ExtrusionOrder.hpp>
#include <libslic3r/GCode/SmoothPath.hpp>
#include <libslic3r/Geometry/ArcWelder.hpp>
#include <libslic3r/Geometry/Circle.hpp>
#include <libslic3r/SVG.hpp>
#include <libslic3r/libslic3r.h>

using namespace Slic3r;

TEST_CASE("arc basics", "[ArcWelder]") {
    using namespace Slic3r::Geometry;

    WHEN("arc from { 2000.f, 1000.f } to { 1000.f, 2000.f }") {
        Vec2f p1{ 2000.f, 1000.f };
        Vec2f p2{ 1000.f, 2000.f };
        float r{ 1000.f };
        const double s = 1000.f / sqrt(2.);
        THEN("90 degrees arc, CCW") {
            Vec2f c = ArcWelder::arc_center(p1, p2, r, true);
            Vec2f m = ArcWelder::arc_middle_point(p1, p2, r, true);
            REQUIRE(is_approx(c, Vec2f{ 1000.f, 1000.f }));
            REQUIRE(ArcWelder::arc_angle(p1, p2, r) == Approx(0.5 * M_PI));
            REQUIRE(ArcWelder::arc_length(p1, p2, r) == Approx(r * 0.5 * M_PI).epsilon(0.001));
            REQUIRE(is_approx(m, Vec2f{ 1000.f + s, 1000.f + s }, 0.001f));
        }
        THEN("90 degrees arc, CW") {
            Vec2f c = ArcWelder::arc_center(p1, p2, r, false);
            Vec2f m = ArcWelder::arc_middle_point(p1, p2, r, false);
            REQUIRE(is_approx(c, Vec2f{ 2000.f, 2000.f }));
            REQUIRE(is_approx(m, Vec2f{ 2000.f - s, 2000.f - s }, 0.001f));
        }
        THEN("270 degrees arc, CCW") {
            Vec2f c = ArcWelder::arc_center(p1, p2, - r, true);
            Vec2f m = ArcWelder::arc_middle_point(p1, p2, - r, true);
            REQUIRE(is_approx(c, Vec2f{ 2000.f, 2000.f }));
            REQUIRE(ArcWelder::arc_angle(p1, p2, - r) == Approx(1.5 * M_PI));
            REQUIRE(ArcWelder::arc_length(p1, p2, - r) == Approx(r * 1.5 * M_PI).epsilon(0.001));
            REQUIRE(is_approx(m, Vec2f{ 2000.f + s, 2000.f + s }, 0.001f));
        }
        THEN("270 degrees arc, CW") {
            Vec2f c = ArcWelder::arc_center(p1, p2, - r, false);
            Vec2f m = ArcWelder::arc_middle_point(p1, p2, - r, false);
            REQUIRE(is_approx(c, Vec2f{ 1000.f, 1000.f }));
            REQUIRE(is_approx(m, Vec2f{ 1000.f - s, 1000.f - s }, 0.001f));
        }
    }
    WHEN("arc from { 1707.11f, 1707.11f } to { 1000.f, 2000.f }") {
        Vec2f p1{ 1707.11f, 1707.11f };
        Vec2f p2{ 1000.f, 2000.f };
        float r{ 1000.f };
        Vec2f center1 = Vec2f{ 1000.f, 1000.f };
        // Center on the other side of the CCW arch.
        Vec2f center2 = center1 + 2. * (0.5 * (p1 + p2) - center1);
        THEN("45 degrees arc, CCW") {
            Vec2f c = ArcWelder::arc_center(p1, p2, r, true);
            REQUIRE(is_approx(c, center1, 1.f));
            REQUIRE(ArcWelder::arc_angle(p1, p2, r) == Approx(0.25 * M_PI));
            REQUIRE(ArcWelder::arc_length(p1, p2, r) == Approx(r * 0.25 * M_PI).epsilon(0.001));
        }
        THEN("45 degrees arc, CW") {
            Vec2f c = ArcWelder::arc_center(p1, p2, r, false);
            REQUIRE(is_approx(c, center2, 1.f));
        }
        THEN("315 degrees arc, CCW") {
            Vec2f c = ArcWelder::arc_center(p1, p2, - r, true);
            REQUIRE(is_approx(c, center2, 1.f));
            REQUIRE(ArcWelder::arc_angle(p1, p2, - r) == Approx((2. - 0.25) * M_PI));
            REQUIRE(ArcWelder::arc_length(p1, p2, - r) == Approx(r * (2. - 0.25) * M_PI).epsilon(0.001));
        }
        THEN("315 degrees arc, CW") {
            Vec2f c = ArcWelder::arc_center(p1, p2, - r, false);
            REQUIRE(is_approx(c, center1, 1.f));
        }
    }
    WHEN("arc from { 1866.f, 1500.f } to { 1000.f, 2000.f }") {
        Vec2f p1{ 1866.f, 1500.f };
        Vec2f p2{ 1000.f, 2000.f };
        float r{ 1000.f };
        Vec2f center1 = Vec2f{ 1000.f, 1000.f };
        // Center on the other side of the CCW arch.
        Vec2f center2 = center1 + 2. * (0.5 * (p1 + p2) - center1);
        THEN("60 degrees arc, CCW") {
            Vec2f c = ArcWelder::arc_center(p1, p2, r, true);
            REQUIRE(is_approx(c, center1, 1.f));
            REQUIRE(is_approx(ArcWelder::arc_angle(p1, p2, r), float(M_PI / 3.), 0.001f));
            REQUIRE(ArcWelder::arc_length(p1, p2, r) == Approx(r * M_PI / 3.).epsilon(0.001));
        }
        THEN("60 degrees arc, CW") {
            Vec2f c = ArcWelder::arc_center(p1, p2, r, false);
            REQUIRE(is_approx(c, center2, 1.f));
        }
        THEN("300 degrees arc, CCW") {
            Vec2f c = ArcWelder::arc_center(p1, p2, - r, true);
            REQUIRE(is_approx(c, center2, 1.f));
            REQUIRE(is_approx(ArcWelder::arc_angle(p1, p2, - r), float((2. - 1./3.) * M_PI), 0.001f));
            REQUIRE(ArcWelder::arc_length(p1, p2, - r) == Approx(r * (2. - 1. / 3.) * M_PI).epsilon(0.001));
        }
        THEN("300 degrees arc, CW") {
            Vec2f c = ArcWelder::arc_center(p1, p2, - r, false);
            REQUIRE(is_approx(c, center1, 1.f));
        }
    }
}

TEST_CASE("arc discretization", "[ArcWelder]") {
    using namespace Slic3r::Geometry;
    WHEN("arc from { 2, 1 } to { 1, 2 }") {
        const Point p1         = Point::new_scale(2., 1.);
        const Point p2         = Point::new_scale(1., 2.);
        const Point center     = Point::new_scale(1., 1.);
        const float radius     = scaled<float>(1.);
        const float resolution = scaled<float>(0.002);
        auto test = [center, resolution, radius](const Point &p1, const Point &p2, const float r, const bool ccw) {
            Vec2f  c = ArcWelder::arc_center(p1.cast<float>(), p2.cast<float>(), r, ccw);
            REQUIRE((p1.cast<float>() - c).norm() == Approx(radius));
            REQUIRE((c - center.cast<float>()).norm() == Approx(0.));
            Points pts = ArcWelder::arc_discretize(p1, p2, r, ccw, resolution);
            REQUIRE(pts.size() >= 2);
            REQUIRE(pts.front() == p1);
            REQUIRE(pts.back() == p2);
            for (const Point &p : pts)
                REQUIRE(std::abs((p.cast<double>() - c.cast<double>()).norm() - double(radius)) < double(resolution + SCALED_EPSILON));
        };
        THEN("90 degrees arc, CCW") {
            test(p1, p2, radius, true);
        }
        THEN("270 degrees arc, CCW") {
            test(p2, p1, - radius, true);
        }
        THEN("90 degrees arc, CW") {
            test(p2, p1, radius, false);
        }
        THEN("270 degrees arc, CW") {
            test(p1, p2, - radius, false);
        }
    }
}

void test_arc_fit_variance(const Point &p1, const Point &p2, const float r, const float r_fit, const bool ccw, const Points::const_iterator begin, const Points::const_iterator end)
{
    using namespace Slic3r::Geometry;
    double variance          = ArcWelder::arc_fit_variance(p1, p2, r, ccw, begin, end);
    double variance_fit      = ArcWelder::arc_fit_variance(p1, p2, r_fit, ccw, begin, end);
    REQUIRE(variance_fit < variance);
};

void test_arc_fit_max_deviation(const Point &p1, const Point &p2, const float r, const float r_fit, const bool ccw, const Points::const_iterator begin, const Points::const_iterator end)
{
    using namespace Slic3r::Geometry;
    double max_deviation     = ArcWelder::arc_fit_max_deviation(p1, p2, r, ccw, begin, end);
    double max_deviation_fit = ArcWelder::arc_fit_max_deviation(p1, p2, r_fit, ccw, begin, end);
    REQUIRE(std::abs(max_deviation_fit) < std::abs(max_deviation));
};

void test_arc_fit(const Point &p1, const Point &p2, const float r, const float r_fit, const bool ccw, const Points::const_iterator begin, const Points::const_iterator end)
{
    test_arc_fit_variance(p1, p2, r, r_fit, ccw, begin, end);
    test_arc_fit_max_deviation(p1, p2, r, r_fit, ccw, begin, end);
};

TEST_CASE("arc fitting", "[ArcWelder]") {
    using namespace Slic3r::Geometry;

    WHEN("arc from { 2, 1 } to { 1, 2 }") {
        const Point p1         = Point::new_scale(2., 1.);
        const Point p2         = Point::new_scale(1., 2.);
        const Point center     = Point::new_scale(1., 1.);
        const float radius     = scaled<float>(1.);
        const float resolution = scaled<float>(0.002);
        auto test = [center, resolution](const Point &p1, const Point &p2, const float r, const bool ccw) {
            Points pts = ArcWelder::arc_discretize(p1, p2, r, ccw, resolution);
            ArcWelder::Path path = ArcWelder::fit_path(pts, resolution + SCALED_EPSILON, ArcWelder::default_scaled_resolution);
            REQUIRE(path.size() == 2);
            REQUIRE(path.front().point == p1);
            REQUIRE(path.front().radius == 0.f);
            REQUIRE(path.back().point == p2);
            REQUIRE(path.back().ccw() == ccw);
            test_arc_fit(p1, p2, r, path.back().radius, true, pts.begin(), pts.end());
        };
        THEN("90 degrees arc, CCW is fitted") {
            test(p1, p2, radius, true);
        }
        THEN("270 degrees arc, CCW is fitted") {
            test(p2, p1, - radius, true);
        }
        THEN("90 degrees arc, CW is fitted") {
            test(p2, p1, radius, false);
        }
        THEN("270 degrees arc, CW is fitted") {
            test(p1, p2, - radius, false);
        }
    }

    WHEN("arc from { 2, 1 } to { 1, 2 }, another arc from { 2, 1 } to { 0, 2 }, tangentially connected") {
        const Point p1 = Point::new_scale(2., 1.);
        const Point p2 = Point::new_scale(1., 2.);
        const Point p3 = Point::new_scale(0., 3.);
        const Point center1 = Point::new_scale(1., 1.);
        const Point center2 = Point::new_scale(1., 3.);
        const float radius = scaled<float>(1.);
        const float resolution = scaled<float>(0.002);
        auto test = [center1, center2, resolution](const Point &p1, const Point &p2, const Point &p3, const float r, const bool ccw) {
            Points pts = ArcWelder::arc_discretize(p1, p2, r, ccw, resolution);
            size_t num_pts1 = pts.size();
            {
                Points pts2 = ArcWelder::arc_discretize(p2, p3, - r, ! ccw, resolution);
                REQUIRE(pts.back() == pts2.front());
                pts.insert(pts.end(), std::next(pts2.begin()), pts2.end());
            }
            ArcWelder::Path path = ArcWelder::fit_path(pts, resolution + SCALED_EPSILON, ArcWelder::default_scaled_resolution);
            REQUIRE(path.size() == 3);
            REQUIRE(path.front().point == p1);
            REQUIRE(path.front().radius == 0.f);
            REQUIRE(path[1].point == p2);
            REQUIRE(path[1].ccw() == ccw);
            REQUIRE(path.back().point == p3);
            REQUIRE(path.back().ccw() == ! ccw);
            test_arc_fit(p1, p2, r, path[1].radius, ccw, pts.begin(), pts.begin() + num_pts1);
            test_arc_fit(p2, p3, - r, path.back().radius, ! ccw, pts.begin() + num_pts1 - 1, pts.end());
        };
        THEN("90 degrees arches, CCW are fitted") {
            test(p1, p2, p3, radius, true);
        }
        THEN("270 degrees arc, CCW is fitted") {
            test(p3, p2, p1, -radius, true);
        }
        THEN("90 degrees arc, CW is fitted") {
            test(p3, p2, p1, radius, false);
        }
        THEN("270 degrees arc, CW is fitted") {
            test(p1, p2, p3, -radius, false);
        }
    }
}

TEST_CASE("least squares arc fitting, interpolating end points", "[ArcWelder]") {
    using namespace Slic3r::Geometry;

    // Generate bunch of random arches.
    const coord_t                 max_coordinate = scaled<coord_t>(sqrt(250. - 1.));
    static constexpr const double min_radius     = scaled<double>(0.01);
    static constexpr const double max_radius     = scaled<double>(250.);
//  static constexpr const double deviation      = scaled<double>(0.5);
    static constexpr const double deviation      = scaled<double>(0.1);
    // Seeded with a fixed seed, to be repeatable.
    std::mt19937                            rng(867092346);
    std::uniform_int_distribution<int32_t>  coord_sampler(0, int32_t(max_coordinate));
    std::uniform_real_distribution<double>  angle_sampler(0.001, 2. * M_PI - 0.001);
    std::uniform_real_distribution<double>  radius_sampler(min_radius, max_radius);
    std::uniform_int_distribution<int>      num_samples_sampler(1, 100);
    auto test_arc_fitting = [&rng, &coord_sampler, &num_samples_sampler, &angle_sampler, &radius_sampler]() {
        auto sample_point = [&rng, &coord_sampler]() {
            return Vec2d(coord_sampler(rng), coord_sampler(rng));
        };
        // Start and end point of the arc:
        Vec2d  center_pos = sample_point();
        double angle0     = angle_sampler(rng);
        double angle      = angle_sampler(rng);
        double radius     = radius_sampler(rng);
        Vec2d  v1         = Eigen::Rotation2D(angle0) * Vec2d(1., 0.);
        Vec2d  v2         = Eigen::Rotation2D(angle0 + angle) * Vec2d(1., 0.);
        Vec2d  start_pos  = center_pos + radius * v1;
        Vec2d  end_pos    = center_pos + radius * v2;
        std::vector<Vec2d> samples;
        size_t num_samples = num_samples_sampler(rng);
        for (size_t i = 0; i < num_samples; ++ i) {
            double sample_r = sqrt(std::uniform_real_distribution<double>(sqr(std::max(0., radius - deviation)), sqr(radius + deviation))(rng));
            double sample_a = std::uniform_real_distribution<double>(0., angle)(rng);
            Vec2d  pt = center_pos + Eigen::Rotation2D(angle0 + sample_a) * Vec2d(sample_r, 0.);
            samples.emplace_back(pt);
            assert((pt - center_pos).norm() > radius - deviation - SCALED_EPSILON);
            assert((pt - center_pos).norm() < radius + deviation + SCALED_EPSILON);
        }
//        Vec2d new_center = ArcWelder::arc_fit_center_algebraic_ls(start_pos, end_pos, center_pos, samples.begin(), samples.end());
        THEN("Center is fitted correctly") {
            std::optional<Vec2d> new_center_opt = ArcWelder::arc_fit_center_gauss_newton_ls(start_pos, end_pos, center_pos, samples.begin(), samples.end(), 10);
            REQUIRE(new_center_opt);
            if (new_center_opt) {
                Vec2d  new_center = *new_center_opt;
                double total_deviation = 0;
                double new_total_deviation = 0;
                for (const Vec2d &s : samples) {
                    total_deviation += sqr((s - center_pos).norm() - radius);
                    new_total_deviation += sqr((s - new_center).norm() - radius);
                }
                total_deviation /= double(num_samples);
                new_total_deviation /= double(num_samples);
                REQUIRE(new_total_deviation <= total_deviation);

#if 0
                double dist = (center_pos - new_center).norm();
                printf("Radius: %lf, Angle: %lf deg, Samples: %d, Dist: %lf\n", unscaled<double>(radius), 180. * angle / M_PI, int(num_samples), unscaled<double>(dist));
    //            REQUIRE(is_approx(center_pos, new_center, deviation));
                if (dist > scaled<double>(1.)) {
                    static int irun = 0;
                    char path[2048];
                    sprintf(path, "d:\\temp\\debug\\circle-fit-%d.svg", irun++);
                    Eigen::AlignedBox<double, 2> bbox(start_pos, end_pos);
                    for (const Vec2d& sample : samples)
                        bbox.extend(sample);
                    bbox.extend(center_pos);
                    Slic3r::SVG svg(path, BoundingBox(bbox.min().cast<coord_t>(), bbox.max().cast<coord_t>()).inflated(bbox.sizes().maxCoeff() * 0.05));
                    Points arc_src;
                    for (size_t i = 0; i <= 1000; ++i)
                        arc_src.emplace_back((center_pos + Eigen::Rotation2D(angle0 + double(i) * angle / 1000.) * Vec2d(radius, 0.)).cast<coord_t>());
                    svg.draw(Polyline(arc_src));
                    Points arc_new;
                    double new_arc_angle = ArcWelder::arc_angle(start_pos, end_pos, (new_center - start_pos).norm());
                    for (size_t i = 0; i <= 1000; ++i)
                        arc_new.emplace_back((new_center + Eigen::Rotation2D(double(i) * new_arc_angle / 1000.) * (start_pos - new_center)).cast<coord_t>());
                    svg.draw(Polyline(arc_new), "magenta");
                    svg.draw(Point(start_pos.cast<coord_t>()), "blue");
                    svg.draw(Point(end_pos.cast<coord_t>()), "blue");
                    svg.draw(Point(center_pos.cast<coord_t>()), "blue");
                    for (const Vec2d &sample : samples)
                        svg.draw(Point(sample.cast<coord_t>()), "red");
                    svg.draw(Point(new_center.cast<coord_t>()), "magenta");
                }
                if (!is_approx(center_pos, new_center, scaled<double>(5.))) {
                    printf("Failed\n");
                }
#endif
            } else {
                printf("Fitting failed\n");
            }
        }
    };

    WHEN("Generating a random arc and randomized arc samples") {
        for (size_t i = 0; i < 1000; ++ i)
            test_arc_fitting();
    }
}

TEST_CASE("arc wedge test", "[ArcWelder]") {
    using namespace Slic3r::Geometry;

    WHEN("test point inside wedge, arc from { 2, 1 } to { 1, 2 }") {
        const int64_t s  = 1000000;
        const Vec2i64 p1{ 2 * s, s };
        const Vec2i64 p2{ s, 2 * s };
        const Vec2i64 center{ s, s };
        const int64_t radius{ s };
        auto test = [center](
            // Arc data
            const Vec2i64 &p1, const Vec2i64 &p2, const int64_t r, const bool ccw,
            // Test data
            const Vec2i64 &ptest, const bool ptest_inside) {
            const Vec2d c = ArcWelder::arc_center(p1.cast<double>(), p2.cast<double>(), double(r), ccw);
            REQUIRE(is_approx(c, center.cast<double>()));
            REQUIRE(ArcWelder::inside_arc_wedge(p1, p2, center, r > 0, ccw, ptest) == ptest_inside);
            REQUIRE(ArcWelder::inside_arc_wedge(p1.cast<double>(), p2.cast<double>(), double(r), ccw, ptest.cast<double>()) == ptest_inside);
        };
        auto test_quadrants = [center, test](
            // Arc data
            const Vec2i64 &p1, const Vec2i64 &p2, const int64_t r, const bool ccw,
            // Test data
            const Vec2i64 &ptest1, const bool ptest_inside1,
            const Vec2i64 &ptest2, const bool ptest_inside2, 
            const Vec2i64 &ptest3, const bool ptest_inside3,
            const Vec2i64 &ptest4, const bool ptest_inside4) {
            test(p1, p2, r, ccw, ptest1 + center, ptest_inside1);
            test(p1, p2, r, ccw, ptest2 + center, ptest_inside2);
            test(p1, p2, r, ccw, ptest3 + center, ptest_inside3);
            test(p1, p2, r, ccw, ptest4 + center, ptest_inside4);
        };
        THEN("90 degrees arc, CCW") {
            test_quadrants(p1, p2, radius, true, 
                Vec2i64{   s,   s }, true,
                Vec2i64{   s, - s }, false,
                Vec2i64{ - s,   s }, false,
                Vec2i64{ - s, - s }, false);
        }
        THEN("270 degrees arc, CCW") {
            test_quadrants(p2, p1, -radius, true,
                Vec2i64{   s,   s }, false,
                Vec2i64{   s, - s }, true,
                Vec2i64{ - s,   s }, true,
                Vec2i64{ - s, - s }, true);
        }
        THEN("90 degrees arc, CW") {
            test_quadrants(p2, p1, radius, false,
                Vec2i64{   s,   s }, true,
                Vec2i64{   s, - s }, false,
                Vec2i64{ - s,   s }, false,
                Vec2i64{ - s, - s }, false);
        }
        THEN("270 degrees arc, CW") {
            test_quadrants(p1, p2, -radius, false,
                Vec2i64{   s,   s }, false,
                Vec2i64{   s, - s }, true,
                Vec2i64{ - s,   s }, true,
                Vec2i64{ - s, - s }, true);
        }
    }
}

// Distilled a test case for failing assert(p != prev) inside GCodeGenerator::_extrude() that is caused
// by performing simplification of each ExtrusionPath in ExtrusionMultiPath one by one and not
// simplifying ExtrusionMultiPath as a whole.
TEST_CASE("ExtrusionMultiPath simplification", "[ArcWelderMultiPathSimplify][!mayfail]")
{
    using namespace Slic3r::Geometry;
    using namespace Slic3r::GCode;

    ExtrusionMultiPath multi_path;

    multi_path.paths.emplace_back(Polyline({Point(3615254, 8843476), Point(5301926, 8703627), Point(5503271, 8717959),
                                            Point(5787717, 8834837), Point(7465587, 10084995), Point(7565376, 10117372)}),
                                  ExtrusionAttributes(ExtrusionRole::SolidInfill, ExtrusionFlow(0.0626713, 0.449999f, 0.15f), false));

    multi_path.paths.emplace_back(Polyline({Point(7565376, 10117372), Point(7751661, 10097239)}),
                                  ExtrusionAttributes(ExtrusionRole::SolidInfill, ExtrusionFlow(0.0604367, 0.435101f, 0.15f), false));

    multi_path.paths.emplace_back(Polyline({Point(7751661, 10097239), Point(11289346, 8638614), Point(11412324, 8600432)}),
                                  ExtrusionAttributes(ExtrusionRole::SolidInfill, ExtrusionFlow(0.0547566, 0.397234f, 0.15f), false));

    multi_path.paths.emplace_back(Polyline({Point(11412324, 8600432), Point(11727623, 8578798)}),
                                  ExtrusionAttributes(ExtrusionRole::SolidInfill, ExtrusionFlow(0.059829, 0.43105f, 0.15f), false));

    multi_path.paths.emplace_back(Polyline({Point(11727623, 8578798), Point(12042923, 8557165)}),
                                  ExtrusionAttributes(ExtrusionRole::SolidInfill, ExtrusionFlow(0.0654324, 0.468406f, 0.15f), false));

    multi_path.paths.emplace_back(Polyline({Point(12042923, 8557165), Point(12358223, 8535532), Point(12339460, 8545477)}),
                                  ExtrusionAttributes(ExtrusionRole::SolidInfill, ExtrusionFlow(0.0710358, 0.505762f, 0.15f), false));

    multi_path.paths.emplace_back(Polyline({Point(12339460, 8545477), Point(12035789, 8689023)}),
                                  ExtrusionAttributes(ExtrusionRole::SolidInfill, ExtrusionFlow(0.0701369, 0.499769f, 0.15f), false));

    multi_path.paths.emplace_back(Polyline({Point(12035789, 8689023), Point(11732119, 8832569)}),
                                  ExtrusionAttributes(ExtrusionRole::SolidInfill, ExtrusionFlow(0.0650101, 0.465591f, 0.15f), false));

    multi_path.paths.emplace_back(Polyline({Point(11732119, 8832569), Point(11428449, 8976115)}),
                                  ExtrusionAttributes(ExtrusionRole::SolidInfill, ExtrusionFlow(0.0598834, 0.431413f, 0.15f), false));

    multi_path.paths.emplace_back(Polyline({Point(11428449, 8976115), Point(7890375, 10433797)}),
                                  ExtrusionAttributes(ExtrusionRole::SolidInfill, ExtrusionFlow(0.0547566, 0.397234f, 0.15f), false));

    multi_path.paths.emplace_back(Polyline({Point(7890375, 10433797), Point(7890196, 10433871)}),
                                  ExtrusionAttributes(ExtrusionRole::SolidInfill, ExtrusionFlow(0.0546036, 0.396214f, 0.15f), false));

    multi_path.paths.emplace_back(Polyline({Point(7890196, 10433871), Point(7645162, 10520244)}),
                                  ExtrusionAttributes(ExtrusionRole::SolidInfill, ExtrusionFlow(0.0586375, 0.423107f, 0.15f), false));

    multi_path.paths.emplace_back(Polyline({Point(7645162, 10520244), Point(7400129, 10606618), Point(6491466, 10980845),
                                            Point(3782930, 8968079)}),
                                  ExtrusionAttributes(ExtrusionRole::SolidInfill, ExtrusionFlow(0.0626713, 0.449999f, 0.15f), false));

    const double    resolution = 8000.;
    SmoothPathCache smooth_path_cache;
    SmoothPath      smooth_path = smooth_path_cache.resolve_or_fit(multi_path, false, resolution);

    double min_segment_length = std::numeric_limits<double>::max();
    for (const SmoothPathElement &el : smooth_path) {
        assert(el.path.size() > 1);
        Point prev_pt = el.path.front().point;

        for (auto segment_it = std::next(el.path.begin()); segment_it != el.path.end(); ++segment_it) {
            if (const double length = (segment_it->point - prev_pt).cast<double>().norm(); length < min_segment_length) {
                min_segment_length = length;
            }
        }
    }

    REQUIRE(min_segment_length >= resolution);
}

TEST_CASE("SmoothPath clipping test", "[ArcWelder]") {
    using namespace Slic3r::Geometry;

    const Polyline polyline = {
        Point(9237362, -279099),   Point(9239309, -204770),   Point(9232158, 477899),    Point(9153712, 1292530),
        Point(9014384, 2036579),   Point(8842322, 2697128),   Point(8569131, 3468590),   Point(8287136, 4090253),
        Point(8050736, 4537759),   Point(7786167, 4978071),   Point(7502123, 5396751),   Point(7085512, 5937730),
        Point(6536631, 6536722),   Point(5937701, 7085536),   Point(5336389, 7545178),   Point(4766354, 7921046),
        Point(4287299, 8181151),   Point(3798566, 8424823),   Point(3161891, 8687141),   Point(2477384, 8903260),
        Point(1985727, 9025657),   Point(1488659, 9120891),   Point(811611, 9208824),    Point(229795, 9234222),
        Point(-477899, 9232158),   Point(-1292541, 9153710),  Point(-1963942, 9030487),  Point(-2483966, 8901437),
        Point(-2967612, 8752145),  Point(-3606656, 8511944),  Point(-4098726, 8277235),  Point(-4583048, 8025111),
        Point(-5164553, 7667365),  Point(-5602853, 7343037),  Point(-6030084, 7003203),  Point(-6532687, 6541035),
        Point(-7085558, 5937673),  Point(-7502041, 5396860),  Point(-7802209, 4952884),  Point(-8061668, 4518435),
        Point(-8375899, 3912214),  Point(-8689042, 3156205),  Point(-8915304, 2433948),  Point(-9073554, 1769674),
        Point(-9194504, 960323),   Point(-9238723, 227049),   Point(-9237360, -279112),  Point(-9194498, -960380),
        Point(-9073524, -1769810), Point(-8895452, -2505523), Point(-8689032, -3156238), Point(-8375859, -3912298),
        Point(-8025112, -4583044), Point(-7667378, -5164532), Point(-7180536, -5822455), Point(-6729193, -6334406),
        Point(-6350620, -6713810), Point(-5973693, -7051366), Point(-5438560, -7475505), Point(-4756170, -7927163),
        Point(-4110103, -8277232), Point(-3651006, -8489813), Point(-3015355, -8738921), Point(-2492584, -8893770),
        Point(-1963947, -9030483), Point(-1286636, -9154696), Point(-590411, -9222659),  Point(14602, -9244383),
        Point(974789, -9192915),   Point(1634833, -9095889),  Point(2193590, -8977466),  Point(2851102, -8793883),
        Point(3612042, -8509372),  Point(4098709, -8277242),  Point(4583076, -8025095),  Point(5164577, -7667349),
        Point(5822437, -7180551),  Point(6388368, -6677987),  Point(6866030, -6190211),  Point(7236430, -5740880),
        Point(7660739, -5174380),  Point(8088357, -4476558),  Point(8394013, -3866175),  Point(8593000, -3400880),
        Point(8768650, -2918284),  Point(8915319, -2433894),  Point(9073549, -1769711),  Point(9194508, -960282),
        Point(9237362, -279099)
    };

    const ExtrusionAttributes extrusion_attributes(ExtrusionRole::Perimeter, ExtrusionFlow{1.0, 1.0, 1.0});
    const GCode::SmoothPath   smooth_path        = {GCode::SmoothPathElement{extrusion_attributes, ArcWelder::fit_path(polyline.points, 32000., 0.05)}};
    const double              smooth_path_length = GCode::length(smooth_path);

    const size_t clip_segment_cnt = 20;
    for (size_t segment_idx = 1; segment_idx <= clip_segment_cnt; ++segment_idx) {
        const double      clip_length         = static_cast<double>(segment_idx) * (smooth_path_length / (clip_segment_cnt + 1));
        GCode::SmoothPath smooth_path_clipped = smooth_path;

        clip_end(smooth_path_clipped, smooth_path_length - clip_length, scaled<double>(GCode::ExtrusionOrder::min_gcode_segment_length));

        const double smooth_path_clipped_length = GCode::length(smooth_path_clipped);
        const double relative_diff              = std::abs(1. - (clip_length / smooth_path_clipped_length));
        REQUIRE(relative_diff <= 0.000001);
    }
}

#if 0
// For quantization
//#include <libslic3r/GCode/GCodeWriter.hpp>

TEST_CASE("arc quantization", "[ArcWelder]") {
    using namespace Slic3r::Geometry;

    WHEN("generating a bunch of random arches") {
        static constexpr const size_t  len            = 100000;
        static constexpr const coord_t max_coordinate = scaled<coord_t>(250.);
        static constexpr const float   max_radius     = scaled<float>(250.);
        ArcWelder::Segments path;
        path.reserve(len + 1);
        // Seeded with a fixed seed, to be repeatable.
        std::mt19937 rng(987432690);
        // Generate bunch of random arches.
        std::uniform_int_distribution<int32_t>  coord_sampler(0, int32_t(max_coordinate));
        std::uniform_real_distribution<float>   radius_sampler(- max_radius, max_radius);
        std::uniform_int_distribution<int>      orientation_sampler(0, 1);
        path.push_back({ Point{coord_sampler(rng), coord_sampler(rng)}, 0, ArcWelder::Orientation::CCW });
        for (size_t i = 0; i < len; ++ i) {
            ArcWelder::Segment seg { Point{coord_sampler(rng), coord_sampler(rng)}, radius_sampler(rng), orientation_sampler(rng) ? ArcWelder::Orientation::CCW : ArcWelder::Orientation::CW };
            while ((seg.point.cast<double>() - path.back().point.cast<double>()).norm() > 2. * std::abs(seg.radius))
                seg = { Point{coord_sampler(rng), coord_sampler(rng)}, radius_sampler(rng), orientation_sampler(rng) ? ArcWelder::Orientation::CCW : ArcWelder::Orientation::CW };
            path.push_back(seg);
        }
        // Run the test, quantize coordinates and radius, find the maximum error of quantization comparing the two arches.
        struct ArcEval {
            double error;
            double radius;
            double angle;
        };
        std::vector<ArcEval> center_errors_R;
        center_errors_R.reserve(len);
        std::vector<double> center_errors_R_exact;
        center_errors_R_exact.reserve(len);
        std::vector<double> center_errors_IJ;
        center_errors_IJ.reserve(len);
        for (size_t i = 0; i < len; ++ i) {
            // Source arc:
            const Vec2d  start_point = unscaled<double>(path[i].point);
            const Vec2d  end_point   = unscaled<double>(path[i + 1].point);
            const double radius      = unscaled<double>(path[i + 1].radius);
            const bool   ccw         = path[i + 1].ccw();
            const Vec2d  center      = ArcWelder::arc_center(start_point, end_point, radius, ccw);
            {
                const double d1 = (start_point - center).norm();
                const double d2 = (end_point - center).norm();
                const double dx = (end_point - start_point).norm();
                assert(std::abs(d1 - std::abs(radius)) < EPSILON);
                assert(std::abs(d2 - std::abs(radius)) < EPSILON);
            }
            // Quantized arc:
            const Vec2d  start_point_quantized { GCodeFormatter::quantize_xyzf(start_point.x()), GCodeFormatter::quantize_xyzf(start_point.y()) };
            const Vec2d  end_point_quantized   { GCodeFormatter::quantize_xyzf(end_point  .x()), GCodeFormatter::quantize_xyzf(end_point  .y()) };
            const double radius_quantized      { GCodeFormatter::quantize_xyzf(radius) };
            const Vec2d  center_quantized      { GCodeFormatter::quantize_xyzf(center     .x()), GCodeFormatter::quantize_xyzf(center     .y()) };
            // Evaulate maximum error for the quantized arc given by the end points and radius.
            const Vec2d  center_from_quantized = ArcWelder::arc_center(start_point_quantized, end_point_quantized, radius, ccw);
            const Vec2d  center_reconstructed  = ArcWelder::arc_center(start_point_quantized, end_point_quantized, radius_quantized, ccw);
#if 0
            center_errors_R.push_back({
                std::abs((center_reconstructed - center).norm()),
                radius,
                ArcWelder::arc_angle(start_point, end_point, radius) * 180. / M_PI
            });
            if (center_errors_R.back().error > 0.15)
                printf("Fuj\n");
#endif
            center_errors_R_exact.emplace_back(std::abs((center_from_quantized - center).norm()));
            if (center_errors_R_exact.back() > 0.15)
                printf("Fuj\n");
            center_errors_IJ.emplace_back(std::abs((center_quantized - center).norm()));
            if (center_errors_IJ.back() > 0.15)
                printf("Fuj\n");

            // Adjust center of the arc to the quantized end points.
            Vec2d third_point = ArcWelder::arc_middle_point(start_point, end_point, radius, ccw);
            double third_point_radius = (third_point - center).norm();
            assert(std::abs(third_point_radius - std::abs(radius)) < EPSILON);
            std::optional<Vec2d> center_adjusted = try_circle_center(start_point_quantized, end_point_quantized, third_point, EPSILON);
            //assert(center_adjusted);
            if (center_adjusted) {
                const double radius_adjusted = (center_adjusted.value() - start_point_quantized).norm() * (radius > 0 ? 1. : -1.);
                const double radius_adjusted_quantized = GCodeFormatter::quantize_xyzf(radius_adjusted);
                // Evaulate maximum error for the quantized arc given by the end points and radius.
                const Vec2f  center_reconstructed = ArcWelder::arc_center(start_point_quantized.cast<float>(), end_point_quantized.cast<float>(), float(radius_adjusted_quantized), ccw);
                double rtest = std::abs(radius_adjusted_quantized);
                double d1 = std::abs((center_reconstructed - start_point.cast<float>()).norm() - rtest);
                double d2 = std::abs((center_reconstructed - end_point.cast<float>()).norm() - rtest);
                double d3 = std::abs((center_reconstructed - third_point.cast<float>()).norm() - rtest);
                double d = std::max(d1, std::max(d2, d3));
                center_errors_R.push_back({
                    d,
                    radius,
                    ArcWelder::arc_angle(start_point, end_point, radius) * 180. / M_PI
                    });
            } else {
                printf("Adjusted circle is collinear.\n");
            }
        }
        std::sort(center_errors_R.begin(), center_errors_R.end(), [](auto l, auto r) { return l.error > r.error; });
        std::sort(center_errors_R_exact.begin(), center_errors_R_exact.end(), [](auto l, auto r) { return l > r; });
        std::sort(center_errors_IJ.begin(), center_errors_IJ.end(), [](auto l, auto r) { return l > r; });
        printf("Maximum error R: %lf\n", center_errors_R.back().error);
        printf("Maximum error R exact: %lf\n", center_errors_R_exact.back());
        printf("Maximum error IJ: %lf\n", center_errors_IJ.back());
    }
}
#endif
