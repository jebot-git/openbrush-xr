// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "document.h"
namespace ob {
void writeTiltDirectory(const std::filesystem::path &path,
                        const std::map<std::string, Bytes> &files);
}
