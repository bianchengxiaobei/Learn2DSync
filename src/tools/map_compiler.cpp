#include "core/map.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cout
        << "usage: nsync_mapc --in PATH [--out PATH] [--check]\n"
        << "  --in PATH   source .json or compiled .bin\n"
        << "  --out PATH  write compiled map binary\n"
        << "  --check     decode the compiled binary and compare fingerprints\n";
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path input;
    std::filesystem::path output;
    bool check = false;

    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--help") == 0 ||
            std::strcmp(argv[index], "-h") == 0) {
            print_usage();
            return 0;
        }
        if (std::strcmp(argv[index], "--in") == 0 && index + 1 < argc) {
            input = argv[++index];
            continue;
        }
        if (std::strcmp(argv[index], "--out") == 0 && index + 1 < argc) {
            output = argv[++index];
            continue;
        }
        if (std::strcmp(argv[index], "--check") == 0) {
            check = true;
            continue;
        }
        std::cerr << "mapc: unknown option: " << argv[index] << "\n";
        print_usage();
        return 1;
    }

    if (input.empty()) {
        print_usage();
        return 1;
    }

    nsync::core::Map map;
    std::string error;
    if (!nsync::core::load_map_file(input, map, &error)) {
        std::cerr << "mapc: " << error << "\n";
        return 1;
    }

    const std::vector<std::uint8_t> binary =
        nsync::core::encode_map_bin(map);
    if (binary.empty()) {
        std::cerr << "mapc: failed to encode map\n";
        return 1;
    }

    if (check) {
        nsync::core::Map decoded;
        if (!nsync::core::decode_map_bin(
                binary.data(), binary.size(), decoded, &error)) {
            std::cerr << "mapc: binary roundtrip failed: " << error << "\n";
            return 1;
        }
        if (nsync::core::map_fingerprint(map) !=
            nsync::core::map_fingerprint(decoded)) {
            std::cerr << "mapc: binary fingerprint mismatch\n";
            return 1;
        }
    }

    if (!output.empty()) {
        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        if (!file ||
            !file.write(
                reinterpret_cast<const char*>(binary.data()),
                static_cast<std::streamsize>(binary.size()))) {
            std::cerr << "mapc: failed to write " << output.string() << "\n";
            return 1;
        }
    }

    std::cout
        << "mapc: id=" << map.id
        << " solids=" << map.solids.size()
        << " platforms=" << map.one_way_platforms.size()
        << " bytes=" << binary.size()
        << " fingerprint=" << nsync::core::map_fingerprint(map)
        << "\n";
    return 0;
}
