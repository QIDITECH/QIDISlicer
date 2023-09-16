#include <iostream>
#include <ostream>

#include <libslic3r/TriangleMesh.hpp>

#include <boost/filesystem.hpp>

void print_arrange_polygons(const std::string &dirpath, std::ostream &out)
{
    using namespace Slic3r;

    boost::filesystem::path p = dirpath;

    if (!boost::filesystem::exists(p) || !boost::filesystem::is_directory(p))
        return;

    for (const auto& entry : boost::filesystem::directory_iterator(p)) {
        if (!boost::filesystem::is_regular_file(entry)) {
            continue;
        }

        TriangleMesh mesh;
        mesh.ReadSTLFile(entry.path().c_str());
        ExPolygons outline = mesh.horizontal_projection();

        out << "// " << entry.path().filename() << ": " << std::endl;
        for (const ExPolygon &expoly : outline) {
            out << "MyPoly{\n";  // Start of polygon

            out << "\t{\n";  // Start of contour
            for (const auto& point : expoly.contour.points) {
                out << "        {" << point.x() << ", " << point.y() << "},\n";  // Print point coordinates
            }
            out << "    },\n";  // End of contour

            out << "    {\n"; // start of holes
            for (const auto& hole : expoly.holes) {
                out << "        {\n";  // Start of hole
                for (const auto& point : hole.points) {
                    out << "            {" << point.x() << ", " << point.y() << "},\n";  // Print point coordinates
                }
                out << "        },\n";  // End of hole Polygon
            }
            out << "    }\n"; // end of holes Polygons
            out << "},\n";  // End of ExPolygon
        }
    }
}

void print_arrange_items(const std::string &dirpath, std::ostream &out)
{
    using namespace Slic3r;

    boost::filesystem::path p = dirpath;

    if (!boost::filesystem::exists(p) || !boost::filesystem::is_directory(p))
        return;

    for (const auto& entry : boost::filesystem::directory_iterator(p)) {
        if (!boost::filesystem::is_regular_file(entry)) {
            continue;
        }

        TriangleMesh mesh;
        mesh.ReadSTLFile(entry.path().c_str());
        ExPolygons outline = mesh.horizontal_projection();

        out << "ExPolygons{ " << "// " << entry.path().filename() << ":\n";
        for (const ExPolygon &expoly : outline) {
            out << "    MyPoly{\n";  // Start of polygon

            out << "        {\n";  // Start of contour
            for (const auto& point : expoly.contour.points) {
                out << "            {" << point.x() << ", " << point.y() << "},\n";  // Print point coordinates
            }
            out << "        },\n";  // End of contour

            out << "        {\n"; // start of holes
            for (const auto& hole : expoly.holes) {
                out << "            {\n";  // Start of hole
                for (const auto& point : hole.points) {
                    out << "                {" << point.x() << ", " << point.y() << "},\n";  // Print point coordinates
                }
                out << "            },\n";  // End of hole Polygon
            }
            out << "        }\n"; // end of holes Polygons
            out << "    },\n";  // End of ExPolygon
        }
        out << "},\n";
    }
}

int main(int argc, const char *argv[])
{
    if (argc <= 1)
        return -1;

    std::string dirpath = argv[1];

    print_arrange_items(dirpath, std::cout);

    return 0;
}
