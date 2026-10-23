// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "brush.h"
#include "document.h"
namespace ob {
struct Vertex {
  Vec3 position, normal;
  Color color;
  float emission = 0;
};
struct Mesh {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
};
Mesh strokeMesh(const Stroke &stroke, BrushShape shape = BrushShape::Tube,
                unsigned sides = 8);
} // namespace ob

namespace ob {
Mesh brushMesh(const Stroke &stroke);
Mesh documentMesh(const Document &document,const Stroke &stroke);
}
