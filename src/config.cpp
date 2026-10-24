#include "config.hpp"

#include <Windows.h>
#include <Lm.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace backup_service {
namespace {

[[nodiscard]] std::wstring trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::wstring to_wide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        throw std::runtime_error("Configuration is not valid UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), size) <= 0) {
        throw std::runtime_error("Could not decode UTF-8 configuration value");
    }
    return result;
}

[[nodiscard]] bool equals_ordinal_ignore_case(std::wstring_view left, std::wstring_view right) {
    if (left.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        right.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

[[nodiscard]] bool is_local_server(std::wstring_view server) {
    if (equals_ordinal_ignore_case(server, L"localhost") ||
        equals_ordinal_ignore_case(server, L".") ||
        equals_ordinal_ignore_case(server, L"127.0.0.1") ||
        equals_ordinal_ignore_case(server, L"::1")) {
        return true;
    }

    std::array<wchar_t, MAX_COMPUTERNAME_LENGTH + 1U> computer_name{};
    DWORD computer_name_length = static_cast<DWORD>(computer_name.size());
    return GetComputerNameW(computer_name.data(), &computer_name_length) != FALSE &&
           equals_ordinal_ignore_case(
               server, std::wstring_view(computer_name.data(), computer_name_length));
}

[[nodiscard]] std::filesystem::path resolve_local_unc_path(const std::filesystem::path& path) {
    const std::wstring value = path.native();
    if (value.size() < 5U || value[0] != L'\\' || value[1] != L'\\' || value[2] == L'?' || value[2] == L'.') {
        return path;
    }

    const auto server_end = value.find_first_of(L"\\/", 2U);
    if (server_end == std::wstring::npos || server_end == 2U) {
        return path;
    }
    const auto share_begin = server_end + 1U;
    if (share_begin >= value.size() || value[share_begin] == L'\\' || value[share_begin] == L'/') {
        return path;
    }
    const auto share_end = value.find_first_of(L"\\/", share_begin);
    const auto server = std::wstring_view(value).substr(2U, server_end - 2U);
    if (!is_local_server(server)) {
        return path;
    }

    const auto share_length = (share_end == std::wstring::npos ? value.size() : share_end) - share_begin;
    std::wstring share_name = value.substr(share_begin, share_length);
    SHARE_INFO_2* share_info = nullptr;
    const NET_API_STATUS status = NetShareGetInfo(nullptr, share_name.data(), 2U,
                                                  reinterpret_cast<LPBYTE*>(&share_info));
    if (status != NERR_Success) {
        throw std::system_error(static_cast<int>(status), std::system_category(),
                                "Cannot resolve local UNC share");
    }

    const std::wstring local_root = share_info->shi2_path == nullptr ? L"" : share_info->shi2_path;
    NetApiBufferFree(share_info);
    if (local_root.empty()) {
        throw std::runtime_error("Local UNC share does not map to a filesystem path");
    }

    std::filesystem::path resolved(local_root);
    if (share_end != std::wstring::npos && share_end + 1U < value.size()) {
        resolved /= std::filesystem::path(value.substr(share_end + 1U));
    }
    return resolved.lexically_normal();
}

} // namespace

configuration load_configuration(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open configuration file: " + path.string());
    }

    configuration result;
    bool source_set = false;
    bool archive_set = false;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.front() == '\xEF') {
            if (line.size() < 3U || static_cast<unsigned char>(line[1]) != 0xBBU ||
                static_cast<unsigned char>(line[2]) != 0xBFU) {
                throw std::runtime_error("Invalid UTF-8 byte order mark");
            }
            line.erase(0, 3);
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const auto key = trim(to_wide(line.substr(0, separator)));
        const auto value = trim(to_wide(line.substr(separator + 1U)));
        if (key == L"source_directory") {
            result.source_directory = value;
            source_set = true;
        } else if (key == L"archive_path") {
            result.archive_path = value;
            archive_set = true;
        } else if (key == L"patterns") {
            std::size_t begin = 0;
            while (begin <= value.size()) {
                const auto end = value.find(L';', begin);
                const auto pattern = trim(value.substr(begin, end == std::wstring::npos
                                                                ? std::wstring::npos
                                                                : end - begin));
                if (!pattern.empty()) {
                    result.patterns.push_back(pattern);
                }
                if (end == std::wstring::npos) {
                    break;
                }
                begin = end + 1U;
            }
        } else if (key == L"interval_seconds") {
            const auto parsed = std::stoul(value);
            if (parsed == 0U || parsed > 86400U) {
                throw std::runtime_error("interval_seconds must be between 1 and 86400");
            }
            result.interval_seconds = static_cast<unsigned int>(parsed);
        }
    }

    if (!source_set || !archive_set || result.patterns.empty()) {
        throw std::runtime_error("Configuration requires source_directory, archive_path, and patterns");
    }
    if (result.source_directory.empty() || result.archive_path.empty()) {
        throw std::runtime_error("Configured paths cannot be empty");
    }
    result.source_directory = resolve_local_unc_path(result.source_directory);
    result.archive_path = resolve_local_unc_path(result.archive_path);
    return result;
}

bool wildcard_match(const std::wstring& pattern, const std::wstring& value) {
    std::size_t pattern_index = 0;
    std::size_t value_index = 0;
    std::size_t star_index = std::wstring::npos;
    std::size_t retry_index = 0;
    while (value_index < value.size()) {
        if (pattern_index < pattern.size() &&
            (pattern[pattern_index] == L'?' ||
             CompareStringOrdinal(&pattern[pattern_index], 1, &value[value_index], 1, TRUE) == CSTR_EQUAL)) {
            ++pattern_index;
            ++value_index;
        } else if (pattern_index < pattern.size() && pattern[pattern_index] == L'*') {
            star_index = pattern_index++;
            retry_index = value_index;
        } else if (star_index != std::wstring::npos) {
            pattern_index = star_index + 1U;
            value_index = ++retry_index;
        } else {
            return false;
        }
    }
    while (pattern_index < pattern.size() && pattern[pattern_index] == L'*') {
        ++pattern_index;
    }
    return pattern_index == pattern.size();
}

} // namespace backup_service
