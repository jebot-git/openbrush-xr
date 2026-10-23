// SPDX-License-Identifier: Apache-2.0
#include "gltf.h"
#include "brush.h"
#include <cassert>
#include <fstream>
int main(int argc, char **argv) {
  assert(argc == 2);
  std::filesystem::path dir = argv[1];
  std::filesystem::create_directories(dir);
  ob::Document document;
  for (size_t i = 0; i < ob::brushes().size() + 1; ++i) {
    ob::Stroke s;
    s.brush = i < ob::brushes().size() ? ob::brushes()[i].guid
                                       : "ffffffff-ffff-ffff-ffff-ffffffffffff";
    s.size = .02f;
    s.color = {.2f, .4f, .8f, i % 2 ? .5f : 1.f};
    s.layer = i % 3;
    s.group = 42;
    s.points = {{{float(i) * .1f, 1, -1}, {}, 1, 0},
                {{float(i) * .1f + .05f, 1.1f, -1.1f}, {}, .7f, 1},
                {{float(i) * .1f + .1f, 1.2f, -1.2f}, {}, .5f, 2}};
    document.strokes.push_back(s);
  }
  document.strokes[1].color = {1.5f, -.2f, .8f, .5f};
  // Replace per-point colors on the wire brush.
  document.strokes[0].extensions[16] = {1,   0, 0,   0, 3, 0,   0,
                                        0,   7, 255, 0, 0, 255, 0,
                                        255, 0, 128, 0, 0, 255, 255};
  ob::writeGltf(document, dir / "brushes.gltf");
  ob::writeGltf(document, dir / "brushes.glb");
  ob::writeGltf({}, dir / "empty.gltf");
  ob::writeGltf({}, dir / "empty.glb");
  ob::atomicWrite(dir / "invalid.glb", {1, 2, 3});
  auto invalid = document;
  invalid.strokes.back().size = -1;
  bool rejected = false;
  try {
    ob::writeGltf(invalid, dir / "invalid.glb");
  } catch (...) {
    rejected = true;
  }
  assert(rejected);
  std::ifstream existing(dir / "invalid.glb", std::ios::binary);
  assert(existing.get() == 1 && existing.get() == 2 && existing.get() == 3 &&
         existing.get() == EOF);
  rejected = false;
  try {
    ob::writeGltf(document, dir / "invalid.txt");
  } catch (...) {
    rejected = true;
  }
  assert(rejected && !std::filesystem::exists(dir / "invalid.txt"));
}
