#include "shared/pak.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>

namespace nsync::shared::pak {
namespace {

void write_u32(std::ostream& output, std::uint32_t value) {
    char bytes[4] = {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8) & 0xffu),
        static_cast<char>((value >> 16) & 0xffu),
        static_cast<char>((value >> 24) & 0xffu),
    };
    output.write(bytes, sizeof(bytes));
}

void write_u64(std::ostream& output, std::uint64_t value) {
    char bytes[8]{};
    for (int i = 0; i < 8; ++i) {
        bytes[i] = static_cast<char>((value >> (i * 8)) & 0xffu);
    }
    output.write(bytes, sizeof(bytes));
}

bool read_bytes(std::istream& input, void* destination, std::size_t size) {
    input.read(static_cast<char*>(destination), static_cast<std::streamsize>(size));
    return input.good() || input.gcount() == static_cast<std::streamsize>(size);
}

bool read_u32(std::istream& input, std::uint32_t& value) {
    unsigned char bytes[4]{};
    if (!read_bytes(input, bytes, sizeof(bytes))) {
        return false;
    }
    value = static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) |
        (static_cast<std::uint32_t>(bytes[3]) << 24);
    return true;
}

bool read_u64(std::istream& input, std::uint64_t& value) {
    unsigned char bytes[8]{};
    if (!read_bytes(input, bytes, sizeof(bytes))) {
        return false;
    }
    value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8);
    }
    return true;
}

} // namespace

bool write_pak(
    const std::filesystem::path& source_dir,
    const std::filesystem::path& pak_path,
    std::string& error) {
    std::error_code fs_error;
    if (!std::filesystem::is_directory(source_dir, fs_error)) {
        error = "source directory does not exist: " + source_dir.string();
        return false;
    }

    const std::filesystem::path root =
        std::filesystem::weakly_canonical(source_dir, fs_error);
    if (fs_error) {
        error = "cannot resolve source directory: " + source_dir.string();
        return false;
    }

    std::vector<Entry> entries;
    for (std::filesystem::recursive_directory_iterator iterator(root, fs_error), end;
         iterator != end;
         iterator.increment(fs_error)) {
        if (fs_error) {
            error = "failed while scanning assets: " + fs_error.message();
            return false;
        }
        if (!iterator->is_regular_file(fs_error)) {
            continue;
        }

        std::error_code relative_error;
        const std::filesystem::path relative =
            std::filesystem::relative(iterator->path(), root, relative_error);
        if (relative_error) {
            error = "failed to compute relative path: " + iterator->path().string();
            return false;
        }

        Entry entry;
        entry.path = relative.generic_string();
        entry.size = iterator->file_size(fs_error);
        if (fs_error) {
            error = "failed to read file size: " + iterator->path().string();
            return false;
        }
        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.path < b.path;
    });

    std::uint64_t header_size = sizeof(kMagic) + sizeof(std::uint32_t) * 2;
    for (const Entry& entry : entries) {
        header_size += sizeof(std::uint32_t);
        header_size += entry.path.size();
        header_size += sizeof(std::uint64_t) * 2;
    }

    std::uint64_t offset = header_size;
    for (Entry& entry : entries) {
        entry.offset = offset;
        offset += entry.size;
    }

    if (!pak_path.parent_path().empty()) {
        std::filesystem::create_directories(pak_path.parent_path(), fs_error);
        if (fs_error) {
            error = "cannot create output directory: " + pak_path.parent_path().string();
            return false;
        }
    }

    std::ofstream output(pak_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot open pak for writing: " + pak_path.string();
        return false;
    }

    output.write(kMagic, sizeof(kMagic));
    write_u32(output, kVersion);
    write_u32(output, static_cast<std::uint32_t>(entries.size()));
    for (const Entry& entry : entries) {
        write_u32(output, static_cast<std::uint32_t>(entry.path.size()));
        output.write(entry.path.data(), static_cast<std::streamsize>(entry.path.size()));
        write_u64(output, entry.offset);
        write_u64(output, entry.size);
    }

    std::vector<char> buffer(64 * 1024);
    for (const Entry& entry : entries) {
        std::ifstream input(root / entry.path, std::ios::binary);
        if (!input) {
            error = "cannot open asset: " + entry.path;
            return false;
        }
        std::uint64_t remaining = entry.size;
        while (remaining > 0) {
            const std::size_t chunk = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, buffer.size()));
            input.read(buffer.data(), static_cast<std::streamsize>(chunk));
            if (input.gcount() != static_cast<std::streamsize>(chunk)) {
                error = "short read from asset: " + entry.path;
                return false;
            }
            output.write(buffer.data(), static_cast<std::streamsize>(chunk));
            remaining -= chunk;
        }
    }

    if (!output.good()) {
        error = "failed while writing pak: " + pak_path.string();
        return false;
    }
    return true;
}

bool read_pak_index(
    const std::filesystem::path& pak_path,
    Index& index,
    std::string& error) {
    std::ifstream input(pak_path, std::ios::binary);
    if (!input) {
        error = "cannot open pak: " + pak_path.string();
        return false;
    }

    char magic[sizeof(kMagic)]{};
    if (!read_bytes(input, magic, sizeof(magic)) ||
        std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        error = "invalid pak magic: " + pak_path.string();
        return false;
    }

    std::uint32_t version = 0;
    std::uint32_t count = 0;
    if (!read_u32(input, version) || version != kVersion) {
        error = "unsupported pak version: " + pak_path.string();
        return false;
    }
    if (!read_u32(input, count) || count > 1'000'000u) {
        error = "invalid pak entry count: " + pak_path.string();
        return false;
    }

    std::error_code fs_error;
    const std::uint64_t file_size =
        static_cast<std::uint64_t>(std::filesystem::file_size(pak_path, fs_error));
    if (fs_error) {
        error = "cannot read pak size: " + pak_path.string();
        return false;
    }

    index.entries.clear();
    index.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t path_size = 0;
        if (!read_u32(input, path_size) || path_size == 0 || path_size > 1'000'000u) {
            error = "invalid pak entry path: " + pak_path.string();
            return false;
        }

        Entry entry;
        entry.path.resize(path_size);
        if (!read_bytes(input, entry.path.data(), entry.path.size())) {
            error = "truncated pak entry path: " + pak_path.string();
            return false;
        }
        if (!read_u64(input, entry.offset) || !read_u64(input, entry.size)) {
            error = "truncated pak entry: " + pak_path.string();
            return false;
        }
        if (entry.offset > file_size ||
            entry.size > file_size - entry.offset) {
            error = "pak entry out of bounds: " + entry.path;
            return false;
        }
        index.entries.push_back(std::move(entry));
    }

    const std::streampos position = input.tellg();
    if (position < 0) {
        error = "invalid pak index position: " + pak_path.string();
        return false;
    }
    index.data_offset = static_cast<std::uint64_t>(position);
    for (const Entry& entry : index.entries) {
        if (entry.offset < index.data_offset) {
            error = "pak entry overlaps index: " + entry.path;
            return false;
        }
    }
    return true;
}

} // namespace nsync::shared::pak
