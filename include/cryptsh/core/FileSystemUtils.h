#pragma once

#include "cryptsh/core/ArchiveEntry.h"

#include <filesystem>
#include <vector>

namespace cryptsh::fsutils {

std::vector<std::filesystem::path> expandInputs(const std::vector<std::filesystem::path>& inputPaths);
std::string archiveRelativePath(const std::filesystem::path& root, const std::filesystem::path& item);
std::filesystem::path pathFromArchiveUtf8(const std::string& path);
std::int64_t lastWriteTimeUnix(const std::filesystem::path& path);
std::uint32_t crc32ForFile(const std::filesystem::path& path);
void ensureInsideDestination(const std::filesystem::path& destination, const std::filesystem::path& candidate);

} // namespace cryptsh::fsutils
