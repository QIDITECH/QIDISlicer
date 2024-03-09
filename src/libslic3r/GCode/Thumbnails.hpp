#ifndef slic3r_GCodeThumbnails_hpp_
#define slic3r_GCodeThumbnails_hpp_

#include "../Point.hpp"
#include "../PrintConfig.hpp"
#include "ThumbnailData.hpp"

#include <vector>
#include <memory>
#include <string_view>

#include <LibBGCode/binarize/binarize.hpp>
#include <boost/beast/core/detail/base64.hpp>

#include "../libslic3r/enum_bitmask.hpp"
//B3
#include "DataType.h"

namespace Slic3r {
    enum class ThumbnailError : int { InvalidVal, OutOfRange, InvalidExt };
    using ThumbnailErrors = enum_bitmask<ThumbnailError>;
    ENABLE_ENUM_BITMASK_OPERATORS(ThumbnailError);
}


//B3
typedef struct
{
    U16 colo16;
    U8  A0;
    U8  A1;
    U8  A2;
    U8  res0;
    U16 res1;
    U32 qty;
} U16HEAD;
typedef struct
{
    U8  encodever;
    U8  res0;
    U16 oncelistqty;
    U32 PicW;
    U32 PicH;
    U32 mark;
    U32 ListDataSize;
    U32 ColorDataSize;
    U32 res1;
    U32 res2;
} ColPicHead3;

