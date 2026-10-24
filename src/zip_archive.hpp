#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace backup_service {

struct zip_entry_source {
    std::filesystem::path file_path;
    std::string archive_name;
};

void write_zip_archive(const std::filesystem::path& archive_path,
                       const std::vector<zip_entry_source>& sources);

} // namespace backup_service
