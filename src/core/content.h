#pragma once

#include "core/fix32.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nsync::core {

inline constexpr std::size_t kMaxHeroes = 16;
inline constexpr std::size_t kMaxWeapons = 32;
inline constexpr std::size_t kMaxDrops = 16;
inline constexpr std::size_t kMaxSupplies = 16;
inline constexpr std::uint32_t kContentVersion = 2;

enum class HeroType : std::uint8_t {
    Scout = 0,
    Gunner = 1,
    Assault = 2,
    Grenadier = 3,
    Cyborg = 4,
    Hermit = 5,
    Predator = 6,
    Pyromancer = 7,
    Soldier = 8,
    Warrioress = 9,
};

enum class WeaponType : std::uint8_t {
    Rifle = 0,
    Shotgun = 1,
    Rocket = 2,
    Smg = 3,
    Burst = 4,
    Grenade = 5,
    Axe = 6,
};

[[nodiscard]] constexpr bool weapon_is_melee(WeaponType type) {
    return type == WeaponType::Axe;
}

enum class HeroAbilityType : std::uint8_t {
    Dash = 0,
    Shield = 1,
    Overcharge = 2,
};

enum class WallShotAction : std::uint8_t {
    Detonate = 0,
    Bounce = 1,
    Skip = 2,
    Ignore = 3,
};

struct HeroStats {
    HeroType type = HeroType::Scout;
    std::string id;
    std::uint16_t health = 100;
    std::uint16_t shield = 0;
    Fix32 run_speed{};
    Fix32 jump_speed{};
    HeroAbilityType ability = HeroAbilityType::Dash;
    std::string ability_name;
    std::uint16_t ability_cooldown = 0;
    std::uint16_t ability_duration = 0;
    Fix32 ability_power{};
    Fix32 surface_area = Fix32::one();
    std::string weapon;
};

enum class DropKind : std::uint8_t {
    Crystal = 0,
};

enum class DropPickup : std::uint8_t {
    Human = 0,
    Enemy = 1,
    Any = 2,
    None = 3,
};

struct DropStats {
    DropKind kind = DropKind::Crystal;
    std::string id;
    DropPickup pickup = DropPickup::Human;
    std::uint16_t respawn_ticks = 600;
    std::uint16_t vacant_ticks = 30;
    std::uint8_t max_capacity = 1;
    std::string sprite;
};

struct CrystalRules {
    std::uint16_t deliver_ticks = 60;
    std::uint16_t score_per_delivery = 1;
    std::uint16_t attach_score = 0;
    std::uint16_t hold_score = 0;
    std::uint16_t hold_score_ticks = 0;
    std::uint8_t carry_limit = 3;
};

// Original supply artifacts (GESupplyGenerator.Supply).  Health boxes restore
// a fraction of maximum health and start the health_pump buff; buff boxes add
// a damage feature for a fixed duration (id 105 is the +100% box).
enum class SupplyKind : std::uint8_t {
    Health = 0,
    Damage = 1,
};

struct SupplyStats {
    SupplyKind kind = SupplyKind::Health;
    std::string id;
    std::string sprite;
    // Fraction of hpBase restored on pickup (original "gain").
    Fix32 health_gain{};
    // Additive weapon damage multiplier (1 = +100% damage).
    Fix32 damage_bonus{};
    // health_pump: heal_increment every heal_interval ticks for effect_ticks.
    // Damage boxes use effect_ticks as the buff lifespan.
    std::uint16_t effect_ticks = 0;
    std::uint16_t heal_increment = 0;
    std::uint16_t heal_interval = 0;
};

