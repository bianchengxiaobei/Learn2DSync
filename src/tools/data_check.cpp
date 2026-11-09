#include "core/content.h"

#include <cstring>
#include <filesystem>
#include <iostream>

namespace {

void print_usage() {
    std::cout
        << "usage: nsync_datac --in PATH [--check]\n"
        << "  --in PATH   authoring JSON content definition\n"
        << "  --check      validate and print the content fingerprint\n";
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path input;
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
        if (std::strcmp(argv[index], "--check") == 0) {
            check = true;
            continue;
        }
        std::cerr << "datac: unknown option: " << argv[index] << "\n";
        print_usage();
        return 1;
    }

    if (input.empty()) {
        print_usage();
        return 1;
    }

    nsync::core::ContentDatabase database;
    std::string error;
    if (!database.load_file(input, &error)) {
        std::cerr << "datac: " << error << "\n";
        return 1;
    }

    std::cout
        << "datac: heroes=" << database.hero_count()
        << " weapons=" << database.weapon_count()
        << " drops=" << database.drop_count()
        << " supplies=" << database.supply_count()
        << " fingerprint=" << database.fingerprint();
    if (check) {
        nsync::core::ContentDatabase defaults;
        if (defaults.fingerprint() != database.fingerprint()) {
            std::cerr
                << "\ndatac: warning: file differs from built-in defaults\n";
        }
    }
    std::cout << "\n";
    return 0;
}
