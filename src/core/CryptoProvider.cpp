#include "cryptsh/core/CryptoProvider.h"

#include "cryptsh/core/ArchiveError.h"

#include <array>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <memory>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <sstream>
#include <vector>

namespace cryptsh {
namespace {

constexpr std::size_t BufferSize = 64 * 1024;
constexpr int KeySize = 32;
constexpr std::array<char, 8> SignatureMagic = {'C', 'S', 'H', 'S', 'I', 'G', '1', '\0'};
constexpr std::size_t Ed25519PublicKeySize = 32;
constexpr std::size_t Ed25519PrivateKeySize = 32;
constexpr std::size_t Ed25519SignatureSize = 64;

using EvpCipherContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
using EvpPkey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using EvpMdContext = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

std::array<unsigned char, KeySize> deriveKey(const std::string& password,
                                             const ArchiveHeader& header) {
    std::array<unsigned char, KeySize> key{};
    const int ok = PKCS5_PBKDF2_HMAC(
        password.data(),
        static_cast<int>(password.size()),
        header.salt.data(),
        static_cast<int>(header.salt.size()),
        static_cast<int>(header.kdfIterations),
        EVP_sha256(),
        static_cast<int>(key.size()),
        key.data());

    if (ok != 1) {
        throw ArchiveError("OpenSSL failed to derive encryption key.");
    }
    return key;
}

void fillRandom(unsigned char* data, std::size_t size) {
    if (RAND_bytes(data, static_cast<int>(size)) != 1) {
        throw ArchiveError("OpenSSL random generator failed.");
    }
}

std::filesystem::path keyPath() {
    const char* appData = std::getenv("APPDATA");
    const auto base = appData ? std::filesystem::path(appData) : std::filesystem::temp_directory_path();
    return base / "CryptoArchive" / "ed25519_signing.key";
}

std::filesystem::path trustedKeysDirectory() {
    const char* appData = std::getenv("APPDATA");
    const auto base = appData ? std::filesystem::path(appData) : std::filesystem::temp_directory_path();
    return base / "CryptoArchive" / "trusted_keys";
}

std::string hexEncode(const unsigned char* data, std::size_t size) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) {
        output << std::setw(2) << static_cast<int>(data[i]);
    }
    return output.str();
}

std::vector<unsigned char> hexDecode(const std::string& text) {
    if (text.size() % 2 != 0) {
        throw ArchiveError("Invalid public key file.");
    }
    std::vector<unsigned char> bytes;
    bytes.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const auto byteText = text.substr(i, 2);
        bytes.push_back(static_cast<unsigned char>(std::stoul(byteText, nullptr, 16)));
    }
    return bytes;
}

std::string fingerprintForPublicKey(const std::vector<unsigned char>& publicKey) {
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(publicKey.data(), publicKey.size(), digest.data());
    const std::string full = hexEncode(digest.data(), digest.size());
    std::ostringstream pretty;
    for (int i = 0; i < 16; i += 2) {
        if (i > 0) {
            pretty << ':';
        }
        pretty << full.substr(static_cast<std::size_t>(i), 2);
    }
    return pretty.str();
}

struct PublicKeyFile {
    std::string author;
    std::vector<unsigned char> publicKey;
};

PublicKeyFile readPublicKeyFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw ArchiveError("Cannot read public key file.");
    }

    PublicKeyFile key;
    std::string line;
    std::getline(input, line);
    if (line != "CRYPTSH-PUBLIC-KEY") {
        throw ArchiveError("Unsupported public key file.");
    }

    while (std::getline(input, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const auto name = line.substr(0, eq);
        const auto value = line.substr(eq + 1);
        if (name == "author") {
            key.author = value;
        } else if (name == "publicKeyHex") {
            key.publicKey = hexDecode(value);
        }
    }

    if (key.author.empty() || key.publicKey.size() != Ed25519PublicKeySize) {
        throw ArchiveError("Public key file is incomplete.");
    }
    return key;
}

void writePublicKeyFile(const std::filesystem::path& path,
                        const std::string& author,
                        const std::vector<unsigned char>& publicKey) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        throw ArchiveError("Cannot write public key file: " + path.string());
    }
    output << "CRYPTSH-PUBLIC-KEY\n";
    output << "author=" << author << "\n";
    output << "algorithm=Ed25519\n";
    output << "publicKeyHex=" << hexEncode(publicKey.data(), publicKey.size()) << "\n";
    output << "fingerprint=" << fingerprintForPublicKey(publicKey) << "\n";
}

