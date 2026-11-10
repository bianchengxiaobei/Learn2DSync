#pragma once

#include "core/entity.h"
#include "core/rng.h"
#include "core/sim_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nsync::core {

inline constexpr std::uint32_t kSnapshotVersion = 20;
inline constexpr std::uint32_t kSnapshotMaxPlayers = 256;
inline constexpr std::uint32_t kSnapshotMaxControlPoints = 64;
inline constexpr std::uint32_t kSnapshotMaxControlPointEntries = 256;
inline constexpr std::uint32_t kSnapshotMaxControlPointFrames = 256;
inline constexpr std::uint32_t kSnapshotMaxProjectiles = 4096;
inline constexpr std::uint32_t kSnapshotMaxDrops = 512;
inline constexpr std::uint32_t kSnapshotMaxSupplies = 512;
inline constexpr std::uint32_t kSnapshotMaxSupplySpawners = 64;
inline constexpr std::uint32_t kSnapshotMaxMatchTicks = 10'000'000;
inline constexpr std::uint16_t kSnapshotMaxObjectiveTicks = 3600;
inline constexpr std::uint16_t kSnapshotMaxKills = 60'000;
inline constexpr std::uint16_t kSnapshotMaxShield = 4096;
inline constexpr std::uint32_t kSnapshotMaxDamage = 1'000'000'000;

struct ControlPointSnapshot {
    std::uint8_t def = 0;
    std::uint16_t entry = 0;
    std::int16_t ticks = 0;
    std::uint16_t hold_ticks = 0;
};

struct WorldSnapshot {
    std::uint32_t version = kSnapshotVersion;
    std::uint32_t tick = 0;
    std::uint32_t match_ticks = 0;
    Pcg32State rng{};
    std::array<std::int32_t, 2> scores{};
    std::uint16_t score_limit = 30;
    std::uint8_t winner = 0;
    std::uint8_t game_mode = 0;
    std::uint8_t objective_owner = 0;
    std::uint8_t objective_capturing_team = 0;
    std::uint16_t objective_progress = 0;
    std::uint16_t objective_score_ticks = 0;
    std::vector<ControlPointSnapshot> control_points;
    std::vector<std::uint16_t> control_point_vacant_entries;
    std::vector<std::uint32_t> control_point_respawn_frames;
    std::uint8_t control_points_recycled = 0;
    EntityAllocatorState entities;
    std::vector<Player> players;
    std::vector<Projectile> projectiles;
    std::vector<Drop> drops;
    std::vector<SupplySpawnerState> supply_spawners;
    std::vector<SupplyInstance> supplies;
};

std::vector<std::uint8_t> encode_snapshot_payload(const WorldSnapshot& snapshot);
std::vector<std::uint8_t> encode_snapshot(const WorldSnapshot& snapshot);
bool decode_snapshot(
    const std::uint8_t* data,
    std::size_t size,
    WorldSnapshot& snapshot,
    std::string* error = nullptr);
bool decode_snapshot(
    const std::vector<std::uint8_t>& data,
    WorldSnapshot& snapshot,
    std::string* error = nullptr);
std::uint64_t snapshot_hash(const WorldSnapshot& snapshot);
std::uint64_t snapshot_hash_payload(
    const std::uint8_t* payload,
    std::size_t size);
bool snapshot_hash_encoded(
    const std::vector<std::uint8_t>& encoded,
    std::uint64_t& hash);

} // namespace nsync::core
