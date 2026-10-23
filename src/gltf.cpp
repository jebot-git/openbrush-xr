// SPDX-License-Identifier: Apache-2.0
#include "gltf.h"
#include "mesh.h"
#include <cstring>
#include <limits>
namespace ob {
namespace {
void word(Bytes &bytes, uint32_t value) {
  for (int i = 0; i < 4; ++i)
    bytes.push_back(uint8_t(value >> (i * 8)));
}
void real(Bytes &bytes, float value) {
  if (!std::isfinite(value))
    throw std::runtime_error("Non-finite glTF vertex");
  uint32_t bits;
  std::memcpy(&bits, &value, 4);
  word(bytes, bits);
}
std::string base64(const Bytes &bytes) {
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve((bytes.size() + 2) / 3 * 4);
  for (size_t i = 0; i < bytes.size(); i += 3) {
    uint32_t value = uint32_t(bytes[i]) << 16;
    if (i + 1 < bytes.size())
      value |= uint32_t(bytes[i + 1]) << 8;
    if (i + 2 < bytes.size())
      value |= bytes[i + 2];
    result += alphabet[value >> 18];
    result += alphabet[(value >> 12) & 63];
    result += i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=';
    result += i + 2 < bytes.size() ? alphabet[value & 63] : '=';
  }
  return result;
}
} // namespace
void writeGltf(const Document &document, const std::filesystem::path &path) {
  const auto extension = path.extension().string();
  if (extension != ".gltf" && extension != ".glb")
    throw std::runtime_error("Choose a .gltf or .glb export filename");
  Json root = {
      {"asset",
       {{"version", "2.0"}, {"generator", "OpenBrush Native for de-xr"}}},
      {"scene", 0},
      {"scenes", Json::array({Json::object()})}};
  Json nodes = Json::array(), meshes = Json::array(), views = Json::array(),
       accessors = Json::array(), materials = Json::array();
  Bytes binary;
  std::map<uint32_t, size_t> layers;
  std::map<std::pair<std::string, bool>, size_t> materialIds;
  bool unlit = false;
  auto accessor = [&](size_t view, size_t offset, int component, size_t count,
                      const char *type) {
    size_t index = accessors.size();
    accessors.push_back({{"bufferView", view},
                         {"byteOffset", offset},
                         {"componentType", component},
                         {"count", count},
                         {"type", type}});
    return index;
  };
  for (size_t strokeIndex = 0; strokeIndex < document.strokes.size();
       ++strokeIndex) {
    const auto &stroke = document.strokes[strokeIndex];
    auto mesh = brushMesh(stroke);
    if (mesh.indices.empty())
      continue;
    if (mesh.vertices.size() > std::numeric_limits<uint32_t>::max() ||
        mesh.indices.size() > std::numeric_limits<uint32_t>::max() ||
        binary.size() + uint64_t(mesh.vertices.size()) * 40 +
                uint64_t(mesh.indices.size()) * 4 >
            std::numeric_limits<uint32_t>::max() - 1024)
      throw std::runtime_error("Artwork exceeds glTF export size limit");
    auto start = binary.size();
    Vec3 low = mesh.vertices.front().position, high = low;
    bool transparent = false;
    for (const auto &v : mesh.vertices) {
      for (auto value :
           {v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y,
            v.normal.z, v.color.r, v.color.g, v.color.b, v.color.a})
        real(binary, value);
      low = {std::min(low.x, v.position.x), std::min(low.y, v.position.y),
             std::min(low.z, v.position.z)};
      high = {std::max(high.x, v.position.x), std::max(high.y, v.position.y),
              std::max(high.z, v.position.z)};
      transparent |= v.color.a < 1;
    }
    auto vertexView = views.size();
    views.push_back({{"buffer", 0},
                     {"byteOffset", start},
                     {"byteLength", binary.size() - start},
                     {"byteStride", 40},
                     {"target", 34962}});
    auto positions =
        accessor(vertexView, 0, 5126, mesh.vertices.size(), "VEC3");
    accessors[positions]["min"] = {low.x, low.y, low.z};
    accessors[positions]["max"] = {high.x, high.y, high.z};
    auto normals = accessor(vertexView, 12, 5126, mesh.vertices.size(), "VEC3");
    auto colors = accessor(vertexView, 24, 5126, mesh.vertices.size(), "VEC4");
    start = binary.size();
    for (auto index : mesh.indices) {
      if (index >= mesh.vertices.size())
        throw std::runtime_error("Invalid brush mesh index");
      word(binary, index);
    }
    auto indexView = views.size();
    views.push_back({{"buffer", 0},
                     {"byteOffset", start},
                     {"byteLength", binary.size() - start},
                     {"target", 34963}});
    auto indices = accessor(indexView, 0, 5125, mesh.indices.size(), "SCALAR");
    auto key = std::make_pair(stroke.brush, transparent);
    auto found = materialIds.find(key);
    size_t material;
    const auto &brush = brushFor(stroke.brush);
    if (found != materialIds.end())
      material = found->second;
    else {
      material = materials.size();
      materialIds[key] = material;
      Json value = {{"name", brush.name},
                    {"doubleSided", true},
                    {"pbrMetallicRoughness",
                     {{"baseColorFactor", {1, 1, 1, 1}},
                      {"metallicFactor", 0},
                      {"roughnessFactor", 1}}}};
      if (transparent)
        value["alphaMode"] = "BLEND";
      if (brush.emission >= 1) {
        value["extensions"]["KHR_materials_unlit"] = Json::object();
        unlit = true;
      }
      value["extras"] = {{"openbrushBrushGuid", stroke.brush},
                         {"nativeEmission", brush.emission}};
      materials.push_back(std::move(value));
    }
    size_t meshIndex = meshes.size();
    meshes.push_back({{"name", "Stroke " + std::to_string(strokeIndex + 1)},
                      {"primitives", Json::array({{{"attributes",
                                                    {{"POSITION", positions},
                                                     {"NORMAL", normals},
                                                     {"COLOR_0", colors}}},
                                                   {"indices", indices},
                                                   {"material", material},
                                                   {"mode", 4}}})}});
    size_t node = nodes.size();
    nodes.push_back({{"name", "Stroke " + std::to_string(strokeIndex + 1)},
                     {"mesh", meshIndex},
                     {"extras",
                      {{"openbrushBrushGuid", stroke.brush},
                       {"layer", stroke.layer},
                       {"group", stroke.group},
                       {"sourceStroke", strokeIndex}}}});
    if (!layers.count(stroke.layer)) {
      layers[stroke.layer] = nodes.size();
      nodes.push_back({{"name", "Layer " + std::to_string(stroke.layer)},
                       {"children", Json::array()},
                       {"extras", {{"layer", stroke.layer}}}});
    }
    nodes[layers.at(stroke.layer)]["children"].push_back(node);
  }
  if (!binary.empty()) {
    root["nodes"] = std::move(nodes);
    root["meshes"] = std::move(meshes);
    root["materials"] = std::move(materials);
    root["bufferViews"] = std::move(views);
    root["accessors"] = std::move(accessors);
    for (auto [layer, node] : layers)
      root["scenes"][0]["nodes"].push_back(node);
    root["buffers"] = Json::array({{{"byteLength", binary.size()}}});
    if (extension == ".gltf")
      root["buffers"][0]["uri"] =
          "data:application/octet-stream;base64," + base64(binary);
  }
  if (unlit)
    root["extensionsUsed"] = {"KHR_materials_unlit"};
  auto json = root.dump(extension == ".gltf" ? 2 : -1);
  Bytes output;
  if (extension == ".gltf")
    output.assign(json.begin(), json.end());
  else {
    while (json.size() % 4)
      json += ' ';
    while (binary.size() % 4)
      binary.push_back(0);
    uint64_t size =
        20ull + json.size() + (binary.empty() ? 0 : 8ull + binary.size());
    if (size > std::numeric_limits<uint32_t>::max())
      throw std::runtime_error("GLB exceeds 4 GiB limit");
    word(output, 0x46546c67);
    word(output, 2);
    word(output, size);
    word(output, json.size());
    word(output, 0x4e4f534a);
    output.insert(output.end(), json.begin(), json.end());
    if (!binary.empty()) {
      word(output, binary.size());
      word(output, 0x004e4942);
      output.insert(output.end(), binary.begin(), binary.end());
    }
  }
  atomicWrite(path, output);
}
} // namespace ob
