#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace backup_service {

struct configuration {
    std::filesystem::path source_directory;
    std::filesystem::path archive_path;
    std::vector<std::wstring> patterns;
    unsigned int interval_seconds{60};
};

[[nodiscard]] configuration load_configuration(const std::filesystem::path& path);
[[nodiscard]] bool wildcard_match(const std::wstring& pattern, const std::wstring& value);

} // namespace backup_service
