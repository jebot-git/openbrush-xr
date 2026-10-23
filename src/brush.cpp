// SPDX-License-Identifier: Apache-2.0
#include "brush.h"
#include <algorithm>
namespace ob {
const std::vector<Brush> &brushes() {
  static const std::vector<Brush> list = {
      {"4391385a-cf83-4396-9e33-31e4e4930b27", "Wire", BrushShape::Tube},
      {"2d35bcf0-e4d8-452c-97b1-3311be063130", "Flat", BrushShape::Ribbon},
      {"429ed64a-4e97-4466-84d3-145a861ef684", "Marker", BrushShape::Flat},
      {"f5c336cf-5108-4b40-ade9-c687504385ab", "Ink", BrushShape::Ribbon},
      {"2241cd32-8ba2-48a5-9ee7-2caef7e9ed62", "Light", BrushShape::Tube, 1},
      {"6a1cf9f9-032c-45ec-9b1d-a6680bee30f7", "Dots", BrushShape::Dots},
      {"0eb4db27-3f82-408d-b5a1-19ebd7d5b711", "Stars", BrushShape::Stars},
      {"cf019139-d41c-4eb0-a1d0-5cf54b0a42f3", "Highlighter", BrushShape::Flat,
       .5f}};
  return list;
}
bool sameBrush(const std::string &a, const std::string &b) {
  auto fold = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; };
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(),
                    [&](char x, char y) { return fold(x) == fold(y); });
}
bool knownBrush(const std::string &id) {
  for (auto &b : brushes())
    if (sameBrush(b.guid, id))
      return true;
  return false;
}
const Brush &brushFor(const std::string &id) {
  for (auto &b : brushes())
    if (sameBrush(b.guid, id))
      return b;
  return brushes().front();
}
} // namespace ob
