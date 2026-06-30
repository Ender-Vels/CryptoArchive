# CryptoArchive

CryptoArchive is a Windows desktop archive manager written in C++20 and Qt 6. It introduces the custom `.cryptsh` container format and combines per-file adaptive compression, authenticated encryption, integrity checks, digital signatures, and a familiar archive-browser interface.

The project was developed as a bachelor's qualification work focused on archive format design, binary serialization, lossless compression, cryptographic protection, streaming file processing, and desktop UI engineering.

## Highlights

- Creates and opens custom `.cryptsh` archives.
- Archives individual files, multiple selections, and complete directory trees.
- Preserves nested folders and relative paths.
- Uses per-file adaptive storage: Deflate is kept only when it reduces the file size; otherwise the original data is stored.
- Encrypts the internal archive payload with AES-256-GCM.
- Derives the encryption key from the user password with PBKDF2-SHA256.
- Adds CRC32 checks for individual archived files.
- Adds an Ed25519 digital signature and embeds author verification data.
- Supports public-key export and trusted-key import.
- Opens folders inside an archive and supports backward navigation and search.
- Extracts the complete archive or selected entries.
- Runs long create/extract operations in the background with progress reporting.
- Integrates with Windows Explorer through the installer: file association and **Add to CryptoArchive** context-menu action.

## Technology Stack

| Component | Technology |
|---|---|
| Language | C++20 |
| Desktop UI | Qt 6 Widgets |
| Background tasks | Qt Concurrent / `QFutureWatcher` |
| Compression | zlib / Deflate |
| Cryptography | OpenSSL |
| Encryption | AES-256-GCM |
| Password-based KDF | PBKDF2-SHA256 |
| Digital signature | Ed25519 |
| Integrity check | CRC32 and GCM authentication tag |
| Build system | CMake 3.20+ |
| Windows packaging | Inno Setup 6 |

## Architecture

The application is split into a Qt UI executable and a reusable static core library.

```text
MainWindow (Qt Widgets)
        |
        v
ArchiveService
   |          |
   v          v
ArchiveWriter ArchiveReader
   |          |
   +----+-----+
        |
        +-- ArchiveFormat      binary header and serialization
        +-- Compressor         zlib/Deflate file processing
        +-- CryptoProvider     encryption, signatures, key management
        +-- FileSystemUtils    traversal, paths, timestamps, CRC32
```

### Main components

- `ArchiveService` exposes high-level create, open, extract, selected-extract, and test operations.
- `ArchiveWriter` builds the internal container, chooses `Deflate` or `Stored` per file, encrypts the payload, and appends a signature.
- `ArchiveReader` decrypts the payload, reads archive metadata, validates entries, and restores selected or all files.
- `ArchiveFormat` owns the `.cryptsh` magic values, version, header encoding, and fixed-width integer serialization.
- `Compressor` performs streaming Deflate compression and decompression through zlib.
- `CryptoProvider` implements AES-256-GCM encryption, password-based key derivation, Ed25519 signatures, and trusted public keys.
- `FileSystemUtils` expands input directories, creates portable archive paths, calculates CRC32 values, and prevents extraction outside the selected destination.

## The `.cryptsh` Format

A `.cryptsh` file contains three logical sections:

```text
+-------------------------------+
| Archive header                |
| magic, version, KDF settings, |
| nonce, encrypted size, tag    |
+-------------------------------+
| AES-256-GCM encrypted payload |
| inner magic, entry metadata,  |
| Stored/Deflate file blocks    |
+-------------------------------+
| Ed25519 signature block       |
| author and public-key data    |
+-------------------------------+
```

Each file entry records its type, compression method, UTF-8 archive path, original size, stored size, modification time, and CRC32 value. Numeric fields are serialized explicitly instead of writing compiler-dependent C++ structures directly.

## Adaptive Compression

Compression is selected independently for every file:

1. Formats that are already compressed are stored directly.
2. Other files are temporarily compressed with Deflate.
3. The compressed and original sizes are compared.
4. Deflate is used only if the compressed result is smaller; otherwise the original file is stored.