namespace Slic3r::GCodeThumbnails {

struct CompressedImageBuffer
{
    void       *data { nullptr };
    size_t      size { 0 };
    virtual ~CompressedImageBuffer() {}
    virtual std::string_view tag() const = 0;
};

std::unique_ptr<CompressedImageBuffer> compress_thumbnail(const ThumbnailData &data, GCodeThumbnailsFormat format);

typedef std::vector<std::pair<GCodeThumbnailsFormat, Vec2d>> GCodeThumbnailDefinitionsList;

using namespace std::literals;
std::pair<GCodeThumbnailDefinitionsList, ThumbnailErrors> make_and_check_thumbnail_list(const std::string& thumbnails_string, const std::string_view def_ext = "PNG"sv);
std::pair<GCodeThumbnailDefinitionsList, ThumbnailErrors> make_and_check_thumbnail_list(const ConfigBase &config);

std::string get_error_string(const ThumbnailErrors& errors);


//B3
std::string compress_qidi_thumbnail(const ThumbnailData &data, GCodeThumbnailsFormat format);
int         ColPic_EncodeStr(U16 *fromcolor16, int picw, int pich, U8 *outputdata, int outputmaxtsize, int colorsmax);
template<typename WriteToOutput, typename ThrowIfCanceledCallback>
inline void export_thumbnails_to_file(ThumbnailsGeneratorCallback &thumbnail_cb, const std::vector<std::pair<GCodeThumbnailsFormat, Vec2d>>& thumbnails_list, WriteToOutput output, ThrowIfCanceledCallback throw_if_canceled)
{
    // Write thumbnails using base64 encoding
    if (thumbnail_cb != nullptr) {
        //B3
        int count = 0;
        for (const auto& [format, size] : thumbnails_list) {
        static constexpr const size_t max_row_length = 78;
        //B54
        ThumbnailsList thumbnails = thumbnail_cb(ThumbnailsParams{{size}, true, false, false, false});
        for (const ThumbnailData& data : thumbnails)
            if (data.is_valid()) {
                switch (format) {
                case GCodeThumbnailsFormat::QIDI: {
                    //auto compressed = compress_qidi_thumbnail(data, format);

                    //if (count == 0) {
                    //    output((boost::format("\n\n;gimage:%s\n\n") % compressed).str().c_str());
                    //    count++;
                    //    break;
                    //} else {
                    //    output((boost::format("\n\n;simage:%s\n\n") % compressed).str().c_str());
                    //    count++;
                    //    break;
                    //}
                    break;
                }
                default: {
                    auto compressed = compress_thumbnail(data, format);
                    if (compressed->data && compressed->size) {
                        std::string encoded;
                        encoded.resize(boost::beast::detail::base64::encoded_size(compressed->size));
                        encoded.resize(boost::beast::detail::base64::encode((void *) encoded.data(), (const void *) compressed->data,
                                                                            compressed->size));

                        output((boost::format("\n;\n; %s begin %dx%d %d\n") % compressed->tag() % data.width % data.height % encoded.size())
                                   .str()
                                   .c_str());

                        while (encoded.size() > max_row_length) {
                            output((boost::format("; %s\n") % encoded.substr(0, max_row_length)).str().c_str());
                            encoded = encoded.substr(max_row_length);
                        }

                        if (encoded.size() > 0)
                            output((boost::format("; %s\n") % encoded).str().c_str());

                        output((boost::format("; %s end\n;\n") % compressed->tag()).str().c_str());

                    }
                }

                }
                throw_if_canceled();
            }
    }
}
}
//B3
template<typename WriteToOutput, typename ThrowIfCanceledCallback>
inline void export_qidi_thumbnails_to_file(ThumbnailsGeneratorCallback &                               thumbnail_cb,
                                      const std::vector<std::pair<GCodeThumbnailsFormat, Vec2d>> &thumbnails_list,
                                      WriteToOutput                                               output,
                                      ThrowIfCanceledCallback                                     throw_if_canceled)
{
    // Write thumbnails using base64 encoding
    if (thumbnail_cb != nullptr) {
        //B3
        int count = 0;
        for (const auto &[format, size] : thumbnails_list) {
            static constexpr const size_t max_row_length = 78;
            ThumbnailsList                thumbnails     = thumbnail_cb(ThumbnailsParams{{size}, true, false, false, true});
            for (const ThumbnailData &data : thumbnails)
                if (data.is_valid()) {
                    switch (format) {
                    case GCodeThumbnailsFormat::QIDI: {
                        auto compressed = compress_qidi_thumbnail(data, format);

                        if (count == 0) {
                            output((boost::format("\n\n;gimage:%s\n\n") % compressed).str().c_str());
                            count++;
                            break;
                        } else {
                            output((boost::format("\n\n;simage:%s\n\n") % compressed).str().c_str());
                            count++;
                            break;
                        }
                    }
                    default: {
                        //auto compressed = compress_thumbnail(data, format);
                        //if (compressed->data && compressed->size) {
                        //    std::string encoded;
                        //    encoded.resize(boost::beast::detail::base64::encoded_size(compressed->size));
                        //    encoded.resize(boost::beast::detail::base64::encode((void *) encoded.data(), (const void *) compressed->data,
                        //                                                        compressed->size));

                        //    output((boost::format("\n;\n; %s begin %dx%d %d\n") % compressed->tag() % data.width % data.height %
                        //            encoded.size())
                        //               .str()
                        //               .c_str());

                        //    while (encoded.size() > max_row_length) {
                        //        output((boost::format("; %s\n") % encoded.substr(0, max_row_length)).str().c_str());
                        //        encoded = encoded.substr(max_row_length);
                        //    }

                        //    if (encoded.size() > 0)
                        //        output((boost::format("; %s\n") % encoded).str().c_str());

                        //    output((boost::format("; %s end\n;\n") % compressed->tag()).str().c_str());
                        //}
                    }
                    }
                    throw_if_canceled();
                }
        }
    }
}


template<typename ThrowIfCanceledCallback>
inline void generate_binary_thumbnails(ThumbnailsGeneratorCallback& thumbnail_cb, std::vector<bgcode::binarize::ThumbnailBlock>& out_thumbnails,
    const std::vector<std::pair<GCodeThumbnailsFormat, Vec2d>> &thumbnails_list, ThrowIfCanceledCallback throw_if_canceled)
{
    using namespace bgcode::core;
    using namespace bgcode::binarize;
    out_thumbnails.clear();
    assert(thumbnail_cb != nullptr);
    if (thumbnail_cb != nullptr) {
        for (const auto& [format, size] : thumbnails_list) {
            ThumbnailsList thumbnails = thumbnail_cb(ThumbnailsParams{ {size}, true, true, true, true });
            for (const ThumbnailData &data : thumbnails)
                if (data.is_valid()) {
                    auto compressed = compress_thumbnail(data, format);
                    if (compressed->data != nullptr && compressed->size > 0) {
                        ThumbnailBlock& block = out_thumbnails.emplace_back(ThumbnailBlock());
                        block.params.width = (uint16_t)data.width;
                        block.params.height = (uint16_t)data.height;
                        switch (format) {
                        case GCodeThumbnailsFormat::PNG: { block.params.format = (uint16_t)EThumbnailFormat::PNG; break; }
                        case GCodeThumbnailsFormat::JPG: { block.params.format = (uint16_t)EThumbnailFormat::JPG; break; }
                        case GCodeThumbnailsFormat::QOI: { block.params.format = (uint16_t)EThumbnailFormat::QOI; break; }
                        }
                        block.data.resize(compressed->size);
                        memcpy(block.data.data(), compressed->data, compressed->size);
                    }
                }
        }
    }
}




} // namespace Slic3r::GCodeThumbnails

#endif // slic3r_GCodeThumbnails_hpp_
