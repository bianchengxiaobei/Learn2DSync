#include "core/input_command.h"
#include "core/map.h"
#include "net/local_net.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options {
    int minutes = 60;
    int matches = 1;
    int players = 10;
    int humans = 2;
    bool realtime = false;
};

void print_usage() {
    std::printf(
        "usage: nsync_soak [options]\n"
        "  --minutes N        simulated match minutes (default 60)\n"
        "  --matches N        concurrent local matches (default 1)\n"
        "  --players N        players per match (default 10)\n"
        "  --humans N         local clients per match (default 2)\n"
        "  --realtime         pace at 60 ticks/second instead of max speed\n"
        "  --help             show this help\n");
}

bool parse_int(const char* text, int& value) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

nsync::net::InputProvider make_provider(int slot) {
    return [slot](std::uint32_t tick) {
        using namespace nsync::core;
        InputCommand command;
        command.tick = tick;
        command.sequence = static_cast<std::uint8_t>(tick & 0xFFU);
        const bool right =
            ((tick / 45U) + static_cast<std::uint32_t>(slot)) % 2U == 0U;
        command.buttons |= right ? kButtonRight : kButtonLeft;
        if (tick % 47U == 0U) {
            command.buttons |= kButtonJump;
        }
        if (tick % 3U != 2U) {
            command.buttons |= kButtonFire;
        }
        command.aim_x = slot % 2 == 0 ? 32767 : -32767;
        return command;
    };
}

struct MatchRun {
    std::unique_ptr<nsync::net::LocalSession> session;
    std::uint64_t mismatches = 0;
    std::uint64_t desyncs = 0;
    std::uint64_t corrections = 0;
};

int run_soak(const Options& options) {
    if (options.minutes < 1 || options.minutes > 24 * 60 ||
        options.matches < 1 || options.matches > 64 ||
        options.players < 1 ||
        options.players > nsync::core::kMaxPlayers ||
        options.humans < 1 || options.humans > options.players) {
        std::fprintf(stderr, "invalid soak options\n");
        return 1;
    }

    const std::uint32_t ticks = static_cast<std::uint32_t>(
        options.minutes * 60 * 60);
    std::vector<std::unique_ptr<MatchRun>> runs;
    runs.reserve(static_cast<std::size_t>(options.matches));

    for (int match = 0; match < options.matches; ++match) {
        auto run = std::make_unique<MatchRun>();
        run->session = std::make_unique<nsync::net::LocalSession>();

        nsync::net::NetConfig config;
        config.latency_ticks = 5;
        config.jitter_ticks = 2;
        config.input_delay_ticks = 16;
        config.snapshot_interval = 1;
        config.seed = static_cast<std::uint64_t>(1000 + match);

        std::vector<nsync::net::InputProvider> providers;
        for (int slot = 0; slot < options.humans; ++slot) {
            providers.push_back(make_provider(slot));
        }
        std::string error;
        if (!run->session->start(
                nsync::core::Map::default_arena(),
                static_cast<std::uint64_t>(2000 + match),
                options.humans,
                options.players,
                std::move(providers),
                config,
                24U * 60U * 60U,
                999,
                &error)) {
            std::fprintf(
                stderr,
                "soak match %d start failed: %s\n",
                match,
                error.c_str());
            return 1;
        }
        runs.push_back(std::move(run));
    }

    std::printf(
        "soak start: matches=%d players=%d humans=%d ticks=%u realtime=%d\n",
        options.matches,
        options.players,
        options.humans,
        ticks,
        options.realtime ? 1 : 0);
    std::fflush(stdout);

    const auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> threads;
    threads.reserve(runs.size());
    for (const std::unique_ptr<MatchRun>& run : runs) {
        nsync::net::LocalSession* session = run->session.get();
        MatchRun* result = run.get();
        threads.emplace_back([session, result, ticks, options] {
            const auto tick_duration =
                std::chrono::microseconds(1000000 / 60);
            auto next_tick = std::chrono::steady_clock::now();
            for (std::uint32_t tick = 0; tick < ticks; ++tick) {
                session->tick();
                if ((tick % 60U) == 0U) {
                    for (int client = 0;
                         client < session->client_count();
                         ++client) {
                        if (session->client(client).world().hash() !=
                            session->server().world().hash()) {
                            ++result->mismatches;
                        }
                        if (session->client(client).stats().desyncs != 0) {
                            ++result->desyncs;
                        }
                        result->corrections +=
                            session->client(client).stats().corrections;
                    }
                }
                if (options.realtime) {
                    next_tick += tick_duration;
                    std::this_thread::sleep_until(next_tick);
                }
            }
            for (int client = 0;
                 client < session->client_count();
                 ++client) {
                if (session->client(client).world().hash() !=
                    session->server().world().hash()) {
                    ++result->mismatches;
                }
                if (session->client(client).stats().desyncs != 0) {
                    ++result->desyncs;
                }
                result->corrections +=
                    session->client(client).stats().corrections;
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    const auto end = std::chrono::steady_clock::now();
    const double wall_seconds =
        std::chrono::duration<double>(end - start).count();
    std::uint64_t mismatches = 0;
    std::uint64_t desyncs = 0;
    std::uint64_t corrections = 0;
    for (const std::unique_ptr<MatchRun>& run : runs) {
        mismatches += run->mismatches;
        desyncs += run->desyncs;
        corrections += run->corrections;
    }
    const bool ok =
        mismatches == 0 && desyncs == 0 && corrections == 0;
    std::printf(
        "soak result: ok=%d wall_s=%.3f ticks_per_s=%.1f "
        "mismatches=%llu desyncs=%llu corrections=%llu\n",
        ok ? 1 : 0,
        wall_seconds,
        wall_seconds > 0.0
            ? static_cast<double>(ticks) * options.matches / wall_seconds
            : 0.0,
        static_cast<unsigned long long>(mismatches),
        static_cast<unsigned long long>(desyncs),
        static_cast<unsigned long long>(corrections));
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const char* argument = argv[index];
        if (std::strcmp(argument, "--help") == 0 ||
            std::strcmp(argument, "-h") == 0) {
            print_usage();
            return 0;
        }
        if (std::strcmp(argument, "--realtime") == 0) {
            options.realtime = true;
            continue;
        }
        if ((std::strcmp(argument, "--minutes") == 0 ||
             std::strcmp(argument, "--matches") == 0 ||
             std::strcmp(argument, "--players") == 0 ||
             std::strcmp(argument, "--humans") == 0) &&
            index + 1 < argc) {
            int parsed = 0;
            if (!parse_int(argv[++index], parsed) || parsed < 0) {
                std::fprintf(
                    stderr,
                    "invalid value for %s\n",
                    argument);
                return 1;
            }
            if (std::strcmp(argument, "--minutes") == 0) {
                options.minutes = parsed;
            } else if (std::strcmp(argument, "--matches") == 0) {
                options.matches = parsed;
            } else if (std::strcmp(argument, "--players") == 0) {
                options.players = parsed;
            } else {
                options.humans = parsed;
            }
            continue;
        }
        print_usage();
        return 1;
    }
    return run_soak(options);
}