std::array<unsigned char, Ed25519PrivateKeySize> loadOrCreatePrivateKey() {
    const auto path = keyPath();
    std::array<unsigned char, Ed25519PrivateKeySize> key{};
    if (std::filesystem::exists(path)) {
        std::ifstream input(path, std::ios::binary);
        input.read(reinterpret_cast<char*>(key.data()), static_cast<std::streamsize>(key.size()));
        if (input.gcount() == static_cast<std::streamsize>(key.size())) {
            return key;
        }
    }

    std::filesystem::create_directories(path.parent_path());
    fillRandom(key.data(), key.size());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(key.data()), static_cast<std::streamsize>(key.size()));
    return key;
}

std::vector<unsigned char> publicKeyFromPrivate(const std::array<unsigned char, Ed25519PrivateKeySize>& privateKey) {
    EvpPkey key(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, privateKey.data(), privateKey.size()), EVP_PKEY_free);
    if (!key) {
        throw ArchiveError("OpenSSL failed to create Ed25519 private key.");
    }

    std::vector<unsigned char> publicKey(Ed25519PublicKeySize);
    std::size_t publicKeySize = publicKey.size();
    if (EVP_PKEY_get_raw_public_key(key.get(), publicKey.data(), &publicKeySize) != 1 || publicKeySize != publicKey.size()) {
        throw ArchiveError("OpenSSL failed to export Ed25519 public key.");
    }
    return publicKey;
}

ArchiveHeader readArchiveHeaderOnly(const std::filesystem::path& archivePath) {
    std::ifstream input(archivePath, std::ios::binary);
    if (!input) {
        throw ArchiveError("Cannot open archive for signature processing.");
    }
    return ArchiveFormat::readHeader(input);
}

std::uint64_t signedByteCount(const std::filesystem::path& archivePath) {
    const auto header = readArchiveHeaderOnly(archivePath);
    return 68 + header.encryptedSize;
}

std::array<unsigned char, SHA256_DIGEST_LENGTH> sha256Prefix(const std::filesystem::path& path, std::uint64_t bytesToHash) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw ArchiveError("Cannot read archive for SHA-256.");
    }

    SHA256_CTX ctx{};
    SHA256_Init(&ctx);
    std::array<unsigned char, BufferSize> buffer{};
    std::uint64_t remaining = bytesToHash;
    while (remaining > 0) {
        const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(buffer.size(), remaining));
        input.read(reinterpret_cast<char*>(buffer.data()), chunk);
        if (input.gcount() != chunk) {
            throw ArchiveError("Archive is truncated before signature boundary.");
        }
        SHA256_Update(&ctx, buffer.data(), static_cast<std::size_t>(chunk));
        remaining -= static_cast<std::uint64_t>(chunk);
    }

    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256_Final(digest.data(), &ctx);
    return digest;
}

std::vector<unsigned char> signDigest(const std::array<unsigned char, SHA256_DIGEST_LENGTH>& digest,
                                      const std::array<unsigned char, Ed25519PrivateKeySize>& privateKey) {
    EvpPkey key(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, privateKey.data(), privateKey.size()), EVP_PKEY_free);
    EvpMdContext ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!key || !ctx || EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) != 1) {
        throw ArchiveError("OpenSSL failed to initialize Ed25519 signing.");
    }

    std::vector<unsigned char> signature(Ed25519SignatureSize);
    std::size_t signatureSize = signature.size();
    if (EVP_DigestSign(ctx.get(), signature.data(), &signatureSize, digest.data(), digest.size()) != 1) {
        throw ArchiveError("OpenSSL failed to sign archive digest.");
    }
    signature.resize(signatureSize);
    return signature;
}

bool verifyDigestSignature(const std::array<unsigned char, SHA256_DIGEST_LENGTH>& digest,
                           const std::vector<unsigned char>& publicKey,
                           const std::vector<unsigned char>& signature) {
    EvpPkey key(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, publicKey.data(), publicKey.size()), EVP_PKEY_free);
    EvpMdContext ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!key || !ctx || EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) != 1) {
        throw ArchiveError("OpenSSL failed to initialize Ed25519 verification.");
    }
    return EVP_DigestVerify(ctx.get(), signature.data(), signature.size(), digest.data(), digest.size()) == 1;
}

struct SignatureBlock {
    bool present = false;
    std::vector<unsigned char> publicKey;
    std::vector<unsigned char> signature;
};

