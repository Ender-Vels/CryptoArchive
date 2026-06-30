#include "cryptsh/core/ArchiveReader.h"

#include "cryptsh/core/ArchiveError.h"
#include "cryptsh/core/ArchiveFormat.h"
#include "cryptsh/core/Compressor.h"
#include "cryptsh/core/CryptoProvider.h"
#include "cryptsh/core/FileSystemUtils.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <zlib.h>

namespace cryptsh {
namespace {

ArchiveEntry readEntryHeader(std::istream& input) {
    ArchiveEntry entry;
    entry.type = static_cast<EntryType>(ArchiveFormat::readU8(input));
    if (entry.type == EntryType::End) {
        return entry;
    }
    if (entry.type != EntryType::File && entry.type != EntryType::Directory) {
        throw ArchiveError("Archive contains an unknown entry type.");
    }

    const auto pathSize = ArchiveFormat::readU32(input);
    if (pathSize == 0 || pathSize > 64 * 1024) {
        throw ArchiveError("Archive contains an invalid path length.");
    }
    entry.path.resize(pathSize);
    input.read(entry.path.data(), static_cast<std::streamsize>(entry.path.size()));
    entry.originalSize = ArchiveFormat::readU64(input);
    entry.storedSize = ArchiveFormat::readU64(input);
    entry.modifiedUnixTime = ArchiveFormat::readI64(input);
    entry.crc32 = ArchiveFormat::readU32(input);
    entry.compression = static_cast<CompressionMethod>(ArchiveFormat::readU8(input));
    if (entry.compression != CompressionMethod::Stored && entry.compression != CompressionMethod::Deflate) {
        throw ArchiveError("Archive contains an unknown compression method.");
    }
    if (!input) {
        throw ArchiveError("Archive entry header is corrupted.");
    }
    return entry;
}

void skipBytes(std::istream& input, std::uint64_t count) {
    std::array<char, 64 * 1024> buffer{};
    while (count > 0) {
        const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(buffer.size(), count));
        input.read(buffer.data(), chunk);
        if (input.gcount() != chunk) {
            throw ArchiveError("Archive payload is truncated.");
        }
        count -= static_cast<std::uint64_t>(chunk);
    }
}

void copyStreamBytes(std::istream& input, std::ostream& output, std::uint64_t size) {
    std::array<char, 64 * 1024> buffer{};
    std::uint64_t remaining = size;
    while (remaining > 0) {
        const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(buffer.size(), remaining));
        input.read(buffer.data(), chunk);
        if (input.gcount() != chunk) {
            throw ArchiveError("Archive file data is truncated.");
        }
        output.write(buffer.data(), chunk);
        remaining -= static_cast<std::uint64_t>(chunk);
    }
}

void extractStoredFile(std::istream& input, const std::filesystem::path& outputPath, const ArchiveEntry& entry) {
    std::ofstream output(outputPath, std::ios::binary);
    if (!output) {
        throw ArchiveError("Cannot create extracted file: " + outputPath.string());
    }

    copyStreamBytes(input, output, entry.storedSize);
    output.close();

    if (fsutils::crc32ForFile(outputPath) != entry.crc32) {
        throw ArchiveError("CRC32 check failed for extracted file: " + outputPath.string());
    }
}

void extractFile(std::istream& input, const std::filesystem::path& outputPath, const ArchiveEntry& entry) {
    if (entry.compression == CompressionMethod::Stored) {
        extractStoredFile(input, outputPath, entry);
        return;
    }

    const auto compressedPath = ArchiveFormat::makeTempPath(".file.deflate");
    try {
        std::ofstream compressed(compressedPath, std::ios::binary);
        if (!compressed) {
            throw ArchiveError("Cannot create temporary compressed file.");
        }
        copyStreamBytes(input, compressed, entry.storedSize);
        compressed.close();
        try {
            Compressor().decompressFile(compressedPath, outputPath);
        } catch (const std::exception& error) {
            throw ArchiveError("Cannot decompress archived file '" + entry.path + "': " + error.what());
        }
        if (fsutils::crc32ForFile(outputPath) != entry.crc32) {
            throw ArchiveError("CRC32 check failed for extracted file: " + outputPath.string());
        }
    } catch (...) {
        std::filesystem::remove(compressedPath);
        throw;
    }
    std::filesystem::remove(compressedPath);
}

void testFile(std::istream& input, const ArchiveEntry& entry) {
    const auto outputPath = ArchiveFormat::makeTempPath(".test");
    try {
        extractFile(input, outputPath, entry);
    } catch (...) {
        std::filesystem::remove(outputPath);
        throw;
    }
    std::filesystem::remove(outputPath);
}

bool isSelectedEntry(const std::set<std::string>& selected, const std::string& path) {
    for (const auto& item : selected) {
        if (path == item || path.rfind(item + "/", 0) == 0) {
            return true;
        }
    }
    return false;
}

std::filesystem::path preparePlainPayload(const std::filesystem::path& archivePath, const std::string& password) {
    const auto plainPath = ArchiveFormat::makeTempPath(".plain");
    try {
        CryptoProvider().decryptFile(archivePath, plainPath, password);
        return plainPath;
    } catch (...) {
        std::filesystem::remove(plainPath);
        throw;
    }
}

