// SPDX-License-Identifier: Apache-2.0
#include "scene.h"
namespace ob {
namespace {
Transform readTransform(const Json &value, bool layer) {
  if (value.is_null())
    return {};
  if (!value.is_array() || value.size() != 3 || !value[0].is_array() ||
      value[0].size() != 3 || !value[1].is_array() || value[1].size() != 4)
    throw std::runtime_error("Invalid Open Brush scene/layer transform");
  const auto &p = value[0], &q = value[1];
  float scale = value[2].get<float>();
  Transform result{{p[0].get<float>() * .1f, p[1].get<float>() * .1f,
                    -p[2].get<float>() * .1f},
                   {-q[0].get<float>(), -q[1].get<float>(), q[2].get<float>(),
                    q[3].get<float>()},
                   scale};
  if (!finite(result.position) || !finite(result.orientation) ||
      !std::isfinite(scale))
    throw std::runtime_error("Non-finite Open Brush transform");
  // Same legacy-zero recovery and scale sanitization as SaveLoadScript and
  // CanvasScript.
  if (layer && scale == 0)
    scale = 1;
  result.scale = std::clamp(std::abs(scale), .0001f, 10000.f);
  result.orientation = normalized(result.orientation);
  return result;
}
const Json *layers(const Document &document) {
  auto found = document.metadata.find("Layers");
  if (found == document.metadata.end() || found->is_null())
    return nullptr;
  if (!found->is_array() || found->size() > 4096)
    throw std::runtime_error("Invalid Open Brush layer list");
  return &*found;
}
const Json *layer(const Document &d, uint32_t index) {
  auto list = layers(d);
  if (!list || index >= list->size())
    return nullptr;
  auto &value = (*list)[index];
  if (!value.is_object())
    throw std::runtime_error("Invalid Open Brush layer");
  return &value;
}
} // namespace
Transform strokeTransform(const Document &document, uint32_t index) {
  Transform scene, local;
  auto found = document.metadata.find("SceneTransformInRoomSpace");
  if (found != document.metadata.end())
    scene = readTransform(*found, false);
  if (auto value = layer(document, index)) {
    auto pose = value->find("Transform");
    if (pose != value->end())
      local = readTransform(*pose, true);
  }
  return compose(scene, local);
}
Stroke transformedStroke(Stroke stroke, Transform pose) {
  for (auto &p : stroke.points) {
    p.position = transform(pose, p.position);
    p.orientation = multiply(pose.orientation, p.orientation);
  }
  stroke.size *= pose.scale;
  return stroke;
}
bool sourceLayerVisible(const Document &document, uint32_t index) {
  auto value = layer(document, index);
  return !value || value->value("Visible", false);
}
std::string sourceLayerName(const Document &document, uint32_t index) {
  auto value = layer(document, index);
  if (!value)
    return {};
  auto name = value->find("Name");
  return name == value->end() || name->is_null() ? std::string()
                                                 : name->get<std::string>();
}
size_t sourceLayerCount(const Document &document) {
  auto list = layers(document);
  return list ? list->size() : 0;
}
void validateScene(const Document &document) {
  strokeTransform(document, 0);
  for (size_t i = 0; i < sourceLayerCount(document); ++i) {
    strokeTransform(document, i);
    sourceLayerVisible(document, i);
    sourceLayerName(document, i);
  }
}
void setSourceLayerVisible(Document &document, uint32_t index, bool visible) {
  if (index >= 4096)
    throw std::runtime_error("Layer ID exceeds metadata capacity");
  auto &list = document.metadata["Layers"];
  if (list.is_null())
    list = Json::array();
  if (!list.is_array())
    throw std::runtime_error("Invalid Open Brush layer list");
  while (list.size() <= index)
    list.push_back(
        {{"Name", "Layer " + std::to_string(list.size())},
         {"Visible", true},
         {"Transform", Json::array({Json::array({0, 0, 0}),
                                    Json::array({0, 0, 0, 1}), 1})}});
  list[index]["Visible"] = visible;
}
} // namespace ob
