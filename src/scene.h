// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "document.h"
namespace ob {
// Strokes remain in canvas-local RH meters. Only display/tool coordinates use
// SceneTransformInRoomSpace * Layers[layer].Transform from original metadata.
Transform strokeTransform(const Document &document, uint32_t layer);
Stroke transformedStroke(Stroke stroke, Transform pose);
bool sourceLayerVisible(const Document &document, uint32_t layer);
std::string sourceLayerName(const Document &document, uint32_t layer);
size_t sourceLayerCount(const Document &document);
void validateScene(const Document &document);
void setSourceLayerVisible(Document &document, uint32_t layer, bool visible);
} // namespace ob
