#pragma once

#include "core/geometry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nsync::core {

inline constexpr std::size_t kMapSpawnCount = 10;
inline constexpr std::size_t kMapMaxShapes = 2048;
inline constexpr std::size_t kMapMaxDropSpawns = 256;
inline constexpr std::size_t kMapMaxDropDepots = 16;
inline constexpr std::size_t kMapMaxDepotDrops = 8;
inline constexpr std::size_t kMapMaxDropIdLength = 64;
inline constexpr std::size_t kMapMaxSupplySpawners = 32;
inline constexpr std::size_t kMapMaxSupplyEntries = 8;
inline constexpr std::size_t kMapMaxSupplyList = 4;
inline constexpr std::uint16_t kMapMaxSupplyInterval = 3600;
inline constexpr std::uint16_t kMapDefaultDeliverTicks = 60;
inline constexpr std::uint16_t kMapMaxDeliverTicks = 3600;
inline constexpr std::uint16_t kMapDefaultObjectiveCaptureTicks = 180;
inline constexpr std::uint16_t kMapDefaultObjectiveScoreTicks = 300;
inline constexpr std::uint16_t kMapMaxObjectiveTicks = 3600;
inline constexpr std::size_t kMapMaxControlPointDefs = 16;
inline constexpr std::size_t kMapMaxControlPointEntries = 64;
inline constexpr std::uint16_t kMapMaxControlPointInterval = 3600;
inline constexpr std::uint8_t kDropDepotNeutral = 0xFF;
inline constexpr std::uint32_t kMapBinaryMagic = 0x4F4E524DU; // "ONRM"
inline constexpr std::uint16_t kMapBinaryVersion = 10;

struct JumpPad {
    AABB bounds{};
    Fix32 velocity{};
};

struct DropSpawn {
    Vec2 position{};
    std::string drop_id;
};

struct DropDepot {
    std::uint8_t team = 0;
    AABB bounds{};
    std::uint16_t deliver_ticks = kMapDefaultDeliverTicks;
    std::vector<std::string> accepted_drops;
};

// Original GESupplyGenerator plugin ("supply_gen"): one item at a time is
// spawned at a random entry after start_tick; the next timer only starts once
// the previous item is gone.  Intervals are in 60 Hz ticks.
struct SupplySpawner {
    std::vector<std::string> supplies;
    std::vector<Vec2> entries;
    std::uint32_t start_tick = 0;
    std::uint16_t min_interval = 0;
    std::uint16_t max_interval = 0;
};

struct Objective {
    AABB area{};
    std::uint16_t capture_ticks = kMapDefaultObjectiveCaptureTicks;
    std::uint16_t score_ticks = kMapDefaultObjectiveScoreTicks;
    std::int16_t capture_score = 0;
    std::int16_t hold_score = 1;
};

// One control-point definition from the original cp_gen plugin.  capture_ticks
// is counted in generator updates (framesPerTick), score_ticks in engine ticks.
struct ControlPointDef {
    Vec2 dimensions{};
    std::uint16_t capture_ticks = 1;
    std::uint16_t score_ticks = 0;
    std::int16_t capture_score = 0;
    std::int16_t hold_score = 0;
};

// Original ControlPointGenerator: points spawn at `entries` on a schedule,
// are captured using one signed counter each, and are recycled in groups of
// `capture_group_size` once fully captured (0 = never recycle).
struct ControlPointGenerator {
    std::uint16_t update_interval = 1;
    std::uint32_t start_tick = 0;
    std::uint32_t min_interval = 0;
    std::uint32_t max_interval = 0;
    std::uint8_t instance_limit = 0;
    std::uint8_t capture_group_size = 0;
    std::vector<ControlPointDef> points;
    std::vector<Vec2> entries;

    [[nodiscard]] AABB area(
        std::size_t def_index,
        std::size_t entry_index) const;
};

struct Map {
    std::string id;
    AABB bounds{
        Vec2{Fix32::from_int(-14), Fix32::zero()},
        Vec2{Fix32::from_int(14), Fix32::from_int(16)}};
    std::vector<Segment> borders;
    std::vector<AABB> solids;
    std::vector<AABB> one_way_platforms;
    std::vector<std::array<Vec2, 4>> platform_shapes;
    std::vector<std::uint8_t> platform_hatchway;
    std::vector<JumpPad> jump_pads;
    std::optional<Objective> objective;
    std::optional<ControlPointGenerator> control_points;
    std::array<Vec2, kMapSpawnCount> spawns{};
    std::vector<DropSpawn> drop_spawns;
    std::vector<DropDepot> drop_depots;
    std::vector<SupplySpawner> supply_spawners;

    static Map default_arena();
    [[nodiscard]] bool has_control_objective() const {
        return objective.has_value() || control_points.has_value();
    }
    [[nodiscard]] bool valid(std::string* error = nullptr) const;
};

bool load_map_json(
    std::string_view json,
    Map& map,
    std::string* error = nullptr);

[[nodiscard]] std::vector<std::uint8_t> encode_map_bin(const Map& map);
bool decode_map_bin(
    const std::uint8_t* data,
    std::size_t size,
    Map& map,
    std::string* error = nullptr);
bool load_map_file(
    const std::filesystem::path& path,
    Map& map,
    std::string* error = nullptr);

[[nodiscard]] std::uint64_t map_fingerprint(const Map& map);

} // namespace nsync::core