void reportProgress(const ArchiveOptions& options, int current, int total, const std::string& message) {
    if (options.progress) {
        options.progress(current, total, message);
    }
}

int countEntries(std::istream& input, const std::set<std::string>* selected = nullptr) {
    int total = 0;
    input.clear();
    input.seekg(0);
    ArchiveFormat::verifyInnerMagic(input);
    while (true) {
        ArchiveEntry entry = readEntryHeader(input);
        if (entry.type == EntryType::End) {
            break;
        }
        const bool included = !selected || isSelectedEntry(*selected, entry.path);
        if (included) {
            ++total;
        }
        if (entry.type == EntryType::File) {
            skipBytes(input, entry.storedSize);
        }
    }
    input.clear();
    input.seekg(0);
    ArchiveFormat::verifyInnerMagic(input);
    return std::max(total, 1);
}

} // namespace

ArchiveMetadata ArchiveReader::readMetadata(const std::filesystem::path& archivePath, const std::string& password) const {
    const auto plainPath = preparePlainPayload(archivePath, password);
    ArchiveMetadata metadata;
    try {
        std::ifstream input(plainPath, std::ios::binary);
        ArchiveFormat::verifyInnerMagic(input);
        while (true) {
            ArchiveEntry entry = readEntryHeader(input);
            if (entry.type == EntryType::End) {
                break;
            }
            metadata.entries.push_back(entry);
            if (entry.type == EntryType::File) {
                skipBytes(input, entry.storedSize);
            }
        }
    } catch (...) {
        std::filesystem::remove(plainPath);
        throw;
    }
    std::filesystem::remove(plainPath);
    return metadata;
}

void ArchiveReader::extract(const std::filesystem::path& archivePath,
                            const std::filesystem::path& destination,
                            const std::string& password,
                            const ArchiveOptions& options) const {
    reportProgress(options, 0, 100, "Decrypting archive...");
    const auto plainPath = preparePlainPayload(archivePath, password);
    try {
        std::filesystem::create_directories(destination);
        std::ifstream input(plainPath, std::ios::binary);
        const int total = countEntries(input);
        int extracted = 0;

        while (true) {
            ArchiveEntry entry = readEntryHeader(input);
            if (entry.type == EntryType::End) {
                break;
            }

            const auto outputPath = destination / fsutils::pathFromArchiveUtf8(entry.path);
            fsutils::ensureInsideDestination(destination, outputPath);
            if (entry.type == EntryType::Directory) {
                std::filesystem::create_directories(outputPath);
            } else {
                std::filesystem::create_directories(outputPath.parent_path());
                extractFile(input, outputPath, entry);
            }
            ++extracted;
            reportProgress(options, extracted, total, "Extracting: " + entry.path);
        }
    } catch (...) {
        std::filesystem::remove(plainPath);
        throw;
    }
    std::filesystem::remove(plainPath);
}

void ArchiveReader::extractSelected(const std::filesystem::path& archivePath,
                                    const std::filesystem::path& destination,
                                    const std::string& password,
                                    const std::vector<std::string>& selectedPaths,
                                    const ArchiveOptions& options) const {
    if (selectedPaths.empty()) {
        throw ArchiveError("Choose at least one archive entry.");
    }

    const std::set<std::string> selected(selectedPaths.begin(), selectedPaths.end());
    reportProgress(options, 0, 100, "Decrypting archive...");
    const auto plainPath = preparePlainPayload(archivePath, password);
    try {
        std::filesystem::create_directories(destination);
        std::ifstream input(plainPath, std::ios::binary);
        const int total = countEntries(input, &selected);
        int extracted = 0;

        while (true) {
            ArchiveEntry entry = readEntryHeader(input);
            if (entry.type == EntryType::End) {
                break;
            }

            const bool shouldExtract = isSelectedEntry(selected, entry.path);
            const auto outputPath = destination / fsutils::pathFromArchiveUtf8(entry.path);
            if (shouldExtract) {
                fsutils::ensureInsideDestination(destination, outputPath);
                if (entry.type == EntryType::Directory) {
                    std::filesystem::create_directories(outputPath);
                } else {
                    std::filesystem::create_directories(outputPath.parent_path());
                    extractFile(input, outputPath, entry);
                }
                ++extracted;
                reportProgress(options, extracted, total, "Extracting: " + entry.path);
            } else if (entry.type == EntryType::File) {
                skipBytes(input, entry.storedSize);
            }
        }
    } catch (...) {
        std::filesystem::remove(plainPath);
        throw;
    }
    std::filesystem::remove(plainPath);
}

void ArchiveReader::test(const std::filesystem::path& archivePath, const std::string& password) const {
    const auto plainPath = preparePlainPayload(archivePath, password);
    try {
        std::ifstream input(plainPath, std::ios::binary);
        ArchiveFormat::verifyInnerMagic(input);
        while (true) {
            ArchiveEntry entry = readEntryHeader(input);
            if (entry.type == EntryType::End) {
                break;
            }
            if (entry.type == EntryType::File) {
                testFile(input, entry);
            }
        }
    } catch (...) {
        std::filesystem::remove(plainPath);
        throw;
    }
    std::filesystem::remove(plainPath);
}

} 
