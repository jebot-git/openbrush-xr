// SPDX-License-Identifier: Apache-2.0
#include "mesh.h"
#include "scene.h"
namespace ob {
namespace {
std::vector<Color> colors(const Stroke &s) {
  std::vector<Color> result(s.points.size(), s.color);
  auto it = s.extensions.find(16);
  if (it == s.extensions.end())
    return result;
  const auto &b = it->second;
  if (b.size() < 8)
    return result;
  auto word = [&](size_t p) {
    return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 |
           uint32_t(b[p + 3]) << 24;
  };
  auto mode = word(0), count = word(4);
  if (mode == 0 || mode > 3 || count > s.points.size())
    return result;
  size_t masks = (count + 7) / 8, offset = 8 + masks;
  if (offset > b.size())
    return result;
  for (size_t i = 0; i < count; ++i)
    if (b[8 + i / 8] & (1u << (i % 8))) {
      if (b.size() - offset < 4)
        return result;
      Color c{b[offset] / 255.f, b[offset + 1] / 255.f, b[offset + 2] / 255.f,
              b[offset + 3] / 255.f};
      offset += 4;
      if (mode == 2)
        c = {s.color.r * c.r, s.color.g * c.g, s.color.b * c.b,
             s.color.a * c.a};
      if (mode == 3)
        c = {std::min(1.f, s.color.r + c.r), std::min(1.f, s.color.g + c.g),
             std::min(1.f, s.color.b + c.b), std::min(1.f, s.color.a + c.a)};
      result[i] = c;
    }
  return result;
}
} // namespace
Mesh strokeMesh(const Stroke &s, BrushShape shape, unsigned sides) {
  validate(s);
  if (sides < 3 || sides > 32)
    throw std::runtime_error("Invalid brush tube resolution");
  Mesh m;
  if (s.points.empty())
    return m;
  auto sourceColors = colors(s);
  std::vector<Point> points;
  std::vector<Color> pointColors;
  for (size_t i = 0; i < s.points.size(); ++i) {
    const auto &p = s.points[i];
    if (points.empty() || length(p.position - points.back().position) > 1e-6f) {
      points.push_back(p);
      pointColors.push_back(sourceColors[i]);
    }
  }
  if (shape == BrushShape::Dots || shape == BrushShape::Stars) {
    Vec3 previous{};
    bool first = true;
    unsigned ring = shape == BrushShape::Stars ? 10 : 12;
    for (size_t i = 0; i < points.size(); ++i) {
      const auto &p = points[i];
      float radius = s.size * s.scale * .5f * std::clamp(p.pressure, .02f, 1.f);
      if (!first && length(p.position - previous) < s.size * s.scale * .8f)
        continue;
      previous = p.position;
      first = false;
      auto right = rotate(p.orientation, {1, 0, 0}),
           up = rotate(p.orientation, {0, 1, 0}),
           normal = rotate(p.orientation, {0, 0, 1});
      uint32_t base = m.vertices.size();
      m.vertices.push_back({p.position, normal, pointColors[i]});
      for (unsigned j = 0; j < ring; ++j) {
        float angle = 6.283185307f * j / ring,
              scale = shape == BrushShape::Stars && j % 2 ? .4f : 1.f;
        m.vertices.push_back(
            {p.position + (right * std::cos(angle) + up * std::sin(angle)) *
                              (radius * scale),
             normal, pointColors[i]});
      }
      for (unsigned j = 0; j < ring; ++j)
        m.indices.insert(m.indices.end(),
                         {base, base + 1 + j, base + 1 + (j + 1) % ring});
    }
    return m;
  }
  if (points.size() == 1) {
    auto p = points.front();
    p.position = p.position +
                 rotate(p.orientation, {0, 0, -std::max(s.size * .01f, 1e-5f)});
    points.push_back(p);
    pointColors.push_back(pointColors.front());
  }
  const unsigned ring = shape == BrushShape::Tube ? sides : 2;
  Vec3 previous{};
  for (size_t i = 0; i < points.size(); ++i) {
    const auto &p = points[i];
    auto tangent =
        normalized(points[i + 1 < points.size() ? i + 1 : i].position -
                       points[i ? i - 1 : i].position,
                   {0, 0, -1});
    auto right = rotate(p.orientation, {1, 0, 0});
    right = right - tangent * dot(tangent, right);
    if (length(right) < 1e-5f)
      right = cross(tangent,
                    std::abs(tangent.y) < .9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0});
    right = normalized(right);
    // Parallel transport avoids arbitrary rotations as tube tangents cross
    // axes.
    if (shape == BrushShape::Tube && i) {
      auto carried = previous - tangent * dot(previous, tangent);
      if (length(carried) > 1e-5f)
        right = normalized(carried);
    }
    previous = right;
    auto up = normalized(cross(tangent, right));
    float radius = s.size * s.scale * .5f * std::clamp(p.pressure, .02f, 1.f);
    for (unsigned j = 0; j < ring; ++j) {
      Vec3 normal, offset;
      if (shape == BrushShape::Tube) {
        float angle = 6.283185307f * j / ring;
        normal = right * std::cos(angle) + up * std::sin(angle);
        offset = normal * radius;
      } else {
        normal = up;
        offset = right * (j ? radius : -radius);
        if (shape == BrushShape::Flat)
          offset = offset + up * (s.size * .04f);
      }
      m.vertices.push_back({p.position + offset, normal, pointColors[i]});
    }
    if (i)
      for (unsigned j = 0; j < (shape == BrushShape::Tube ? ring : 1); ++j) {
        uint32_t a = (i - 1) * ring + j, b = (i - 1) * ring + (j + 1) % ring,
                 c = i * ring + j, d = i * ring + (j + 1) % ring;
        m.indices.insert(m.indices.end(), {a, b, c, b, d, c});
      }
  }
  if (shape == BrushShape::Tube) {
    for (size_t end : {size_t(0), points.size() - 1}) {
      Vec3 direction = normalized(
          end ? points.back().position - points[points.size() - 2].position
              : points[0].position - points[1].position);
      uint32_t center = m.vertices.size();
      m.vertices.push_back({points[end].position, direction, pointColors[end]});
      for (unsigned j = 0; j < ring; ++j) {
        uint32_t a = end * ring + j, b = end * ring + (j + 1) % ring;
        if (end)
          m.indices.insert(m.indices.end(), {center, a, b});
        else
          m.indices.insert(m.indices.end(), {center, b, a});
      }
    }
  }
  return m;
}
Mesh documentMesh(const Document &document, const Stroke &stroke) {
  auto mesh = brushMesh(stroke);
  auto pose = strokeTransform(document, stroke.layer);
  for (auto &v : mesh.vertices) {
    v.position = transform(pose, v.position);
    v.normal = rotate(pose.orientation, v.normal);
  }
  return mesh;
}
Mesh brushMesh(const Stroke &s) {
  const auto &brush = brushFor(s.brush);
  auto mesh = strokeMesh(s, brush.shape);
  for (auto &v : mesh.vertices) {
    v.emission = brush.emission;
    if (brush.name == "Highlighter")
      v.color.a *= .45f;
  }
  return mesh;
}
} // namespace ob
