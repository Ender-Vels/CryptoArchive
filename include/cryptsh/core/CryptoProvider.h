#pragma once

#include "cryptsh/core/ArchiveFormat.h"

#include <filesystem>
#include <string>

namespace cryptsh {

struct SignatureInfo {
    bool present = false;
    bool valid = false;
    bool trusted = false;
    std::string author;
    std::string fingerprint;
};

class CryptoProvider {
public:
    ArchiveHeader encryptFile(const std::filesystem::path& inputPath,
                              const std::filesystem::path& archivePath,
                              const std::string& password,
                              int kdfIterations) const;

    void decryptFile(const std::filesystem::path& archivePath,
                     const std::filesystem::path& outputPath,
                     const std::string& password) const;

    void appendSignature(const std::filesystem::path& archivePath) const;
    bool verifySignature(const std::filesystem::path& archivePath) const;
    SignatureInfo signatureInfo(const std::filesystem::path& archivePath) const;
    void exportPublicKey(const std::filesystem::path& outputPath, const std::string& author) const;
    void importTrustedPublicKey(const std::filesystem::path& keyPath) const;
};

} // namespace cryptsh
