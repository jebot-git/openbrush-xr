// SPDX-License-Identifier: Apache-2.0
#include "tilt-directory.h"
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
namespace ob {
void writeTiltDirectory(const std::filesystem::path &path,
                        const std::map<std::string, Bytes> &files) {
  auto target = std::filesystem::absolute(path);
  if (std::filesystem::is_symlink(target))
    target = std::filesystem::canonical(target);
  auto pattern = target.string() + ".XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back(0);
  auto created = mkdtemp(buffer.data());
  if (!created)
    throw std::runtime_error("Cannot create temporary artwork directory");
  std::filesystem::path temporary(created);
  try {
    for (const auto &[name, data] : files) {
      std::filesystem::path relative(name);
      if (name.empty() || relative.is_absolute())
        throw std::runtime_error("Invalid attachment path");
      for (const auto &part : relative)
        if (part == "..")
          throw std::runtime_error("Invalid attachment path");
      auto destination = temporary / relative;
      std::filesystem::create_directories(destination.parent_path());
      atomicWrite(destination, data);
    }
    auto syncDirectory = [](const std::filesystem::path &directory) {
      int fd = open(directory.c_str(), O_DIRECTORY | O_RDONLY);
      if (fd < 0)
        throw std::runtime_error("Cannot open artwork directory for sync");
      int result = fsync(fd);
      close(fd);
      if (result)
        throw std::runtime_error("Cannot sync artwork directory");
    };
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(temporary))
      if (entry.is_directory())
        syncDirectory(entry.path());
    syncDirectory(temporary);
    std::filesystem::permissions(temporary,
                                 std::filesystem::status(target).permissions());
    // Exchange two complete directories atomically. On failure the original is
    // untouched; no rename-to-backup gap can leave a missing artwork directory.
    if (syscall(SYS_renameat2, AT_FDCWD, temporary.c_str(), AT_FDCWD,
                target.c_str(), RENAME_EXCHANGE) != 0)
      throw std::runtime_error("Cannot atomically replace artwork directory");
    int parent = open(target.parent_path().c_str(), O_DIRECTORY | O_RDONLY);
    if (parent >= 0) {
      fsync(parent);
      close(parent);
    }
    std::error_code ec;
    std::filesystem::remove_all(temporary, ec);
  } catch (...) {
    std::error_code ec;
    std::filesystem::remove_all(temporary, ec);
    throw;
  }
}
} // namespace ob
