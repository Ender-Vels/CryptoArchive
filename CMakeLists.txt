cmake_minimum_required(VERSION 3.20)

project(CryptoArchive
    VERSION 0.2.0
    DESCRIPTION "Adaptive archive manager with cryptographic protection (.cryptsh)"
    LANGUAGES CXX RC
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

add_library(cryptsh_core STATIC
    src/core/ArchiveFormat.cpp
    src/core/ArchiveReader.cpp
    src/core/ArchiveService.cpp
    src/core/ArchiveWriter.cpp
    src/core/Compressor.cpp
    src/core/CryptoProvider.cpp
    src/core/FileSystemUtils.cpp
)

target_include_directories(cryptsh_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cryptsh_core
    PUBLIC
        OpenSSL::Crypto
        ZLIB::ZLIB
)

add_executable(CryptoArchive WIN32
    include/cryptsh/ui/MainWindow.h
    src/app/app.rc
    src/app/main.cpp
    src/app/MainWindow.cpp
    src/app/resources.qrc
)

target_link_libraries(CryptoArchive
    PRIVATE
        cryptsh_core
        Qt6::Widgets
        Qt6::Concurrent
)

add_executable(cryptsh_smoke_test
    tests/smoke.cpp
)

target_link_libraries(cryptsh_smoke_test
    PRIVATE
        cryptsh_core
)

if(WIN32)
    add_custom_command(TARGET CryptoArchive POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:CryptoArchive>
            $<TARGET_FILE_DIR:CryptoArchive>
        COMMAND_EXPAND_LISTS
    )

    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${QT_BIN_DIR}"
        REQUIRED
    )

    add_custom_command(TARGET CryptoArchive POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --no-translations
            --no-system-d3d-compiler
            --no-opengl-sw
            "$<TARGET_FILE:CryptoArchive>"
        COMMENT "Deploying Qt runtime files next to CryptoArchive.exe"
    )
endif()

install(TARGETS CryptoArchive RUNTIME DESTINATION bin)

set(CPACK_PACKAGE_NAME "CryptoArchive")
set(CPACK_PACKAGE_VENDOR "CryptoArchive")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Adaptive encrypted archive manager for .cryptsh files")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "CryptoArchive")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
endif()
set(CPACK_GENERATOR "ZIP")
include(CPack)
