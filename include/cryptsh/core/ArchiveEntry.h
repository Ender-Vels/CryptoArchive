#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace cryptsh {

enum class EntryType : std::uint8_t {
    End = 0,
    File = 1,
    Directory = 2
};

enum class CompressionMethod : std::uint8_t {
    Stored = 0,
    Deflate = 1
};

struct ArchiveEntry {
    EntryType type = EntryType::File;
    CompressionMethod compression = CompressionMethod::Stored;
    std::string path;
    std::uint64_t originalSize = 0;
    std::uint64_t storedSize = 0;
    std::int64_t modifiedUnixTime = 0;
    std::uint32_t crc32 = 0;
};

struct ArchiveOptions {
    using ProgressCallback = std::function<void(int current, int total, const std::string& message)>;

    int kdfIterations = 250000;
    int compressionLevel = 1;
    ProgressCallback progress;
};

struct ArchiveMetadata {
    std::vector<ArchiveEntry> entries;
};

} // namespace cryptsh
