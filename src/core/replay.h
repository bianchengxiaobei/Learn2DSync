#pragma once

#include "core/input_command.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nsync::core {

inline constexpr std::uint32_t kReplayVersion = 1;
inline constexpr std::uint32_t kReplayMaxTicks = 1'000'000;

struct ReplayFrame {
    std::uint32_t tick = 0;
    std::vector<PlayerCommand> commands;
};

struct Replay {
    std::uint32_t version = kReplayVersion;
    std::uint64_t seed = 1;
    std::uint16_t player_count = 2;
    std::vector<ReplayFrame> frames;
};

struct ReplayRun {
    std::vector<std::uint32_t> hash_ticks;
    std::vector<std::uint64_t> hashes;
};

Replay make_golden_replay(std::uint64_t seed = 1, std::uint32_t tick_count = 120);

std::vector<std::uint8_t> encode_replay(const Replay& replay);
bool decode_replay(
    const std::uint8_t* data,
    std::size_t size,
    Replay& replay,
    std::string* error = nullptr);
bool decode_replay(
    const std::vector<std::uint8_t>& data,
    Replay& replay,
    std::string* error = nullptr);

bool save_replay(
    const std::filesystem::path& path,
    const Replay& replay,
    std::string* error = nullptr);
bool load_replay(
    const std::filesystem::path& path,
    Replay& replay,
    std::string* error = nullptr);

bool run_replay(
    const Replay& replay,
    std::uint32_t hash_every,
    ReplayRun& run,
    std::string* error = nullptr);

} // namespace nsync::core
