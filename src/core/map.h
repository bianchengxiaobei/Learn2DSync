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
inline constexpr std::uint16_t kMapDefaultDeliverTicks = 60;
inline constexpr std::uint16_t kMapMaxDeliverTicks = 3600;
inline constexpr std::uint8_t kDropDepotNeutral = 0xFF;
inline constexpr std::uint32_t kMapBinaryMagic = 0x4F4E524DU; // "ONRM"
inline constexpr std::uint16_t kMapBinaryVersion = 5;

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

struct Map {
    std::string id;
    AABB bounds{
        Vec2{Fix32::from_int(-14), Fix32::zero()},
        Vec2{Fix32::from_int(14), Fix32::from_int(16)}};
    std::vector<AABB> solids;
    std::vector<AABB> one_way_platforms;
    std::vector<std::uint8_t> platform_hatchway;
    std::vector<JumpPad> jump_pads;
    std::optional<AABB> objective;
    std::array<Vec2, kMapSpawnCount> spawns{};
    std::vector<DropSpawn> drop_spawns;
    std::vector<DropDepot> drop_depots;

    static Map default_arena();
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
