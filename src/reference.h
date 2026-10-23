// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "document.h"
namespace ob {
std::shared_ptr<const RasterImage> decodeReference(const Bytes &encoded);
ReferenceImage readReference(const std::filesystem::path &path);
void validate(const ReferenceImage &image);
void loadReferences(Document &document);
void storeReferences(const Document &document, Json &metadata,
                     std::map<std::string, Bytes> &files);
} // namespace ob