This prevents video, image, document, or existing archive formats from growing due to ineffective recompression.

## Requirements

- Windows 10 or Windows 11, x64
- Visual Studio with the **Desktop development with C++** workload
- CMake 3.20 or newer
- Qt 6 with an MSVC x64 kit
- vcpkg
- OpenSSL (`x64-windows`)
- zlib (`x64-windows`)
- Inno Setup 6, only when building the installer

The checked-in `CMakePresets.json` currently expects:

```text
D:\vcpkg
D:\Qt\6.11.0\msvc2022_64
```

If your dependencies are installed elsewhere, update `CMakePresets.json` or pass equivalent CMake options manually.

## Install Dependencies with vcpkg

```cmd
cd /d D:\
git clone https://github.com/microsoft/vcpkg.git
cd D:\vcpkg
bootstrap-vcpkg.bat
vcpkg.exe install openssl:x64-windows zlib:x64-windows
```

Qt should be installed separately with the Qt Online Installer. Select a Qt 6 MSVC x64 kit compatible with your Visual Studio toolchain.

## Build with CMake

Using the included preset:

```cmd
cmake --preset vs2026-x64-vcpkg
cmake --build --preset vs2026-x64-vcpkg-release
```

The preset uses the Visual Studio 18 2026 generator. If your Visual Studio version differs, change the generator in `CMakePresets.json` or configure manually:

```cmd
cmake -S . -B build-vcpkg ^
  -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DCMAKE_PREFIX_PATH=D:/Qt/6.11.0/msvc2022_64

cmake --build build-vcpkg --config Release
```

The post-build step runs `windeployqt` and places the required Qt runtime files next to `CryptoArchive.exe`.

## Build in Visual Studio

1. Open Visual Studio.
2. Select **Open a local folder** and choose the repository directory.
3. Allow Visual Studio to configure the CMake project.
4. Select the desired x64 preset/configuration.
5. Build the `CryptoArchive` target.

If package discovery fails, verify the Qt and vcpkg paths in `CMakePresets.json` and delete the old CMake cache before configuring again.

## Run the Application

After a successful Release build, run:

```text
build-vcpkg\Release\CryptoArchive.exe
```

Typical workflow:

1. Select files or a folder.
2. Choose the `.cryptsh` output path.
3. Enter and confirm an archive password.
4. Wait for the non-blocking progress operation to finish.
5. Open the resulting archive, enter its password, browse its contents, and extract all or selected entries.

The password controls access to encrypted content. Public keys are used for author verification and are not decryption keys.

## Tests

The repository includes a core smoke test that creates an archive, reads its metadata, extracts it, and validates the restored data.

Build and run it with:

```cmd
cmake --build build-vcpkg --config Release --target cryptsh_smoke_test
build-vcpkg\Release\cryptsh_smoke_test.exe
```

## Windows Installer

The Inno Setup script is located at:

```text
packaging\CryptoArchive.iss
```

Before compiling it, build a Release package and ensure that the script's `SourceDir` points to the deployed application directory. Compile the script with Inno Setup 6 to produce the installer.

The installer registers the `.cryptsh` extension and the Windows Explorer context-menu command.

## Project Structure

```text
assets/                 application icon
include/cryptsh/core/   public core headers
include/cryptsh/ui/     Qt UI headers
packaging/              Inno Setup script
src/app/                application entry point and MainWindow
src/core/               archive, compression, crypto, and filesystem logic
tests/                  smoke test
CMakeLists.txt           build targets and dependencies
CMakePresets.json        local Visual Studio/vcpkg presets
```

## Security Notes

- Do not lose the archive password; the application does not contain a password-recovery mechanism.
- Exported public keys can be shared safely for signature verification. Never share the private signing key.
- The format is an educational custom container and has not undergone an independent security audit.
- Extract only archives received from trusted sources, even though destination-path validation is implemented.

## Current Scope

CryptoArchive currently uses zlib/Deflate. Zstandard, LZMA/LZMA2, archive recovery records, and richer preview support are possible future extensions.
