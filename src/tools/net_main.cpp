#include "core/map.h"
#include "net/local_net.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cout
        << "usage: nsync_net [options]\n"
        << "  --clients N           local clients (default 2)\n"
        << "  --ticks N             fixed ticks to run (default 600)\n"
        << "  --latency-ms N        simulated round-trip latency (default 150)\n"
        << "  --jitter-ms N         simulated one-way jitter (default 30)\n"
        << "  --loss-percent N      packet loss 0..100\n"
        << "  --duplicate-percent N packet duplication 0..100\n"
        << "  --snapshot-interval N snapshot cadence in ticks\n"
        << "  --log-every N         progress log cadence, 0 = only final\n"
        << "  --seed N              deterministic network seed\n"
        << "  --help                 show this help\n";
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

std::uint32_t ticks_from_ms(int milliseconds, int tick_rate = 60) {
    if (milliseconds <= 0) {
        return 0;
    }
    return static_cast<std::uint32_t>(
        (static_cast<std::int64_t>(milliseconds) * tick_rate + 500) / 1000);
}

nsync::net::InputProvider make_demo_provider(int slot) {
    return [slot](std::uint32_t tick) {
        using namespace nsync::core;
        InputCommand command;
        command.tick = tick;
        command.sequence = static_cast<std::uint8_t>(tick & 0xFFU);

        const bool right =
            ((tick / 75U) + static_cast<std::uint32_t>(slot)) % 2U == 0U;
        command.buttons |= right ? kButtonRight : kButtonLeft;
        if ((tick % 120U) == 0U) {
            command.buttons |= kButtonJump;
        }
        if ((tick % 4U) != 3U) {
            command.buttons |= kButtonFire;
        }
        if ((tick % 180U) == 0U) {
            command.buttons |= kButtonReload;
        }
        command.aim_x = slot % 2 == 0 ? 32767 : -32767;
        command.aim_y = 0;
        return command;
    };
}

} // namespace

