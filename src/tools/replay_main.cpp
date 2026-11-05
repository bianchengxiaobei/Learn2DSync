#include "core/replay.h"
#include "core/world.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace {

void print_usage() {
    std::printf(
        "usage: nsync_replay [--ticks N] [--seed N] [--hash-every N]\n"
        "                     [--write PATH] [--read PATH] [--verify]\n");
}

bool parse_u32(const char* text, std::uint32_t& value) {
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool parse_u64(const char* text, std::uint64_t& value) {
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    value = static_cast<std::uint64_t>(parsed);
    return true;
}

bool compare_runs(
    const nsync::core::ReplayRun& first,
    const nsync::core::ReplayRun& second) {
    if (first.hash_ticks != second.hash_ticks || first.hashes != second.hashes) {
        std::fprintf(stderr, "determinism check failed\n");
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    using namespace nsync::core;

    std::uint32_t ticks = 120;
    std::uint64_t seed = 1;
    std::uint32_t hash_every = 1;
    bool verify = false;
    std::string write_path;
    std::string read_path;

    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--help") == 0) {
            print_usage();
            return 0;
        }
        if (std::strcmp(argv[index], "--verify") == 0) {
            verify = true;
            continue;
        }
        if (index + 1 >= argc) {
            print_usage();
            return 2;
        }
        const char* value = argv[++index];
        if (std::strcmp(argv[index - 1], "--ticks") == 0) {
            if (!parse_u32(value, ticks) || ticks == 0 || ticks > kReplayMaxTicks) {
                std::fprintf(stderr, "invalid --ticks\n");
                return 2;
            }
        } else if (std::strcmp(argv[index - 1], "--seed") == 0) {
            if (!parse_u64(value, seed)) {
                std::fprintf(stderr, "invalid --seed\n");
                return 2;
            }
        } else if (std::strcmp(argv[index - 1], "--hash-every") == 0) {
            if (!parse_u32(value, hash_every) || hash_every == 0) {
                std::fprintf(stderr, "invalid --hash-every\n");
                return 2;
            }
        } else if (std::strcmp(argv[index - 1], "--write") == 0) {
            write_path = value;
        } else if (std::strcmp(argv[index - 1], "--read") == 0) {
            read_path = value;
        } else {
            print_usage();
            return 2;
        }
    }

    Replay replay;
    std::string error;
    if (!read_path.empty()) {
        if (!load_replay(read_path, replay, &error)) {
            std::fprintf(stderr, "replay load failed: %s\n", error.c_str());
            return 1;
        }
    } else {
        replay = make_golden_replay(seed, ticks);
        if (!write_path.empty() && !save_replay(write_path, replay, &error)) {
            std::fprintf(stderr, "replay save failed: %s\n", error.c_str());
            return 1;
        }
    }

    ReplayRun first;
    if (!run_replay(replay, hash_every, first, &error)) {
        std::fprintf(stderr, "replay failed: %s\n", error.c_str());
        return 1;
    }

    for (std::size_t index = 0; index < first.hashes.size(); ++index) {
        std::printf(
            "tick %u hash %016llx\n",
            first.hash_ticks[index],
            static_cast<unsigned long long>(first.hashes[index]));
    }

    if (verify) {
        ReplayRun second;
        if (!run_replay(replay, hash_every, second, &error)) {
            std::fprintf(stderr, "second replay failed: %s\n", error.c_str());
            return 1;
        }
        if (!compare_runs(first, second)) {
            return 1;
        }
        std::printf("deterministic: yes\n");
    }

    std::printf(
        "ticks=%zu final_tick=%u final_hash=%016llx\n",
        replay.frames.size(),
        first.hash_ticks.empty() ? 0U : first.hash_ticks.back(),
        first.hashes.empty()
            ? 0ULL
            : static_cast<unsigned long long>(first.hashes.back()));
    return 0;
}
