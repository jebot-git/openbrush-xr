// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>
#include <vector>
namespace ob {
enum class BrushShape { Tube, Ribbon, Flat, Dots, Stars };
struct Brush {
  std::string guid, name;
  BrushShape shape;
  float emission = 0;
};
const std::vector<Brush> &brushes();
const Brush &brushFor(const std::string &guid);
bool knownBrush(const std::string &guid);
bool sameBrush(const std::string &a, const std::string &b);
} // namespace ob
