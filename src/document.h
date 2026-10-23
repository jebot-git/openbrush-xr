// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "brush-math.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
namespace ob {
using Json = nlohmann::json;
using Bytes = std::vector<uint8_t>;
struct Color {
  float r = 1, g = 1, b = 1, a = 1;
};
struct Point {
  Vec3 position;
  Quat orientation;
  float pressure = 1;
  uint32_t timeMs = 0;
  std::map<unsigned, uint32_t> extensions;
  std::optional<Vec3> savedPosition;
};
struct Stroke {
  std::string brush = "2d35bcf0-e4d8-452c-97b1-3311be063130";
  Color color;
  float size = .015f, scale = 1, savedSize = 0;
  uint32_t flags = 0, group = 0, seed = 0, layer = 0;
  uint32_t savedStrokeMask = 31, savedPointMask = 3,
           savedBrushIndex = UINT32_MAX;
  std::vector<Point> points;
  // Preserve unknown extensions when existing artworks are edited and saved.
  std::map<unsigned, Bytes> extensions;
};
struct RasterImage {
  int width = 0, height = 0;
  Bytes rgba, png;
};
struct ReferenceImage {
  std::string name;
  uint32_t layer = 0;
  Transform pose{{0, 1.3f, -1.2f}, {}, .6f}; // Scale is image width in meters.
  float opacity = .5f;
  std::shared_ptr<const RasterImage> pixels;
};
struct Document {
  std::vector<Stroke> strokes;
  std::vector<ReferenceImage> references;
  Bytes sketchHeader, containerHeader;
  uint32_t sketchVersion = 0;
  Json metadata = Json::object();
  std::map<std::string, Bytes> attachments;
  std::vector<std::string> warnings;
};
void validate(const Stroke &stroke);
class History {
  struct Edit {
    size_t index;
    Stroke stroke;
    bool inserted;
    std::optional<ReferenceImage> reference;
  };
  Document &document;
  using Command = std::vector<Edit>;
  std::vector<Command> past, future;
  void execute(Command command);
  void apply(const Command &command, bool forward);
  size_t saved = 0;
  bool savedReachable = true;
  void apply(const Edit &edit, bool forward);

public:
  explicit History(Document &value) : document(value) {}
  void append(Stroke stroke);
  void appendReference(ReferenceImage image);
  void eraseReference(size_t index);
  void replaceReference(size_t index, ReferenceImage image);
  void erase(size_t index);
  void appendMany(std::vector<Stroke> strokes);
  void replace(const std::vector<std::pair<size_t, Stroke>> &strokes);
  void eraseMany(std::vector<size_t> indices);
  bool canUndo() const { return !past.empty(); }
  bool canRedo() const { return !future.empty(); }
  bool eraseNear(Vec3 position, float radius);
  bool undo();
  bool redo();
  bool dirty() const { return !savedReachable || past.size() != saved; }
  void markDirty() { savedReachable = false; }
  void markSaved() {
    saved = past.size();
    savedReachable = true;
  }
};
Document readTilt(const std::filesystem::path &path);
void writeTilt(const Document &document, const std::filesystem::path &path);
Bytes encodeSketch(const Document &document, Json &metadata);
Document decodeSketch(const Bytes &data, const Json &metadata);
void atomicWrite(const std::filesystem::path &path, const Bytes &bytes);
} // namespace ob