int main(int argc, char** argv) {
    int clients = 2;
    int ticks = 600;
    int latency_ms = 150;
    int jitter_ms = 30;
    int loss_percent = 0;
    int duplicate_percent = 0;
    int snapshot_interval = 1;
    int log_every = 60;
    int seed = 1;

    for (int i = 1; i < argc; ++i) {
        const char* argument = argv[i];
        auto value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << "\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (std::strcmp(argument, "--help") == 0 ||
            std::strcmp(argument, "-h") == 0) {
            print_usage();
            return 0;
        }
        if (std::strcmp(argument, "--clients") == 0) {
            const char* text = value(argument);
            if (text == nullptr || !parse_int(text, clients)) {
                return 1;
            }
            continue;
        }
        if (std::strcmp(argument, "--ticks") == 0) {
            const char* text = value(argument);
            if (text == nullptr || !parse_int(text, ticks)) {
                return 1;
            }
            continue;
        }
        if (std::strcmp(argument, "--latency-ms") == 0) {
            const char* text = value(argument);
            if (text == nullptr || !parse_int(text, latency_ms)) {
                return 1;
            }
            continue;
        }
        if (std::strcmp(argument, "--jitter-ms") == 0) {
            const char* text = value(argument);
            if (text == nullptr || !parse_int(text, jitter_ms)) {
                return 1;
            }
            continue;
        }
        if (std::strcmp(argument, "--loss-percent") == 0) {
            const char* text = value(argument);
            if (text == nullptr || !parse_int(text, loss_percent)) {
                return 1;
            }
            continue;
        }
        if (std::strcmp(argument, "--duplicate-percent") == 0) {
            const char* text = value(argument);
            if (text == nullptr || !parse_int(text, duplicate_percent)) {
                return 1;
            }
            continue;
        }
        if (std::strcmp(argument, "--snapshot-interval") == 0) {
            const char* text = value(argument);
            if (text == nullptr || !parse_int(text, snapshot_interval)) {
                return 1;
            }
            continue;
        }
        if (std::strcmp(argument, "--log-every") == 0) {
            const char* text = value(argument);
            if (text == nullptr || !parse_int(text, log_every)) {
                return 1;
            }
            continue;
        }
        if (std::strcmp(argument, "--seed") == 0) {
            const char* text = value(argument);
            if (text == nullptr || !parse_int(text, seed)) {
                return 1;
            }
            continue;
        }

        std::cerr << "unknown option: " << argument << "\n";
        print_usage();
        return 1;
    }

    clients = std::clamp(clients, 1, nsync::core::kMaxPlayers);
    ticks = std::max(1, ticks);
    latency_ms = std::max(0, latency_ms);
    jitter_ms = std::max(0, jitter_ms);
    loss_percent = std::clamp(loss_percent, 0, 100);
    duplicate_percent = std::clamp(duplicate_percent, 0, 100);
    snapshot_interval = std::max(1, snapshot_interval);
    log_every = std::max(0, log_every);

    nsync::net::NetConfig config;
    config.latency_ticks = ticks_from_ms(latency_ms / 2);
    config.jitter_ticks = ticks_from_ms(jitter_ms);
    config.loss_percent = static_cast<std::uint32_t>(loss_percent);
    config.duplicate_percent = static_cast<std::uint32_t>(duplicate_percent);
    config.snapshot_interval = static_cast<std::uint32_t>(snapshot_interval);
    config.input_delay_ticks = config.latency_ticks * 2U +
        config.jitter_ticks * 2U + 2U;
    config.seed = static_cast<std::uint64_t>(std::max(0, seed));

    std::vector<nsync::net::InputProvider> providers;
    providers.reserve(static_cast<std::size_t>(clients));
    for (int index = 0; index < clients; ++index) {
        providers.push_back(make_demo_provider(index));
    }

    nsync::net::LocalSession session;
    std::string error;
    if (!session.start(
            nsync::core::Map::default_arena(),
            static_cast<std::uint64_t>(std::max(0, seed)),
            clients,
            clients,
            std::move(providers),
            config,
            10U * 60U * 60U,
            999,
            &error)) {
        std::cerr << "session start failed: " << error << "\n";
        return 1;
    }

    std::cout
        << "local net: clients=" << clients
        << " ticks=" << ticks
        << " rtt=" << latency_ms << "ms"
        << " jitter=" << jitter_ms << "ms"
        << " loss=" << loss_percent << "%"
        << " duplicate=" << duplicate_percent << "%"
        << " input_delay=" << config.input_delay_ticks << " ticks\n";

    for (int tick = 0; tick < ticks; ++tick) {
        session.tick();
        if (log_every > 0 && (tick + 1) % log_every == 0) {
            const auto& first = session.client(0).stats();
            std::cout
                << "tick " << (tick + 1)
                << " client0 ack=" << first.ack_tick
                << " pred=" << first.predicted_tick
                << " rtt=" << first.rtt_ticks * 1000U / 60U << "ms"
                << " rollback=" << first.last_rollback_ticks
                << " corrections=" << first.corrections
                << " desyncs=" << first.desyncs
                << "\n";
        }
    }

    const auto& network = session.network().stats();
    std::cout
        << "network sent=" << network.packets_sent
        << " dropped=" << network.packets_dropped
        << " duplicated=" << network.packets_duplicated
        << " delivered=" << network.packets_delivered << "\n";

    bool exact = true;
    for (int index = 0; index < session.client_count(); ++index) {
        const auto& stats = session.client(index).stats();
        if (session.client(index).world().hash() !=
            session.server().world().hash()) {
            exact = false;
        }
        std::cout
            << "client" << index
            << " slot=" << static_cast<int>(session.client(index).local_slot())
            << " ack=" << stats.ack_tick
            << " pred=" << stats.predicted_tick
            << " rollbacks=" << stats.rollbacks
            << " event_rollbacks=" << stats.event_rollbacks
            << " snapshot_mismatches=" << stats.snapshot_mismatches
            << " snapshot_restores=" << stats.snapshot_restores
            << " corrections=" << stats.corrections
            << " desyncs=" << stats.desyncs
            << " rollback_ticks=" << stats.total_rollback_ticks
            << " max_rollback=" << stats.max_rollback_ticks
            << " graph=" << session.client(index).graph().samples().size()
            << "\n";
        const auto& samples = session.client(index).graph().samples();
        if (!samples.empty()) {
            const auto& sample = samples.back();
            std::cout
                << "  netgraph tick=" << sample.tick
                << " ack=" << sample.ack_tick
                << " rtt=" << sample.rtt_ticks * 1000U / 60U << "ms"
                << " rollback=" << sample.rollback_ticks
                << " matched=" << (sample.matched ? 1 : 0)
                << "\n";
        }
    }
    std::cout
        << "server hash=" << session.server().world().hash()
        << " prediction=" << (exact ? "exact" : "corrected")
        << "\n";
    return 0;
}
