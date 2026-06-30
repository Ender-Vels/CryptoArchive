#include "cryptsh/core/ArchiveFormat.h"

#include "cryptsh/core/ArchiveError.h"

#include <chrono>
#include <random>

namespace cryptsh {
namespace {

template <typename T>
void writeLittleEndian(std::ostream& output, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        output.put(static_cast<char>((static_cast<std::uint64_t>(value) >> (i * 8)) & 0xff));
    }
    if (!output) {
        throw ArchiveError("Failed to write archive data.");
    }
}

template <typename T>
T readLittleEndian(std::istream& input) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        const int byte = input.get();
        if (byte == EOF) {
            throw ArchiveError("Unexpected end of archive.");
        }
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(byte)) << (i * 8);
    }
    return static_cast<T>(value);
}

} // namespace

void ArchiveFormat::writeHeader(std::ostream& output, const ArchiveHeader& header) {
    output.write(header.magic.data(), static_cast<std::streamsize>(header.magic.size()));
    writeU16(output, header.version);
    writeU16(output, header.flags);
    writeU32(output, header.kdfIterations);
    output.write(reinterpret_cast<const char*>(header.salt.data()), static_cast<std::streamsize>(header.salt.size()));
    output.write(reinterpret_cast<const char*>(header.nonce.data()), static_cast<std::streamsize>(header.nonce.size()));
    writeU64(output, header.encryptedSize);
    output.write(reinterpret_cast<const char*>(header.authTag.data()), static_cast<std::streamsize>(header.authTag.size()));
    if (!output) {
        throw ArchiveError("Failed to write archive header.");
    }
}

ArchiveHeader ArchiveFormat::readHeader(std::istream& input) {
    ArchiveHeader header{};
    input.read(header.magic.data(), static_cast<std::streamsize>(header.magic.size()));
    header.version = readU16(input);
    header.flags = readU16(input);
    header.kdfIterations = readU32(input);
    input.read(reinterpret_cast<char*>(header.salt.data()), static_cast<std::streamsize>(header.salt.size()));
    input.read(reinterpret_cast<char*>(header.nonce.data()), static_cast<std::streamsize>(header.nonce.size()));
    header.encryptedSize = readU64(input);
    input.read(reinterpret_cast<char*>(header.authTag.data()), static_cast<std::streamsize>(header.authTag.size()));

    if (!input || header.magic != Magic) {
        throw ArchiveError("This is not a .cryptsh archive or the header is corrupted.");
    }
    if (header.version != Version) {
        throw ArchiveError("Unsupported .cryptsh archive version. Recreate the archive with the current CryptoArchive version.");
    }
    return header;
}

void ArchiveFormat::writeInnerMagic(std::ostream& output) {
    output.write(InnerMagic.data(), static_cast<std::streamsize>(InnerMagic.size()));
}

void ArchiveFormat::verifyInnerMagic(std::istream& input) {
    std::array<char, 8> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!input || magic != InnerMagic) {
        throw ArchiveError("Archive payload is corrupted or the password is incorrect.");
    }
}

void ArchiveFormat::writeU8(std::ostream& output, std::uint8_t value) {
    output.put(static_cast<char>(value));
    if (!output) {
        throw ArchiveError("Failed to write archive byte.");
    }
}

void ArchiveFormat::writeU16(std::ostream& output, std::uint16_t value) {
    writeLittleEndian(output, value);
}

void ArchiveFormat::writeU32(std::ostream& output, std::uint32_t value) {
    writeLittleEndian(output, value);
}

void ArchiveFormat::writeU64(std::ostream& output, std::uint64_t value) {
    writeLittleEndian(output, value);
}

void ArchiveFormat::writeI64(std::ostream& output, std::int64_t value) {
    writeU64(output, static_cast<std::uint64_t>(value));
}

std::uint8_t ArchiveFormat::readU8(std::istream& input) {
    const int byte = input.get();
    if (byte == EOF) {
        throw ArchiveError("Unexpected end of archive.");
    }
    return static_cast<std::uint8_t>(byte);
}

std::uint16_t ArchiveFormat::readU16(std::istream& input) {
    return readLittleEndian<std::uint16_t>(input);
}

std::uint32_t ArchiveFormat::readU32(std::istream& input) {
    return readLittleEndian<std::uint32_t>(input);
}

std::uint64_t ArchiveFormat::readU64(std::istream& input) {
    return readLittleEndian<std::uint64_t>(input);
}

std::int64_t ArchiveFormat::readI64(std::istream& input) {
    return static_cast<std::int64_t>(readU64(input));
}

std::filesystem::path ArchiveFormat::makeTempPath(const std::string& suffix) {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::random_device random;
    return std::filesystem::temp_directory_path()
        / ("cryptsh_" + std::to_string(now) + "_" + std::to_string(random()) + suffix);
}

} 
