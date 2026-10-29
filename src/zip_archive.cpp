#include "zip_archive.hpp"

#include <Windows.h>
#include <zip.h>

#include <stdexcept>
#include <string>

namespace backup_service {
namespace {

[[nodiscard]] std::string to_utf8(const std::wstring& value) {
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0 && !value.empty()) {
        throw std::runtime_error("Could not encode path as UTF-8");
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    if (length > 0 && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), result.data(), length,
                                          nullptr, nullptr) <= 0) {
        throw std::runtime_error("Could not encode path as UTF-8");
    }
    return result;
}

class archive_handle {
public:
    explicit archive_handle(zip_t* archive) : archive_(archive) {}
    ~archive_handle() { if (archive_ != nullptr) zip_discard(archive_); }
    archive_handle(const archive_handle&) = delete;
    archive_handle& operator=(const archive_handle&) = delete;

    [[nodiscard]] zip_t* get() const { return archive_; }
    void close() {
        zip_t* archive = archive_;
        archive_ = nullptr;
        if (zip_close(archive) != 0) {
            const std::string message = zip_strerror(archive);
            zip_discard(archive);
            throw std::runtime_error("Could not update ZIP archive: " + message);
        }
    }

private:
    zip_t* archive_;
};

} // namespace

void update_zip_archive(const std::filesystem::path& archive_path,
                        const std::vector<zip_entry_source>& sources) {
    int error_code = 0;
    const auto archive_name = to_utf8(archive_path.wstring());
    archive_handle archive(zip_open(archive_name.c_str(), ZIP_CREATE, &error_code));
    if (archive.get() == nullptr) {
        zip_error_t error;
        zip_error_init_with_code(&error, error_code);
        const std::string message = zip_error_strerror(&error);
        zip_error_fini(&error);
        throw std::runtime_error("Could not open ZIP archive: " + message);
    }

    for (const auto& source_file : sources) {
        zip_source_t* source = zip_source_win32w(archive.get(), source_file.file_path.c_str(), 0, -1);
        if (source == nullptr) {
            throw std::runtime_error("Could not read source file for ZIP archive: " +
                                     source_file.file_path.string() + ": " + zip_strerror(archive.get()));
        }
        if (zip_file_add(archive.get(), source_file.archive_name.c_str(), source,
                         ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0) {
            zip_source_free(source);
            throw std::runtime_error("Could not add file to ZIP archive: " +
                                     source_file.archive_name + ": " + zip_strerror(archive.get()));
        }
    }

    archive.close();
}

} // namespace backup_service
