#pragma once

#include "cryptsh/core/ArchiveEntry.h"

#include <filesystem>
#include <string>
#include <vector>

namespace cryptsh {

class ArchiveReader {
public:
    ArchiveMetadata readMetadata(const std::filesystem::path& archivePath, const std::string& password) const;
    void extract(const std::filesystem::path& archivePath,
                 const std::filesystem::path& destination,
                 const std::string& password,
                 const ArchiveOptions& options = {}) const;

    void extractSelected(const std::filesystem::path& archivePath,
                         const std::filesystem::path& destination,
                         const std::string& password,
                         const std::vector<std::string>& selectedPaths,
                         const ArchiveOptions& options = {}) const;

    void test(const std::filesystem::path& archivePath, const std::string& password) const;
};

} // namespace cryptsh
