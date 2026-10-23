// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "brush.h"
#include "document.h"
#include <memory>
#include <mutex>
#include <set>
namespace ob {
enum class Tool { Paint, Erase, Select, Recolor, Navigate };
struct ToolSettings {
  std::string brush = brushes().front().guid;
  Color color{.15f, .65f, 1, 1};
  float size = .015f, eraser = .03f;
  uint32_t layer = 0;
  Tool tool = Tool::Paint;
  bool mirror = false;
};
struct EditorFrame {
  std::shared_ptr<const Document> document;
  std::vector<Stroke> active;
  ToolSettings settings;
  uint64_t revision = 0;
  bool dirty = false, undo = false, redo = false;
  std::set<size_t> selected;
  Transform navigation;
  uint64_t toolRevision = 1;
  bool currentLayerVisible = true;
};
struct EditorSnapshot {
  Document document;
  uint64_t revision;
};
class Editor {
  mutable std::recursive_mutex mutex;
  Document document;
  std::unique_ptr<History> history;
  ToolSettings settings;
  Stroke active;
  int owner = 0;
  std::set<size_t> selected;
  uint64_t revision = 1, toolRevision = 1;
  std::shared_ptr<const Document> cached;
  std::set<uint32_t> hiddenLayers;
  Transform navigation;
  bool sampleInternal(int source, Point point, float sizeScale);
  void changed();
  void finishLocked();
  size_t hit(Vec3 point, float radius) const;

public:
  Editor();
  EditorFrame frame();
  void load(Document document, bool modified = false);
  void addReference(ReferenceImage image);
  void changeReference(size_t index, ReferenceImage image);
  void removeReference(size_t index);
  Document copy(bool includeActive = false) const;
  EditorSnapshot snapshot(bool includeActive = false) const;
  void saved(uint64_t version);
  void configure(ToolSettings settings);
  ToolSettings tools() const;
  bool sample(int source, Point point);
  bool sampleRoom(int source, Point point);
  bool eraseRoom(Vec3 point, float radius);
  bool selectRayRoom(Vec3 origin, Vec3 direction, float radius,
                     bool additive = false);
  void navigate(Transform transform);
  Transform canvas() const;
  void finish(int source = 0);
  bool erase(Vec3 point, float radius);
  bool select(Vec3 point, float radius, bool additive = false);
  bool selectRay(Vec3 origin, Vec3 direction, float radius,
                 bool additive = false);
  void selectAll();
  void deleteSelection();
  void transformSelection(Vec3 translation, Quat rotation, float scale);
  void duplicateSelection(Vec3 offset = {.03f, 0, 0});
  void recolorSelection(Color color);
  bool undo();
  bool redo();
  std::vector<uint32_t> layers() const;
  std::string layerReferenceName(uint32_t layer) const;
  void layerVisible(uint32_t layer, bool visible);
  bool layerVisible(uint32_t layer) const;
};
} // namespace ob
