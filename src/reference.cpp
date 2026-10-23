// SPDX-License-Identifier: Apache-2.0
#include "reference.h"
#include <csetjmp>
#include <cstdlib>
#include <fstream>
#include <jpeglib.h>
#include <png.h>
namespace ob {
namespace {
constexpr size_t maxPixels = 16 * 1024 * 1024, totalPixels = 32 * 1024 * 1024;
bool dimensions(size_t w, size_t h) {
  return w && h && w <= 8192 && h <= 8192 && w * h <= maxPixels;
}
struct JpegReader {
  jpeg_decompress_struct decoder{};
  jpeg_error_mgr error{};
  std::jmp_buf jump;
  unsigned char *raw = nullptr;
  ~JpegReader() {
    jpeg_destroy_decompress(&decoder);
    std::free(raw);
  }
};
void decodeJpeg(const Bytes &bytes, RasterImage &image) {
  // Heap-owned state remains defined across libjpeg's error longjmp.
  auto reader = std::make_unique<JpegReader>();
  auto &d = reader->decoder;
  d.err = jpeg_std_error(&reader->error);
  reader->error.error_exit = [](j_common_ptr c) {
    auto r = reinterpret_cast<JpegReader *>(c->client_data);
    std::longjmp(r->jump, 1);
  };
  d.client_data = reader.get();
  if (setjmp(reader->jump))
    throw std::runtime_error("Cannot decode JPEG reference image");
  jpeg_create_decompress(&d);
  jpeg_mem_src(&d, bytes.data(), bytes.size());
  jpeg_read_header(&d, TRUE);
  if (!dimensions(d.image_width, d.image_height))
    throw std::runtime_error(
        "Reference image exceeds 8192 pixels per side or 16 megapixels");
  d.out_color_space = JCS_RGB;
  jpeg_start_decompress(&d);
  size_t size = size_t(d.output_width) * d.output_height * 3;
  reader->raw = static_cast<unsigned char *>(std::malloc(size));
  if (!reader->raw)
    throw std::bad_alloc();
  while (d.output_scanline < d.output_height) {
    JSAMPROW row = reader->raw + size_t(d.output_scanline) * d.output_width * 3;
    jpeg_read_scanlines(&d, &row, 1);
  }
  image.width = d.output_width;
  image.height = d.output_height;
  jpeg_finish_decompress(&d);
  image.rgba.resize(size / 3 * 4);
  for (size_t i = 0; i < size / 3; ++i) {
    for (int c = 0; c < 3; ++c)
      image.rgba[i * 4 + c] = reader->raw[i * 3 + c];
    image.rgba[i * 4 + 3] = 255;
  }
}
} // namespace
std::shared_ptr<const RasterImage> decodeReference(const Bytes &encoded) {
  if (encoded.size() > 80 * 1024 * 1024 || encoded.size() < 8)
    throw std::runtime_error("Invalid reference image file size");
  auto pixels = std::make_shared<RasterImage>();
  if (png_sig_cmp(encoded.data(), 0, 8) == 0) {
    png_image png{};
    png.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&png, encoded.data(),
                                          encoded.size())) {
      png_image_free(&png);
      throw std::runtime_error("Cannot read PNG reference image");
    }
    if (!dimensions(png.width, png.height)) {
      png_image_free(&png);
      throw std::runtime_error(
          "Reference image exceeds 8192 pixels per side or 16 megapixels");
    }
    pixels->width = png.width;
    pixels->height = png.height;
    png.format = PNG_FORMAT_RGBA;
    try {
      pixels->rgba.resize(PNG_IMAGE_SIZE(png));
      if (!png_image_finish_read(&png, nullptr, pixels->rgba.data(), 0,
                                 nullptr))
        throw std::runtime_error("Cannot decode PNG reference image");
    } catch (...) {
      png_image_free(&png);
      throw;
    }
    png_image_free(&png);
  } else if (encoded[0] == 255 && encoded[1] == 216)
    decodeJpeg(encoded, *pixels);
  else
    throw std::runtime_error("Reference images must be PNG or JPEG");
  png_image png{};
  png.version = PNG_IMAGE_VERSION;
  png.width = pixels->width;
  png.height = pixels->height;
  png.format = PNG_FORMAT_RGBA;
  png_alloc_size_t size = 0;
  if (!png_image_write_to_memory(&png, nullptr, &size, 0, pixels->rgba.data(),
                                 0, nullptr))
    throw std::runtime_error("Cannot encode reference image");
  pixels->png.resize(size);
  if (!png_image_write_to_memory(&png, pixels->png.data(), &size, 0,
                                 pixels->rgba.data(), 0, nullptr))
    throw std::runtime_error("Cannot encode reference image");
  pixels->png.resize(size);
  return pixels;
}
ReferenceImage readReference(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  auto size = input.tellg();
  if (!input || size < 0 || size > 64 * 1024 * 1024)
    throw std::runtime_error("Cannot read reference image (maximum 64 MiB)");
  Bytes bytes(size);
  input.seekg(0);
  if (!input.read(reinterpret_cast<char *>(bytes.data()), bytes.size()))
    throw std::runtime_error("Cannot read reference image");
  ReferenceImage result;
  result.name = path.filename().string();
  result.pixels = decodeReference(bytes);
  return result;
}
void validate(const ReferenceImage &image) {
  if (!image.pixels || !dimensions(image.pixels->width, image.pixels->height) ||
      image.pixels->rgba.size() !=
          size_t(image.pixels->width) * image.pixels->height * 4 ||
      image.pixels->png.empty() || !finite(image.pose) ||
      image.pose.scale < .001f || image.pose.scale > 1000 ||
      !std::isfinite(image.opacity) || image.opacity < 0 || image.opacity > 1 ||
      image.name.size() > 4096)
    throw std::runtime_error("Invalid reference image");
}
void loadReferences(Document &document) {
  auto item = document.metadata.find("DeXrReferenceImages");
  if (item == document.metadata.end())
    return;
  if (!item->is_object() || item->value("version", 0) != 1) {
    document.warnings.push_back(
        "Unsupported de-xr reference image metadata preserved");
    return;
  }
  const auto &images = item->at("images");
  if (!images.is_array() || images.size() > 128)
    throw std::runtime_error("Invalid reference image list");
  size_t pixels = 0;
  for (const auto &value : images) {
    auto file = value.at("file").get<std::string>();
    auto found = document.attachments.find(file);
    if (found == document.attachments.end())
      throw std::runtime_error("Embedded reference image is missing");
    ReferenceImage image;
    image.name = value.at("name");
    image.layer = value.at("layer");
    image.opacity = value.at("opacity");
    const auto &p = value.at("position"), &q = value.at("orientation");
    if (p.size() != 3 || q.size() != 4)
      throw std::runtime_error("Invalid reference image placement");
    image.pose = {
        {p[0], p[1], p[2]}, {q[0], q[1], q[2], q[3]}, value.at("width")};
    image.pixels = decodeReference(found->second);
    validate(image);
    pixels += size_t(image.pixels->width) * image.pixels->height;
    if (pixels > totalPixels)
      throw std::runtime_error("Reference images exceed 32 megapixels total");
    document.references.push_back(std::move(image));
  }
  // Native images own shared, immutable assets; avoid copying their encoded
  // bytes again with every document snapshot. They are restored on save.
  for (const auto &value : images) {
    auto file = value.at("file").get<std::string>();
    if (file.rfind("de-xr/reference-", 0) == 0)
      document.attachments.erase(file);
  }
}
void storeReferences(const Document &document, Json &metadata,
                     std::map<std::string, Bytes> &files) {
  auto old = metadata.find("DeXrReferenceImages");
  if (document.references.empty() &&
      (old == metadata.end() || !old->is_object() ||
       old->value("version", 0) != 1))
    return;
  Json images = Json::array();
  size_t index = 0;
  for (const auto &image : document.references) {
    validate(image);
    std::string file;
    do {
      file = "de-xr/reference-" + std::to_string(index++) + ".png";
    } while (files.count(file));
    files[file] = image.pixels->png;
    auto p = image.pose.position;
    auto q = image.pose.orientation;
    images.push_back({{"file", file},
                      {"name", image.name},
                      {"layer", image.layer},
                      {"position", {p.x, p.y, p.z}},
                      {"orientation", {q.x, q.y, q.z, q.w}},
                      {"width", image.pose.scale},
                      {"opacity", image.opacity}});
  }
  metadata["DeXrReferenceImages"] = {{"version", 1},
                                     {"images", std::move(images)}};
}
} // namespace ob
