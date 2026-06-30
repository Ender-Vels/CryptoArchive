#include "cryptsh/core/ArchiveService.h"

#include "cryptsh/core/ArchiveReader.h"
#include "cryptsh/core/ArchiveWriter.h"

namespace cryptsh {

void ArchiveService::createArchive(const std::vector<std::filesystem::path>& inputPaths,
                                   const std::filesystem::path& archivePath,
                                   const std::string& password,
                                   const ArchiveOptions& options) const {
    ArchiveWriter().create(inputPaths, archivePath, password, options);
}

ArchiveMetadata ArchiveService::openArchive(const std::filesystem::path& archivePath,
                                            const std::string& password) const {
    return ArchiveReader().readMetadata(archivePath, password);
}

void ArchiveService::extractArchive(const std::filesystem::path& archivePath,
                                    const std::filesystem::path& destination,
                                    const std::string& password,
                                    const ArchiveOptions& options) const {
    ArchiveReader().extract(archivePath, destination, password, options);
}

void ArchiveService::extractSelected(const std::filesystem::path& archivePath,
                                     const std::filesystem::path& destination,
                                     const std::string& password,
                                     const std::vector<std::string>& selectedPaths,
                                     const ArchiveOptions& options) const {
    ArchiveReader().extractSelected(archivePath, destination, password, selectedPaths, options);
}

void ArchiveService::testArchive(const std::filesystem::path& archivePath,
                                 const std::string& password) const {
    ArchiveReader().test(archivePath, password);
}

} 
