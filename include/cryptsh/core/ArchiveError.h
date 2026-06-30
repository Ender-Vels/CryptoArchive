#pragma once

#include <stdexcept>
#include <string>

namespace cryptsh {

class ArchiveError final : public std::runtime_error {
public:
    explicit ArchiveError(const std::string& message)
        : std::runtime_error(message) {}
};

} // namespace cryptsh
