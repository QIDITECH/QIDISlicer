#ifndef slic3r_GCodeThumbnails_hpp_
#define slic3r_GCodeThumbnails_hpp_

#include "../Point.hpp"
#include "../PrintConfig.hpp"
#include "ThumbnailData.hpp"

#include <vector>
#include <memory>
#include <string_view>

#include <boost/beast/core/detail/base64.hpp>

#include "DataType.h"

typedef struct
{
    U16 colo16;
    U8 A0;
    U8 A1;
    U8 A2;
    U8 res0;
    U16 res1;
    U32 qty;
}U16HEAD;
typedef struct
{
    U8 encodever;
    U8 res0;
    U16 oncelistqty;
    U32 PicW;
    U32 PicH;
    U32 mark;
    U32 ListDataSize;
    U32 ColorDataSize;
    U32 res1;
    U32 res2;
}ColPicHead3;

namespace Slic3r::GCodeThumbnails {

struct CompressedImageBuffer
{
    void       *data { nullptr };
    size_t      size { 0 };
    virtual ~CompressedImageBuffer() {}
    virtual std::string_view tag() const = 0;
};

std::unique_ptr<CompressedImageBuffer> compress_thumbnail(const ThumbnailData &data, GCodeThumbnailsFormat format);
//B3
std::string compress_qidi_thumbnail(
    const ThumbnailData &data, GCodeThumbnailsFormat format);
int ColPic_EncodeStr(U16* fromcolor16, int picw, int pich, U8* outputdata, int outputmaxtsize, int colorsmax);
template<typename WriteToOutput, typename ThrowIfCanceledCallback>
inline void export_thumbnails_to_file(ThumbnailsGeneratorCallback &thumbnail_cb, const std::vector<Vec2d> &sizes, GCodeThumbnailsFormat format, WriteToOutput output, ThrowIfCanceledCallback throw_if_canceled)
{
    // Write thumbnails using base64 encoding
    if (thumbnail_cb != nullptr) {
        static constexpr const size_t max_row_length = 78;
        // ThumbnailsList thumbnails = thumbnail_cb(ThumbnailsParams{ sizes, true, true, true, true });
        //B3
        ThumbnailsList thumbnails = thumbnail_cb(ThumbnailsParams{ sizes, false, false, false, true });
        int count = 0;
        for (const ThumbnailData& data : thumbnails)
            if (data.is_valid()) {
                //B3
                switch (format) {
                case GCodeThumbnailsFormat::QIDI:
                {
                    auto compressed = compress_qidi_thumbnail(data,
                                                                  format);
                    if (count == 0) {
                        output(
                            (boost::format("\n\n;gimage:%s\n\n") % compressed)
                                .str()
                                .c_str());
                        count++;
                        break;
                    } else {
                        output(
                            (boost::format("\n\n;simage:%s\n\n") % compressed)
                                .str()
                                .c_str());
                        count++;
                        break;
                    }
                }
                case GCodeThumbnailsFormat::JPG:
                default: {
                    auto compressed = compress_thumbnail(data, format);
                    if (compressed->data && compressed->size) {
                        std::string encoded;
                        encoded.resize(
                            boost::beast::detail::base64::encoded_size(
                                compressed->size));
                        encoded.resize(boost::beast::detail::base64::encode(
                            (void *) encoded.data(),
                            (const void *) compressed->data,
                            compressed->size));
                        output((boost::format("\n;\n; %s begin %dx%d %d\n") %
                                compressed->tag() % data.width % data.height %
                                encoded.size())
                                   .str()
                                   .c_str());

                        while (encoded.size() > max_row_length) {
                            output((boost::format("; %s\n") %
                                    encoded.substr(0, max_row_length))
                                       .str()
                                       .c_str());
                            encoded = encoded.substr(max_row_length);
                        }

                        if (encoded.size() > 0)
                            output((boost::format("; %s\n") % encoded)
                                       .str()
                                       .c_str());

                        output((boost::format("; %s end\n;\n") %
                                compressed->tag())
                                   .str()
                                   .c_str());
                    }
                }

                }

                throw_if_canceled();
            }
    }
}

} // namespace Slic3r::GCodeThumbnails

#endif // slic3r_GCodeThumbnails_hpp_
