#include "Thumbnails.hpp"

#include <qoi.h>
#include <jpeglib.h>
#include <jmorecfg.h>
#include <stdlib.h>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/log/trivial.hpp>
#include <string>
#include <cstdint>

#include "libslic3r/miniz_extension.hpp" // IWYU pragma: keep
#include "../format.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "miniz.h"

namespace Slic3r::GCodeThumbnails {

using namespace std::literals;

struct CompressedPNG : CompressedImageBuffer 
{
    ~CompressedPNG() override { if (data) mz_free(data); }
    std::string_view tag() const override { return "thumbnail"sv; }
};

struct CompressedJPG : CompressedImageBuffer
{
    ~CompressedJPG() override { free(data); }
    std::string_view tag() const override { return "thumbnail_JPG"sv; }
};

struct CompressedQOI : CompressedImageBuffer 
{
    ~CompressedQOI() override { free(data); }
    std::string_view tag() const override { return "thumbnail_QOI"sv; }
};

std::unique_ptr<CompressedImageBuffer> compress_thumbnail_png(const ThumbnailData &data)
{
    auto out = std::make_unique<CompressedPNG>();
    out->data = tdefl_write_image_to_png_file_in_memory_ex((const void*)data.pixels.data(), data.width, data.height, 4, &out->size, MZ_DEFAULT_LEVEL, 1);
    return out;
}

std::unique_ptr<CompressedImageBuffer> compress_thumbnail_jpg(const ThumbnailData& data)
{
    // Take vector of RGBA pixels and flip the image vertically
    std::vector<unsigned char> rgba_pixels(data.pixels.size());
    const unsigned int row_size = data.width * 4;
    for (unsigned int y = 0; y < data.height; ++y) {
        ::memcpy(rgba_pixels.data() + (data.height - y - 1) * row_size, data.pixels.data() + y * row_size, row_size);
    }

    // Store pointers to scanlines start for later use
    std::vector<unsigned char*> rows_ptrs;
    rows_ptrs.reserve(data.height);
    for (unsigned int y = 0; y < data.height; ++y) {
        rows_ptrs.emplace_back(&rgba_pixels[y * row_size]);
    }

    std::vector<unsigned char> compressed_data(data.pixels.size());
    unsigned char* compressed_data_ptr = compressed_data.data();
    unsigned long compressed_data_size = data.pixels.size();

    jpeg_error_mgr err;
    jpeg_compress_struct info;
    info.err = jpeg_std_error(&err);
    jpeg_create_compress(&info);
    jpeg_mem_dest(&info, &compressed_data_ptr, &compressed_data_size);

    info.image_width = data.width;
    info.image_height = data.height;
    info.input_components = 4;
    info.in_color_space = JCS_EXT_RGBA;

    jpeg_set_defaults(&info);
    jpeg_set_quality(&info, 85, TRUE);
    jpeg_start_compress(&info, TRUE);

    jpeg_write_scanlines(&info, rows_ptrs.data(), data.height);
    jpeg_finish_compress(&info);
    jpeg_destroy_compress(&info);

    // FIXME -> Add error checking

    auto out = std::make_unique<CompressedJPG>();
    out->data = malloc(compressed_data_size);
    out->size = size_t(compressed_data_size);
    ::memcpy(out->data, (const void*)compressed_data.data(), out->size);
    return out;
}

//B3
std::string compress_thumbnail_qidi(const ThumbnailData &data)
{
    auto out = std::make_unique<CompressedPNG>();
    // BOOST_LOG_TRIVIAL(error) << data.width;
    int width  = int(data.width);
    int height = int(data.height);

    if (data.width * data.height > 500 * 500) {
        width  = 500;
        height = 500;
    }
    U16 color16[500 * 500];
    // U16 *color16 = new U16[data.width * data.height];
    // for (int i = 0; i < 200*200; i++) color16[i] = 522240;
    unsigned char outputdata[500 * 500 * 10];
    // unsigned char *outputdata = new unsigned char[data.width * data.height * 10];

    std::vector<uint8_t> rgba_pixels(data.pixels.size() * 4);
    size_t               row_size = width * 4;
    for (size_t y = 0; y < height; ++y)
        memcpy(rgba_pixels.data() + y * row_size, data.pixels.data() + y * row_size, row_size);
    const unsigned char *pixels;
    pixels   = (const unsigned char *) rgba_pixels.data();
    int rrrr = 0, gggg = 0, bbbb = 0, aaaa = 0, rgb = 0;

    int time = width * height - 1; // 200*200-1;

    for (unsigned int r = 0; r < height; ++r) {
        unsigned int rr = r * width;
        for (unsigned int c = 0; c < width; ++c) {
            unsigned int cc = width - c - 1;
            rrrr            = int(pixels[4 * (rr + cc) + 0]) >> 3;
            gggg            = int(pixels[4 * (rr + cc) + 1]) >> 2;
            bbbb            = int(pixels[4 * (rr + cc) + 2]) >> 3;
            aaaa            = int(pixels[4 * (rr + cc) + 3]);
            if (aaaa == 0) {
                rrrr = 239 >> 3;
                gggg = 243 >> 2;
                bbbb = 247 >> 3;
            }
            rgb             = (rrrr << 11) | (gggg << 5) | bbbb;
            color16[time--] = rgb;
        }
    }

    int         res = ColPic_EncodeStr(color16, width, height, outputdata, height * width * 10, 1024);
    std::string temp;

    // for (unsigned char i : outputdata) { temp += i; }
    for (unsigned int i = 0; i < sizeof(outputdata); ++i) {
        temp += outputdata[i];
        // unsigned char strr = outputdata[i];
        // temp += strr;
    }
    // out->data = tdefl_write_image_to_png_file_in_memory_ex((const void*)data.pixels.data(), data.width, data.height, 4, &out->size,
    // MZ_DEFAULT_LEVEL, 1);
    return temp;
}
std::unique_ptr<CompressedImageBuffer> compress_thumbnail_qoi(const ThumbnailData &data)
{
    qoi_desc desc;
    desc.width      = data.width;
    desc.height     = data.height;
    desc.channels   = 4;
    desc.colorspace = QOI_SRGB;

    // Take vector of RGBA pixels and flip the image vertically
    std::vector<uint8_t> rgba_pixels(data.pixels.size() * 4);
    size_t row_size = data.width * 4;
    for (size_t y = 0; y < data.height; ++ y)
        memcpy(rgba_pixels.data() + (data.height - y - 1) * row_size, data.pixels.data() + y * row_size, row_size);
    
    auto out = std::make_unique<CompressedQOI>();
    int  size;
    out->data = qoi_encode((const void*)rgba_pixels.data(), &desc, &size);
    out->size = size;
    return out;
}

std::unique_ptr<CompressedImageBuffer> compress_thumbnail(const ThumbnailData &data, GCodeThumbnailsFormat format)
{
    switch (format) {
        case GCodeThumbnailsFormat::PNG:
        default:
            return compress_thumbnail_png(data);
        case GCodeThumbnailsFormat::JPG:
            return compress_thumbnail_jpg(data);
        case GCodeThumbnailsFormat::QOI:
            return compress_thumbnail_qoi(data);
    }
}

//B3
std::string compress_qidi_thumbnail(const ThumbnailData &data, GCodeThumbnailsFormat format) { return compress_thumbnail_qidi(data); }

std::pair<GCodeThumbnailDefinitionsList, ThumbnailErrors> make_and_check_thumbnail_list(const std::string& thumbnails_string, const std::string_view def_ext /*= "PNG"sv*/)
{
    if (thumbnails_string.empty())
        return {};

    std::istringstream is(thumbnails_string);
    std::string point_str;

    ThumbnailErrors errors;

    // generate thumbnails data to process it

    GCodeThumbnailDefinitionsList thumbnails_list;
    while (std::getline(is, point_str, ',')) {
        Vec2d point(Vec2d::Zero());
        GCodeThumbnailsFormat format;
        std::istringstream iss(point_str);
        std::string coord_str;
        if (std::getline(iss, coord_str, 'x') && !coord_str.empty()) {
            std::istringstream(coord_str) >> point(0);
            if (std::getline(iss, coord_str, '/') && !coord_str.empty()) {
                std::istringstream(coord_str) >> point(1);

                if (0 < point(0) && point(0) < 1000 && 0 < point(1) && point(1) < 1000) {
                    std::string ext_str;
                    std::getline(iss, ext_str, '/');

                    if (ext_str.empty())
                        ext_str = def_ext.empty() ? "PNG"sv : def_ext;

                    // check validity of extention
                    boost::to_upper(ext_str);
                    if (!ConfigOptionEnum<GCodeThumbnailsFormat>::from_string(ext_str, format)) {
                        format = GCodeThumbnailsFormat::PNG;
                        errors = enum_bitmask(errors | ThumbnailError::InvalidExt);
                    }

                    thumbnails_list.emplace_back(std::make_pair(format, point));
                }
                else
                    errors = enum_bitmask(errors | ThumbnailError::OutOfRange);
                continue;
            }
        }
        errors = enum_bitmask(errors | ThumbnailError::InvalidVal);
    }

    return std::make_pair(std::move(thumbnails_list), errors);
}

std::pair<GCodeThumbnailDefinitionsList, ThumbnailErrors> make_and_check_thumbnail_list(const ConfigBase& config)
{
    // ??? Unit tests or command line slicing may not define "thumbnails" or "thumbnails_format".
    // ??? If "thumbnails_format" is not defined, export to PNG.

    // generate thumbnails data to process it

    if (const auto thumbnails_value = config.option<ConfigOptionString>("thumbnails"))
        return make_and_check_thumbnail_list(thumbnails_value->value);

    return {};
}

std::string get_error_string(const ThumbnailErrors& errors)
{
    std::string error_str;

    if (errors.has(ThumbnailError::InvalidVal))
        error_str += "\n - " + format("Invalid input format. Expected vector of dimensions in the following format: \"%1%\"", "XxY/EXT, XxY/EXT, ...");
    if (errors.has(ThumbnailError::OutOfRange))
        error_str += "\n - Input value is out of range";
    if (errors.has(ThumbnailError::InvalidExt))
        error_str += "\n - Some extension in the input is invalid";

    return error_str;
}

//B3
static void colmemmove(U8 *dec, U8 *src, int lenth)
{
    if (src < dec) {
        dec += lenth - 1;
        src += lenth - 1;
        while (lenth > 0) {
            *(dec--) = *(src--);
            lenth--;
        }
    } else {
        while (lenth > 0) {
            *(dec++) = *(src++);
            lenth--;
        }
    }
}
static void colmemcpy(U8 *dec, U8 *src, int lenth)
{
    while (lenth > 0) {
        *(dec++) = *(src++);
        lenth--;
    }
}
static void colmemset(U8 *dec, U8 val, int lenth)
{
    while (lenth > 0) {
        *(dec++) = val;
        lenth--;
    }
}
static void ADList0(U16 val, U16HEAD *listu16, int *listqty, int maxqty)
{
    U8  A0;
    U8  A1;
    U8  A2;
    int qty = *listqty;
    if (qty >= maxqty)
        return;
    for (int i = 0; i < qty; i++) {
        if (listu16[i].colo16 == val) {
            listu16[i].qty++;
            return;
        }
    }
    A0         = (U8) (val >> 11);
    A1         = (U8) ((val << 5) >> 10);
    A2         = (U8) ((val << 11) >> 11);
    U16HEAD *a = &listu16[qty];
    a->colo16  = val;
    a->A0      = A0;
    a->A1      = A1;
    a->A2      = A2;
    a->qty     = 1;
    *listqty   = qty + 1;
}
static int Byte8bitEncode(U16 *fromcolor16, U16 *listu16, int listqty, int dotsqty, U8 *outputdata, int decMaxBytesize)
{
    U8  tid, sid;
    int dots     = 0;
    int srcindex = 0;
    int decindex = 0;
    int lastid   = 0;
    int temp     = 0;
    while (dotsqty > 0) {
        dots = 1;
        for (int i = 0; i < (dotsqty - 1); i++) {
            if (fromcolor16[srcindex + i] != fromcolor16[srcindex + i + 1])
                break;
            dots++;
            if (dots == 255)
                break;
        }
        temp = 0;
        for (int i = 0; i < listqty; i++) {
            if (listu16[i] == fromcolor16[srcindex]) {
                temp = i;
                break;
            }
        }
        tid = (U8) (temp % 32);
        sid = (U8) (temp / 32);
        if (lastid != sid) {
            if (decindex >= decMaxBytesize)
                goto IL_END;
            outputdata[decindex] = 7;
            outputdata[decindex] <<= 5;
            outputdata[decindex] += sid;
            decindex++;
            lastid = sid;
        }
        if (dots <= 6) {
            if (decindex >= decMaxBytesize)
                goto IL_END;
            outputdata[decindex] = (U8) dots;
            outputdata[decindex] <<= 5;
            outputdata[decindex] += tid;
            decindex++;
        } else {
            if (decindex >= decMaxBytesize)
                goto IL_END;
            outputdata[decindex] = 0;
            outputdata[decindex] += tid;
            decindex++;
            if (decindex >= decMaxBytesize)
                goto IL_END;
            outputdata[decindex] = (U8) dots;
            decindex++;
        }
        srcindex += dots;
        dotsqty -= dots;
    }
IL_END:
    return decindex;
}
static int ColPicEncode(U16 *fromcolor16, int picw, int pich, U8 *outputdata, int outputmaxtsize, int colorsmax)
{
    U16HEAD      l0;
    int          cha0, cha1, cha2, fid, minval;
    ColPicHead3 *Head0 = null;
    U16HEAD      Listu16[1024];
    int          ListQty = 0;
    int          enqty   = 0;
    int          dotsqty = picw * pich;
    if (colorsmax > 1024)
        colorsmax = 1024;
    for (int i = 0; i < dotsqty; i++) {
        int ch = (int) fromcolor16[i];
        ADList0(ch, Listu16, &ListQty, 1024);
    }

    for (int index = 1; index < ListQty; index++) {
        l0 = Listu16[index];
        for (int i = 0; i < index; i++) {
            if (l0.qty >= Listu16[i].qty) {
                colmemmove((U8 *) &Listu16[i + 1], (U8 *) &Listu16[i], (index - i) * sizeof(U16HEAD));
                colmemcpy((U8 *) &Listu16[i], (U8 *) &l0, sizeof(U16HEAD));
                break;
            }
        }
    }
    while (ListQty > colorsmax) {
        l0     = Listu16[ListQty - 1];
        minval = 255;
        fid    = -1;
        for (int i = 0; i < colorsmax; i++) {
            cha0 = Listu16[i].A0 - l0.A0;
            if (cha0 < 0)
                cha0 = 0 - cha0;
            cha1 = Listu16[i].A1 - l0.A1;
            if (cha1 < 0)
                cha1 = 0 - cha1;
            cha2 = Listu16[i].A2 - l0.A2;
            if (cha2 < 0)
                cha2 = 0 - cha2;
            int chall = cha0 + cha1 + cha2;
            if (chall < minval) {
                minval = chall;
                fid    = i;
            }
        }
        for (int i = 0; i < dotsqty; i++) {
            if (fromcolor16[i] == l0.colo16)
                fromcolor16[i] = Listu16[fid].colo16;
        }
        ListQty = ListQty - 1;
    }
    Head0 = ((ColPicHead3 *) outputdata);
    colmemset(outputdata, 0, sizeof(ColPicHead3));
    Head0->encodever    = 3;
    Head0->oncelistqty  = 0;
    Head0->mark         = 0x05DDC33C;
    Head0->ListDataSize = ListQty * 2;
    for (int i = 0; i < ListQty; i++) {
        U16 *l0 = (U16 *) &outputdata[sizeof(ColPicHead3)];
        l0[i]   = Listu16[i].colo16;
    }
    enqty                = Byte8bitEncode(fromcolor16, (U16 *) &outputdata[sizeof(ColPicHead3)], Head0->ListDataSize >> 1, dotsqty,
                           &outputdata[sizeof(ColPicHead3) + Head0->ListDataSize],
                           outputmaxtsize - sizeof(ColPicHead3) - Head0->ListDataSize);
    Head0->ColorDataSize = enqty;
    Head0->PicW          = picw;
    Head0->PicH          = pich;
    return sizeof(ColPicHead3) + Head0->ListDataSize + Head0->ColorDataSize;
}
int ColPic_EncodeStr(U16 *fromcolor16, int picw, int pich, U8 *outputdata, int outputmaxtsize, int colorsmax)
{
    int qty      = 0;
    int temp     = 0;
    int strindex = 0;
    int hexindex = 0;
    U8  TempBytes[4];
    qty = ColPicEncode(fromcolor16, picw, pich, outputdata, outputmaxtsize, colorsmax);
    if (qty == 0)
        return 0;
    temp = 3 - (qty % 3);
    while (temp > 0) {
        outputdata[qty] = 0;
        qty++;
        temp--;
    }
    if ((qty * 4 / 3) >= outputmaxtsize)
        return 0;
    hexindex = qty;
    strindex = (qty * 4 / 3);
    while (hexindex > 0) {
        hexindex -= 3;
        strindex -= 4;

        TempBytes[0] = (U8) (outputdata[hexindex] >> 2);
        TempBytes[1] = (U8) (outputdata[hexindex] & 3);
        TempBytes[1] <<= 4;
        TempBytes[1] += ((U8) (outputdata[hexindex + 1] >> 4));
        TempBytes[2] = (U8) (outputdata[hexindex + 1] & 15);
        TempBytes[2] <<= 2;
        TempBytes[2] += ((U8) (outputdata[hexindex + 2] >> 6));
        TempBytes[3] = (U8) (outputdata[hexindex + 2] & 63);

        TempBytes[0] += 48;
        if (TempBytes[0] == (U8) '\\')
            TempBytes[0] = 126;
        TempBytes[0 + 1] += 48;
        if (TempBytes[0 + 1] == (U8) '\\')
            TempBytes[0 + 1] = 126;
        TempBytes[0 + 2] += 48;
        if (TempBytes[0 + 2] == (U8) '\\')
            TempBytes[0 + 2] = 126;
        TempBytes[0 + 3] += 48;
        if (TempBytes[0 + 3] == (U8) '\\')
            TempBytes[0 + 3] = 126;

        outputdata[strindex]     = TempBytes[0];
        outputdata[strindex + 1] = TempBytes[1];
        outputdata[strindex + 2] = TempBytes[2];
        outputdata[strindex + 3] = TempBytes[3];
    }
    qty             = qty * 4 / 3;
    outputdata[qty] = 0;
    return qty;
}


} // namespace Slic3r::GCodeThumbnails
