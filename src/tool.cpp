// SPDX-License-Identifier: Apache-2.0
#include "document.h"
#include "gltf.h"
#include "mesh.h"
#include <iostream>
int main(int argc, char **argv) try {
  if (argc == 4 && std::string(argv[1]) == "--export-gltf") {
    ob::writeGltf(ob::readTilt(argv[2]), argv[3]);
    return 0;
  }
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: openbrush-tool SKETCH.tilt [ROUNDTRIP.tilt]\n       openbrush-tool --export-gltf SKETCH.tilt OUTPUT.gltf|OUTPUT.glb\n";
    return 2;
  }
  auto d = ob::readTilt(argv[1]);
  size_t points = 0, triangles = 0;
  for (const auto &s : d.strokes) {
    points += s.points.size();
    triangles += ob::strokeMesh(s).indices.size() / 3;
  }
  std::cout << ob::Json{{"strokes", d.strokes.size()},
                        {"points", points},
                        {"triangles", triangles},
                        {"warnings", d.warnings}}
                   .dump(2)
            << '\n';
  if (argc > 2)
    ob::writeTilt(d, argv[2]);
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
