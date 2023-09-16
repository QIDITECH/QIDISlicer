#ifndef QIDIPARTS_H
#define QIDIPARTS_H

#include <vector>
#include <libslic3r/ExPolygon.hpp>

using TestData = std::vector<Slic3r::Polygon>;
using TestDataEx = std::vector<Slic3r::ExPolygons>;

extern const TestData QIDI_PART_POLYGONS;
extern const TestData QIDI_STEGOSAUR_POLYGONS;
extern const TestDataEx QIDI_PART_POLYGONS_EX;

#endif // QIDIPARTS_H
