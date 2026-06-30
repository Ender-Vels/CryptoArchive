#pragma once

#include <filesystem>

namespace cryptsh {

class Compressor {
public:
    void compressFile(const std::filesystem::path& inputPath,
                      const std::filesystem::path& outputPath,
                      int level) const;

    void decompressFile(const std::filesystem::path& inputPath,
                        const std::filesystem::path& outputPath) const;
};

} // namespace cryptsh
