#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace cryptsh {

struct ArchiveHeader {
    std::array<char, 8> magic{};
    std::uint16_t version = 1;
    std::uint16_t flags = 0;
    std::uint32_t kdfIterations = 0;
    std::array<unsigned char, 16> salt{};
    std::array<unsigned char, 12> nonce{};
    std::uint64_t encryptedSize = 0;
    std::array<unsigned char, 16> authTag{};
};

class ArchiveFormat {
public:
    static constexpr std::array<char, 8> Magic = {'C', 'R', 'Y', 'P', 'T', 'S', 'H', '\0'};
    static constexpr std::array<char, 8> InnerMagic = {'C', 'S', 'H', 'D', 'A', 'T', 'A', '\0'};
    static constexpr std::uint16_t Version = 2;

    static void writeHeader(std::ostream& output, const ArchiveHeader& header);
    static ArchiveHeader readHeader(std::istream& input);

    static void writeInnerMagic(std::ostream& output);
    static void verifyInnerMagic(std::istream& input);

    static void writeU8(std::ostream& output, std::uint8_t value);
    static void writeU16(std::ostream& output, std::uint16_t value);
    static void writeU32(std::ostream& output, std::uint32_t value);
    static void writeU64(std::ostream& output, std::uint64_t value);
    static void writeI64(std::ostream& output, std::int64_t value);

    static std::uint8_t readU8(std::istream& input);
    static std::uint16_t readU16(std::istream& input);
    static std::uint32_t readU32(std::istream& input);
    static std::uint64_t readU64(std::istream& input);
    static std::int64_t readI64(std::istream& input);

    static std::filesystem::path makeTempPath(const std::string& suffix);
};

} // namespace cryptsh