struct WeaponStats {
    WeaponType type = WeaponType::Rifle;
    std::string id;
    std::uint16_t magazine = 0;
    std::uint16_t fire_interval = 0;
    std::uint16_t reload_ticks = 0;
    std::uint16_t reload_size = 0;
    std::uint16_t warmup_ticks = 0;
    std::uint16_t damage = 0;
    std::uint16_t pellets = 1;
    std::uint16_t projectile_life = 0;
    Fix32 projectile_speed{};
    Fix32 spread{};
    Fix32 projectile_half_size{};
    Fix32 range{};
    Fix32 falloff_start = Fix32::one();
    Fix32 falloff_end = Fix32::one();
    Fix32 explosion_radius{};
    std::uint16_t explosion_damage = 0;
    std::uint8_t explosive = 0;
    std::uint8_t platform_puncher = 0;
    std::uint8_t terrain_bounces = 0;
    std::uint8_t explode_on_expire = 0;
    Fix32 blast_falloff_start = Fix32::one();
    Fix32 blast_falloff_end = Fix32::one();
    Fix32 blast_self_ratio = Fix32::one();
    Fix32 blast_friend_ratio = Fix32::one();
    Fix32 explosion_impulse{};
    Fix32 explosion_ground_impulse{};
    Fix32 impulse_falloff_start = Fix32::one();
    Fix32 impulse_falloff_end = Fix32::one();
    Fix32 critical_chance = Fix32::from_ratio(1, 5);
    Fix32 critical_ratio = Fix32::from_ratio(3, 10);
    Fix32 joint_x{};
    Fix32 joint_y{};
    Fix32 gun_x{};
    Fix32 gun_y{};
    Fix32 divergence_degrees{};
    Fix32 dispersion_degrees{};
    std::uint16_t dispersion_ticks = 0;
    Fix32 gravity{};
    Fix32 normal_velocity{};
    Fix32 damping_rate = Fix32::one();
    std::uint16_t detonator_hp = 0;
    std::uint8_t shell_impact = 0;
    std::uint8_t damage_sensor = 0;
    WallShotAction wall_shot_action = WallShotAction::Detonate;
};

class ContentDatabase {
public:
    ContentDatabase();

    void reset_defaults();
    bool load_json(std::string_view text, std::string* error = nullptr);
    bool load_file(
        const std::filesystem::path& path,
        std::string* error = nullptr);

    [[nodiscard]] std::size_t hero_count() const { return hero_count_; }
    [[nodiscard]] std::size_t weapon_count() const { return weapon_count_; }
    [[nodiscard]] std::size_t drop_count() const { return drop_count_; }
    [[nodiscard]] std::size_t supply_count() const { return supply_count_; }
    [[nodiscard]] const HeroStats& hero(std::size_t index) const;
    [[nodiscard]] const WeaponStats& weapon(std::size_t index) const;
    [[nodiscard]] const DropStats& drop(std::size_t index) const;
    [[nodiscard]] const SupplyStats& supply(std::size_t index) const;
    [[nodiscard]] const HeroStats* find_hero(std::string_view id) const;
    [[nodiscard]] const WeaponStats* find_weapon(std::string_view id) const;
    [[nodiscard]] const DropStats* find_drop(std::string_view id) const;
    [[nodiscard]] const SupplyStats* find_supply(std::string_view id) const;
    [[nodiscard]] std::uint8_t find_drop_type(std::string_view id) const;
    [[nodiscard]] std::uint8_t find_supply_type(std::string_view id) const;
    [[nodiscard]] const CrystalRules& crystal_rules() const {
        return crystal_rules_;
    }
    [[nodiscard]] std::uint64_t fingerprint() const;

private:
    std::array<HeroStats, kMaxHeroes> heroes_{};
    std::array<WeaponStats, kMaxWeapons> weapons_{};
    std::array<DropStats, kMaxDrops> drops_{};
    std::array<SupplyStats, kMaxSupplies> supplies_{};
    CrystalRules crystal_rules_{};
    std::size_t hero_count_ = 0;
    std::size_t weapon_count_ = 0;
    std::size_t drop_count_ = 0;
    std::size_t supply_count_ = 0;
};

[[nodiscard]] ContentDatabase& content();
[[nodiscard]] const HeroStats& hero_stats(HeroType type);
[[nodiscard]] const WeaponStats& weapon_stats(WeaponType type);
[[nodiscard]] WeaponType hero_weapon(HeroType type);
[[nodiscard]] WeaponType next_weapon(WeaponType type);
[[nodiscard]] std::string_view hero_name(HeroType type);
[[nodiscard]] std::string_view weapon_name(WeaponType type);

} // namespace nsync::core
