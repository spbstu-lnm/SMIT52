#include "zip_archive.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace backup_service {
namespace {

struct central_entry {
    std::string name;
    std::uint32_t crc{};
    std::uint32_t size{};
    std::uint32_t offset{};
};

void write_u16(std::ostream& output, std::uint16_t value) {
    const std::array<char, 2> bytes{static_cast<char>(value & 0xFFU),
                                    static_cast<char>((value >> 8U) & 0xFFU)};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_u32(std::ostream& output, std::uint32_t value) {
    write_u16(output, static_cast<std::uint16_t>(value & 0xFFFFU));
    write_u16(output, static_cast<std::uint16_t>(value >> 16U));
}

[[nodiscard]] std::uint32_t crc32(const unsigned char* data, std::size_t size, std::uint32_t crc) {
    crc = ~crc;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

[[nodiscard]] std::uint32_t checked_u32(std::uint64_t value, const char* description) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(std::string(description) + " exceeds classic ZIP limits");
    }
    return static_cast<std::uint32_t>(value);
}

} // namespace

void write_zip_archive(const std::filesystem::path& archive_path,
                       const std::vector<zip_entry_source>& sources) {
    std::ofstream output(archive_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Cannot create archive: " + archive_path.string());
    }

    std::vector<central_entry> entries;
    entries.reserve(sources.size());
    std::array<unsigned char, 64U * 1024U> buffer{};
    for (const auto& source : sources) {
        if (source.archive_name.size() > std::numeric_limits<std::uint16_t>::max()) {
            throw std::runtime_error("Archive entry name is too long");
        }
        std::ifstream input(source.file_path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("Cannot read source file: " + source.file_path.string());
        }
        const auto offset = checked_u32(static_cast<std::uint64_t>(output.tellp()), "Archive offset");
        write_u32(output, 0x04034B50U);
        write_u16(output, 20U);
        write_u16(output, 0x0808U); // UTF-8 names and data descriptor follow the file data.
        write_u16(output, 0U); // Stored, without compression.
        write_u16(output, 0U);
        write_u16(output, 0x0021U); // 1980-01-01, the earliest DOS ZIP date.
        write_u32(output, 0U);
        write_u32(output, 0U);
        write_u32(output, 0U);
        write_u16(output, static_cast<std::uint16_t>(source.archive_name.size()));
        write_u16(output, 0U);
        output.write(source.archive_name.data(), static_cast<std::streamsize>(source.archive_name.size()));

        std::uint32_t checksum = 0U;
        std::uint64_t size = 0U;
        while (input) {
            input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
            const auto count = input.gcount();
            if (count <= 0) {
                break;
            }
            const auto chunk_size = static_cast<std::size_t>(count);
            checksum = crc32(buffer.data(), chunk_size, checksum);
            size += static_cast<std::uint64_t>(chunk_size);
            if (size > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error("Source file exceeds classic ZIP 4 GiB limit");
            }
            output.write(reinterpret_cast<const char*>(buffer.data()), count);
        }
        if (!input.eof() || !output) {
            throw std::runtime_error("Failed while reading or writing archive data");
        }
        const auto checked_size = checked_u32(size, "Source file size");
        write_u32(output, 0x08074B50U);
        write_u32(output, checksum);
        write_u32(output, checked_size);
        write_u32(output, checked_size);
        entries.push_back({source.archive_name, checksum, checked_size, offset});
    }

    const auto central_offset = checked_u32(static_cast<std::uint64_t>(output.tellp()), "Central directory offset");
    for (const auto& entry : entries) {
        write_u32(output, 0x02014B50U);
        write_u16(output, 20U);
        write_u16(output, 20U);
        write_u16(output, 0x0808U);
        write_u16(output, 0U);
        write_u16(output, 0x0021U);
        write_u16(output, 0U);
        write_u32(output, entry.crc);
        write_u32(output, entry.size);
        write_u32(output, entry.size);
        write_u16(output, static_cast<std::uint16_t>(entry.name.size()));
        write_u16(output, 0U);
        write_u16(output, 0U);
        write_u16(output, 0U);
        write_u16(output, 0U);
        write_u32(output, 0U);
        write_u32(output, entry.offset);
        output.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
    }

    const auto end_offset = checked_u32(static_cast<std::uint64_t>(output.tellp()), "Archive size");
    const auto central_size = end_offset - central_offset;
    if (entries.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error("Archive exceeds classic ZIP entry count limit");
    }
    write_u32(output, 0x06054B50U);
    write_u16(output, 0U);
    write_u16(output, 0U);
    write_u16(output, static_cast<std::uint16_t>(entries.size()));
    write_u16(output, static_cast<std::uint16_t>(entries.size()));
    write_u32(output, central_size);
    write_u32(output, central_offset);
    write_u16(output, 0U);
    if (!output) {
        throw std::runtime_error("Failed to finish ZIP archive");
    }
}

} // namespace backup_service
