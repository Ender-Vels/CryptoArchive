#include "cryptsh/core/FileSystemUtils.h"

#include "cryptsh/core/ArchiveError.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <zlib.h>

namespace cryptsh::fsutils {

std::vector<std::filesystem::path> expandInputs(const std::vector<std::filesystem::path>& inputPaths) {
    std::vector<std::filesystem::path> result;
    for (const auto& path : inputPaths) {
        if (!std::filesystem::exists(path)) {
            throw ArchiveError("Input path does not exist: " + path.string());
        }
        result.push_back(path);
        if (std::filesystem::is_directory(path)) {
            for (const auto& item : std::filesystem::recursive_directory_iterator(path)) {
                result.push_back(item.path());
            }
        }
    }
    return result;
}

std::string archiveRelativePath(const std::filesystem::path& root, const std::filesystem::path& item) {
    const auto base = std::filesystem::is_directory(root) ? root.parent_path() : root.parent_path();
    auto relativePath = std::filesystem::relative(item, base);
    if (relativePath.empty()) {
        relativePath = item.filename();
    }

    auto relative = relativePath.generic_u8string();
    if (relative.empty() || relative.rfind(u8"..", 0) == 0) {
        relative = item.filename().generic_u8string();
    }

    return {reinterpret_cast<const char*>(relative.data()), relative.size()};
}

std::filesystem::path pathFromArchiveUtf8(const std::string& path) {
    const auto* first = reinterpret_cast<const char8_t*>(path.data());
    const auto* last = first + path.size();
    return std::filesystem::path(std::u8string(first, last));
}

std::int64_t lastWriteTimeUnix(const std::filesystem::path& path) {
    const auto fileTime = std::filesystem::last_write_time(path);
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(systemTime);
}

std::uint32_t crc32ForFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw ArchiveError("Cannot read file for CRC32: " + path.string());
    }

    std::uint32_t crc = crc32(0L, Z_NULL, 0);
    std::array<unsigned char, 64 * 1024> buffer{};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        if (input.gcount() > 0) {
            crc = crc32(crc, buffer.data(), static_cast<uInt>(input.gcount()));
        }
    }
    return crc;
}

void ensureInsideDestination(const std::filesystem::path& destination, const std::filesystem::path& candidate) {
    const auto canonicalDestination = std::filesystem::weakly_canonical(destination);
    const auto canonicalCandidate = std::filesystem::weakly_canonical(candidate.parent_path());
    const auto [destIt, candidateIt] = std::mismatch(
        canonicalDestination.begin(),
        canonicalDestination.end(),
        canonicalCandidate.begin(),
        canonicalCandidate.end());
    if (destIt != canonicalDestination.end()) {
        throw ArchiveError("Archive entry tries to escape destination folder.");
    }
}

} 
