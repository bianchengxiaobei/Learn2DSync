#include "shared/pak.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: nsync_pak <assets-dir> <output.pak>\n";
        return 1;
    }

    std::string error;
    if (!nsync::shared::pak::write_pak(argv[1], argv[2], error)) {
        std::cerr << "pak failed: " << error << '\n';
        return 1;
    }

    std::cout << "wrote " << argv[2] << '\n';
    return 0;
}