SignatureBlock readSignatureBlock(const std::filesystem::path& archivePath) {
    const auto bytesToSign = signedByteCount(archivePath);
    const auto fileSize = std::filesystem::file_size(archivePath);
    if (fileSize == bytesToSign) {
        return {};
    }

    std::ifstream input(archivePath, std::ios::binary);
    if (!input) {
        throw ArchiveError("Cannot read archive signature.");
    }
    input.seekg(static_cast<std::streamoff>(bytesToSign));

    std::array<char, 8> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!input || magic != SignatureMagic) {
        return {};
    }

    const auto publicKeySize = ArchiveFormat::readU32(input);
    const auto signatureSize = ArchiveFormat::readU32(input);
    if (publicKeySize != Ed25519PublicKeySize || signatureSize != Ed25519SignatureSize) {
        return {};
    }

    SignatureBlock block;
    block.present = true;
    block.publicKey.resize(publicKeySize);
    block.signature.resize(signatureSize);
    input.read(reinterpret_cast<char*>(block.publicKey.data()), static_cast<std::streamsize>(block.publicKey.size()));
    input.read(reinterpret_cast<char*>(block.signature.data()), static_cast<std::streamsize>(block.signature.size()));
    if (!input) {
        return {};
    }
    return block;
}

std::string trustedAuthorFor(const std::vector<unsigned char>& publicKey) {
    const auto trustedDir = trustedKeysDirectory();
    if (!std::filesystem::exists(trustedDir)) {
        return {};
    }

    for (const auto& item : std::filesystem::directory_iterator(trustedDir)) {
        if (!item.is_regular_file() || item.path().extension() != ".cshpub") {
            continue;
        }
        try {
            const auto key = readPublicKeyFile(item.path());
            if (key.publicKey == publicKey) {
                return key.author;
            }
        } catch (...) {
            continue;
        }
    }
    return {};
}

} // namespace

ArchiveHeader CryptoProvider::encryptFile(const std::filesystem::path& inputPath,
                                          const std::filesystem::path& archivePath,
                                          const std::string& password,
                                          int kdfIterations) const {
    ArchiveHeader header{};
    header.magic = ArchiveFormat::Magic;
    header.version = ArchiveFormat::Version;
    header.kdfIterations = static_cast<std::uint32_t>(kdfIterations);
    fillRandom(header.salt.data(), header.salt.size());
    fillRandom(header.nonce.data(), header.nonce.size());

    const auto key = deriveKey(password, header);
    std::ifstream input(inputPath, std::ios::binary);
    std::ofstream output(archivePath, std::ios::binary);
    if (!input || !output) {
        throw ArchiveError("Cannot open files for encryption.");
    }

    ArchiveFormat::writeHeader(output, header);

    EvpCipherContext ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx ||
        EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(header.nonce.size()), nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), header.nonce.data()) != 1) {
        throw ArchiveError("OpenSSL failed to initialize AES-256-GCM encryption.");
    }

    std::array<unsigned char, BufferSize> inBuffer{};
    std::array<unsigned char, BufferSize + EVP_MAX_BLOCK_LENGTH> outBuffer{};
    std::uint64_t encryptedSize = 0;

    while (input) {
        input.read(reinterpret_cast<char*>(inBuffer.data()), static_cast<std::streamsize>(inBuffer.size()));
        const int readBytes = static_cast<int>(input.gcount());
        if (readBytes == 0) {
            break;
        }

        int outBytes = 0;
        if (EVP_EncryptUpdate(ctx.get(), outBuffer.data(), &outBytes, inBuffer.data(), readBytes) != 1) {
            throw ArchiveError("OpenSSL encryption failed.");
        }
        output.write(reinterpret_cast<const char*>(outBuffer.data()), outBytes);
        encryptedSize += static_cast<std::uint64_t>(outBytes);
    }

    int finalBytes = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), outBuffer.data(), &finalBytes) != 1) {
        throw ArchiveError("OpenSSL failed to finalize encryption.");
    }
    output.write(reinterpret_cast<const char*>(outBuffer.data()), finalBytes);
    encryptedSize += static_cast<std::uint64_t>(finalBytes);

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(header.authTag.size()), header.authTag.data()) != 1) {
        throw ArchiveError("OpenSSL failed to produce authentication tag.");
    }

    header.encryptedSize = encryptedSize;
    output.seekp(0);
    ArchiveFormat::writeHeader(output, header);
    return header;
}

