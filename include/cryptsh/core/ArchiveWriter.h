#pragma once

#include "cryptsh/core/ArchiveEntry.h"

#include <filesystem>
#include <string>
#include <vector>

namespace cryptsh {

class ArchiveWriter {
public:
    void create(const std::vector<std::filesystem::path>& inputPaths,
                const std::filesystem::path& archivePath,
                const std::string& password,
                const ArchiveOptions& options) const;
};

} // namespace cryptsh
