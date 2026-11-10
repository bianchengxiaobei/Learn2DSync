#pragma once

#include "core/entity.h"
#include "core/fix32.h"
#include "core/gadgets.h"

#include <array>
#include <cstdint>

namespace nsync::core {

struct Player {
    EntityId id{};
    Vec2 position{};
    Vec2 velocity{};
    Vec2 aim{Fix32::one(), Fix32::zero()};
    Fix32 health{};
    std::uint16_t shield = 0;
    std::uint16_t buttons = 0;
    std::uint16_t fire_cooldown = 0;
    std::uint16_t dispersion_ticks = 0;
    std::uint16_t warmup_timer = 0;
    std::uint16_t reload_timer = 0;
    std::uint16_t respawn_timer = 0;
    std::uint16_t ammo = 0;
    std::uint16_t weapon_switch_cooldown = 0;
    std::array<std::uint16_t, kGadgetSlots> gadget_cooldowns{};
    std::array<std::uint16_t, kGadgetSlots> gadget_active_ticks{};
    std::array<Fix32, kGadgetSlots> gadget_fuel{};
    std::uint16_t invulnerable_ticks = 0;
    std::uint16_t heal_ticks = 0;
    std::uint16_t heal_interval = 0;
    std::uint16_t supply_heal_ticks = 0;
    std::uint16_t supply_heal_interval = 0;
    std::uint16_t supply_heal_period = 0;
    std::uint16_t supply_heal_amount = 0;
    std::uint16_t damage_bonus_ticks = 0;
    Fix32 damage_bonus{};
    std::uint16_t slow_ticks = 0;
    std::uint16_t stun_ticks = 0;
    std::uint8_t gadget_latch = 0;
    std::uint8_t fire_latch = 0;
    std::uint16_t shield_regen_delay = 0;
    std::uint16_t kills = 0;
    std::uint16_t deaths = 0;
    std::uint16_t depot_hold_ticks = 0;
    std::uint16_t hold_score_timer = 0;
    std::uint16_t crystals_delivered = 0;
    std::uint32_t damage_dealt = 0;
    std::uint32_t blocked_damage = 0;
    std::uint8_t carried_drops = 0;
    std::uint8_t slot = 0;
    std::uint8_t team = 0;
    std::uint8_t weapon = 0;
    std::uint8_t hero = 0;
    std::uint8_t bot = 0;
    std::uint8_t alive = 0;
    std::uint8_t grounded = 0;
    std::uint8_t drop_through_ticks = 0;
    std::uint8_t pad_boost = 0;
    std::uint8_t pad_boost_ticks = 0;
    std::int8_t facing = 1;
};

enum class DropState : std::uint8_t {
    Home = 0,
    Carried = 1,
    Loose = 2,
};

struct Drop {
    EntityId id{};
    EntityId carrier{};
    Vec2 position{};
    Vec2 home{};
    std::uint16_t respawn_timer = 0;
    std::uint16_t vacant_ticks = 0;
    std::uint8_t drop_type = 0;
    std::uint8_t state = 0;
};

// A static supply item spawned by a map SupplySpawner.  The original game
// spawns one item per generator at a time; `spawner` is the index into
// Map::supply_spawners and `type` the index into ContentDatabase supplies.
struct SupplyInstance {
    std::uint8_t spawner = 0;
    std::uint8_t type = 0;
    Vec2 position{};
};

// Per-generator countdown.  A zero timer with no active item starts the
// respawn delay; the timer counts down in engine ticks.
struct SupplySpawnerState {
    std::uint32_t timer = 0;
};

struct Projectile {
    EntityId id{};
    EntityId owner{};
    Vec2 position{};
    Vec2 velocity{};
    Vec2 origin{};
    Fix32 half_size{};
    Fix32 range{};
    Fix32 falloff_start = Fix32::one();
    Fix32 falloff_end = Fix32::one();
    Fix32 traveled{};
    Fix32 explosion_radius{};
    Fix32 blast_falloff_start = Fix32::one();
    Fix32 blast_falloff_end = Fix32::one();
    Fix32 blast_self_ratio = Fix32::one();
    Fix32 blast_friend_ratio = Fix32::one();
    Fix32 explosion_impulse{};
    Fix32 explosion_ground_impulse{};
    Fix32 impulse_falloff_start = Fix32::one();
    Fix32 impulse_falloff_end = Fix32::one();
    Fix32 critical_ratio{};
    Fix32 gravity{};
    Fix32 detonator_hp{};
    Fix32 detonator_hp_step{};
    Fix32 detonate_damping = Fix32::one();
    Fix32 normal_velocity{};
    Fix32 damping_rate = Fix32::one();
    std::uint16_t damage = 0;
    std::uint16_t explosion_damage = 0;
    std::uint16_t life_ticks = 0;
    std::uint16_t slow_ticks = 0;
    std::uint16_t stun_ticks = 0;
    std::uint8_t team = 0;
    std::uint8_t explosive = 0;
    std::uint8_t terrain_bounces = 0;
    std::uint8_t explode_on_expire = 0;
    std::uint8_t platform_puncher = 0;
    std::uint8_t shell_impact = 0;
    std::uint8_t damage_sensor = 0;
    std::uint8_t ignore_wall = 0;
    // Presentation-only tag: 0 = weapon projectile, gadget type + 1 otherwise.
    std::uint8_t gadget = 0;
};

} // namespace nsync::core