void CryptoProvider::decryptFile(const std::filesystem::path& archivePath,
                                 const std::filesystem::path& outputPath,
                                 const std::string& password) const {
    if (!verifySignature(archivePath)) {
        throw ArchiveError("Digital signature verification failed.");
    }

    std::ifstream input(archivePath, std::ios::binary);
    std::ofstream output(outputPath, std::ios::binary);
    if (!input || !output) {
        throw ArchiveError("Cannot open files for decryption.");
    }

    const ArchiveHeader header = ArchiveFormat::readHeader(input);
    const auto key = deriveKey(password, header);

    EvpCipherContext ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx ||
        EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(header.nonce.size()), nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), header.nonce.data()) != 1) {
        throw ArchiveError("OpenSSL failed to initialize AES-256-GCM decryption.");
    }

    std::array<unsigned char, BufferSize> inBuffer{};
    std::array<unsigned char, BufferSize + EVP_MAX_BLOCK_LENGTH> outBuffer{};
    std::uint64_t remaining = header.encryptedSize;

    while (remaining > 0) {
        const auto toRead = static_cast<std::streamsize>(std::min<std::uint64_t>(inBuffer.size(), remaining));
        input.read(reinterpret_cast<char*>(inBuffer.data()), toRead);
        const int readBytes = static_cast<int>(input.gcount());
        if (readBytes <= 0) {
            throw ArchiveError("Encrypted archive payload is truncated.");
        }
        remaining -= static_cast<std::uint64_t>(readBytes);

        int outBytes = 0;
        if (EVP_DecryptUpdate(ctx.get(), outBuffer.data(), &outBytes, inBuffer.data(), readBytes) != 1) {
            throw ArchiveError("OpenSSL decryption failed.");
        }
        output.write(reinterpret_cast<const char*>(outBuffer.data()), outBytes);
    }

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(header.authTag.size()), const_cast<unsigned char*>(header.authTag.data())) != 1) {
        throw ArchiveError("OpenSSL failed to set authentication tag.");
    }

    int finalBytes = 0;
    if (EVP_DecryptFinal_ex(ctx.get(), outBuffer.data(), &finalBytes) != 1) {
        throw ArchiveError("Wrong password or archive integrity check failed.");
    }
    output.write(reinterpret_cast<const char*>(outBuffer.data()), finalBytes);
}

void CryptoProvider::appendSignature(const std::filesystem::path& archivePath) const {
    const auto bytesToSign = signedByteCount(archivePath);
    std::filesystem::resize_file(archivePath, bytesToSign);

    const auto privateKey = loadOrCreatePrivateKey();
    const auto publicKey = publicKeyFromPrivate(privateKey);
    const auto digest = sha256Prefix(archivePath, bytesToSign);
    const auto signature = signDigest(digest, privateKey);

    std::ofstream output(archivePath, std::ios::binary | std::ios::app);
    if (!output) {
        throw ArchiveError("Cannot append digital signature to archive.");
    }

    output.write(SignatureMagic.data(), static_cast<std::streamsize>(SignatureMagic.size()));
    ArchiveFormat::writeU32(output, static_cast<std::uint32_t>(publicKey.size()));
    ArchiveFormat::writeU32(output, static_cast<std::uint32_t>(signature.size()));
    output.write(reinterpret_cast<const char*>(publicKey.data()), static_cast<std::streamsize>(publicKey.size()));
    output.write(reinterpret_cast<const char*>(signature.data()), static_cast<std::streamsize>(signature.size()));
}

bool CryptoProvider::verifySignature(const std::filesystem::path& archivePath) const {
    const auto bytesToSign = signedByteCount(archivePath);
    const auto block = readSignatureBlock(archivePath);
    if (!block.present) {
        return true;
    }

    const auto digest = sha256Prefix(archivePath, bytesToSign);
    return verifyDigestSignature(digest, block.publicKey, block.signature);
}

SignatureInfo CryptoProvider::signatureInfo(const std::filesystem::path& archivePath) const {
    SignatureInfo info;
    const auto bytesToSign = signedByteCount(archivePath);
    const auto block = readSignatureBlock(archivePath);
    info.present = block.present;
    if (!block.present) {
        return info;
    }

    const auto digest = sha256Prefix(archivePath, bytesToSign);
    info.valid = verifyDigestSignature(digest, block.publicKey, block.signature);
    info.fingerprint = fingerprintForPublicKey(block.publicKey);
    if (info.valid) {
        info.author = trustedAuthorFor(block.publicKey);
        info.trusted = !info.author.empty();
    }
    return info;
}

void CryptoProvider::exportPublicKey(const std::filesystem::path& outputPath, const std::string& author) const {
    if (author.empty()) {
        throw ArchiveError("Author name cannot be empty.");
    }
    const auto privateKey = loadOrCreatePrivateKey();
    const auto publicKey = publicKeyFromPrivate(privateKey);
    writePublicKeyFile(outputPath, author, publicKey);
}

void CryptoProvider::importTrustedPublicKey(const std::filesystem::path& sourcePath) const {
    const auto key = readPublicKeyFile(sourcePath);
    const auto trustedDir = trustedKeysDirectory();
    std::filesystem::create_directories(trustedDir);

    std::string safeFingerprint = fingerprintForPublicKey(key.publicKey);
    std::replace(safeFingerprint.begin(), safeFingerprint.end(), ':', '-');
    const auto target = trustedDir / ("trusted_" + safeFingerprint + ".cshpub");
    writePublicKeyFile(target, key.author, key.publicKey);
}

} 