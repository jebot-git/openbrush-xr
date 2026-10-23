// SPDX-License-Identifier: Apache-2.0
// Format derived from Open Brush SketchWriter.cs and TiltFile.cs; see
// provenance.
#include "document.h"
#include "reference.h"
#include <archive.h>
#include <archive_entry.h>
#include <cstring>
#include <fstream>
#include <set>
namespace ob {
namespace {
constexpr size_t limit = 256 * 1024 * 1024, pointLimit = 4000000;
struct Reader {
  const Bytes &bytes;
  size_t offset = 0;
  void need(size_t count) {
    if (count > bytes.size() - offset)
      throw std::runtime_error("Truncated sketch");
  }
  uint32_t u32() {
    need(4);
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
      v |= uint32_t(bytes[offset++]) << (8 * i);
    return v;
  }
  float real() {
    uint32_t bits = u32();
    float v;
    memcpy(&v, &bits, 4);
    return v;
  }
  Bytes take(size_t n) {
    need(n);
    Bytes result(bytes.begin() + offset, bytes.begin() + offset + n);
    offset += n;
    return result;
  }
};
struct Writer {
  Bytes data;
  void u32(uint32_t v) {
    for (int i = 0; i < 4; ++i)
      data.push_back((v >> (8 * i)) & 255);
  }
  void real(float v) {
    uint32_t bits;
    memcpy(&bits, &v, 4);
    u32(bits);
  }
  void append(const Bytes &b) {
    if (b.size() > limit - data.size())
      throw std::runtime_error("Sketch exceeds supported size");
    data.insert(data.end(), b.begin(), b.end());
  }
};
Bytes readFile(const std::filesystem::path &p) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  if (!f)
    throw std::runtime_error("Cannot open sketch");
  auto n = f.tellg();
  if (n < 0 || uint64_t(n) > limit)
    throw std::runtime_error("Sketch exceeds 256 MiB file limit");
  Bytes data(static_cast<size_t>(n));
  f.seekg(0);
  if (!data.empty() &&
      !f.read(reinterpret_cast<char *>(data.data()), data.size()))
    throw std::runtime_error("Cannot read sketch");
  return data;
}
std::runtime_error archiveError(archive *a) {
  return std::runtime_error(archive_error_string(a)
                                ? archive_error_string(a)
                                : "Archive operation failed");
}
} // namespace
Document decodeSketch(const Bytes &bytes, const Json &metadata) {
  Reader r{bytes};
  if (r.u32() != 0xc576a5cd)
    throw std::runtime_error("Unsupported sketch sentinel");
  auto version = r.u32();
  if (version < 5 || version > 6)
    throw std::runtime_error(
        "Only Open Brush sketch versions 5 and 6 are supported");
  if (r.u32() != 0)
    throw std::runtime_error("Unsupported reserved sketch header");
  Document d;
  d.metadata = metadata;
  d.sketchHeader = r.take(r.u32());
  auto brushList = metadata.at("BrushIndex").get<std::vector<std::string>>();
  uint32_t count = r.u32();
  if (count > 1000000 || count > (bytes.size() - r.offset) / 36)
    throw std::runtime_error("Invalid stroke count");
  d.strokes.reserve(count);
  size_t total = 0;
  for (uint32_t i = 0; i < count; ++i) {
    Stroke s;
    auto index = r.u32();
    if (index >= brushList.size())
      throw std::runtime_error("Invalid brush index");
    s.brush = brushList[index];
    s.color = {r.real(), r.real(), r.real(), r.real()};
    s.size = r.real() * .1f;
    uint32_t sm = r.u32(), pm = r.u32();
    for (unsigned bit = 0; bit < 32; ++bit)
      if (sm & (1u << bit)) {
        if (bit == 0)
          s.flags = r.u32();
        else if (bit == 1)
          s.scale = r.real();
        else if (bit == 2)
          s.group = r.u32();
        else if (bit == 3)
          s.seed = r.u32();
        else if (bit == 4)
          s.layer = r.u32();
        else
          s.extensions[bit] = r.take(bit < 16 ? 4 : r.u32());
      }
    auto points = r.u32();
    unsigned fields = 0;
    for (uint32_t m = pm; m; m &= m - 1)
      ++fields;
    if (points > pointLimit - total ||
        points > (bytes.size() - r.offset) / (28 + fields * 4))
      throw std::runtime_error("Invalid control point count");
    total += points;
    s.points.reserve(points);
    for (uint32_t j = 0; j < points; ++j) {
      Point p;
      p.position = {r.real() * .1f, r.real() * .1f, -r.real() * .1f};
      p.orientation = {-r.real(), -r.real(), r.real(), r.real()};
      for (unsigned bit = 0; bit < 32; ++bit)
        if (pm & (1u << bit)) {
          if (bit == 0)
            p.pressure = r.real();
          else if (bit == 1)
            p.timeMs = r.u32();
          else
            p.extensions[bit] = r.u32();
        }
      s.points.push_back(std::move(p));
    }
    validate(s);
    d.strokes.push_back(std::move(s));
  }
  if (r.offset != bytes.size())
    throw std::runtime_error("Unexpected trailing sketch data");
  if (metadata.contains("ModelIndex") || metadata.contains("ImageIndex") ||
      metadata.contains("Videos"))
    d.warnings.push_back("Referenced media retained in metadata but not yet "
                         "rendered by the native frontend");
  return d;
}
Bytes encodeSketch(const Document &d, Json &metadata) {
  Writer w;
  w.u32(0xc576a5cd);
  bool extended = !d.sketchHeader.empty();
  for (auto &s : d.strokes)
    for (auto &item : s.extensions)
      if (item.first >= 16)
        extended = true;
  w.u32(extended ? 6 : 5);
  w.u32(0);
  w.u32(d.sketchHeader.size());
  w.append(d.sketchHeader);
  if (d.strokes.size() > 1000000)
    throw std::runtime_error("Too many strokes");
  w.u32(d.strokes.size());
  std::vector<std::string> brushes;
  size_t total = 0;
  for (const auto &s : d.strokes) {
    validate(s);
    if (s.points.size() > pointLimit - total)
      throw std::runtime_error("Too many control points");
    total += s.points.size();
    auto it = std::find(brushes.begin(), brushes.end(), s.brush);
    uint32_t index = it - brushes.begin();
    if (it == brushes.end())
      brushes.push_back(s.brush);
    w.u32(index);
    for (float v : {s.color.r, s.color.g, s.color.b, s.color.a})
      w.real(v);
    w.real(s.size * 10);
    uint32_t sm = 31, pm = 3;
    for (const auto &e : s.extensions) {
      if (e.first < 5 || e.first > 31 || (e.first < 16 && e.second.size() != 4))
        throw std::runtime_error("Invalid stroke extension");
      sm |= 1u << e.first;
    }
    for (const auto &p : s.points)
      for (const auto &e : p.extensions) {
        if (e.first < 2 || e.first > 31)
          throw std::runtime_error("Invalid point extension");
        pm |= 1u << e.first;
      }
    w.u32(sm);
    w.u32(pm);
    w.u32(s.flags);
    w.real(s.scale);
    w.u32(s.group);
    w.u32(s.seed);
    w.u32(s.layer);
    for (const auto &e : s.extensions) {
      if (e.first >= 16)
        w.u32(e.second.size());
      w.append(e.second);
    }
    w.u32(s.points.size());
    for (const auto &p : s.points) {
      for (float v : {p.position.x * 10, p.position.y * 10, -p.position.z * 10,
                      -p.orientation.x, -p.orientation.y, p.orientation.z,
                      p.orientation.w})
        w.real(v);
      w.real(p.pressure);
      w.u32(p.timeMs);
      for (unsigned bit = 2; bit < 32; ++bit)
        if (pm & (1u << bit)) {
          auto it = p.extensions.find(bit);
          w.u32(it == p.extensions.end() ? 0 : it->second);
        }
    }
    if (w.data.size() > limit)
      throw std::runtime_error("Sketch exceeds supported size");
  }
  metadata = d.metadata;
  metadata["BrushIndex"] = brushes;
  if (!metadata.contains("SchemaVersion"))
    metadata["SchemaVersion"] = 3;
  return w.data;
}
Document readTilt(const std::filesystem::path &path) {
  std::map<std::string, Bytes> files;
  if (std::filesystem::is_directory(path)) {
    for (auto name :
         {"metadata.json", "data.sketch", "thumbnail.png", "hires.png"})
      if (std::filesystem::exists(path / name))
        files[name] = readFile(path / name);
  } else {
    auto bytes = readFile(path);
    if (bytes.size() < 20)
      throw std::runtime_error("Truncated .tilt container");
    Reader header{bytes};
    if (header.u32() != 0x546c6974)
      throw std::runtime_error("Not an Open Brush .tilt container");
    auto packed = header.u32();
    unsigned length = packed & 0xffff, version = packed >> 16;
    if (version != 1 || length < 16 || length > bytes.size() - 4)
      throw std::runtime_error("Unsupported .tilt header");
    if (memcmp(bytes.data() + length, "PK\x03\x04", 4))
      throw std::runtime_error("Missing ZIP container");
    auto a = archive_read_new();
    archive_read_support_format_zip(a);
    size_t total = 0;
    try {
      if (archive_read_open_memory(a, bytes.data() + length,
                                   bytes.size() - length) != ARCHIVE_OK)
        throw archiveError(a);
      archive_entry *entry;
      int status;
      while ((status = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        std::string name = archive_entry_pathname(entry);
        std::filesystem::path relative(name);
        if (name.empty() || relative.is_absolute() ||
            archive_entry_filetype(entry) != AE_IFREG)
          throw std::runtime_error("Unsupported .tilt archive entry");
        for (const auto &part : relative)
          if (part == "..")
            throw std::runtime_error("Invalid .tilt archive path");
        if (files.count(name))
          throw std::runtime_error("Duplicate .tilt archive entry");
        auto size = archive_entry_size(entry);
        if (size < 0 || uint64_t(size) > limit - total)
          throw std::runtime_error("Expanded sketch exceeds 256 MiB");
        Bytes data(static_cast<size_t>(size));
        size_t offset = 0;
        while (offset < data.size()) {
          auto n =
              archive_read_data(a, data.data() + offset, data.size() - offset);
          if (n <= 0)
            throw archiveError(a);
          offset += size_t(n);
        }
        total += data.size();
        files.emplace(name, std::move(data));
      }
      if (status != ARCHIVE_EOF)
        throw archiveError(a);
      archive_read_free(a);
    } catch (...) {
      archive_read_free(a);
      throw;
    }
  }
  if (!files.count("data.sketch"))
    throw std::runtime_error("Missing data.sketch");
  auto metadata = files.find("metadata.json");
  if (metadata == files.end())
    metadata = files.find("main.json");
  if (metadata == files.end())
    throw std::runtime_error("Missing metadata.json");
  auto d = decodeSketch(files.at("data.sketch"), Json::parse(metadata->second));
  files.erase("data.sketch");
  files.erase("metadata.json");
  files.erase("main.json");
  d.attachments = std::move(files);
  loadReferences(d);
  return d;
}
void writeTilt(const Document &d, const std::filesystem::path &path) {
  Json metadata;
  auto sketch = encodeSketch(d, metadata);
  auto files = d.attachments;
  storeReferences(d, metadata, files);
  files["data.sketch"] = std::move(sketch);
  auto json = metadata.dump(2);
  files["metadata.json"] = Bytes(json.begin(), json.end());
  Writer w;
  w.u32(0x546c6974);
  w.u32(0x00010010);
  w.u32(0);
  w.u32(0);
  auto a = archive_write_new();
  archive_write_set_format_zip(a);
  archive_write_set_options(a, "zip:compression=store");
  Bytes zip;
  try {
    auto write = [](archive *, void *client, const void *data,
                    size_t count) -> la_ssize_t {
      auto &b = *static_cast<Bytes *>(client);
      if (count > limit - b.size())
        return -1;
      auto p = static_cast<const uint8_t *>(data);
      b.insert(b.end(), p, p + count);
      return count;
    };
    if (archive_write_open(a, &zip, nullptr, write, nullptr) != ARCHIVE_OK)
      throw archiveError(a);
    for (const auto &file : files) {
      std::filesystem::path name(file.first);
      if (name.is_absolute() || file.first.empty())
        throw std::runtime_error("Invalid attachment path");
      for (auto &part : name)
        if (part == "..")
          throw std::runtime_error("Invalid attachment path");
      auto entry = archive_entry_new();
      archive_entry_set_pathname(entry, file.first.c_str());
      archive_entry_set_filetype(entry, AE_IFREG);
      archive_entry_set_perm(entry, 0644);
      archive_entry_set_size(entry, file.second.size());
      auto status = archive_write_header(a, entry);
      archive_entry_free(entry);
      if (status != ARCHIVE_OK)
        throw archiveError(a);
      if (!file.second.empty() &&
          archive_write_data(a, file.second.data(), file.second.size()) !=
              static_cast<la_ssize_t>(file.second.size()))
        throw archiveError(a);
    }
    if (archive_write_close(a) != ARCHIVE_OK)
      throw archiveError(a);
    archive_write_free(a);
  } catch (...) {
    archive_write_free(a);
    throw;
  }
  w.append(zip);
  atomicWrite(path, w.data);
}
} // namespace ob
