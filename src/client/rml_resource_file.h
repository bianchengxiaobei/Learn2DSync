#pragma once

#include "shared/resource.h"

#include <RmlUi/Core/FileInterface.h>

#include <cstdint>
#include <string>
#include <vector>

namespace nsync::client {

class RmlResourceFile final : public Rml::FileInterface {
public:
    explicit RmlResourceFile(shared::ResourceManager& resources);

    Rml::FileHandle Open(const Rml::String& path) override;
    void Close(Rml::FileHandle file) override;
    std::size_t Read(void* buffer, std::size_t size, Rml::FileHandle file) override;
    bool Seek(Rml::FileHandle file, long offset, int origin) override;
    std::size_t Tell(Rml::FileHandle file) override;
    std::size_t Length(Rml::FileHandle file) override;
    bool LoadFile(const Rml::String& path, Rml::String& out_data) override;

private:
    struct MemoryFile {
        std::vector<std::uint8_t> data;
        std::size_t position = 0;
    };

    bool load(const std::string& path, std::vector<std::uint8_t>& data) const;

    shared::ResourceManager& resources_;
};

} // namespace nsync::client
