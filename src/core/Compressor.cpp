#include "cryptsh/core/Compressor.h"

#include "cryptsh/core/ArchiveError.h"

#include <array>
#include <fstream>
#include <zlib.h>

namespace cryptsh {
namespace {

constexpr std::size_t BufferSize = 64 * 1024;

void checkZlib(int code, const char* operation) {
    if (code != Z_OK && code != Z_STREAM_END) {
        throw ArchiveError(std::string(operation) + " failed.");
    }
}

} // namespace

void Compressor::compressFile(const std::filesystem::path& inputPath,
                              const std::filesystem::path& outputPath,
                              int level) const {
    std::ifstream input(inputPath, std::ios::binary);
    std::ofstream output(outputPath, std::ios::binary);
    if (!input || !output) {
        throw ArchiveError("Cannot open files for compression.");
    }

    z_stream stream{};
    checkZlib(deflateInit2(&stream, level, Z_DEFLATED, 15, 9, Z_DEFAULT_STRATEGY), "deflateInit2");

    std::array<unsigned char, BufferSize> inBuffer{};
    std::array<unsigned char, BufferSize> outBuffer{};
    int flush = Z_NO_FLUSH;

    do {
        input.read(reinterpret_cast<char*>(inBuffer.data()), static_cast<std::streamsize>(inBuffer.size()));
        stream.avail_in = static_cast<uInt>(input.gcount());
        stream.next_in = inBuffer.data();
        flush = input.eof() ? Z_FINISH : Z_NO_FLUSH;

        do {
            stream.avail_out = static_cast<uInt>(outBuffer.size());
            stream.next_out = outBuffer.data();
            checkZlib(deflate(&stream, flush), "deflate");
            const auto produced = outBuffer.size() - stream.avail_out;
            output.write(reinterpret_cast<const char*>(outBuffer.data()), static_cast<std::streamsize>(produced));
        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);

    deflateEnd(&stream);
}

void Compressor::decompressFile(const std::filesystem::path& inputPath,
                                const std::filesystem::path& outputPath) const {
    std::ifstream input(inputPath, std::ios::binary);
    std::ofstream output(outputPath, std::ios::binary);
    if (!input || !output) {
        throw ArchiveError("Cannot open files for decompression.");
    }

    z_stream stream{};
    checkZlib(inflateInit(&stream), "inflateInit");

    std::array<unsigned char, BufferSize> inBuffer{};
    std::array<unsigned char, BufferSize> outBuffer{};
    int result = Z_OK;

    while (result != Z_STREAM_END) {
        input.read(reinterpret_cast<char*>(inBuffer.data()), static_cast<std::streamsize>(inBuffer.size()));
        stream.avail_in = static_cast<uInt>(input.gcount());
        stream.next_in = inBuffer.data();
        if (stream.avail_in == 0) {
            break;
        }

        do {
            stream.avail_out = static_cast<uInt>(outBuffer.size());
            stream.next_out = outBuffer.data();
            result = inflate(&stream, Z_NO_FLUSH);
            checkZlib(result, "inflate");
            const auto produced = outBuffer.size() - stream.avail_out;
            output.write(reinterpret_cast<const char*>(outBuffer.data()), static_cast<std::streamsize>(produced));
        } while (stream.avail_out == 0);
    }

    inflateEnd(&stream);
    if (result != Z_STREAM_END) {
        throw ArchiveError("Compressed archive payload is truncated.");
    }
}

} 
