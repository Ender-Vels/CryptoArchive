#include "cryptsh/core/ArchiveService.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    const auto root = std::filesystem::temp_directory_path() / "cryptsh_smoke_input";
    const auto out = std::filesystem::temp_directory_path() / "cryptsh_smoke_output";
    const auto archive = std::filesystem::temp_directory_path() / "cryptsh_smoke.cryptsh";

    std::filesystem::remove_all(root);
    std::filesystem::remove_all(out);
    std::filesystem::remove(archive);
    std::filesystem::create_directories(root / "sub");

    {
        std::ofstream file(root / "hello.txt", std::ios::binary);
        file << "hello hello hello hello hello hello hello\n";
    }
    {
        std::ofstream file(root / "sub" / "data.bin", std::ios::binary);
        for (int i = 0; i < 128 * 1024; ++i) {
            file.put(static_cast<char>('A' + (i % 3)));
        }
    }

    cryptsh::ArchiveService service;
    service.createArchive({root}, archive, "pass");
    const auto metadata = service.openArchive(archive, "pass");
    if (metadata.entries.empty()) {
        std::cerr << "metadata is empty\n";
        return 1;
    }
    service.extractArchive(archive, out, "pass");

    if (!std::filesystem::exists(out / "cryptsh_smoke_input" / "hello.txt") ||
        !std::filesystem::exists(out / "cryptsh_smoke_input" / "sub" / "data.bin")) {
        std::cerr << "extracted files are missing\n";
        return 2;
    }

    std::cout << "smoke ok\n";
    return 0;
}
