#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nsync::core {

inline constexpr std::uint32_t kHeroRigVersion = 1;
inline constexpr std::size_t kMaxHeroRigs = 16;
inline constexpr std::size_t kMaxHeroParts = 32;
inline constexpr std::size_t kMaxHeroWeapons = 16;

inline constexpr std::uint32_t kHeroPoseVersion = 1;
inline constexpr std::size_t kMaxHeroPoseStates = 16;
inline constexpr std::size_t kMaxHeroPoseTracks = 24;

struct HeroPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct HeroPart {
    std::string role;
    std::string sprite;
    int z = 0;
    HeroPoint pivot{};
    std::array<HeroPoint, 4> quad{};
    std::array<HeroPoint, 4> uv{};
};

struct HeroWeapon {
    std::string id;
    std::string sprite;
    int z = 22;
    HeroPoint pivot{};
    std::array<HeroPoint, 4> quad{};
    std::array<HeroPoint, 4> uv{};
};

struct HeroRig {
    std::string hero;
    HeroPoint root_pivot{0.0f, -0.9f};
    std::vector<HeroPart> parts;
    std::vector<HeroWeapon> weapons;
};

struct HeroRigSet {
    std::string atlas = "generated/original_heroes/heroes.atlas.json";
    std::string image = "generated/original_heroes/heroes.png";
    float height = 1.8f;
    std::vector<HeroRig> heroes;

    bool load_json(std::string_view json, std::string* error = nullptr);
    bool load_file(
        const std::filesystem::path& path,
        std::string* error = nullptr);
    [[nodiscard]] bool valid(std::string* error = nullptr) const;
    [[nodiscard]] const HeroRig* find(std::string_view hero_id) const;
};

struct HeroPoseTrack {
    std::string role;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float sway_x = 0.0f;
    float sway_y = 0.0f;
    float rotation = 0.0f;
    float sway_rotation = 0.0f;
    float frequency = 0.0f;
    float phase = 0.0f;
};

struct HeroPoseState {
    std::string name;
    float duration = 1.0f;
    bool loop = true;
    float root_rotation_start = 0.0f;
    float root_rotation_end = 0.0f;
    float root_offset_x_start = 0.0f;
    float root_offset_x_end = 0.0f;
    float root_offset_y_start = 0.0f;
    float root_offset_y_end = 0.0f;
    float alpha_start = 1.0f;
    float alpha_end = 1.0f;
    std::vector<HeroPoseTrack> tracks;
};

struct HeroPoseRecoil {
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float rotation_deg = 0.0f;
    float decay = 16.0f;
};

struct HeroPoseSet {
    std::vector<HeroPoseState> states;
    HeroPoseRecoil recoil{};

    bool load_json(std::string_view json, std::string* error = nullptr);
    bool load_file(
        const std::filesystem::path& path,
        std::string* error = nullptr);
    [[nodiscard]] bool valid(std::string* error = nullptr) const;
    [[nodiscard]] const HeroPoseState* find(std::string_view name) const;
};

} // namespace nsync::core
