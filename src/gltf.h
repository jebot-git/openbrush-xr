// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "document.h"
namespace ob {
// Export native brush meshes in document space (RH, Y-up, meters).
// .gltf embeds its buffer; .glb uses a binary chunk. Both are atomic single
// files.
void writeGltf(const Document &document, const std::filesystem::path &path);
} // namespace ob
