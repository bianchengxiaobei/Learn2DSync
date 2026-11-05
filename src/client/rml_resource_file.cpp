#include "client/rml_resource_file.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace nsync::client {
namespace {

bool read_filesystem_file(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>& data) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return false;
    }
    const std::streampos end = input.tellg();
    if (end < 0) {
        return false;
    }
    const auto size = static_cast<std::uint64_t>(end);
    if (size > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    data.resize(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    if (size > 0 &&
        !input.read(
            reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(size))) {
        return false;
    }
    return true;
}

} // namespace

RmlResourceFile::RmlResourceFile(shared::ResourceManager& resources)
    : resources_(resources) {}

Rml::FileHandle RmlResourceFile::Open(const Rml::String& path) {
    std::vector<std::uint8_t> data;
    if (!load(path, data)) {
        return 0;
    }

    auto* file = new MemoryFile{std::move(data), 0};
    return reinterpret_cast<Rml::FileHandle>(file);
}

void RmlResourceFile::Close(Rml::FileHandle file) {
    delete reinterpret_cast<MemoryFile*>(file);
}

std::size_t RmlResourceFile::Read(
    void* buffer,
    std::size_t size,
    Rml::FileHandle file) {
    auto* memory_file = reinterpret_cast<MemoryFile*>(file);
    if (memory_file == nullptr || memory_file->position >= memory_file->data.size()) {
        return 0;
    }

    const std::size_t available = memory_file->data.size() - memory_file->position;
    const std::size_t count = std::min(size, available);
    if (count > 0 && buffer != nullptr) {
        std::memcpy(
            buffer,
            memory_file->data.data() + memory_file->position,
            count);
    }
    memory_file->position += count;
    return count;
}

bool RmlResourceFile::Seek(Rml::FileHandle file, long offset, int origin) {
    auto* memory_file = reinterpret_cast<MemoryFile*>(file);
    if (memory_file == nullptr) {
        return false;
    }

    std::int64_t base = 0;
    switch (origin) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = static_cast<std::int64_t>(memory_file->position); break;
        case SEEK_END: base = static_cast<std::int64_t>(memory_file->data.size()); break;
        default: return false;
    }

    const std::int64_t position = base + static_cast<std::int64_t>(offset);
    if (position < 0 ||
        static_cast<std::uint64_t>(position) > memory_file->data.size()) {
        return false;
    }
    memory_file->position = static_cast<std::size_t>(position);
    return true;
}

std::size_t RmlResourceFile::Tell(Rml::FileHandle file) {
    auto* memory_file = reinterpret_cast<MemoryFile*>(file);
    return memory_file == nullptr ? 0 : memory_file->position;
}

std::size_t RmlResourceFile::Length(Rml::FileHandle file) {
    auto* memory_file = reinterpret_cast<MemoryFile*>(file);
    return memory_file == nullptr ? 0 : memory_file->data.size();
}

bool RmlResourceFile::LoadFile(const Rml::String& path, Rml::String& out_data) {
    const Rml::FileHandle file = Open(path);
    if (file == 0) {
        return false;
    }

    const std::size_t size = Length(file);
    out_data.resize(size);
    const std::size_t read = Read(out_data.data(), size, file);
    Close(file);
    return read == size;
}

bool RmlResourceFile::load(
    const std::string& path,
    std::vector<std::uint8_t>& data) const {
    if (auto resource = resources_.read(path)) {
        data = std::move(*resource);
        return true;
    }

    const std::filesystem::path filesystem_path(path);
    if (!filesystem_path.is_absolute()) {
        return false;
    }
    return read_filesystem_file(filesystem_path, data);
}

} // namespace nsync::client
