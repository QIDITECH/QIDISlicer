#include "SLAArchiveWriter.hpp"
#include "SLAArchiveFormatRegistry.hpp"

namespace Slic3r {

std::unique_ptr<SLAArchiveWriter>
SLAArchiveWriter::create(const std::string &archtype, const SLAPrinterConfig &cfg)
{
    std::unique_ptr<SLAArchiveWriter> ret;
    auto factory = get_writer_factory(archtype.c_str());

    if (factory)
        ret = factory(cfg);

    return ret;
}

} // namespace Slic3r
