#include "backup.hpp"

#include "zip_archive.hpp"

#include <Windows.h>

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <string>
#include <system_error>

namespace backup_service {
namespace {

[[nodiscard]] std::string to_utf8(const std::wstring& value) {
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0 && !value.empty()) {
        throw std::runtime_error("Could not encode archive path as UTF-8");
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    if (size > 0 && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                        static_cast<int>(value.size()), result.data(), size,
                                        nullptr, nullptr) <= 0) {
        throw std::runtime_error("Could not encode archive path as UTF-8");
    }
    return result;
}

[[nodiscard]] bool matches_configuration(const configuration& config, const std::wstring& name) {
    return std::ranges::any_of(config.patterns, [&name](const auto& pattern) {
        return wildcard_match(pattern, name);
    });
}

[[nodiscard]] std::filesystem::path normalized_path(const std::filesystem::path& path) {
    std::error_code error;
    auto result = std::filesystem::weakly_canonical(path, error);
    if (error) {
        result = std::filesystem::absolute(path, error).lexically_normal();
    }
    return result;
}

} // namespace

void create_backup(const configuration& config, const std::function<bool()>& should_stop) {
    if (!std::filesystem::is_directory(config.source_directory)) {
        throw std::runtime_error("Source directory does not exist or is not accessible");
    }

    const auto archive_path = normalized_path(config.archive_path);
    const std::filesystem::path temporary_path = archive_path.wstring() + L".tmp";
    std::vector<zip_entry_source> files;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        config.source_directory, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        throw std::system_error(error, "Cannot enumerate source directory");
    }

    for (; iterator != end; iterator.increment(error)) {
        if (should_stop()) {
            return;
        }
        if (error) {
            error.clear();
            continue;
        }
        const auto status = iterator->symlink_status(error);
        if (error) {
            error.clear();
            continue;
        }
        if (std::filesystem::is_symlink(status)) {
            if (iterator->is_directory(error)) {
                iterator.disable_recursion_pending();
            }
            error.clear();
            continue;
        }
        if (!std::filesystem::is_regular_file(status) ||
            normalized_path(iterator->path()) == archive_path ||
            normalized_path(iterator->path()) == temporary_path) {
            continue;
        }

        const auto filename = iterator->path().filename().wstring();
        if (!matches_configuration(config, filename)) {
            continue;
        }
        auto relative_name = std::filesystem::relative(iterator->path(), config.source_directory, error);
        if (error) {
            error.clear();
            continue;
        }
        std::wstring zip_name = relative_name.generic_wstring();
        std::ranges::replace(zip_name, L'\\', L'/');
        files.push_back({iterator->path(), to_utf8(zip_name)});
    }
    if (error) {
        throw std::system_error(error, "Cannot finish enumerating source directory");
    }
    if (should_stop()) {
        return;
    }

    std::ranges::sort(files, {}, &zip_entry_source::archive_name);
    const auto archive_directory = archive_path.parent_path();
    if (!archive_directory.empty()) {
        std::filesystem::create_directories(archive_directory);
    }
    try {
        write_zip_archive(temporary_path, files);
        if (!MoveFileExW(temporary_path.c_str(), archive_path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "Cannot replace backup archive");
        }
    } catch (...) {
        std::filesystem::remove(temporary_path, error);
        throw;
    }
}

} // namespace backup_service
