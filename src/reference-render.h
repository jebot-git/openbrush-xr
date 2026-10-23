// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "render.h"
namespace ob {
class ReferenceRenderer {
  struct Texture {
    GLuint id = 0;
    std::shared_ptr<const RasterImage> pixels;
  };
  GLuint program = 0, vao = 0, vbo = 0;
  std::map<const RasterImage *, Texture> textures;

public:
  ReferenceRenderer();
  ~ReferenceRenderer();
  void draw(const std::vector<ReferenceImage> &images, const Mat4 &vp);
  void abandon();
};
} // namespace ob
