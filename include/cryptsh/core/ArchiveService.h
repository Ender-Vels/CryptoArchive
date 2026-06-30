#pragma once

#include "cryptsh/core/ArchiveEntry.h"

#include <filesystem>
#include <string>
#include <vector>

namespace cryptsh {

class ArchiveService {
public:
    void createArchive(const std::vector<std::filesystem::path>& inputPaths,
                       const std::filesystem::path& archivePath,
                       const std::string& password,
                       const ArchiveOptions& options = {}) const;

    ArchiveMetadata openArchive(const std::filesystem::path& archivePath,
                                const std::string& password) const;

    void extractArchive(const std::filesystem::path& archivePath,
                        const std::filesystem::path& destination,
                        const std::string& password,
                        const ArchiveOptions& options = {}) const;

    void extractSelected(const std::filesystem::path& archivePath,
                         const std::filesystem::path& destination,
                         const std::string& password,
                         const std::vector<std::string>& selectedPaths,
                         const ArchiveOptions& options = {}) const;

    void testArchive(const std::filesystem::path& archivePath,
                     const std::string& password) const;
};

} // namespace cryptsh
