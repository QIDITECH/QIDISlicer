#ifndef ARCHIVETRAITS_HPP
#define ARCHIVETRAITS_HPP

#include <string>

#include "SLAArchiveWriter.hpp"
#include "SLAArchiveReader.hpp"

#include "libslic3r/Zipper.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

class SL1Archive: public SLAArchiveWriter {
    SLAPrinterConfig m_cfg;
    
protected:
    std::unique_ptr<sla::RasterBase> create_raster() const override;
    sla::RasterEncoder get_encoder() const override;

    SLAPrinterConfig & cfg() { return m_cfg; }
    const SLAPrinterConfig & cfg() const { return m_cfg; }

    void export_print(Zipper &,
                      const SLAPrint       &print,
                      const ThumbnailsList &thumbnails,
                      const std::string    &projectname);

public:

    SL1Archive() = default;
    explicit SL1Archive(const SLAPrinterConfig &cfg): m_cfg(cfg) {}
    explicit SL1Archive(SLAPrinterConfig &&cfg): m_cfg(std::move(cfg)) {}

    void export_print(const std::string     fname,
                      const SLAPrint       &print,
                      const ThumbnailsList &thumbnails,
                      const std::string    &projectname = "") override;
};

class SL1Reader: public SLAArchiveReader {
    SLAImportQuality m_quality = SLAImportQuality::Balanced;
    std::function<bool(int)> m_progr;
    std::string m_fname;

public:
    // If the profile is missing from the archive (older PS versions did not have
    // it), profile_out's initial value will be used as fallback. profile_out will be empty on
    // function return if the archive did not contain any profile.
    ConfigSubstitutions read(std::vector<ExPolygons> &slices,
                             DynamicPrintConfig      &profile_out) override;

    ConfigSubstitutions read(DynamicPrintConfig &profile) override;

    SL1Reader() = default;
    SL1Reader(const std::string       &fname,
              SLAImportQuality         quality,
              std::function<bool(int)> progr)
        : m_quality(quality), m_progr(progr), m_fname(fname)
    {}
};

struct RasterParams {
    sla::RasterBase::Trafo trafo; // Raster transformations
    coord_t        width, height; // scaled raster dimensions (not resolution)
    double         px_h, px_w;    // pixel dimesions
};

RasterParams get_raster_params(const DynamicPrintConfig &cfg);

void invert_raster_trafo(ExPolygons &                  expolys,
                         const sla::RasterBase::Trafo &trafo,
                         coord_t                       width,
                         coord_t                       height);

} // namespace Slic3r::sla

#endif // ARCHIVETRAITS_HPP
