// SPDX-License-Identifier: Apache-2.0
#include "editor.h"
#include "reference.h"
#include <limits>
namespace ob {
namespace {
Stroke mirrorStroke(Stroke s) {
  for (auto &p : s.points) {
    p.position.x = -p.position.x;
    p.orientation = {p.orientation.x, -p.orientation.y, -p.orientation.z,
                     p.orientation.w};
  }
  return s;
}
} // namespace
Editor::Editor() : history(std::make_unique<History>(document)) {}
void Editor::changed() {
  ++revision;
  cached.reset();
}
EditorFrame Editor::frame() {
  std::lock_guard lock(mutex);
  if (!cached) {
    auto d = std::make_shared<Document>(document);
    d->strokes.erase(std::remove_if(d->strokes.begin(), d->strokes.end(),
                                    [&](const Stroke &s) {
                                      return hiddenLayers.count(s.layer);
                                    }),
                     d->strokes.end());
    d->references.erase(std::remove_if(d->references.begin(),
                                       d->references.end(),
                                       [&](const auto &image) {
                                         return hiddenLayers.count(image.layer);
                                       }),
                        d->references.end());
    cached = d;
  }
  EditorFrame result{cached,
                     {},
                     settings,
                     revision,
                     history->dirty() || owner != 0,
                     history->canUndo(),
                     history->canRedo(),
                     selected,
                     navigation,
                     toolRevision,
                     !hiddenLayers.count(settings.layer)};
  if (owner && !active.points.empty()) {
    result.active.push_back(active);
    if (settings.mirror)
      result.active.push_back(mirrorStroke(active));
  }
  if (!selected.empty()) {
    Vec3 low{1e30f, 1e30f, 1e30f}, high{-1e30f, -1e30f, -1e30f};
    bool any = false;
    for (auto i : selected)
      if (!hiddenLayers.count(document.strokes[i].layer))
        for (auto &p : document.strokes[i].points) {
          any = true;
          low = {std::min(low.x, p.position.x), std::min(low.y, p.position.y),
                 std::min(low.z, p.position.z)};
          high = {std::max(high.x, p.position.x),
                  std::max(high.y, p.position.y),
                  std::max(high.z, p.position.z)};
        }
    if (any) {
      low = low - Vec3{.008f, .008f, .008f};
      high = high + Vec3{.008f, .008f, .008f};
      Vec3 corners[8];
      for (int i = 0; i < 8; ++i)
        corners[i] = {i & 1 ? high.x : low.x, i & 2 ? high.y : low.y,
                      i & 4 ? high.z : low.z};
      for (int i = 0; i < 8; ++i)
        for (int bit = 1; bit <= 4; bit *= 2)
          if (!(i & bit)) {
            Stroke box;
            box.brush = "2241cd32-8ba2-48a5-9ee7-2caef7e9ed62";
            box.color = {1, .8f, .1f, 1};
            box.size = .001f;
            box.points = {{corners[i], {}, 1, 0}, {corners[i | bit], {}, 1, 0}};
            result.active.push_back(std::move(box));
          }
    }
  }
  return result;
}
Document Editor::copy(bool includeActive) const {
  std::lock_guard lock(mutex);
  auto result = document;
  if (includeActive && owner && !active.points.empty()) {
    result.strokes.push_back(active);
    if (settings.mirror)
      result.strokes.push_back(mirrorStroke(active));
  }
  return result;
}
EditorSnapshot Editor::snapshot(bool includeActive) const {
  std::lock_guard lock(mutex);
  return {copy(includeActive), revision};
}
void Editor::load(Document value, bool modified) {
  std::lock_guard lock(mutex);
  for (const auto &s : value.strokes)
    validate(s);
  for (const auto &image : value.references)
    validate(image);
  document = std::move(value);
  history = std::make_unique<History>(document);
  if (modified)
    history->markDirty();
  owner = 0;
  active = Stroke{};
  selected.clear();
  hiddenLayers.clear();
  navigation = {};
  changed();
}
void Editor::addReference(ReferenceImage image) {
  validate(image);
  std::lock_guard lock(mutex);
  if (document.references.size() >= 128)
    throw std::runtime_error("Too many reference images");
  size_t pixels = size_t(image.pixels->width) * image.pixels->height;
  for (const auto &r : document.references)
    pixels += size_t(r.pixels->width) * r.pixels->height;
  if (pixels > 32 * 1024 * 1024)
    throw std::runtime_error("Reference images exceed 32 megapixels total");
  finishLocked();
  history->appendReference(std::move(image));
  changed();
}
void Editor::changeReference(size_t index, ReferenceImage image) {
  std::lock_guard lock(mutex);
  finishLocked();
  // Placement edits retain the immutable asset.
  image.pixels = document.references.at(index).pixels;
  history->replaceReference(index, std::move(image));
  changed();
}
void Editor::removeReference(size_t index) {
  std::lock_guard lock(mutex);
  finishLocked();
  history->eraseReference(index);
  changed();
}
void Editor::saved(uint64_t version) {
  std::lock_guard lock(mutex);
  if (version == revision)
    history->markSaved();
}
void Editor::configure(ToolSettings value) {
  if (int(value.tool) < 0 || int(value.tool) > int(Tool::Navigate))
    throw std::runtime_error("Invalid tool");
  Stroke probe;
  probe.size = value.size;
  probe.brush = value.brush;
  probe.color = value.color;
  validate(probe);
  if (!std::isfinite(value.eraser) || value.eraser <= 0 || value.eraser > 10)
    throw std::runtime_error("Invalid eraser size");
  std::lock_guard lock(mutex);
  finishLocked();
  settings = std::move(value);
  ++toolRevision;
}
ToolSettings Editor::tools() const {
  std::lock_guard lock(mutex);
  return settings;
}
void Editor::finishLocked() {
  if (owner && !active.points.empty()) {
    std::vector<Stroke> strokes{std::move(active)};
    if (settings.mirror)
      strokes.push_back(mirrorStroke(strokes.front()));
    history->appendMany(std::move(strokes));
    selected.clear();
    changed();
  }
  owner = 0;
  active = Stroke{};
}
void Editor::finish(int source) {
  std::lock_guard lock(mutex);
  if (source == 0 || source == owner)
    finishLocked();
}
bool Editor::sample(int source, Point p) {
  std::lock_guard lock(mutex);
  return sampleInternal(source, p, 1);
}
bool Editor::sampleRoom(int source, Point p) {
  std::lock_guard lock(mutex);
  auto inv = inverse(navigation);
  p.position = transform(inv, p.position);
  p.orientation = multiply(inv.orientation, p.orientation);
  return sampleInternal(source, p, 1 / navigation.scale);
}
bool Editor::eraseRoom(Vec3 p, float radius) {
  std::lock_guard lock(mutex);
  return erase(transform(inverse(navigation), p), radius / navigation.scale);
}
bool Editor::selectRayRoom(Vec3 origin, Vec3 direction, float radius,
                           bool additive) {
  std::lock_guard lock(mutex);
  auto inv = inverse(navigation);
  return selectRay(transform(inv, origin), rotate(inv.orientation, direction),
                   radius / navigation.scale, additive);
}
void Editor::navigate(Transform value) {
  if (!finite(value) || value.scale < .05f || value.scale > 20)
    throw std::runtime_error("Invalid canvas navigation");
  std::lock_guard lock(mutex);
  finishLocked();
  navigation = value;
}
Transform Editor::canvas() const {
  std::lock_guard lock(mutex);
  return navigation;
}
bool Editor::sampleInternal(int source, Point p, float sizeScale) {
  std::lock_guard lock(mutex);
  if (owner && owner != source)
    return false;
  if (settings.tool == Tool::Navigate)
    return false;
  if (hiddenLayers.count(settings.layer))
    return false;
  if (settings.tool == Tool::Erase)
    return erase(p.position, settings.eraser * sizeScale);
  if (settings.tool == Tool::Select)
    return select(p.position, settings.eraser * sizeScale, true);
  if (settings.tool == Tool::Recolor) {
    if (!select(p.position, settings.eraser * sizeScale, false))
      return false;
    recolorSelection(settings.color);
    return true;
  }
  if (!owner) {
    active = Stroke{};
    active.brush = settings.brush;
    active.color = settings.color;
    active.size = settings.size * sizeScale;
    active.layer = settings.layer;
    owner = source;
  }
  Stroke probe;
  probe.brush = active.brush;
  probe.color = active.color;
  probe.size = active.size;
  probe.scale = active.scale;
  probe.points = {p};
  validate(probe);
  if (active.points.empty() ||
      length(active.points.back().position - p.position) >
          std::max(.0003f * sizeScale, settings.size * sizeScale * .025f))
    active.points.push_back(std::move(p));
  return true;
}
size_t Editor::hit(Vec3 point, float radius) const {
  for (size_t i = document.strokes.size(); i > 0; --i) {
    auto &s = document.strokes[i - 1];
    if (hiddenLayers.count(s.layer))
      continue;
    for (size_t j = 0; j < s.points.size(); ++j)
      if (segmentDistance(point, s.points[j].position,
                          s.points[j ? j - 1 : 0].position) <=
          radius + s.size * s.scale * .5f)
        return i - 1;
  }
  return std::numeric_limits<size_t>::max();
}
bool Editor::erase(Vec3 p, float radius) {
  std::lock_guard lock(mutex);
  finishLocked();
  auto i = hit(p, radius);
  if (i == std::numeric_limits<size_t>::max())
    return false;
  history->erase(i);
  selected.clear();
  changed();
  return true;
}
bool Editor::select(Vec3 p, float radius, bool additive) {
  std::lock_guard lock(mutex);
  auto i = hit(p, radius);
  if (!additive)
    selected.clear();
  if (i == std::numeric_limits<size_t>::max())
    return false;
  selected.insert(i);
  return true;
}
bool Editor::selectRay(Vec3 origin, Vec3 direction, float radius,
                       bool additive) {
  std::lock_guard lock(mutex);
  direction = normalized(direction, {0, 0, -1});
  size_t best = std::numeric_limits<size_t>::max();
  float nearest = 1e30f;
  for (size_t i = 0; i < document.strokes.size(); ++i) {
    const auto &s = document.strokes[i];
    if (hiddenLayers.count(s.layer))
      continue;
    for (size_t j = 0; j < s.points.size(); ++j) {
      auto a = s.points[j].position, b = s.points[j ? j - 1 : j].position,
           v = b - a, w = origin - a;
      float c = dot(v, v), bDot = dot(direction, v), d = dot(direction, w),
            e = dot(v, w), den = c - bDot * bDot;
      float u = std::clamp(den > 1e-10f ? (e - bDot * d) / den : 0.f, 0.f, 1.f),
            t = bDot * u - d;
      if (t < 0) {
        t = 0;
        u = c > 1e-10f ? std::clamp(e / c, 0.f, 1.f) : 0;
      }
      if (length(origin + direction * t - a - v * u) <=
              radius + s.size * s.scale * .5f &&
          t < nearest) {
        best = i;
        nearest = t;
      }
    }
  }
  if (!additive)
    selected.clear();
  if (best == std::numeric_limits<size_t>::max())
    return false;
  selected.insert(best);
  return true;
}
void Editor::selectAll() {
  std::lock_guard lock(mutex);
  finishLocked();
  selected.clear();
  for (size_t i = 0; i < document.strokes.size(); ++i)
    if (!hiddenLayers.count(document.strokes[i].layer))
      selected.insert(i);
}
void Editor::deleteSelection() {
  std::lock_guard lock(mutex);
  finishLocked();
  history->eraseMany({selected.begin(), selected.end()});
  selected.clear();
  changed();
}
void Editor::transformSelection(Vec3 translation, Quat rotation, float scale) {
  if (!finite(translation) || !finite(rotation) || !std::isfinite(scale) ||
      scale <= 0 || scale > 100)
    throw std::runtime_error("Invalid selection transform");
  std::lock_guard lock(mutex);
  finishLocked();
  if (selected.empty())
    return;
  Vec3 center{};
  size_t count = 0;
  for (auto i : selected)
    for (auto &p : document.strokes[i].points) {
      center = center + p.position;
      ++count;
    }
  if (!count)
    return;
  center = center * (1.f / count);
  auto mul = [](Quat a, Quat b) {
    return normalized(Quat{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                           a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                           a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                           a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z});
  };
  std::vector<std::pair<size_t, Stroke>> changes;
  for (auto i : selected) {
    auto s = document.strokes[i];
    for (auto &p : s.points) {
      p.position = center + translation +
                   rotate(rotation, (p.position - center) * scale);
      p.orientation = mul(rotation, p.orientation);
    }
    s.size *= scale;
    changes.push_back({i, std::move(s)});
  }
  history->replace(changes);
  changed();
}
void Editor::duplicateSelection(Vec3 offset) {
  std::lock_guard lock(mutex);
  finishLocked();
  std::vector<Stroke> copies;
  for (auto i : selected) {
    auto s = document.strokes[i];
    for (auto &p : s.points)
      p.position = p.position + offset;
    copies.push_back(std::move(s));
  }
  auto first = document.strokes.size();
  history->appendMany(std::move(copies));
  selected.clear();
  for (size_t i = first; i < document.strokes.size(); ++i)
    selected.insert(i);
  changed();
}
void Editor::recolorSelection(Color color) {
  std::lock_guard lock(mutex);
  finishLocked();
  std::vector<std::pair<size_t, Stroke>> changes;
  for (auto i : selected) {
    auto s = document.strokes[i];
    s.color = color;
    s.extensions.erase(16);
    changes.push_back({i, std::move(s)});
  }
  history->replace(changes);
  changed();
}
bool Editor::undo() {
  std::lock_guard lock(mutex);
  finishLocked();
  if (!history->undo())
    return false;
  selected.clear();
  changed();
  return true;
}
bool Editor::redo() {
  std::lock_guard lock(mutex);
  finishLocked();
  if (!history->redo())
    return false;
  selected.clear();
  changed();
  return true;
}
std::vector<uint32_t> Editor::layers() const {
  std::lock_guard lock(mutex);
  std::set<uint32_t> ids{settings.layer};
  for (auto &s : document.strokes)
    ids.insert(s.layer);
  for (const auto &image : document.references)
    ids.insert(image.layer);
  return {ids.begin(), ids.end()};
}
std::string Editor::layerReferenceName(uint32_t layer) const {
  std::lock_guard lock(mutex);
  for (const auto &image : document.references)
    if (image.layer == layer)
      return image.name.empty() ? "Reference" : image.name;
  return {};
}
void Editor::layerVisible(uint32_t layer, bool visible) {
  std::lock_guard lock(mutex);
  finishLocked();
  if (visible)
    hiddenLayers.erase(layer);
  else {
    hiddenLayers.insert(layer);
    for (auto it = selected.begin(); it != selected.end();)
      if (document.strokes[*it].layer == layer)
        it = selected.erase(it);
      else
        ++it;
  }
  changed();
}
bool Editor::layerVisible(uint32_t layer) const {
  std::lock_guard lock(mutex);
  return !hiddenLayers.count(layer);
}
} // namespace ob
