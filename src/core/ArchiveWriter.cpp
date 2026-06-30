#include "cryptsh/core/ArchiveWriter.h"

#include "cryptsh/core/ArchiveError.h"
#include "cryptsh/core/ArchiveFormat.h"
#include "cryptsh/core/Compressor.h"
#include "cryptsh/core/CryptoProvider.h"
#include "cryptsh/core/FileSystemUtils.h"

#include <array>
#include <cctype>
#include <future>
#include <limits>
#include <fstream>
#include <set>
#include <thread>

namespace cryptsh {
namespace {

void reportProgress(const ArchiveOptions& options, int current, int total, const std::string& message) {
    if (options.progress) {
        options.progress(current, total, message);
    }
}

std::string lowerExtension(const std::filesystem::path& path) {
    auto extension = path.extension().u8string();
    std::string value(reinterpret_cast<const char*>(extension.data()), extension.size());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool shouldStoreWithoutDeflate(const std::filesystem::path& path) {
    static const std::set<std::string> AlreadyCompressedExtensions = {
        ".7z", ".avi", ".bz2", ".cab", ".docx", ".epub", ".flac", ".gif",
        ".gz", ".heic", ".jpeg", ".jpg", ".m4a", ".mkv", ".mov", ".mp3", ".mp4",
        ".ogg", ".pdf", ".png", ".pptx", ".rar", ".webm", ".webp", ".xlsx", ".xz",
        ".zip", ".zst"
    };
    return AlreadyCompressedExtensions.contains(lowerExtension(path));
}

struct PreparedEntry {
    ArchiveEntry entry;
    std::filesystem::path payloadPath;
    bool temporaryPayload = false;
};

void writeEntryHeader(std::ostream& output, const ArchiveEntry& entry) {
    ArchiveFormat::writeU8(output, static_cast<std::uint8_t>(entry.type));
    if (entry.type == EntryType::End) {
        return;
    }
    if (entry.path.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw ArchiveError("Archive path is too long.");
    }
    ArchiveFormat::writeU32(output, static_cast<std::uint32_t>(entry.path.size()));
    output.write(entry.path.data(), static_cast<std::streamsize>(entry.path.size()));
    ArchiveFormat::writeU64(output, entry.originalSize);
    ArchiveFormat::writeU64(output, entry.storedSize);
    ArchiveFormat::writeI64(output, entry.modifiedUnixTime);
    ArchiveFormat::writeU32(output, entry.crc32);
    ArchiveFormat::writeU8(output, static_cast<std::uint8_t>(entry.compression));
}

void copyFileBytes(const std::filesystem::path& path,
                   std::ostream& output,
                   std::uint64_t maxBytes = std::numeric_limits<std::uint64_t>::max()) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw ArchiveError("Cannot read input file: " + path.string());
    }
    std::array<char, 64 * 1024> buffer{};
    std::uint64_t remaining = maxBytes;
    while (input && remaining > 0) {
        const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(buffer.size(), remaining));
        input.read(buffer.data(), chunk);
        if (input.gcount() > 0) {
            output.write(buffer.data(), input.gcount());
            remaining -= static_cast<std::uint64_t>(input.gcount());
        }
    }
}

std::filesystem::path choosePayload(const std::filesystem::path& item,
                                    ArchiveEntry& entry,
                                    int compressionLevel) {
    if (shouldStoreWithoutDeflate(item)) {
        entry.compression = CompressionMethod::Stored;
        entry.storedSize = entry.originalSize;
        return item;
    }

    const auto compressedPath = ArchiveFormat::makeTempPath(".file.deflate");
    Compressor().compressFile(item, compressedPath, compressionLevel);

    const auto compressedSize = std::filesystem::file_size(compressedPath);
    if (compressedSize < entry.originalSize) {
        entry.compression = CompressionMethod::Deflate;
        entry.storedSize = compressedSize;
        return compressedPath;
    }

    std::filesystem::remove(compressedPath);
    entry.compression = CompressionMethod::Stored;
    entry.storedSize = entry.originalSize;
    return item;
}

PreparedEntry prepareEntry(const std::filesystem::path& root,
                           const std::filesystem::path& item,
                           int compressionLevel) {
    PreparedEntry prepared;
    prepared.entry.type = std::filesystem::is_directory(item) ? EntryType::Directory : EntryType::File;
    prepared.entry.path = fsutils::archiveRelativePath(root, item);
    prepared.entry.modifiedUnixTime = fsutils::lastWriteTimeUnix(item);
    if (prepared.entry.type == EntryType::File) {
        prepared.entry.originalSize = std::filesystem::file_size(item);
        prepared.entry.crc32 = fsutils::crc32ForFile(item);
        prepared.payloadPath = choosePayload(item, prepared.entry, compressionLevel);
        prepared.temporaryPayload = prepared.payloadPath != item;
    }
    return prepared;
}

void writePreparedEntry(std::ostream& plain, const PreparedEntry& prepared) {
    writeEntryHeader(plain, prepared.entry);
    if (prepared.entry.type == EntryType::File) {
        copyFileBytes(prepared.payloadPath, plain, prepared.entry.storedSize);
    }
}

} // namespace

void ArchiveWriter::create(const std::vector<std::filesystem::path>& inputPaths,
                           const std::filesystem::path& archivePath,
                           const std::string& password,
                           const ArchiveOptions& options) const {
    if (password.empty()) {
        throw ArchiveError("Password cannot be empty.");
    }
    if (inputPaths.empty()) {
        throw ArchiveError("Choose at least one file or folder.");
    }

    const auto plainPath = ArchiveFormat::makeTempPath(".plain");
    try {
        reportProgress(options, 0, 100, "Scanning selected files...");
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> items;
        for (const auto& root : inputPaths) {
            const auto expanded = fsutils::expandInputs({root});
            for (const auto& item : expanded) {
                items.emplace_back(root, item);
            }
        }

        std::ofstream plain(plainPath, std::ios::binary);
        if (!plain) {
            throw ArchiveError("Cannot create temporary archive payload.");
        }
        ArchiveFormat::writeInnerMagic(plain);

        const int packTotal = static_cast<int>(items.size()) + 1;
        int packed = 0;
        const auto workerCount = std::max<unsigned int>(2, std::thread::hardware_concurrency());
        const std::size_t batchSize = std::min<std::size_t>(workerCount, 8);

        for (std::size_t start = 0; start < items.size(); start += batchSize) {
            std::vector<std::future<PreparedEntry>> futures;
            const auto end = std::min(start + batchSize, items.size());
            futures.reserve(end - start);

            for (std::size_t i = start; i < end; ++i) {
                futures.push_back(std::async(std::launch::async, [root = items[i].first, item = items[i].second, level = options.compressionLevel]() {
                    return prepareEntry(root, item, level);
                }));
            }

            for (auto& future : futures) {
                PreparedEntry prepared = future.get();
                try {
                    writePreparedEntry(plain, prepared);
                    ++packed;
                    reportProgress(options, packed, packTotal, "Packing: " + prepared.entry.path);
                } catch (...) {
                    if (prepared.temporaryPayload) {
                        std::filesystem::remove(prepared.payloadPath);
                    }
                    throw;
                }
                if (prepared.temporaryPayload) {
                    std::filesystem::remove(prepared.payloadPath);
                }
            }
        }

        ArchiveEntry end;
        end.type = EntryType::End;
        writeEntryHeader(plain, end);
        plain.close();

        reportProgress(options, packTotal, packTotal, "Encrypting archive payload...");
        CryptoProvider().encryptFile(plainPath, archivePath, password, options.kdfIterations);
        reportProgress(options, packTotal, packTotal, "Signing archive...");
        CryptoProvider().appendSignature(archivePath);
        reportProgress(options, packTotal, packTotal, "Archive created.");
    } catch (...) {
        std::filesystem::remove(plainPath);
        throw;
    }

    std::filesystem::remove(plainPath);
}

} 
