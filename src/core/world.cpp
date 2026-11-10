#include "core/world.h"

#include "core/aim.h"
#include "core/fix_trig.h"
#include "core/gadgets.h"
#include "core/geometry.h"
#include "core/heroes.h"
#include "core/weapons.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

namespace nsync::core {
namespace {

constexpr Fix32 kPlayerHalfWidth = Fix32::from_ratio(35, 100);
constexpr Fix32 kPlayerHalfHeight = Fix32::from_ratio(7, 10);
constexpr Fix32 kAcceleration = Fix32::from_ratio(1, 100);
constexpr Fix32 kGravityUp = Fix32::from_ratio(1, 142);
constexpr Fix32 kGravityDown = Fix32::from_ratio(1, 97);
constexpr Fix32 kGadgetForceToTick = Fix32::from_ratio(13, 1200);
constexpr Fix32 kGadgetLimitToTick = Fix32::from_ratio(13, 600);
constexpr Fix32 kGadgetSlowdownPerTick = Fix32::from_ratio(98235, 100000);
constexpr Fix32 kDashSeed = Fix32::from_ratio(1, 1000);
constexpr Fix32 kKickstartGravityMultiplier = Fix32::from_ratio(-31, 25);
constexpr Fix32 kMaxFallSpeed = Fix32::from_ratio(1, 2);
// The converted border AABBs are 0.12 units thick.  Their top edge is
// therefore 0.06 above the authored border line, which is flush with
// bridges/platforms in the original world.  Allow a grounded hero to step
// over that tiny artificial lip instead of treating it as a wall.
constexpr Fix32 kHorizontalStepUp = Fix32::from_ratio(12, 100);
constexpr std::uint16_t kRespawnTicks = 120;
constexpr std::uint16_t kWeaponSwitchCooldown = 15;
constexpr std::uint16_t kShieldRegenDelay = 105;
constexpr std::uint16_t kShieldRegenInterval = 10;
constexpr std::int32_t kAimMax = 32767;
constexpr Fix32 kHeroMass = Fix32::from_int(100);
constexpr Fix32 kSteamMultiplier = Fix32::from_ratio(13, 10);
constexpr Fix32 kDropHalfSize = Fix32::from_ratio(2, 5);

Fix32 move_toward(Fix32 value, Fix32 target, Fix32 max_delta) {
    if (value < target) {
        return Fix32::min(value + max_delta, target);
    }
    return Fix32::max(value - max_delta, target);
}

Fix32 clamp(Fix32 value, Fix32 minimum, Fix32 maximum) {
    return Fix32::min(Fix32::max(value, minimum), maximum);
}

std::uint16_t projectile_hit_damage(
    const Projectile& projectile,
    Fix32 traveled) {
    if (projectile.damage == 0) {
        return 0;
    }
    Fix32 factor = Fix32::one();
    if (projectile.range > Fix32::zero() &&
        (projectile.falloff_start < Fix32::one() ||
         projectile.falloff_end < Fix32::one())) {
        const Fix32 ratio = traveled / projectile.range;
        if (ratio > projectile.falloff_start) {
            const Fix32 span = Fix32::one() - projectile.falloff_start;
            if (span > Fix32::zero()) {
                const Fix32 offset = Fix32::min(
                    (ratio - projectile.falloff_start) / span,
                    Fix32::one());
                factor = Fix32::one() +
                    (projectile.falloff_end - Fix32::one()) * offset;
            }
        }
    }
    const Fix32 scaled = Fix32::from_int(projectile.damage) * factor *
        (Fix32::one() + projectile.critical_ratio);
    const std::int32_t rounded = scaled.to_int_round();
    return static_cast<std::uint16_t>(
        std::clamp(rounded, 0, 65535));
}

} // namespace

void World::set_map(const Map& map) {
    map_ = map;
    collision_solids_ = map_.solids;
    projectile_solids_.clear();
    projectile_platform_segments_.clear();
    if (map_.borders.empty()) {
        projectile_solids_ = map_.solids;
    }
    const bool has_platform_shapes = !map_.platform_shapes.empty();
    if (has_platform_shapes) {
        for (const std::array<Vec2, 4>& shape : map_.platform_shapes) {
            for (std::size_t edge = 0; edge < shape.size(); ++edge) {
                projectile_platform_segments_.push_back(Segment{
                    shape[edge], shape[(edge + 1) % shape.size()]});
            }
        }
    }
    for (std::size_t index = 0;
         index < map_.one_way_platforms.size();
         ++index) {
        const bool hatchway =
            index >= map_.platform_hatchway.size() ||
            map_.platform_hatchway[index] != 0;
        if (!hatchway) {
            collision_solids_.push_back(map_.one_way_platforms[index]);
            if (!has_platform_shapes) {
                projectile_solids_.push_back(map_.one_way_platforms[index]);
            }
        }
    }
    clear_drops();
    rebuild_control_point_config();
    reset_control_points();
    reset_supplies();
}

World::World() {
    set_map(map_);
    reset();
}

void World::reset(std::uint64_t seed, int player_count) {
    if (player_count < 1) {
        player_count = 1;
    }
    if (player_count > kMaxPlayers) {
        player_count = kMaxPlayers;
    }

    entities_.clear();
    players_.clear();
    projectiles_.clear();
    drops_.clear();
    tick_ = 0;
    scores_ = {};
    match_ticks_ = match_duration_;
    winner_ = 0;
    player_count_ = player_count;
    rng_ = Pcg32(seed);
    reset_control_points();

    for (int slot = 0; slot < player_count_; ++slot) {
        spawn_player(static_cast<std::uint8_t>(slot));
    }
    spawn_drops_from_map();
    reset_supplies();
}

void World::set_bot_slots(std::uint16_t mask) {
    bot_mask_ = mask;
    players_.for_each([&](Player& player) {
        player.bot = player.slot < 16 ? static_cast<std::uint8_t>(
            (mask >> player.slot) & 1U) : 0;
    });
}

void World::set_match_rules(
    std::uint32_t duration_ticks,
    std::uint16_t score_limit) {
    match_duration_ = duration_ticks == 0 ? 1 : duration_ticks;
    score_limit_ = score_limit == 0 ? 1 : score_limit;
    match_ticks_ = match_duration_;
    winner_ = 0;
}

void World::set_game_mode(GameMode mode) {
    game_mode_ = mode;
    reset_control_points();
    players_.for_each([](Player& player) {
        player.carried_drops = 0;
        player.depot_hold_ticks = 0;
        player.hold_score_timer = 0;
    });
    spawn_drops_from_map();
}

Vec2 World::spawn_position(std::uint8_t slot) const {
    const std::size_t team = static_cast<std::size_t>(slot % 2U);
    const std::size_t member = static_cast<std::size_t>(slot / 2U);
    const std::size_t index = team * 5U + member;
    if (index >= map_.spawns.size()) {
        return map_.bounds.center();
    }
    return map_.spawns[index];
}

void World::spawn_player(std::uint8_t slot) {
    const EntityId id = entities_.create();
    if (!id.valid()) {
        return;
    }

    Player player;
    player.id = id;
    player.slot = slot;
    player.team = static_cast<std::uint8_t>(slot % 2U);
    const std::size_t hero_count = content().hero_count();
    player.hero = hero_count == 0
        ? 0
        : static_cast<std::uint8_t>((slot / 2U) % hero_count);
    player.bot = slot < 16
        ? static_cast<std::uint8_t>((bot_mask_ >> slot) & 1U)
        : 0;
    player.position = spawn_position(slot);
    player.aim = Vec2{player.team == 0 ? Fix32::one() : Fix32::from_int(-1), Fix32::zero()};
    player.facing = player.team == 0 ? 1 : -1;
    const HeroStats& hero =
        hero_stats(static_cast<HeroType>(player.hero));
    player.health = Fix32::from_int(hero.health);
    player.shield = hero.shield;
    player.weapon = static_cast<std::uint8_t>(WeaponType::Rifle);
    player.ammo = weapon_stats(WeaponType::Rifle).magazine;
    player.alive = 1;
    reset_player_gadgets(player);
    players_.add(id, player);
}

void equip_hero_default_weapon(Player& player) {
    const WeaponType weapon =
        hero_weapon(static_cast<HeroType>(player.hero));
    player.weapon = static_cast<std::uint8_t>(weapon);
    player.ammo = weapon_stats(weapon).magazine;
    player.reload_timer = 0;
    player.fire_cooldown = 0;
    player.dispersion_ticks = 0;
    player.warmup_timer = 0;
    player.fire_latch = 0;
}

void reset_player_gadgets(Player& player) {
    player.gadget_cooldowns.fill(0);
    player.gadget_active_ticks.fill(0);
    for (std::size_t slot = 0; slot < kGadgetSlots; ++slot) {
        const GadgetStats& gadget = hero_gadget_stats(
            static_cast<HeroType>(player.hero), slot);
        player.gadget_fuel[slot] = gadget_uses_fuel(gadget.type)
            ? kJetPackTuning.capacity
            : Fix32::zero();
    }
}

void World::step(const InputFrame& input) {
    if (winner_ != 0) {
        players_.for_each([](Player& player) { player.buttons = 0; });
        ++tick_;
        return;
    }

    std::vector<PlayerCommand> commands;
    commands.reserve(input.commands.size() + kMaxPlayers);
    for (const PlayerCommand& player_command : input.commands) {
        const Player* player = players_.get(player_command.entity);
        if (player != nullptr && player->bot == 0) {
            commands.push_back(player_command);
        }
    }
    players_.for_each([&](const Player& player) {
        if (player.bot != 0) {
            commands.push_back(make_bot_command(player));
        }
    });
    std::sort(commands.begin(), commands.end(), player_command_less);

    players_.for_each([](Player& player) { player.buttons = 0; });

    for (const PlayerCommand& player_command : commands) {
        if (player_command.command.tick != tick_) {
            continue;
        }
        Player* player = players_.get(player_command.entity);
        if (!player) {
            continue;
        }
        player->buttons = player_command.command.buttons;
        player->aim = decode_aim(player_command.command, player->facing);
        if (player->aim.x > Fix32::zero()) {
            player->facing = 1;
        } else if (player->aim.x < Fix32::zero()) {
            player->facing = -1;
        }
    }

    players_.for_each([&](Player& player) { update_player(player); });
    update_projectiles();
    update_drops();
    update_supplies();
    update_match_state();
    ++tick_;
}

PlayerCommand World::make_bot_command(const Player& bot) const {
    PlayerCommand command;
    command.entity = bot.id;
    command.command.tick = tick_;

    const Player* target = nullptr;
    Fix32 best_distance =
        Fix32::from_raw(std::numeric_limits<std::int32_t>::max());
    players_.for_each([&](const Player& player) {
        if (!player.alive || player.team == bot.team || player.id == bot.id) {
            return;
        }
        const Vec2 delta = player.position - bot.position;
        const Fix32 distance = delta.length_sq();
        if (!target || distance < best_distance) {
            target = &player;
            best_distance = distance;
        }
    });

    Vec2 aim = bot.aim;
    Vec2 movement_target = target == nullptr ? bot.position : target->position;
    const bool control_objective =
        game_mode_ == GameMode::Control && control_points_enabled_;
    bool has_move_target = false;
    if (control_objective) {
        const ControlPointState* best_point = nullptr;
        Fix32 best_distance =
            Fix32::from_raw(std::numeric_limits<std::int32_t>::max());
        bool best_owned = false;
        const std::uint8_t own_team =
            static_cast<std::uint8_t>(bot.team + 1U);
        for (const ControlPointState& point : control_points_) {
            const AABB area = control_point_area(point);
            const Vec2 center = area.center();
            const Vec2 delta = center - bot.position;
            const Fix32 distance = delta.length_sq();
            const bool owned = point_owner(point) == own_team;
            if (best_point == nullptr ||
                (best_owned && !owned) ||
                (owned == best_owned && distance < best_distance)) {
                best_point = &point;
                best_distance = distance;
                best_owned = owned;
            }
        }
        if (best_point != nullptr) {
            const AABB area = control_point_area(*best_point);
            const Vec2 center = area.center();
            if (target == nullptr || !area.contains(bot.position)) {
                movement_target = center;
            }
            has_move_target = true;
            if (target == nullptr) {
                aim = (center - bot.position).normalized();
            }
        } else if (map_.objective.has_value()) {
            const Vec2 center = map_.objective->area.center();
            if (target == nullptr ||
                !map_.objective->area.contains(bot.position)) {
                movement_target = center;
            }
            has_move_target = true;
            if (target == nullptr) {
                aim = (center - bot.position).normalized();
            }
        }
    } else if (game_mode_ == GameMode::Crystal) {
        const DropDepot* home_depot = nullptr;
        for (const DropDepot& depot : map_.drop_depots) {
            if (depot.team == kDropDepotNeutral ||
                depot.team == bot.team) {
                home_depot = &depot;
                break;
            }
        }

        const Drop* nearest_drop = nullptr;
        Fix32 nearest_distance =
            Fix32::from_raw(std::numeric_limits<std::int32_t>::max());
        drops_.for_each([&](const Drop& drop) {
            if (drop.state == static_cast<std::uint8_t>(DropState::Carried)) {
                return;
            }
            if (drop.state == static_cast<std::uint8_t>(DropState::Home) &&
                drop.respawn_timer > 0) {
                return;
            }
            if (drop.state == static_cast<std::uint8_t>(DropState::Loose) &&
                drop.vacant_ticks > 0) {
                return;
            }
            if (!player_accepts_drop(bot, drop)) {
                return;
            }
            const Fix32 distance =
                (drop.position - bot.position).length_sq();
            if (nearest_drop == nullptr || distance < nearest_distance) {
                nearest_drop = &drop;
                nearest_distance = distance;
            }
        });

        const Player* enemy_carrier = nullptr;
        Fix32 enemy_carrier_distance =
            Fix32::from_raw(std::numeric_limits<std::int32_t>::max());
        const Player* ally_carrier = nullptr;
        Fix32 ally_carrier_distance =
            Fix32::from_raw(std::numeric_limits<std::int32_t>::max());
        players_.for_each([&](const Player& other) {
            if (other.alive == 0 || other.id == bot.id ||
                other.carried_drops == 0) {
                return;
            }
            const Fix32 distance =
                (other.position - bot.position).length_sq();
            if (other.team != bot.team) {
                if (enemy_carrier == nullptr ||
                    distance < enemy_carrier_distance) {
                    enemy_carrier = &other;
                    enemy_carrier_distance = distance;
                }
            } else if (ally_carrier == nullptr ||
                       distance < ally_carrier_distance) {
                ally_carrier = &other;
                ally_carrier_distance = distance;
            }
        });

        if (bot.carried_drops > 0 && home_depot != nullptr) {
            movement_target = home_depot->bounds.center();
            has_move_target = true;
        } else if (enemy_carrier != nullptr &&
                   (nearest_drop == nullptr ||
                    enemy_carrier_distance < nearest_distance)) {
            movement_target = enemy_carrier->position;
            has_move_target = true;
        } else if (nearest_drop != nullptr) {
            movement_target = nearest_drop->position;
            has_move_target = true;
        } else if (ally_carrier != nullptr) {
            movement_target = ally_carrier->position;
            has_move_target = true;
        } else if (home_depot != nullptr) {
            movement_target = home_depot->bounds.center();
            has_move_target = true;
        }
        if (target == nullptr && has_move_target) {
            aim = (movement_target - bot.position).normalized();
        }
    }

    if (bot.alive != 0 && !supplies_.empty()) {
        const HeroStats& hero =
            hero_stats(static_cast<HeroType>(bot.hero));
        const Fix32 max_health = Fix32::from_int(hero.health);
        const Fix32 health_ratio = max_health > Fix32::zero()
            ? bot.health / max_health
            : Fix32::zero();
        const SupplyInstance* supply_target = nullptr;
        Fix32 supply_distance =
            Fix32::from_raw(std::numeric_limits<std::int32_t>::max());
        const Fix32 nearby = Fix32::from_int(20 * 20);
        for (const SupplyInstance& supply : supplies_) {
            const SupplyStats& stats = content().supply(supply.type);
            const Fix32 distance =
                (supply.position - bot.position).length_sq();
            if (stats.kind == SupplyKind::Health) {
                if (health_ratio >= Fix32::from_ratio(6, 10) &&
                    (health_ratio >= Fix32::from_ratio(9, 10) ||
                     distance > nearby)) {
                    continue;
                }
            } else if (stats.kind == SupplyKind::Damage) {
                if (bot.damage_bonus_ticks > 0 || distance > nearby) {
                    continue;
                }
            } else {
                continue;
            }
            if (supply_target == nullptr || distance < supply_distance) {
                supply_target = &supply;
                supply_distance = distance;
            }
        }
        if (supply_target != nullptr) {
            movement_target = supply_target->position;
            has_move_target = true;
            if (target == nullptr) {
                aim = (movement_target - bot.position).normalized();
            }
        }
    }

    if (target != nullptr) {
        const Vec2 aim_delta = target->position - bot.position;
        aim = aim_delta.normalized();
        const Vec2 move_delta = movement_target - bot.position;
        if (move_delta.x > Fix32::from_ratio(4, 5)) {
            command.command.buttons |= kButtonRight;
        } else if (move_delta.x < -Fix32::from_ratio(4, 5)) {
            command.command.buttons |= kButtonLeft;
        }
        if (bot.grounded != 0 &&
            (movement_target.y < bot.position.y - Fix32::from_ratio(1, 2) ||
             (tick_ % 150U) == static_cast<std::uint32_t>((bot.slot * 17U) % 150U))) {
            command.command.buttons |= kButtonJump;
        }
        if (std::abs(aim_delta.x.raw()) < 14 * Fix32::kOne) {
            command.command.buttons |= kButtonFire;
        }
        if (bot.ammo == 0) {
            command.command.buttons |= kButtonReload;
        }
    } else if (has_move_target) {
        const Vec2 move_delta = movement_target - bot.position;
        if (move_delta.x > Fix32::from_ratio(4, 5)) {
            command.command.buttons |= kButtonRight;
        } else if (move_delta.x < -Fix32::from_ratio(4, 5)) {
            command.command.buttons |= kButtonLeft;
        }
        if (bot.grounded != 0 &&
            move_delta.y < -Fix32::from_ratio(1, 2)) {
            command.command.buttons |= kButtonJump;
        }
        if (bot.ammo == 0) {
            command.command.buttons |= kButtonReload;
        }
    } else {
        const bool move_right =
            ((tick_ / 180U) + static_cast<std::uint32_t>(bot.slot)) % 2U == 0U;
        command.command.buttons |= move_right ? kButtonRight : kButtonLeft;
        aim = Vec2{move_right ? Fix32::one() : Fix32::from_int(-1), Fix32::zero()};
        if (bot.grounded != 0 && tick_ % 120U == 0U) {
            command.command.buttons |= kButtonJump;
        }
    }

    const std::int64_t aim_x =
        static_cast<std::int64_t>(aim.x.raw()) * kAimMax / Fix32::kOne;
    const std::int64_t aim_y =
        static_cast<std::int64_t>(aim.y.raw()) * kAimMax / Fix32::kOne;
    command.command.aim_x = static_cast<std::int16_t>(
        std::clamp<std::int64_t>(aim_x, -kAimMax, kAimMax));
    command.command.aim_y = static_cast<std::int16_t>(
        std::clamp<std::int64_t>(aim_y, -kAimMax, kAimMax));
    return command;
}

Vec2 World::decode_aim(const InputCommand& command, std::int8_t facing) const {
    Vec2 aim{
        Fix32::from_raw(static_cast<std::int32_t>(command.aim_x) * 2),
        Fix32::from_raw(static_cast<std::int32_t>(command.aim_y) * 2),
    };
    if (aim.length_sq() == Fix32::zero()) {
        return Vec2{Fix32::from_int(facing), Fix32::zero()};
    }
    return aim.normalized();
}

void World::update_player(Player& player) {
    if (!player.alive) {
        if (player.respawn_timer > 0) {
            --player.respawn_timer;
        }
        if (player.respawn_timer == 0) {
            respawn_player(player);
        }
        return;
    }

    if (player.weapon >= content().weapon_count()) {
        player.weapon = static_cast<std::uint8_t>(WeaponType::Rifle);
    }
    if (player.weapon_switch_cooldown > 0) {
        --player.weapon_switch_cooldown;
    }
    if (player.fire_cooldown > 0) {
        --player.fire_cooldown;
    }
    if (player.dispersion_ticks > 0) {
        --player.dispersion_ticks;
    }
    for (std::uint16_t& cooldown : player.gadget_cooldowns) {
        if (cooldown > 0) {
            --cooldown;
        }
    }
    for (std::size_t slot = 0; slot < kGadgetSlots; ++slot) {
        std::uint16_t& active = player.gadget_active_ticks[slot];
        if (active == 0) {
            continue;
        }
        const GadgetStats& gadget = hero_gadget_stats(
            static_cast<HeroType>(player.hero), slot);
        if (gadget_uses_fuel(gadget.type)) {
            continue;
        }
        --active;
    }
    if (player.invulnerable_ticks > 0) {
        --player.invulnerable_ticks;
    }
    if (player.slow_ticks > 0) {
        --player.slow_ticks;
    }
    if (player.stun_ticks > 0) {
        --player.stun_ticks;
    }
    if (player.heal_ticks > 0) {
        const GadgetStats* heal_gadget = nullptr;
        for (std::size_t slot = 0; slot < kGadgetSlots; ++slot) {
            if (player.gadget_active_ticks[slot] == 0) {
                continue;
            }
            const GadgetStats& candidate = hero_gadget_stats(
                static_cast<HeroType>(player.hero), slot);
            if (candidate.heal > 0 || candidate.heal_percent > 0) {
                heal_gadget = &candidate;
                break;
            }
        }
        if (heal_gadget == nullptr) {
            heal_gadget = &gadget_stats(GadgetType::FirstAid);
        }
        if (player.heal_interval > 0) {
            --player.heal_interval;
        }
        if (player.heal_interval == 0) {
            const Fix32 max_health = Fix32::from_int(
                hero_stats(static_cast<HeroType>(player.hero)).health);
            const Fix32 amount = heal_gadget->heal_percent > 0
                ? max_health * Fix32::from_ratio(
                      static_cast<std::int32_t>(heal_gadget->heal_percent),
                      100)
                : Fix32::from_int(heal_gadget->heal);
            player.health = Fix32::min(
                max_health,
                player.health + amount);
            player.heal_interval = heal_gadget->effect_ticks;
        }
        --player.heal_ticks;
    }
    if (player.supply_heal_ticks > 0) {
        if (player.supply_heal_interval > 0) {
            --player.supply_heal_interval;
        }
        if (player.supply_heal_interval == 0) {
            const Fix32 max_health = Fix32::from_int(
                hero_stats(static_cast<HeroType>(player.hero)).health);
            player.health = Fix32::min(
                max_health,
                player.health +
                    Fix32::from_int(player.supply_heal_amount));
            player.supply_heal_interval = player.supply_heal_period;
        }
        --player.supply_heal_ticks;
        if (player.health >= Fix32::from_int(
                hero_stats(static_cast<HeroType>(player.hero)).health)) {
            player.supply_heal_ticks = 0;
        }
    }
    if (player.damage_bonus_ticks > 0) {
        --player.damage_bonus_ticks;
        if (player.damage_bonus_ticks == 0) {
            player.damage_bonus = Fix32::zero();
        }
    }
    if (player.shield_regen_delay > 0) {
        --player.shield_regen_delay;
    }

    const HeroStats& hero =
        hero_stats(static_cast<HeroType>(player.hero));
    if (player.shield_regen_delay == 0 && player.shield < hero.shield &&
        tick_ % kShieldRegenInterval == 0) {
        ++player.shield;
    }

    const WeaponStats& stats =
        weapon_stats(static_cast<WeaponType>(player.weapon));
    const bool melee_weapon = weapon_is_melee(stats.type);
    if (button_down(player.buttons, kButtonWeaponSwitch) &&
        player.stun_ticks == 0 &&
        player.weapon_switch_cooldown == 0) {
        player.weapon = static_cast<std::uint8_t>(
            next_weapon(static_cast<WeaponType>(player.weapon)));
        player.ammo = weapon_stats(static_cast<WeaponType>(player.weapon)).magazine;
        player.reload_timer = 0;
        player.fire_cooldown = 0;
        player.dispersion_ticks = 0;
        player.warmup_timer = 0;
        player.fire_latch = 0;
        player.weapon_switch_cooldown = kWeaponSwitchCooldown;
    }

    if (player.reload_timer > 0) {
        --player.reload_timer;
        if (player.reload_timer == 0) {
            if (stats.reload_size > 0) {
                const std::uint16_t missing =
                    static_cast<std::uint16_t>(
                        stats.magazine - player.ammo);
                player.ammo = static_cast<std::uint16_t>(
                    player.ammo +
                    std::min(stats.reload_size, missing));
                if (player.ammo < stats.magazine) {
                    player.reload_timer = stats.reload_ticks;
                }
            } else {
                player.ammo = stats.magazine;
            }
        }
    }
    if (!melee_weapon && player.reload_timer == 0 && player.ammo == 0 &&
        player.stun_ticks == 0) {
        player.reload_timer = stats.reload_ticks;
    }
    if (!melee_weapon && player.stun_ticks == 0 &&
        button_down(player.buttons, kButtonReload) &&
        player.reload_timer == 0 &&
        player.ammo < stats.magazine) {
        player.reload_timer = stats.reload_ticks;
    }

    constexpr std::uint16_t gadget_buttons[kGadgetSlots] = {
        kButtonGadget1,
        kButtonGadget2,
        kButtonGadget3,
    };
    for (std::size_t slot = 0; slot < kGadgetSlots; ++slot) {
        const std::uint8_t latch =
            static_cast<std::uint8_t>(1U << slot);
        const bool down = button_down(player.buttons, static_cast<Button>(
            gadget_buttons[slot]));
        if (!down) {
            player.gadget_latch = static_cast<std::uint8_t>(
                player.gadget_latch & ~latch);
            continue;
        }
        if ((player.gadget_latch & latch) == 0U) {
            player.gadget_latch = static_cast<std::uint8_t>(
                player.gadget_latch | latch);
            const GadgetStats& gadget = hero_gadget_stats(
                static_cast<HeroType>(player.hero), slot);
            const bool shock_resist = gadget_is_shock_resist(gadget.type);
            if (gadget_uses_fuel(gadget.type) &&
                player.gadget_active_ticks[slot] > 0) {
                if (player.gadget_fuel[slot] >
                    kJetPackTuning.operating_fuel) {
                    player.gadget_active_ticks[slot] = 0;
                }
                continue;
            }
            if (player.gadget_cooldowns[slot] == 0 &&
                (player.stun_ticks == 0 || shock_resist)) {
                activate_gadget(player, slot);
            }
        }
    }

    const bool stunned = player.stun_ticks > 0;
    bool magnetic_active = false;
    bool hyper_dash_active = false;
    bool kickstart_active = false;
    bool turbo_active = false;
    std::size_t turbo_slot = kGadgetSlots;
    Fix32 dash_accel{};
    Fix32 turbo_accel{};
    Fix32 turbo_limit{};
    for (std::size_t slot = 0; slot < kGadgetSlots; ++slot) {
        if (player.gadget_active_ticks[slot] == 0) {
            continue;
        }
        const GadgetStats& active = hero_gadget_stats(
            static_cast<HeroType>(player.hero), slot);
        if (active.type == GadgetType::MagneticNozzle) {
            magnetic_active = true;
        } else if (active.type == GadgetType::HyperAccelerator) {
            hyper_dash_active = true;
            dash_accel = active.impulse * kGadgetForceToTick;
        } else if (active.type == GadgetType::Kickstart) {
            kickstart_active = true;
            dash_accel = active.impulse * kGadgetForceToTick;
        } else if (active.type == GadgetType::TurboPack) {
            turbo_active = true;
            turbo_slot = slot;
            turbo_accel = active.impulse * kGadgetForceToTick;
            turbo_limit = active.speed * kGadgetLimitToTick;
        }
    }
    if (stunned && turbo_active) {
        player.gadget_active_ticks[turbo_slot] = 0;
        turbo_active = false;
    }
    for (std::size_t slot = 0; slot < kGadgetSlots; ++slot) {
        const GadgetStats& gadget = hero_gadget_stats(
            static_cast<HeroType>(player.hero), slot);
        if (!gadget_uses_fuel(gadget.type)) {
            continue;
        }
        Fix32& fuel = player.gadget_fuel[slot];
        if (player.gadget_active_ticks[slot] == 0) {
            fuel = Fix32::min(
                kJetPackTuning.capacity,
                fuel + kJetPackTuning.refuel_per_tick);
        } else {
            fuel = Fix32::max(
                Fix32::zero(),
                fuel - kJetPackTuning.consumption_per_tick);
            if (fuel <= Fix32::zero()) {
                player.gadget_active_ticks[slot] = 0;
            }
        }
    }
    const bool left = button_down(player.buttons, kButtonLeft);
    const bool right = button_down(player.buttons, kButtonRight);
    const bool up = button_down(player.buttons, kButtonJump);
    const bool down = button_down(player.buttons, kButtonDown);
    const std::int32_t heading_x =
        (right ? 1 : 0) - (left ? 1 : 0);
    const std::int32_t heading_y =
        (up ? 1 : 0) - (down ? 1 : 0);

    const bool dash_active = hyper_dash_active || kickstart_active;
    if (dash_active) {
        Vec2 direction = player.velocity.normalized();
        if (direction == Vec2{}) {
            direction = player.aim.normalized();
        }
        if (direction == Vec2{}) {
            direction = Vec2{
                Fix32::from_int(player.facing),
                Fix32::zero()};
        }
        player.velocity += direction * dash_accel;
    } else if (turbo_active) {
        if (heading_x == 0) {
            player.velocity.x *= kGadgetSlowdownPerTick;
        } else if (heading_x > 0) {
            if (player.velocity.x < turbo_limit) {
                player.velocity.x = Fix32::min(
                    player.velocity.x + turbo_accel,
                    turbo_limit);
            }
        } else if (player.velocity.x > -turbo_limit) {
            player.velocity.x = Fix32::max(
                player.velocity.x - turbo_accel,
                -turbo_limit);
        }
        if (heading_y == 0) {
            player.velocity.y *= kGadgetSlowdownPerTick;
        } else if (heading_y > 0) {
            if (player.velocity.y > -turbo_limit) {
                player.velocity.y = Fix32::max(
                    player.velocity.y - turbo_accel,
                    -turbo_limit);
            }
        } else if (player.velocity.y < turbo_limit) {
            player.velocity.y = Fix32::min(
                player.velocity.y + turbo_accel,
                turbo_limit);
        }
    }
    Fix32 target_speed =
        right ? (left ? Fix32::zero() : hero.run_speed)
              : (left ? -hero.run_speed : Fix32::zero());
    if (stunned) {
        target_speed = Fix32::zero();
    }
    if (player.slow_ticks > 0) {
        target_speed *= Fix32::from_ratio(4, 5);
    }
    if (magnetic_active) {
        target_speed *= Fix32::from_ratio(17, 20);
    }
    if (!dash_active && !turbo_active) {
        if (target_speed == Fix32::zero()) {
            player.velocity.x = move_toward(player.velocity.x, Fix32::zero(), kAcceleration);
        } else {
            player.velocity.x = move_toward(player.velocity.x, target_speed, kAcceleration);
        }
    }

    if (!stunned && !dash_active && !turbo_active &&
        button_down(player.buttons, kButtonJump) && player.grounded) {
        Fix32 jump_speed = hero.jump_speed;
        if (player.slow_ticks > 0) {
            jump_speed *= Fix32::from_ratio(4, 5);
        }
        if (magnetic_active) {
            jump_speed *= Fix32::from_ratio(17, 20);
        }
        const AABB pre_jump_box = player_box(player);
        const Fix32 pad_tolerance = Fix32::from_ratio(2, 100);
        std::uint8_t pad_boost = 0;
        std::uint8_t pad_number = 0;
        for (const JumpPad& pad : map_.jump_pads) {
            ++pad_number;
            if (pre_jump_box.max.x <= pad.bounds.min.x ||
                pre_jump_box.min.x >= pad.bounds.max.x ||
                pre_jump_box.max.y < pad.bounds.min.y - pad_tolerance ||
                pre_jump_box.max.y > pad.bounds.max.y + pad_tolerance) {
                continue;
            }
            pad_boost = pad_number;
            break;
        }
        if (pad_boost != 0) {
            player.pad_boost = pad_boost;
            player.pad_boost_ticks = 10;
        }
        player.velocity.y = -jump_speed;
        player.grounded = 0;
    }

    if (player.pad_boost_ticks > 0 &&
        player.pad_boost != 0 &&
        player.pad_boost <= map_.jump_pads.size()) {
        const JumpPad& pad = map_.jump_pads[player.pad_boost - 1];
        const Fix32 pad_limit =
            pad.velocity * Fix32::from_ratio(13, 10);
        player.velocity.y -=
            pad_limit * Fix32::from_ratio(1, 10);
        if (player.velocity.y < -pad_limit) {
            player.velocity.y = -pad_limit;
        }
        --player.pad_boost_ticks;
        if (player.pad_boost_ticks == 0) {
            player.pad_boost = 0;
        }
    } else if (hyper_dash_active) {
        if (player.grounded != 0) {
            player.velocity.y = Fix32::min(
                player.velocity.y +
                    (player.velocity.y < Fix32::zero()
                         ? kGravityUp
                         : kGravityDown),
                kMaxFallSpeed);
        }
    } else if (kickstart_active) {
        const Fix32 gravity =
            player.velocity.y < Fix32::zero() ? kGravityUp : kGravityDown;
        player.velocity.y += gravity * kKickstartGravityMultiplier;
    } else if (!turbo_active) {
        player.velocity.y = Fix32::min(
            player.velocity.y +
                (player.velocity.y < Fix32::zero() ? kGravityUp : kGravityDown),
            kMaxFallSpeed);
    }

    const bool was_grounded = player.grounded != 0;
    player.grounded = 0;
    const AABB previous_box = player_box(player);
    player.position.x += player.velocity.x;
    AABB box = player_box(player);
    const Fix32 contact_tolerance = Fix32::from_ratio(1, 100);
    const auto try_step_up = [&](const AABB& solid) -> bool {
        if (!was_grounded) {
            return false;
        }
        const Fix32 feet = player.position.y + kPlayerHalfHeight;
        const Fix32 step_height = feet - solid.min.y;
        if (step_height <= Fix32::zero() ||
            step_height > kHorizontalStepUp) {
            return false;
        }
        AABB stepped = box;
        stepped.min.y -= step_height;
        stepped.max.y -= step_height;
        for (const AABB& other : collision_solids_) {
            if (&other == &solid) {
                continue;
            }
            if (stepped.overlaps(other)) {
                return false;
            }
        }
        player.position.y -= step_height;
        box = stepped;
        if (box.overlaps(solid)) {
            player.position.y += step_height;
            box = player_box(player);
            return false;
        }
        return true;
    };
    for (const AABB& solid : collision_solids_) {
        if (!box.overlaps(solid)) {
            continue;
        }
        if (player.velocity.x > Fix32::zero()) {
            if (previous_box.max.x > solid.min.x + contact_tolerance) {
                continue;
            }
            if (try_step_up(solid)) {
                continue;
            }
            player.position.x = solid.min.x - kPlayerHalfWidth;
        } else if (player.velocity.x < Fix32::zero()) {
            if (previous_box.min.x < solid.max.x - contact_tolerance) {
                continue;
            }
            if (try_step_up(solid)) {
                continue;
            }
            player.position.x = solid.max.x + kPlayerHalfWidth;
        } else {
            continue;
        }
        player.velocity.x = Fix32::zero();
        box = player_box(player);
    }

    player.position.x = clamp(
        player.position.x,
        map_.bounds.min.x + kPlayerHalfWidth,
        map_.bounds.max.x - kPlayerHalfWidth);

    const Fix32 previous_bottom = player.position.y + kPlayerHalfHeight;
    const Fix32 landing_tolerance = Fix32::from_ratio(1, 100);
    const Fix32 max_step_up = Fix32::from_ratio(7, 10);
    player.position.y += player.velocity.y;
    box = player_box(player);
    if (player.velocity.y > Fix32::zero()) {
        for (const JumpPad& pad : map_.jump_pads) {
            if (!box.overlaps(pad.bounds) ||
                previous_bottom > pad.bounds.min.y + landing_tolerance) {
                continue;
            }
            player.position.y = pad.bounds.min.y - kPlayerHalfHeight;
            player.velocity.y = Fix32::zero();
            player.grounded = 1;
            box = player_box(player);
            break;
        }
    }
    for (const AABB& solid : collision_solids_) {
        if (!box.overlaps(solid)) {
            continue;
        }
        if (player.velocity.y > Fix32::zero()) {
            if (previous_bottom > solid.min.y + landing_tolerance &&
                solid.min.y - previous_bottom < -max_step_up) {
                const Fix32 push_left =
                    (player.position.x + kPlayerHalfWidth) - solid.min.x;
                const Fix32 push_right =
                    solid.max.x - (player.position.x - kPlayerHalfWidth);
                if (push_left < push_right) {
                    player.position.x = solid.min.x - kPlayerHalfWidth;
                    if (player.velocity.x > Fix32::zero()) {
                        player.velocity.x = Fix32::zero();
                    }
                } else {
                    player.position.x = solid.max.x + kPlayerHalfWidth;
                    if (player.velocity.x < Fix32::zero()) {
                        player.velocity.x = Fix32::zero();
                    }
                }
                player.position.x = clamp(
                    player.position.x,
                    map_.bounds.min.x + kPlayerHalfWidth,
                    map_.bounds.max.x - kPlayerHalfWidth);
                box = player_box(player);
                continue;
            }
            player.position.y = solid.min.y - kPlayerHalfHeight;
            player.velocity.y = Fix32::zero();
            player.grounded = 1;
        } else if (player.velocity.y < Fix32::zero()) {
            if (previous_box.min.y < solid.max.y - landing_tolerance) {
                continue;
            }
            player.position.y = solid.max.y + kPlayerHalfHeight;
            player.velocity.y = Fix32::zero();
        } else {
            continue;
        }
        box = player_box(player);
    }

    if (player.velocity.y > Fix32::zero() &&
        player.drop_through_ticks == 0) {
        for (std::size_t index = 0;
             index < map_.one_way_platforms.size();
             ++index) {
            const AABB& platform = map_.one_way_platforms[index];
            const bool hatchway =
                index >= map_.platform_hatchway.size() ||
                map_.platform_hatchway[index] != 0;
            if (!hatchway ||
                !box.overlaps(platform) ||
                previous_bottom > platform.min.y + landing_tolerance) {
                continue;
            }
            player.position.y = platform.min.y - kPlayerHalfHeight;
            player.velocity.y = Fix32::zero();
            player.grounded = 1;
            box = player_box(player);
            break;
        }
    }

    if (button_down(player.buttons, kButtonDown) &&
        player.grounded != 0 &&
        player.drop_through_ticks == 0) {
        const AABB support_box = player_box(player);
        for (std::size_t index = 0;
             index < map_.one_way_platforms.size();
             ++index) {
            const AABB& platform = map_.one_way_platforms[index];
            const bool hatchway =
                index >= map_.platform_hatchway.size() ||
                map_.platform_hatchway[index] != 0;
            if (!hatchway ||
                support_box.min.x >= platform.max.x ||
                support_box.max.x <= platform.min.x ||
                Fix32::abs(support_box.max.y - platform.min.y) >
                    landing_tolerance) {
                continue;
            }
            player.drop_through_ticks = 3;
            player.grounded = 0;
            player.velocity.y = Fix32::max(
                player.velocity.y,
                Fix32::from_ratio(1, 100));
            break;
        }
    }
    if (player.drop_through_ticks > 0) {
        --player.drop_through_ticks;
    }

    const bool fire_down = button_down(player.buttons, kButtonFire);
    if (stunned) {
        player.fire_latch = 0;
        player.warmup_timer = 0;
    } else if (!fire_down) {
        player.fire_latch = 0;
        player.warmup_timer = 0;
    } else if (player.fire_latch == 0) {
        player.fire_latch = 1;
        player.warmup_timer = stats.warmup_ticks;
        if (player.ammo > 0) {
            player.reload_timer = 0;
        }
    } else if (player.warmup_timer > 0) {
        --player.warmup_timer;
    }
    if (!stunned && fire_down &&
        player.fire_cooldown == 0 &&
        player.reload_timer == 0 &&
        player.warmup_timer == 0 &&
        (melee_weapon || player.ammo > 0)) {
        fire(player);
    }
}

void World::activate_gadget(Player& player, std::size_t slot) {
    if (slot >= kGadgetSlots) {
        return;
    }
    const GadgetStats& gadget = hero_gadget_stats(
        static_cast<HeroType>(player.hero), slot);
    if (gadget.cooldown_ticks == 0) {
        return;
    }
    if (gadget_uses_fuel(gadget.type)) {
        if (player.gadget_active_ticks[slot] > 0 ||
            player.gadget_fuel[slot] < kJetPackTuning.operating_fuel) {
            return;
        }
    }
    player.gadget_cooldowns[slot] = gadget.cooldown_ticks;
    if (gadget.active_ticks > 0) {
        player.gadget_active_ticks[slot] =
            gadget_uses_fuel(gadget.type) ? 1 : gadget.active_ticks;
    }

    if (gadget_is_projectile(gadget.type)) {
        spawn_gadget_projectile(player, gadget);
        return;
    }

    switch (gadget.type) {
        case GadgetType::RubyBlade:
            melee_attack(player, gadget);
            break;
        case GadgetType::PowerBattery:
            player.invulnerable_ticks = std::max(
                player.invulnerable_ticks, gadget.active_ticks);
            player.shield_regen_delay = 0;
            break;
        case GadgetType::MagneticNozzle:
        case GadgetType::Toughness:
        case GadgetType::DoubleShotSystem:
        case GadgetType::Bloodletter:
        case GadgetType::Mutator:
        case GadgetType::Avatar:
            break;
        case GadgetType::Kickstart: {
            Vec2 direction = player.aim.normalized();
            if (direction == Vec2{}) {
                direction = Vec2{
                    Fix32::from_int(player.facing),
                    Fix32::zero()};
            }
            player.velocity = direction * kDashSeed;
            if (direction.y < Fix32::zero()) {
                player.grounded = 0;
            }
            break;
        }
        case GadgetType::TurboPack:
            player.velocity = {};
            player.grounded = 0;
            break;
        case GadgetType::HyperAccelerator: {
            Vec2 direction = player.aim.normalized();
            if (direction == Vec2{}) {
                direction = Vec2{
                    Fix32::from_int(player.facing),
                    Fix32::zero()};
            }
            player.velocity = direction * kDashSeed;
            if (direction.y < Fix32::zero()) {
                player.grounded = 0;
            }
            player.invulnerable_ticks = std::max<std::uint16_t>(
                player.invulnerable_ticks, 60);
            break;
        }
        case GadgetType::Dematerializator:
            player.invulnerable_ticks = std::max(
                player.invulnerable_ticks, gadget.active_ticks);
            break;
        case GadgetType::FirstAid:
        case GadgetType::Injection:
            player.heal_ticks = gadget.active_ticks;
            player.heal_interval = gadget.effect_ticks;
            break;
        case GadgetType::Neutralizer:
        case GadgetType::Inhibitor:
            player.slow_ticks = 0;
            player.stun_ticks = 0;
            break;
        case GadgetType::Count:
            break;
        default:
            break;
    }
}

void World::melee_attack(Player& player, const GadgetStats& gadget) {
    melee_strike(
        player,
        gadget.radius,
        gadget.damage,
        Fix32::zero());
}

void World::melee_strike(
    Player& player,
    Fix32 reach,
    std::uint16_t damage,
    Fix32 critical_ratio) {
    const Fix32 scaled =
        Fix32::from_int(damage) * (Fix32::one() + critical_ratio);
    const std::int32_t rounded = scaled.to_int_round();
    if (rounded <= 0) {
        return;
    }
    const std::uint16_t strike_damage = static_cast<std::uint16_t>(
        std::min<std::int32_t>(rounded, 65535));
    const Fix32 reach_sq = reach * reach;
    players_.for_each([&](Player& target) {
        if (!target.alive || target.team == player.team ||
            target.id == player.id) {
            return;
        }
        const Vec2 delta = target.position - player.position;
        if (delta.length_sq() > reach_sq) {
            return;
        }
        const Vec2 direction = delta.normalized();
        if (direction != Vec2{} &&
            direction.dot(player.aim) < Fix32::from_ratio(1, 2)) {
            return;
        }
        apply_damage(target, strike_damage, player.id);
    });
}

void World::spawn_gadget_projectile(
    Player& player,
    const GadgetStats& gadget) {
    const EntityId id = entities_.create();
    if (!id.valid()) {
        return;
    }

    const GadgetKinematics kinematics = gadget_kinematics(gadget.type);
    const Vec2 muzzle = aim_muzzle_point(
        kinematics.joint_x,
        kinematics.joint_y,
        kinematics.gun_x,
        kinematics.gun_y,
        player.position,
        player.aim);
    const bool grenade = gadget.type == GadgetType::FragGrenade ||
        gadget.type == GadgetType::ConcussionGrenade;
    const bool blocked = grenade &&
        wall_shot_blocked(
            player,
            muzzle,
            Fix32::from_ratio(1, 5),
            WallShotAction::Bounce,
            false);
    Projectile projectile;
    projectile.id = id;
    projectile.owner = player.id;
    projectile.team = player.team;
    projectile.position = blocked ? player.position : muzzle;
    projectile.origin = projectile.position;
    projectile.velocity = player.aim * gadget.speed;
    projectile.half_size = Fix32::from_ratio(1, 5);
    projectile.explosion_radius = gadget.radius;
    projectile.damage = gadget.damage;
    projectile.explosion_damage = gadget.explosion_damage;
    projectile.life_ticks = gadget.projectile_life;
    projectile.slow_ticks = 0;
    projectile.stun_ticks = 0;
    projectile.explosive = 1;
    projectile.terrain_bounces = 0;
    projectile.explode_on_expire = 1;
    projectile.gadget = static_cast<std::uint8_t>(gadget.type) + 1;

    switch (gadget.type) {
        case GadgetType::FragGrenade:
            projectile.half_size = Fix32::from_ratio(1, 5);
            projectile.gravity = Fix32::from_ratio(981, 360000);
            projectile.terrain_bounces = 255;
            projectile.range = Fix32::from_int(20);
            projectile.detonator_hp = Fix32::from_int(150);
            projectile.normal_velocity = Fix32::from_ratio(1, 120);
            projectile.shell_impact = 1;
            projectile.damage_sensor = 1;
            projectile.damage = 0;
            projectile.blast_falloff_start = Fix32::from_ratio(55, 100);
            projectile.blast_falloff_end = Fix32::from_ratio(60, 100);
            projectile.blast_self_ratio = Fix32::from_ratio(70, 100);
            projectile.blast_friend_ratio = Fix32::zero();
            projectile.explosion_impulse = Fix32::from_int(1000);
            projectile.explosion_ground_impulse = Fix32::from_int(300);
            projectile.impulse_falloff_start = Fix32::from_ratio(1, 2);
            projectile.impulse_falloff_end = Fix32::from_ratio(3, 10);
            break;
        case GadgetType::StaticShield:
            projectile.half_size = Fix32::from_ratio(13, 10);
            projectile.stun_ticks = gadget.effect_ticks;
            projectile.damage = 0;
            projectile.blast_self_ratio = Fix32::zero();
            projectile.blast_friend_ratio = Fix32::zero();
            break;
        case GadgetType::RocketLaunch:
            projectile.half_size = Fix32::from_ratio(1, 10);
            projectile.blast_falloff_start = Fix32::from_ratio(55, 100);
            projectile.blast_falloff_end = Fix32::from_ratio(60, 100);
            projectile.blast_self_ratio = Fix32::from_ratio(70, 100);
            projectile.blast_friend_ratio = Fix32::zero();
            projectile.explosion_impulse = Fix32::from_int(700);
            projectile.explosion_ground_impulse = Fix32::from_int(200);
            projectile.impulse_falloff_start = Fix32::from_ratio(1, 2);
            projectile.impulse_falloff_end = Fix32::from_ratio(3, 10);
            break;
        case GadgetType::ProtectiveDroid:
            projectile.half_size = Fix32::from_ratio(1, 5);
            projectile.slow_ticks = gadget.effect_ticks;
            projectile.damage = 0;
            break;
        case GadgetType::RocketSalvo:
            projectile.half_size = Fix32::from_ratio(1, 10);
            projectile.blast_falloff_start = Fix32::from_ratio(55, 100);
            projectile.blast_falloff_end = Fix32::from_ratio(60, 100);
            projectile.blast_self_ratio = Fix32::from_ratio(70, 100);
            projectile.blast_friend_ratio = Fix32::zero();
            projectile.explosion_impulse = Fix32::from_int(700);
            projectile.explosion_ground_impulse = Fix32::from_int(200);
            projectile.impulse_falloff_start = Fix32::from_ratio(1, 2);
            projectile.impulse_falloff_end = Fix32::from_ratio(3, 10);
            break;
        case GadgetType::ConcussionGrenade:
            projectile.half_size = Fix32::from_ratio(1, 5);
            projectile.gravity = Fix32::from_ratio(981, 360000);
            projectile.terrain_bounces = 255;
            projectile.range = Fix32::from_int(20);
            projectile.detonator_hp = Fix32::from_int(150);
            projectile.normal_velocity = Fix32::from_ratio(1, 120);
            projectile.shell_impact = 1;
            projectile.damage_sensor = 1;
            projectile.damage = 0;
            projectile.stun_ticks = gadget.effect_ticks;
            projectile.blast_self_ratio = Fix32::from_ratio(70, 100);
            projectile.blast_friend_ratio = Fix32::zero();
            projectile.explosion_impulse = Fix32::from_int(1000);
            projectile.explosion_ground_impulse = Fix32::from_int(300);
            projectile.impulse_falloff_start = Fix32::from_ratio(1, 2);
            projectile.impulse_falloff_end = Fix32::from_ratio(3, 10);
            break;
        case GadgetType::FragMine:
            projectile.half_size = Fix32::from_ratio(1, 5);
            projectile.damage = 0;
            projectile.blast_self_ratio = Fix32::zero();
            projectile.blast_friend_ratio = Fix32::zero();
            projectile.explosion_impulse = Fix32::from_int(1000);
            projectile.explosion_ground_impulse = Fix32::from_int(300);
            projectile.impulse_falloff_start = Fix32::from_ratio(1, 2);
            projectile.impulse_falloff_end = Fix32::from_ratio(3, 10);
            break;
        case GadgetType::Boomerang:
            projectile.half_size = Fix32::from_ratio(2, 5);
            projectile.explosive = 0;
            projectile.terrain_bounces = 3;
            projectile.explode_on_expire = 0;
            break;
        case GadgetType::Trap:
            projectile.half_size = Fix32::from_ratio(1, 5);
            projectile.damage = 0;
            projectile.slow_ticks = gadget.effect_ticks;
            projectile.blast_self_ratio = Fix32::zero();
            projectile.blast_friend_ratio = Fix32::zero();
            break;
        case GadgetType::Sprayer:
            projectile.half_size = Fix32::from_ratio(1, 10);
            projectile.damage = 0;
            projectile.blast_self_ratio = Fix32::zero();
            projectile.blast_friend_ratio = Fix32::zero();
            break;
        case GadgetType::Chemicals:
            projectile.half_size = Fix32::from_ratio(1, 10);
            projectile.damage = 0;
            projectile.slow_ticks = gadget.effect_ticks;
            projectile.blast_self_ratio = Fix32::zero();
            projectile.blast_friend_ratio = Fix32::zero();
            break;
        case GadgetType::Chakram:
            projectile.half_size = Fix32::from_ratio(2, 5);
            projectile.explosive = 0;
            projectile.terrain_bounces = 2;
            projectile.explode_on_expire = 0;
            break;
        case GadgetType::DemonicFace:
            projectile.half_size = Fix32::one();
            projectile.explosive = 0;
            projectile.explode_on_expire = 0;
            break;
        case GadgetType::FireTrap:
            projectile.half_size = Fix32::from_ratio(2, 5);
            projectile.damage = 0;
            projectile.slow_ticks = gadget.effect_ticks;
            projectile.blast_self_ratio = Fix32::zero();
            projectile.blast_friend_ratio = Fix32::zero();
            break;
        case GadgetType::ArmCyberProsthesis:
            projectile.half_size = Fix32::from_ratio(1, 5);
            projectile.damage = 0;
            projectile.stun_ticks = gadget.effect_ticks;
            projectile.blast_self_ratio = Fix32::zero();
            projectile.blast_friend_ratio = Fix32::zero();
            break;
        default:
            break;
    }
    projectiles_.add(id, projectile);
}

void World::fire(Player& player) {
    const WeaponStats& stats =
        weapon_stats(static_cast<WeaponType>(player.weapon));
    const std::uint16_t pellet_count = stats.pellets == 0 ? 1 : stats.pellets;
    std::uint16_t damage = stats.damage;
    std::uint16_t damage_bonus_percent = 0;
    for (std::size_t slot = 0; slot < kGadgetSlots; ++slot) {
        if (player.gadget_active_ticks[slot] == 0) {
            continue;
        }
        const GadgetStats& active = hero_gadget_stats(
            static_cast<HeroType>(player.hero), slot);
        damage_bonus_percent = std::max<std::uint16_t>(
            damage_bonus_percent, active.damage_percent);
    }
    if (damage_bonus_percent > 0) {
        damage = static_cast<std::uint16_t>(
            (Fix32::from_int(damage) *
             (Fix32::one() +
              Fix32::from_ratio(damage_bonus_percent, 100)))
                .to_int_round());
    }

    if (weapon_is_melee(stats.type)) {
        Fix32 critical_ratio{};
        if (stats.critical_chance > Fix32::zero()) {
            const std::int32_t roll =
                static_cast<std::int32_t>(rng_.bounded(1000));
            if (Fix32::from_ratio(roll, 1000) < stats.critical_chance) {
                critical_ratio = stats.critical_ratio;
            }
        }
        melee_strike(player, stats.range, damage, critical_ratio);
        player.fire_cooldown = stats.fire_interval;
        return;
    }

    const auto rotate_aim = [](Vec2 direction, Fix32 degrees) {
        const Fix32 cosine = cos_deg(degrees);
        const Fix32 sine = sin_deg(degrees);
        return Vec2{
            direction.x * cosine - direction.y * sine,
            direction.x * sine + direction.y * cosine};
    };

    Vec2 base_aim = player.aim;
    if (player.dispersion_ticks > 0 &&
        stats.dispersion_degrees > Fix32::zero()) {
        const std::int32_t roll =
            static_cast<std::int32_t>(rng_.bounded(1001)) - 500;
        const Fix32 angle =
            stats.dispersion_degrees * Fix32::from_ratio(roll, 1000);
        base_aim = rotate_aim(base_aim, angle);
    }
    player.dispersion_ticks = stats.dispersion_ticks;

    const WallShotAction wall_action = stats.wall_shot_action;
    const Vec2 muzzle = aim_muzzle_point(stats, player.position, base_aim);
    const bool wall_shot = wall_shot_blocked(
        player,
        muzzle,
        stats.projectile_half_size,
        wall_action,
        stats.platform_puncher != 0);
    if (wall_shot && wall_action == WallShotAction::Skip) {
        player.fire_cooldown = stats.fire_interval;
        return;
    }
    const Vec2 spawn = wall_shot ? player.position : muzzle;

    Fix32 critical_ratio{};
    if (stats.critical_chance > Fix32::zero()) {
        const std::int32_t roll =
            static_cast<std::int32_t>(rng_.bounded(1000));
        if (Fix32::from_ratio(roll, 1000) < stats.critical_chance) {
            critical_ratio = stats.critical_ratio;
        }
    }

    for (std::uint16_t pellet = 0; pellet < pellet_count; ++pellet) {
        Vec2 direction = base_aim;
        if (pellet_count > 1 &&
            stats.divergence_degrees > Fix32::zero()) {
            const Fix32 step =
                stats.divergence_degrees /
                Fix32::from_int(static_cast<std::int32_t>(pellet_count - 1));
            const Fix32 angle =
                step * Fix32::from_int(
                           static_cast<std::int32_t>(2 * pellet) -
                           static_cast<std::int32_t>(pellet_count) + 1);
            direction = rotate_aim(direction, angle);
        } else {
            const std::int32_t spread_x =
                static_cast<std::int32_t>(rng_.bounded(2001)) - 1000;
            const std::int32_t spread_y =
                static_cast<std::int32_t>(rng_.bounded(2001)) - 1000;
            direction =
                (base_aim +
                 Vec2{stats.spread * Fix32::from_ratio(spread_x, 1000),
                      stats.spread * Fix32::from_ratio(spread_y, 1000)})
                    .normalized();
        }
        if (direction == Vec2{}) {
            continue;
        }

        Projectile projectile;
        projectile.owner = player.id;
        projectile.team = player.team;
        projectile.position = spawn;
        projectile.origin = spawn;
        projectile.velocity = direction * stats.projectile_speed;
        projectile.half_size = stats.projectile_half_size;
        projectile.range = stats.range;
        projectile.falloff_start = stats.falloff_start;
        projectile.falloff_end = stats.falloff_end;
        projectile.platform_puncher = stats.platform_puncher;
        projectile.blast_falloff_start = stats.blast_falloff_start;
        projectile.blast_falloff_end = stats.blast_falloff_end;
        projectile.blast_self_ratio = stats.blast_self_ratio;
        projectile.blast_friend_ratio = stats.blast_friend_ratio;
        projectile.explosion_impulse = stats.explosion_impulse;
        projectile.explosion_ground_impulse =
            stats.explosion_ground_impulse;
        projectile.impulse_falloff_start = stats.impulse_falloff_start;
        projectile.impulse_falloff_end = stats.impulse_falloff_end;
        projectile.critical_ratio = critical_ratio;
        projectile.damage = damage;
        projectile.life_ticks = stats.projectile_life;
        projectile.explosive = stats.explosive;
        projectile.explosion_radius = stats.explosion_radius;
        projectile.explosion_damage = stats.explosion_damage;
        projectile.terrain_bounces = stats.terrain_bounces;
        projectile.gravity = stats.gravity;
        projectile.normal_velocity = stats.normal_velocity;
        projectile.damping_rate = stats.damping_rate;
        projectile.shell_impact = stats.shell_impact;
        projectile.damage_sensor = stats.damage_sensor;
        projectile.detonator_hp = Fix32::from_int(stats.detonator_hp);
        if (stats.terrain_bounces > 1 && stats.terrain_bounces != 255 &&
            stats.detonator_hp > 0) {
            projectile.detonator_hp_step =
                projectile.detonator_hp /
                Fix32::from_int(stats.terrain_bounces);
        }
        projectile.ignore_wall =
            wall_action == WallShotAction::Ignore ? 1 : 0;
        projectile.explode_on_expire = stats.explode_on_expire;

        if (wall_shot && wall_action == WallShotAction::Detonate) {
            if (projectile.explosive != 0) {
                explode(projectile);
            }
            continue;
        }

        const EntityId id = entities_.create();
        if (!id.valid()) {
            break;
        }
        projectile.id = id;
        projectiles_.add(id, projectile);
    }

    --player.ammo;
    player.fire_cooldown = stats.fire_interval;
}

bool World::wall_shot_blocked(
    const Player& player,
    Vec2 muzzle,
    Fix32 half_size,
    WallShotAction action,
    bool platform_puncher) const {
    if (action == WallShotAction::Ignore) {
        return false;
    }
    const Segment path{player.position, muzzle};
    const bool wide = action != WallShotAction::Detonate;
    const AABB hero = player_box(player);
    const auto blocks = [&](const AABB& solid, bool platform) {
        // Mirrors GunControllerImpl::checkWallShot: only Platform items are
        // ignored while the hero overlaps them, and standing on top of a
        // platform is contact rather than overlap.  Walls and rocks always
        // count as blockers.
        if (platform && hero.overlaps(solid)) {
            return false;
        }
        const AABB box = wide ? solid.expanded(half_size) : solid;
        Fix32 t_min{};
        Fix32 t_max{};
        return segment_intersects_aabb(path, box, t_min, t_max);
    };
    if (!map_.borders.empty()) {
        const Fix32 radius = wide ? half_size : Fix32::zero();
        for (const Segment& border : map_.borders) {
            Fix32 hit_t{};
            Vec2 normal{};
            if (swept_point_hits_segment(
                    path, border, radius, hit_t, normal)) {
                return true;
            }
        }
    } else {
        for (const AABB& solid : map_.solids) {
            if (blocks(solid, false)) {
                return true;
            }
        }
    }
    if (!platform_puncher) {
        if (!map_.platform_shapes.empty()) {
            const Fix32 radius = wide ? half_size : Fix32::zero();
            for (std::size_t index = 0;
                 index < map_.platform_shapes.size();
                 ++index) {
                if (index < map_.one_way_platforms.size() &&
                    hero.overlaps(map_.one_way_platforms[index])) {
                    continue;
                }
                const std::array<Vec2, 4>& shape =
                    map_.platform_shapes[index];
                for (std::size_t edge = 0; edge < shape.size(); ++edge) {
                    Fix32 hit_t{};
                    Vec2 normal{};
                    if (swept_point_hits_segment(
                            path,
                            Segment{
                                shape[edge],
                                shape[(edge + 1) % shape.size()]},
                            radius,
                            hit_t,
                            normal,
                            true)) {
                        return true;
                    }
                }
            }
        } else {
            for (const AABB& platform : map_.one_way_platforms) {
                if (blocks(platform, true)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool World::projectile_in_range(const Projectile& projectile) {
    if (projectile.range <= Fix32::zero()) {
        return true;
    }
    const Vec2 delta = projectile.position - projectile.origin;
    return delta.length_sq() <= projectile.range * projectile.range;
}

void World::resolve_projectile_collisions() {
    std::vector<EntityId> ids;
    projectiles_.for_each([&](const Projectile& projectile) {
        ids.push_back(projectile.id);
    });
    std::sort(ids.begin(), ids.end(), entity_less);
    std::vector<std::uint8_t> removed(ids.size(), 0);

    for (std::size_t first = 0; first < ids.size(); ++first) {
        if (removed[first] != 0) {
            continue;
        }
        Projectile* a = projectiles_.get(ids[first]);
        if (a == nullptr) {
            continue;
        }
        for (std::size_t second = first + 1; second < ids.size(); ++second) {
            if (removed[second] != 0) {
                continue;
            }
            Projectile* b = projectiles_.get(ids[second]);
            if (b == nullptr) {
                continue;
            }
            if (a->owner == b->owner || a->team == b->team) {
                continue;
            }
            if (a->explosive == 0 && b->explosive == 0 &&
                a->damage_sensor == 0 && b->damage_sensor == 0) {
                continue;
            }
            const Fix32 sum = a->half_size + b->half_size;
            const Vec2 relative_start = a->position - b->position;
            const Vec2 relative_end =
                (a->position + a->velocity) -
                (b->position + b->velocity);
            const AABB box{Vec2{-sum, -sum}, Vec2{sum, sum}};
            Fix32 t_min{};
            Fix32 t_max{};
            if (!segment_intersects_aabb(
                    Segment{relative_start, relative_end},
                    box,
                    t_min,
                    t_max)) {
                continue;
            }

            const bool a_detonates =
                a->explosive != 0 && a->shell_impact != 0;
            const bool b_detonates =
                b->explosive != 0 && b->shell_impact != 0;
            bool remove_a = false;
            bool remove_b = false;
            if (a_detonates || b_detonates) {
                if (a_detonates) {
                    explode(*a);
                    remove_a = true;
                }
                if (b_detonates) {
                    explode(*b);
                    remove_b = true;
                }
                if (!a_detonates) {
                    remove_a = true;
                }
                if (!b_detonates) {
                    remove_b = true;
                }
            } else {
                if (a->damage_sensor != 0 && a->detonator_hp > Fix32::zero() &&
                    b->damage > 0) {
                    a->detonator_hp -= Fix32::from_int(b->damage);
                    remove_b = true;
                    if (a->detonator_hp <= Fix32::from_ratio(1, 1000)) {
                        explode(*a);
                        remove_a = true;
                    }
                }
                if (b->damage_sensor != 0 && b->detonator_hp > Fix32::zero() &&
                    a->damage > 0) {
                    b->detonator_hp -= Fix32::from_int(a->damage);
                    remove_a = true;
                    if (b->detonator_hp <= Fix32::from_ratio(1, 1000)) {
                        explode(*b);
                        remove_b = true;
                    }
                }
            }
            if (remove_a) {
                removed[first] = 1;
            }
            if (remove_b) {
                removed[second] = 1;
            }
            if (removed[first] != 0) {
                break;
            }
        }
    }

    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (removed[index] == 0) {
            continue;
        }
        projectiles_.remove(ids[index]);
        entities_.destroy(ids[index]);
    }
}

void World::update_projectiles() {
    resolve_projectile_collisions();
    std::vector<EntityId> removals;

    projectiles_.for_each([&](Projectile& projectile) {
        if (projectile.gadget ==
            static_cast<std::uint8_t>(GadgetType::StaticShield) + 1) {
            const Player* owner = players_.get(projectile.owner);
            if (owner == nullptr || owner->alive == 0) {
                explode(projectile);
                removals.push_back(projectile.id);
                return;
            }
            projectile.position = owner->position;
            projectile.velocity = owner->velocity;
            bool touched = false;
            players_.for_each([&](const Player& player) {
                if (touched || !player.alive ||
                    player.team == projectile.team) {
                    return;
                }
                const AABB box =
                    player_box(player).expanded(projectile.half_size);
                if (box.contains(projectile.position)) {
                    touched = true;
                }
            });
            if (touched) {
                explode(projectile);
                removals.push_back(projectile.id);
                return;
            }
            if (projectile.life_ticks > 0) {
                --projectile.life_ticks;
            }
            if (projectile.life_ticks == 0) {
                if (projectile.explode_on_expire != 0) {
                    explode(projectile);
                }
                removals.push_back(projectile.id);
            }
            return;
        }
        const bool first_update = projectile.traveled == Fix32::zero();
        projectile.velocity.y += projectile.gravity;
        const Vec2 previous = projectile.position;
        projectile.position += projectile.velocity;
        const Fix32 movement_length =
            (projectile.position - previous).length();
        projectile.traveled += movement_length;

        bool remove = false;
        bool detonate = false;

        const bool grenade_like =
            projectile.explosive != 0 &&
            projectile.gravity > Fix32::zero();
        const bool laser_like =
            projectile.gadget == 0 && projectile.explosive == 0 &&
            projectile.terrain_bounces > 0;
        const bool floor_only = grenade_like || laser_like;
        const bool infinite_floor = projectile.terrain_bounces == 255;
        const Fix32 bounce_restore = grenade_like
            ? Fix32::from_ratio(1, 2)
            : (laser_like ? Fix32::from_ratio(9, 10)
                          : Fix32::from_ratio(3, 4));
        const Fix32 floor_threshold = projectile.normal_velocity >
                Fix32::zero()
            ? projectile.normal_velocity
            : (grenade_like ? Fix32::from_ratio(1, 120)
                            : Fix32::zero());

        const auto finish_impact = [&]() {
            remove = true;
            detonate = projectile.explosive != 0;
        };

        const auto bounce = [&](bool horizontal, Vec2 restore,
                                Fix32 restitution) {
            if (horizontal) {
                projectile.velocity.x = -projectile.velocity.x * restitution;
                projectile.position.x = restore.x;
            } else {
                projectile.velocity.y = -projectile.velocity.y * restitution;
                projectile.position.y = restore.y;
            }
        };

        const auto hit_terrain = [&](bool horizontal, Vec2 restore,
                                     bool from_above) {
            if (floor_only) {
                if (!from_above) {
                    bounce(horizontal, restore, bounce_restore);
                    return;
                }
                const bool hard_enough =
                    Fix32::abs(projectile.velocity.y) >= floor_threshold;
                if (!hard_enough || infinite_floor) {
                    if (projectile.damping_rate != Fix32::one()) {
                        projectile.detonate_damping *=
                            projectile.damping_rate;
                    }
                    bounce(horizontal, restore, bounce_restore);
                    return;
                }
            }
            if (projectile.terrain_bounces == 0) {
                finish_impact();
                return;
            }
            if (projectile.detonator_hp > Fix32::zero() &&
                projectile.detonator_hp_step > Fix32::zero()) {
                projectile.detonator_hp -= projectile.detonator_hp_step;
                if (projectile.detonator_hp <=
                    Fix32::from_ratio(1, 1000)) {
                    finish_impact();
                    return;
                }
            }
            --projectile.terrain_bounces;
            if (projectile.terrain_bounces == 0) {
                finish_impact();
                return;
            }
            if (projectile.damping_rate != Fix32::one()) {
                projectile.detonate_damping *= projectile.damping_rate;
            }
            bounce(horizontal, restore, bounce_restore);
        };

        Fix32 impact_t =
            Fix32::from_raw(std::numeric_limits<std::int32_t>::max());
        bool found = false;
        const AABB* impact_box = nullptr;
        Fix32 impact_padding = Fix32::zero();
        EntityId impact_player = EntityId::none();
        bool impact_border = false;
        Vec2 impact_border_normal{};
        Vec2 impact_start = previous;
        Vec2 impact_end = projectile.position;
        Fix32 impact_distance = Fix32::zero();

        const auto trace = [&](const Segment& path, Fix32 padding) {
            const auto hit_box = [&](const AABB& solid, Fix32 pad) {
                AABB box = pad > Fix32::zero() ? solid.expanded(pad) : solid;
                if (pad > Fix32::zero() && box.contains(path.a) &&
                    !solid.contains(path.a)) {
                    box = solid;
                    pad = Fix32::zero();
                }
                Fix32 t_min{};
                Fix32 t_max{};
                if (!segment_intersects_aabb(path, box, t_min, t_max)) {
                    return;
                }
                const Fix32 t = Fix32::min(
                    Fix32::max(t_min, Fix32::zero()),
                    Fix32::one());
                if (!found || t < impact_t) {
                    found = true;
                    impact_t = t;
                    impact_box = &solid;
                    impact_padding = pad;
                    impact_player = EntityId::none();
                    impact_border = false;
                }
            };
            const auto hit_segment = [&](const Segment& segment,
                                         bool one_sided) {
                Fix32 t{};
                Vec2 normal{};
                if (!swept_point_hits_segment(
                        path, segment, padding, t, normal, one_sided)) {
                    return;
                }
                if (!found || t < impact_t) {
                    found = true;
                    impact_t = t;
                    impact_box = nullptr;
                    impact_padding = Fix32::zero();
                    impact_player = EntityId::none();
                    impact_border = true;
                    impact_border_normal = normal;
                }
            };
            for (const AABB& solid : projectile_solids_) {
                hit_box(solid, padding);
            }
            if (projectile.platform_puncher == 0) {
                if (!projectile_platform_segments_.empty()) {
                    for (const Segment& segment :
                         projectile_platform_segments_) {
                        hit_segment(segment, true);
                    }
                } else {
                    for (std::size_t index = 0;
                         index < map_.one_way_platforms.size();
                         ++index) {
                        const bool hatchway =
                            index >= map_.platform_hatchway.size() ||
                            map_.platform_hatchway[index] != 0;
                        if (!hatchway) {
                            continue;
                        }
                        hit_box(map_.one_way_platforms[index], padding);
                    }
                }
            }
            for (const Segment& border : map_.borders) {
                hit_segment(border, false);
            }
            players_.for_each([&](Player& player) {
                if (!player.alive || player.team == projectile.team ||
                    player.id == projectile.owner) {
                    return;
                }
                const AABB box =
                    player_box(player).expanded(projectile.half_size);
                Fix32 t_min{};
                Fix32 t_max{};
                if (!segment_intersects_aabb(path, box, t_min, t_max)) {
                    return;
                }
                const Fix32 t = Fix32::min(
                    Fix32::max(t_min, Fix32::zero()),
                    Fix32::one());
                if (!found || t < impact_t) {
                    found = true;
                    impact_t = t;
                    impact_box = nullptr;
                    impact_player = player.id;
                    impact_border = false;
                }
            });
        };

        const auto bounce_normal = [&](Vec2 normal, Fix32 restitution) {
            const Fix32 along = projectile.velocity.dot(normal);
            if (along < Fix32::zero()) {
                projectile.velocity -= normal *
                    (along * (Fix32::one() + restitution));
            }
            if (impact_t > Fix32::zero()) {
                projectile.position =
                    impact_start +
                    (impact_end - impact_start) * impact_t +
                    normal * Fix32::from_ratio(1, 1000);
            } else {
                projectile.position = impact_start;
            }
        };

        const auto hit_border = [&](Vec2 normal) {
            const Fix32 normal_velocity_value =
                projectile.velocity.dot(normal);
            if (floor_only) {
                const bool floor = normal.y < Fix32::zero();
                if (!floor) {
                    bounce_normal(normal, bounce_restore);
                    return;
                }
                const bool hard_enough =
                    Fix32::abs(normal_velocity_value) >= floor_threshold;
                if (!hard_enough || infinite_floor) {
                    if (projectile.damping_rate != Fix32::one()) {
                        projectile.detonate_damping *=
                            projectile.damping_rate;
                    }
                    bounce_normal(normal, bounce_restore);
                    return;
                }
            }
            if (projectile.terrain_bounces == 0) {
                finish_impact();
                return;
            }
            if (projectile.detonator_hp > Fix32::zero() &&
                projectile.detonator_hp_step > Fix32::zero()) {
                projectile.detonator_hp -= projectile.detonator_hp_step;
                if (projectile.detonator_hp <=
                    Fix32::from_ratio(1, 1000)) {
                    finish_impact();
                    return;
                }
            }
            --projectile.terrain_bounces;
            if (projectile.terrain_bounces == 0) {
                finish_impact();
                return;
            }
            if (projectile.damping_rate != Fix32::one()) {
                projectile.detonate_damping *= projectile.damping_rate;
            }
            bounce_normal(normal, bounce_restore);
        };

        // Fast shells leave the muzzle within one tick.  Sweep from the
        // shooter's centre to the muzzle first: a wall or a point-blank
        // enemy in front of the shooter must stop the shell instead of
        // letting it spawn on the far side.
        const Fix32 trace_padding =
            floor_only ? projectile.half_size : Fix32::zero();
        if (first_update && projectile.ignore_wall == 0) {
            const Player* owner = players_.get(projectile.owner);
            if (owner != nullptr) {
                impact_start = owner->position;
                impact_end = previous;
                trace(Segment{owner->position, previous}, Fix32::zero());
            }
        }

        // Pick the nearest obstacle along the tick so a wall behind a
        // target no longer swallows the bullet before the player check.
        if (!found) {
            impact_start = previous;
            trace(Segment{previous, projectile.position}, trace_padding);
            if (found) {
                impact_distance = movement_length * impact_t;
            }
        }

        if (found) {
            const Vec2 impact_position =
                impact_start + (impact_end - impact_start) * impact_t;
            if (impact_player.valid()) {
                Player* player = players_.get(impact_player);
                if (player != nullptr) {
                    const bool in_range =
                        projectile.range <= Fix32::zero() ||
                        (impact_position - projectile.origin).length_sq() <=
                            projectile.range * projectile.range;
                    std::uint16_t hit_damage = in_range
                        ? projectile_hit_damage(
                              projectile, impact_distance)
                        : 0;
                    if (hit_damage > 0 &&
                        projectile.detonate_damping != Fix32::one()) {
                        const Fix32 scaled =
                            Fix32::from_int(hit_damage) *
                            projectile.detonate_damping;
                        const std::int32_t rounded = scaled.to_int_round();
                        hit_damage = static_cast<std::uint16_t>(
                            std::clamp(rounded, 0, 65535));
                    }
                    apply_damage(*player, hit_damage, projectile.owner);
                }
                finish_impact();
            } else if (impact_border) {
                hit_border(impact_border_normal);
            } else if (impact_box != nullptr) {
                const Fix32 top = impact_box->min.y - impact_padding;
                const Fix32 bottom = impact_box->max.y + impact_padding;
                const bool from_above =
                    impact_start.y <= top && projectile.position.y >= top;
                const bool from_below =
                    impact_start.y >= bottom && projectile.position.y <= bottom;
                hit_terrain(
                    !from_above && !from_below, impact_position, from_above);
            }
        }


        if (!remove) {
            if (projectile.life_ticks > 0) {
                --projectile.life_ticks;
            }
            if (projectile.life_ticks == 0) {
                remove = true;
                detonate = projectile.explode_on_expire != 0;
            }
        }

        if (detonate) {
            explode(projectile);
        }
        if (remove) {
            removals.push_back(projectile.id);
        }
    });

    for (const EntityId id : removals) {
        projectiles_.remove(id);
        entities_.destroy(id);
    }
}
void World::apply_damage(
    Player& player,
    std::uint16_t damage,
    EntityId attacker_id) {
    if (!player.alive || damage == 0 || player.invulnerable_ticks > 0) {
        return;
    }

    std::uint16_t damage_reduction = 0;
    for (std::size_t slot = 0; slot < kGadgetSlots; ++slot) {
        if (player.gadget_active_ticks[slot] == 0) {
            continue;
        }
        const GadgetStats& active = hero_gadget_stats(
            static_cast<HeroType>(player.hero), slot);
        damage_reduction = std::max<std::uint16_t>(
            damage_reduction, active.damage_reduction_percent);
    }
    if (damage_reduction >= 100) {
        return;
    }
    if (damage_reduction > 0) {
        damage = static_cast<std::uint16_t>(
            (static_cast<std::uint32_t>(damage) *
             (100U - damage_reduction) + 99U) /
            100U);
    }

    Player* attacker = players_.get(attacker_id);
    if (attacker != nullptr && attacker->damage_bonus > Fix32::zero()) {
        const Fix32 boosted = Fix32::from_int(damage) *
            (Fix32::one() + attacker->damage_bonus);
        damage = static_cast<std::uint16_t>(
            std::clamp(boosted.to_int_round(), 0, 65535));
    }
    // health_pump is removed by damage in the original buff definition.
    player.supply_heal_ticks = 0;
    const std::uint16_t absorbed =
        std::min<std::uint16_t>(player.shield, damage);
    const std::uint16_t hp_damage =
        static_cast<std::uint16_t>(damage - absorbed);
    player.shield_regen_delay = kShieldRegenDelay;
    if (absorbed > 0) {
        player.shield = static_cast<std::uint16_t>(
            player.shield - absorbed);
        if (attacker == nullptr || attacker->id != player.id) {
            const std::uint64_t total =
                static_cast<std::uint64_t>(player.blocked_damage) +
                static_cast<std::uint64_t>(absorbed);
            player.blocked_damage = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(
                    total,
                    std::numeric_limits<std::uint32_t>::max()));
        }
    }

    const Fix32 damage_value =
        Fix32::from_int(static_cast<std::int32_t>(hp_damage));
    const Fix32 applied = Fix32::min(player.health, damage_value);
    const bool credited =
        attacker != nullptr && attacker->id != player.id &&
        attacker->team != player.team;
    if (credited && applied > Fix32::zero()) {
        const std::uint64_t total =
            static_cast<std::uint64_t>(attacker->damage_dealt) +
            static_cast<std::uint64_t>(applied.raw() / Fix32::kOne);
        attacker->damage_dealt = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(
                total,
                std::numeric_limits<std::uint32_t>::max()));
    }

    player.health -= damage_value;
    if (player.health > Fix32::zero()) {
        return;
    }

    player.health = Fix32::zero();
    player.shield = 0;
    player.alive = 0;
    player.respawn_timer = kRespawnTicks;
    player.velocity = {};
    player.buttons = 0;
    player.drop_through_ticks = 0;
    player.pad_boost = 0;
    player.pad_boost_ticks = 0;
    drop_carried_drops(player);
    if (player.deaths != std::numeric_limits<std::uint16_t>::max()) {
        ++player.deaths;
    }
    if (credited &&
        attacker->kills != std::numeric_limits<std::uint16_t>::max()) {
        ++attacker->kills;
    }
    if (game_mode_ == GameMode::Tdm && player.team < scores_.size()) {
        ++scores_[1 - player.team];
    }
}

void World::explode(const Projectile& projectile) {
    if (projectile.explosive == 0 || !projectile_in_range(projectile)) {
        return;
    }
    const Fix32 radius_sq =
        projectile.explosion_radius * projectile.explosion_radius;
    players_.for_each([&](Player& player) {
        if (!player.alive) {
            return;
        }
        const Vec2 delta = player.position - projectile.position;
        const Fix32 distance_sq = delta.length_sq();
        if (distance_sq <= radius_sq) {
            const Fix32 distance = Fix32::sqrt(distance_sq);
            Fix32 factor = Fix32::one();
            if (projectile.explosion_radius > Fix32::zero() &&
                projectile.blast_falloff_start < Fix32::one()) {
                const Fix32 ratio =
                    distance / projectile.explosion_radius;
                if (ratio > projectile.blast_falloff_start) {
                    const Fix32 span =
                        Fix32::one() - projectile.blast_falloff_start;
                    if (span > Fix32::zero()) {
                        const Fix32 offset = Fix32::min(
                            (ratio - projectile.blast_falloff_start) / span,
                            Fix32::one());
                        factor = Fix32::one() +
                            (projectile.blast_falloff_end - Fix32::one()) *
                                offset;
                    }
                }
            }
            Fix32 scaled = Fix32::from_int(projectile.explosion_damage) *
                factor * (Fix32::one() + projectile.critical_ratio) *
                projectile.detonate_damping;
            if (player.id == projectile.owner) {
                scaled *= projectile.blast_self_ratio;
            } else if (player.team == projectile.team) {
                scaled *= projectile.blast_friend_ratio;
            }
            const std::int32_t rounded = scaled.to_int_round();
            if (rounded > 0) {
                apply_damage(
                    player,
                    static_cast<std::uint16_t>(std::clamp(rounded, 0, 65535)),
                    projectile.owner);
            }

            Fix32 impulse_factor = Fix32::one();
            if (projectile.explosion_radius > Fix32::zero() &&
                projectile.impulse_falloff_start < Fix32::one()) {
                const Fix32 ratio =
                    distance / projectile.explosion_radius;
                if (ratio > projectile.impulse_falloff_start) {
                    const Fix32 span =
                        Fix32::one() - projectile.impulse_falloff_start;
                    if (span > Fix32::zero()) {
                        const Fix32 offset = Fix32::min(
                            (ratio - projectile.impulse_falloff_start) / span,
                            Fix32::one());
                        impulse_factor = Fix32::one() +
                            (projectile.impulse_falloff_end - Fix32::one()) *
                                offset;
                    }
                }
            }
            const HeroStats& hero =
                hero_stats(static_cast<HeroType>(player.hero));
            const Fix32 impulse_scale =
                hero.surface_area * kSteamMultiplier /
                (kHeroMass * Fix32::from_int(kTickRate));
            if (projectile.explosion_impulse > Fix32::zero()) {
                const Vec2 direction = distance > Fix32::zero()
                    ? delta / distance
                    : Vec2{Fix32::zero(), Fix32::from_int(-1)};
                player.velocity +=
                    direction * (projectile.explosion_impulse *
                                 impulse_factor * impulse_scale *
                                 projectile.detonate_damping);
            }
            if (player.grounded != 0 &&
                projectile.explosion_ground_impulse > Fix32::zero()) {
                player.velocity.y -=
                    projectile.explosion_ground_impulse *
                    impulse_factor * impulse_scale *
                    projectile.detonate_damping;
            }
            const Fix32 velocity_limit =
                Fix32::from_ratio(12, kTickRate);
            const Fix32 speed_sq = player.velocity.length_sq();
            const Fix32 limit_sq = velocity_limit * velocity_limit;
            if (speed_sq > limit_sq && speed_sq > Fix32::zero()) {
                player.velocity = player.velocity *
                    (velocity_limit / Fix32::sqrt(speed_sq));
            }
            if (player.id != projectile.owner &&
                player.team != projectile.team) {
                if (projectile.slow_ticks > 0) {
                    player.slow_ticks = std::max(
                        player.slow_ticks,
                        projectile.slow_ticks);
                }
                if (projectile.stun_ticks > 0) {
                    player.stun_ticks = std::max(
                        player.stun_ticks,
                        projectile.stun_ticks);
                    player.reload_timer = 0;
                }
            }
        }
    });
}

void World::rebuild_control_point_config() {
    control_point_config_ = {};
    control_points_enabled_ = false;
    control_points_legacy_ = false;
    if (map_.control_points.has_value() &&
        !map_.control_points->points.empty() &&
        !map_.control_points->entries.empty()) {
        control_point_config_ = *map_.control_points;
        control_points_enabled_ = true;
        return;
    }
    if (map_.objective.has_value()) {
        const Objective& objective = *map_.objective;
        ControlPointGenerator generator;
        generator.update_interval = 1;
        generator.start_tick = 0;
        generator.min_interval = 0;
        generator.max_interval = 0;
        generator.instance_limit = 1;
        generator.capture_group_size = 0;
        ControlPointDef def;
        def.dimensions = Vec2{
            objective.area.max.x - objective.area.min.x,
            objective.area.max.y - objective.area.min.y};
        def.capture_ticks = objective.capture_ticks;
        def.score_ticks = objective.score_ticks;
        def.capture_score = objective.capture_score;
        def.hold_score = objective.hold_score;
        generator.points.push_back(def);
        generator.entries.push_back(objective.area.center());
        control_point_config_ = generator;
        control_points_enabled_ = true;
        control_points_legacy_ = true;
    }
}

void World::reset_control_points() {
    control_points_.clear();
    cp_vacant_entries_.clear();
    cp_respawn_frames_.clear();
    cp_recycled_ = false;
    if (game_mode_ != GameMode::Control || !control_points_enabled_) {
        return;
    }
    if (control_points_legacy_) {
        ControlPointState point;
        point.def = 0;
        point.entry = 0;
        control_points_.push_back(point);
        return;
    }
    for (std::size_t index = 0;
         index < control_point_config_.entries.size();
         ++index) {
        cp_vacant_entries_.push_back(static_cast<std::uint16_t>(index));
    }
}

AABB World::control_point_area(const ControlPointState& point) const {
    return control_point_config_.area(point.def, point.entry);
}

std::int16_t World::control_point_capture_ticks(
    const ControlPointState& point) const {
    if (point.def >= control_point_config_.points.size()) {
        return 0;
    }
    return static_cast<std::int16_t>(
        control_point_config_.points[point.def].capture_ticks);
}

std::uint16_t World::control_point_score_ticks_needed(
    const ControlPointState& point) const {
    if (point.def >= control_point_config_.points.size()) {
        return 0;
    }
    return control_point_config_.points[point.def].score_ticks;
}

std::uint8_t World::point_owner(const ControlPointState& point) const {
    const std::int16_t capture_ticks = control_point_capture_ticks(point);
    if (capture_ticks <= 0) {
        return kObjectiveNeutral;
    }
    if (point.ticks >= capture_ticks) {
        return 1;
    }
    if (point.ticks <= -capture_ticks) {
        return 2;
    }
    return kObjectiveNeutral;
}

void World::award_control_point_score(
    std::uint8_t team,
    std::int16_t amount) {
    if (team < 1 || team > 2 || amount <= 0) {
        return;
    }
    const std::int64_t score =
        static_cast<std::int64_t>(scores_[team - 1U]) +
        static_cast<std::int64_t>(amount);
    scores_[team - 1U] = static_cast<std::int32_t>(
        std::min<std::int64_t>(
            score,
            std::numeric_limits<std::int32_t>::max()));
}

void World::remove_control_point(std::size_t index) {
    if (index >= control_points_.size()) {
        return;
    }
    cp_vacant_entries_.push_back(control_points_[index].entry);
    control_points_.erase(
        control_points_.begin() + static_cast<std::ptrdiff_t>(index));
    cp_recycled_ = true;
}

void World::spawn_control_point() {
    const ControlPointGenerator& generator = control_point_config_;
    if (generator.points.empty() || cp_vacant_entries_.empty()) {
        return;
    }
    std::size_t pool = cp_vacant_entries_.size();
    if (cp_recycled_ && pool > generator.capture_group_size) {
        pool -= generator.capture_group_size;
    }
    if (pool == 0) {
        pool = 1;
    }
    const std::size_t pick = rng_.bounded(static_cast<std::uint32_t>(pool));
    const std::uint16_t entry = cp_vacant_entries_[pick];
    std::uint8_t def = 0;
    if (generator.points.size() > 1) {
        def = static_cast<std::uint8_t>(
            rng_.bounded(static_cast<std::uint32_t>(
                generator.points.size())));
    }
    ControlPointState point;
    point.def = def;
    point.entry = entry;
    control_points_.push_back(point);
    cp_vacant_entries_.erase(
        cp_vacant_entries_.begin() + static_cast<std::ptrdiff_t>(pick));
}

void World::schedule_control_point_respawns() {
    const ControlPointGenerator& generator = control_point_config_;
    if (generator.instance_limit == 0) {
        return;
    }
    const std::int32_t vacancies =
        static_cast<std::int32_t>(generator.instance_limit) -
        static_cast<std::int32_t>(control_points_.size()) -
        static_cast<std::int32_t>(cp_respawn_frames_.size());
    for (std::int32_t index = 0; index < vacancies; ++index) {
        std::uint32_t interval = generator.min_interval;
        if (generator.max_interval > generator.min_interval) {
            interval += rng_.bounded(
                generator.max_interval - generator.min_interval + 1U);
        }
        cp_respawn_frames_.push_back(tick_ + interval);
    }
    for (;;) {
        std::size_t due = cp_respawn_frames_.size();
        for (std::size_t index = 0;
             index < cp_respawn_frames_.size();
             ++index) {
            if (cp_respawn_frames_[index] <= tick_) {
                due = index;
                break;
            }
        }
        if (due == cp_respawn_frames_.size()) {
            break;
        }
        if (!cp_vacant_entries_.empty()) {
            spawn_control_point();
        }
        cp_respawn_frames_.erase(
            cp_respawn_frames_.begin() + static_cast<std::ptrdiff_t>(due));
    }
}

void World::process_control_points() {
    const ControlPointGenerator& generator = control_point_config_;
    if (generator.points.empty()) {
        return;
    }
    std::vector<std::size_t> captured_indices[2];
    for (std::size_t index = 0; index < control_points_.size(); ++index) {
        ControlPointState& point = control_points_[index];
        if (point.def >= generator.points.size() ||
            point.entry >= generator.entries.size()) {
            point.hold_ticks = 0;
            continue;
        }
        const ControlPointDef& def = generator.points[point.def];
        const AABB area = control_point_area(point);

        bool present[2] = {false, false};
        players_.for_each([&](const Player& player) {
            if (player.alive != 0 && area.contains(player.position)) {
                present[player.team] = true;
            }
        });

        const std::int16_t capture_ticks =
            control_point_capture_ticks(point);
        if (capture_ticks <= 0) {
            point.hold_ticks = 0;
            continue;
        }
        const std::int16_t previous_ticks = point.ticks;
        if (!(present[0] && present[1])) {
            if (present[0]) {
                if (point.ticks < capture_ticks) {
                    ++point.ticks;
                }
            } else if (present[1]) {
                if (point.ticks > -capture_ticks) {
                    --point.ticks;
                }
            }
        }
        const std::uint8_t captured_team =
            point.ticks == capture_ticks
                ? 1
                : (point.ticks == -capture_ticks ? 2 : kObjectiveNeutral);
        if (captured_team == kObjectiveNeutral) {
            point.hold_ticks = 0;
            continue;
        }
        const bool just_captured = previous_ticks != point.ticks;
        if (generator.capture_group_size > 0) {
            captured_indices[captured_team - 1U].push_back(index);
        } else if (just_captured) {
            award_control_point_score(captured_team, def.capture_score);
        }
        if (just_captured) {
            point.hold_ticks = 0;
        } else if (def.hold_score > 0 && def.score_ticks > 0) {
            const std::uint32_t interval =
                generator.update_interval == 0
                    ? 1
                    : generator.update_interval;
            std::uint32_t hold_ticks =
                static_cast<std::uint32_t>(point.hold_ticks) + interval;
            hold_ticks = std::min<std::uint32_t>(hold_ticks, 65535);
            while (hold_ticks >= def.score_ticks) {
                award_control_point_score(captured_team, def.hold_score);
                hold_ticks -= def.score_ticks;
            }
            point.hold_ticks = static_cast<std::uint16_t>(hold_ticks);
        } else {
            point.hold_ticks = 0;
        }
    }

    if (generator.capture_group_size > 0) {
        struct Removal {
            std::size_t index = 0;
            std::uint8_t team = 0;
        };
        std::vector<Removal> removals;
        for (std::uint8_t team = 1; team <= 2; ++team) {
            const std::size_t count =
                captured_indices[team - 1U].size() /
                generator.capture_group_size *
                generator.capture_group_size;
            for (std::size_t index = 0; index < count; ++index) {
                removals.push_back(Removal{
                    captured_indices[team - 1U][index],
                    team});
            }
        }
        std::sort(
            removals.begin(),
            removals.end(),
            [](const Removal& a, const Removal& b) {
                return a.index > b.index;
            });
        for (const Removal& removal : removals) {
            if (removal.index >= control_points_.size()) {
                continue;
            }
            const ControlPointState& point =
                control_points_[removal.index];
            if (point.def < generator.points.size()) {
                award_control_point_score(
                    removal.team,
                    generator.points[point.def].capture_score);
            }
            remove_control_point(removal.index);
        }
    }
}

void World::update_control_points() {
    if (control_points_legacy_) {
        process_control_points();
        return;
    }
    const ControlPointGenerator& generator = control_point_config_;
    const std::uint32_t interval =
        generator.update_interval == 0 ? 1 : generator.update_interval;
    if (tick_ < generator.start_tick || (tick_ % interval) != 0) {
        return;
    }
    process_control_points();
    schedule_control_point_respawns();
}

void World::update_objective() {
    if (game_mode_ != GameMode::Control || !control_points_enabled_) {
        return;
    }
    update_control_points();
}
bool World::player_accepts_drop(
    const Player& player,
    const Drop& drop) const {
    const DropStats& stats = content().drop(drop.drop_type);
    switch (stats.pickup) {
        case DropPickup::Human: return player.bot == 0;
        case DropPickup::Enemy: return player.bot != 0;
        case DropPickup::Any: return true;
        case DropPickup::None: break;
    }
    return false;
}

bool World::depot_accepts_drop(
    const DropDepot& depot,
    const Drop& drop) const {
    if (depot.accepted_drops.empty()) {
        return true;
    }
    const DropStats& stats = content().drop(drop.drop_type);
    return std::find(
               depot.accepted_drops.begin(),
               depot.accepted_drops.end(),
               stats.id) != depot.accepted_drops.end();
}

const DropDepot* World::player_depot(const Player& player) const {
    for (const DropDepot& depot : map_.drop_depots) {
        if ((depot.team == kDropDepotNeutral ||
             depot.team == player.team) &&
            depot.bounds.contains(player.position)) {
            return &depot;
        }
    }
    return nullptr;
}

void World::clear_drops() {
    std::vector<EntityId> ids;
    ids.reserve(drops_.size());
    drops_.for_each([&](const Drop& drop) { ids.push_back(drop.id); });
    for (EntityId id : ids) {
        entities_.destroy(id);
    }
    drops_.clear();
}

void World::spawn_drops_from_map() {
    clear_drops();
    if (game_mode_ != GameMode::Crystal) {
        return;
    }
    for (const DropSpawn& spawn : map_.drop_spawns) {
        if (content().find_drop(spawn.drop_id) == nullptr) {
            continue;
        }
        const EntityId id = entities_.create();
        if (!id.valid()) {
            continue;
        }
        Drop drop;
        drop.position = spawn.position;
        drop.home = spawn.position;
        drop.drop_type = content().find_drop_type(spawn.drop_id);
        drop.state = static_cast<std::uint8_t>(DropState::Home);
        drops_.add(id, drop);
    }
}

void World::drop_carried_drops(Player& player) {
    if (player.carried_drops == 0) {
        player.depot_hold_ticks = 0;
        player.hold_score_timer = 0;
        return;
    }
    drops_.for_each([&](Drop& drop) {
        if (drop.carrier != player.id) {
            return;
        }
        const DropStats& stats = content().drop(drop.drop_type);
        drop.state = static_cast<std::uint8_t>(DropState::Loose);
        drop.carrier = EntityId{};
        drop.position = player.position;
        drop.vacant_ticks = stats.vacant_ticks;
        drop.respawn_timer = stats.respawn_ticks;
    });
    player.carried_drops = 0;
    player.depot_hold_ticks = 0;
    player.hold_score_timer = 0;
}

void World::update_drops() {
    if (drops_.empty()) {
        if (game_mode_ == GameMode::Crystal) {
            players_.for_each([](Player& player) {
                player.depot_hold_ticks = 0;
            });
        }
        return;
    }
    if (game_mode_ != GameMode::Crystal) {
        return;
    }

    drops_.for_each([&](Drop& drop) {
        const DropStats& stats = content().drop(drop.drop_type);
        if (drop.state == static_cast<std::uint8_t>(DropState::Carried)) {
            Player* carrier = players_.get(drop.carrier);
            if (carrier == nullptr || carrier->alive == 0) {
                drop.state = static_cast<std::uint8_t>(DropState::Loose);
                drop.carrier = EntityId{};
                drop.vacant_ticks = stats.vacant_ticks;
                drop.respawn_timer = stats.respawn_ticks;
            } else {
                drop.position = carrier->position;
            }
            return;
        }
        if (drop.state == static_cast<std::uint8_t>(DropState::Home)) {
            if (drop.respawn_timer > 0) {
                --drop.respawn_timer;
            }
            return;
        }
        if (drop.vacant_ticks > 0) {
            --drop.vacant_ticks;
        }
        if (drop.respawn_timer > 0) {
            --drop.respawn_timer;
            if (drop.respawn_timer == 0) {
                drop.state = static_cast<std::uint8_t>(DropState::Home);
                drop.position = drop.home;
            }
        }
    });

    const std::uint8_t carry_limit =
        content().crystal_rules().carry_limit;
    const std::uint16_t attach_score =
        content().crystal_rules().attach_score;
    players_.for_each([&](Player& player) {
        if (player.alive == 0 || player.carried_drops >= carry_limit) {
            return;
        }
        std::array<std::uint8_t, kMaxDrops> type_counts{};
        drops_.for_each([&](const Drop& drop) {
            if (drop.carrier == player.id &&
                drop.drop_type < type_counts.size()) {
                ++type_counts[drop.drop_type];
            }
        });
        const AABB box = player_box(player);
        drops_.for_each([&](Drop& drop) {
            if (player.carried_drops >= carry_limit) {
                return;
            }
            if (drop.state == static_cast<std::uint8_t>(DropState::Carried)) {
                return;
            }
            if (drop.state == static_cast<std::uint8_t>(DropState::Home) &&
                drop.respawn_timer > 0) {
                return;
            }
            if (drop.state == static_cast<std::uint8_t>(DropState::Loose) &&
                drop.vacant_ticks > 0) {
                return;
            }
            if (!player_accepts_drop(player, drop)) {
                return;
            }
            const DropStats& stats = content().drop(drop.drop_type);
            if (drop.drop_type < type_counts.size() &&
                type_counts[drop.drop_type] >= stats.max_capacity) {
                return;
            }
            const AABB drop_box = AABB::from_center(
                drop.position,
                Vec2{kDropHalfSize, kDropHalfSize});
            if (!box.overlaps(drop_box)) {
                return;
            }
            drop.state = static_cast<std::uint8_t>(DropState::Carried);
            drop.carrier = player.id;
            drop.vacant_ticks = 0;
            drop.respawn_timer = 0;
            drop.position = player.position;
            ++player.carried_drops;
            if (drop.drop_type < type_counts.size()) {
                ++type_counts[drop.drop_type];
            }
            if (attach_score > 0) {
                const std::int64_t score =
                    static_cast<std::int64_t>(scores_[player.team]) +
                    static_cast<std::int64_t>(attach_score);
                scores_[player.team] = static_cast<std::int32_t>(
                    std::min<std::int64_t>(
                        score,
                        std::numeric_limits<std::int32_t>::max()));
            }
        });
    });

    const std::uint16_t score_per_delivery =
        content().crystal_rules().score_per_delivery;
    const std::uint16_t hold_score =
        content().crystal_rules().hold_score;
    const std::uint16_t hold_score_ticks =
        content().crystal_rules().hold_score_ticks;
    players_.for_each([&](Player& player) {
        if (player.alive != 0 && player.carried_drops > 0 &&
            hold_score > 0 && hold_score_ticks > 0) {
            if (player.hold_score_timer == 0) {
                player.hold_score_timer = hold_score_ticks;
            }
            if (player.hold_score_timer > 0) {
                --player.hold_score_timer;
            }
            if (player.hold_score_timer == 0) {
                const std::int64_t score =
                    static_cast<std::int64_t>(scores_[player.team]) +
                    static_cast<std::int64_t>(hold_score);
                scores_[player.team] = static_cast<std::int32_t>(
                    std::min<std::int64_t>(
                        score,
                        std::numeric_limits<std::int32_t>::max()));
                player.hold_score_timer = hold_score_ticks;
            }
        } else {
            player.hold_score_timer = 0;
        }
        if (player.alive == 0 || player.carried_drops == 0) {
            player.depot_hold_ticks = 0;
            return;
        }
        const DropDepot* depot = player_depot(player);
        if (depot == nullptr) {
            player.depot_hold_ticks = 0;
            return;
        }
        bool accepted = false;
        drops_.for_each([&](const Drop& drop) {
            if (drop.carrier == player.id &&
                depot_accepts_drop(*depot, drop)) {
                accepted = true;
            }
        });
        if (!accepted) {
            player.depot_hold_ticks = 0;
            return;
        }
        const std::uint16_t needed =
            depot->deliver_ticks == 0 ? 1 : depot->deliver_ticks;
        if (player.depot_hold_ticks < needed) {
            ++player.depot_hold_ticks;
        }
        if (player.depot_hold_ticks < needed) {
            return;
        }

        std::uint8_t delivered = 0;
        drops_.for_each([&](Drop& drop) {
            if (drop.carrier != player.id ||
                !depot_accepts_drop(*depot, drop)) {
                return;
            }
            const DropStats& stats = content().drop(drop.drop_type);
            drop.state = static_cast<std::uint8_t>(DropState::Home);
            drop.carrier = EntityId{};
            drop.position = drop.home;
            drop.respawn_timer = stats.respawn_ticks;
            drop.vacant_ticks = 0;
            ++delivered;
        });
        player.carried_drops = delivered >= player.carried_drops
            ? 0
            : static_cast<std::uint8_t>(player.carried_drops - delivered);
        player.crystals_delivered = static_cast<std::uint16_t>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(player.crystals_delivered) +
                    delivered,
                65535U));
        if (delivered > 0) {
            const std::int64_t score =
                static_cast<std::int64_t>(scores_[player.team]) +
                static_cast<std::int64_t>(delivered) *
                    static_cast<std::int64_t>(score_per_delivery);
            scores_[player.team] = static_cast<std::int32_t>(
                std::min<std::int64_t>(
                    score,
                    std::numeric_limits<std::int32_t>::max()));
        }
        player.depot_hold_ticks = 0;
    });
}

bool World::spawner_has_supply(std::size_t spawner_index) const {
    for (const SupplyInstance& supply : supplies_) {
        if (supply.spawner == spawner_index) {
            return true;
        }
    }
    return false;
}

void World::reset_supplies() {
    supply_spawner_states_.assign(map_.supply_spawners.size(), {});
    supplies_.clear();
    for (std::size_t index = 0; index < map_.supply_spawners.size(); ++index) {
        const std::uint32_t start = map_.supply_spawners[index].start_tick;
        supply_spawner_states_[index].timer = start == 0 ? 1 : start;
    }
}

void World::spawn_supply(std::size_t spawner_index) {
    if (spawner_index >= map_.supply_spawners.size()) {
        return;
    }
    const SupplySpawner& spawner = map_.supply_spawners[spawner_index];
    if (spawner.supplies.empty() || spawner.entries.empty()) {
        return;
    }
    std::vector<std::uint8_t> types;
    types.reserve(spawner.supplies.size());
    for (const std::string& supply_id : spawner.supplies) {
        if (content().find_supply(supply_id) == nullptr) {
            continue;
        }
        types.push_back(content().find_supply_type(supply_id));
    }
    if (types.empty()) {
        return;
    }
    // Mirrors SupplyGeneratorDef::createSupply(index % supplyDefs.Length).
    const std::size_t type_index =
        static_cast<std::size_t>(rng_.bounded(65536U)) % types.size();
    const std::size_t entry_index =
        static_cast<std::size_t>(rng_.bounded(
            static_cast<std::uint32_t>(spawner.entries.size())));
    SupplyInstance supply;
    supply.spawner = static_cast<std::uint8_t>(spawner_index);
    supply.type = types[type_index];
    supply.position = spawner.entries[entry_index];
    supplies_.push_back(supply);
}

void World::apply_supply(Player& player, const SupplyStats& supply) {
    const HeroStats& hero =
        hero_stats(static_cast<HeroType>(player.hero));
    if (supply.kind == SupplyKind::Health) {
        const Fix32 max_health = Fix32::from_int(hero.health);
        const Fix32 heal = Fix32::from_int(
            (max_health * supply.health_gain).to_int_round());
        player.health = Fix32::min(max_health, player.health + heal);
        // Original buff_remover matcher removes negative, non-irremovable
        // buffs (slow / stun are the ones the simulation models).
        player.slow_ticks = 0;
        player.stun_ticks = 0;
        if (supply.heal_increment > 0 && supply.heal_interval > 0 &&
            supply.effect_ticks > 0) {
            player.supply_heal_ticks = supply.effect_ticks;
            player.supply_heal_period = supply.heal_interval;
            player.supply_heal_interval = supply.heal_interval;
            player.supply_heal_amount = supply.heal_increment;
        }
    } else if (supply.kind == SupplyKind::Damage) {
        player.damage_bonus = supply.damage_bonus;
        player.damage_bonus_ticks = supply.effect_ticks;
    }
}

void World::update_supplies() {
    if (map_.supply_spawners.empty()) {
        return;
    }

    for (std::size_t index = 0; index < supply_spawner_states_.size();
         ++index) {
        SupplySpawnerState& state = supply_spawner_states_[index];
        if (state.timer > 0) {
            --state.timer;
            if (state.timer == 0) {
                spawn_supply(index);
            }
        } else if (!spawner_has_supply(index)) {
            // The original computes minInterval + nextInt(max - max + 1),
            // which always yields the minimum interval.
            const std::uint16_t interval =
                map_.supply_spawners[index].min_interval;
            state.timer = interval == 0 ? 1 : interval;
        }
    }

    if (supplies_.empty()) {
        return;
    }
    for (std::size_t index = 0; index < supplies_.size();) {
        bool picked = false;
        players_.for_each([&](Player& player) {
            if (picked || player.alive == 0) {
                return;
            }
            const SupplyStats& stats = content().supply(
                supplies_[index].type);
            if (stats.kind == SupplyKind::Health) {
                const HeroStats& hero = hero_stats(
                    static_cast<HeroType>(player.hero));
                if (player.health >= Fix32::from_int(hero.health) &&
                    player.shield >= hero.shield) {
                    return;
                }
            }
            const Fix32 half = stats.kind == SupplyKind::Health
                ? Fix32::from_ratio(5, 10)
                : Fix32::from_ratio(65, 100);
            const AABB supply_box = AABB::from_center(
                supplies_[index].position, Vec2{half, half});
            if (!player_box(player).overlaps(supply_box)) {
                return;
            }
            apply_supply(player, stats);
            picked = true;
        });
        if (picked) {
            supplies_.erase(
                supplies_.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            ++index;
        }
    }
}

void World::update_match_state() {
    if (winner_ != 0) {
        return;
    }
    update_objective();
    if (scores_[0] >= static_cast<std::int32_t>(score_limit_) ||
        scores_[1] >= static_cast<std::int32_t>(score_limit_)) {
        if (scores_[0] == scores_[1]) {
            winner_ = 3;
        } else {
            winner_ = scores_[0] > scores_[1] ? 1 : 2;
        }
        return;
    }
    if (match_ticks_ > 0) {
        --match_ticks_;
    }
    if (match_ticks_ == 0) {
        if (scores_[0] == scores_[1]) {
            winner_ = 3;
        } else {
            winner_ = scores_[0] > scores_[1] ? 1 : 2;
        }
    }
}

void World::respawn_player(Player& player) {
    const HeroStats& hero =
        hero_stats(static_cast<HeroType>(player.hero));
    player.alive = 1;
    player.health = Fix32::from_int(hero.health);
    player.shield = hero.shield;
    player.gadget_cooldowns.fill(0);
    player.gadget_active_ticks.fill(0);
    player.carried_drops = 0;
    player.depot_hold_ticks = 0;
    player.hold_score_timer = 0;
    player.invulnerable_ticks = 0;
    player.heal_ticks = 0;
    player.heal_interval = 0;
    player.supply_heal_ticks = 0;
    player.supply_heal_interval = 0;
    player.supply_heal_period = 0;
    player.supply_heal_amount = 0;
    player.damage_bonus_ticks = 0;
    player.damage_bonus = Fix32::zero();
    player.slow_ticks = 0;
    player.stun_ticks = 0;
    player.gadget_latch = 0;
    player.shield_regen_delay = 0;
    player.position = spawn_position(player.slot);
    player.velocity = {};
    player.aim = Vec2{player.team == 0 ? Fix32::one() : Fix32::from_int(-1), Fix32::zero()};
    player.facing = player.team == 0 ? 1 : -1;
    player.buttons = 0;
    player.fire_cooldown = 0;
    player.dispersion_ticks = 0;
    player.warmup_timer = 0;
    player.fire_latch = 0;
    player.reload_timer = 0;
    player.respawn_timer = 0;
    player.weapon_switch_cooldown = 0;
    player.ammo = weapon_stats(static_cast<WeaponType>(player.weapon)).magazine;
    player.grounded = 0;
    player.drop_through_ticks = 0;
    player.pad_boost = 0;
    player.pad_boost_ticks = 0;
}

AABB World::player_box(const Player& player) {
    return AABB::from_center(
        player.position,
        Vec2{kPlayerHalfWidth, kPlayerHalfHeight});
}

void World::save(WorldSnapshot& snapshot) const {
    snapshot = {};
    snapshot.version = kSnapshotVersion;
    snapshot.tick = tick_;
    snapshot.match_ticks = match_ticks_;
    snapshot.rng = rng_.snapshot();
    snapshot.scores = scores_;
    snapshot.score_limit = score_limit_;
    snapshot.winner = winner_;
    snapshot.game_mode = static_cast<std::uint8_t>(game_mode_);
    snapshot.objective_owner = objective_owner();
    snapshot.objective_capturing_team = objective_capturing_team();
    snapshot.objective_progress = objective_progress();
    snapshot.objective_score_ticks = objective_score_ticks();
    snapshot.control_points.clear();
    snapshot.control_point_vacant_entries = cp_vacant_entries_;
    snapshot.control_point_respawn_frames = cp_respawn_frames_;
    snapshot.control_points_recycled = cp_recycled_ ? 1 : 0;
    for (const ControlPointState& point : control_points_) {
        ControlPointSnapshot saved;
        saved.def = point.def;
        saved.entry = point.entry;
        saved.ticks = point.ticks;
        saved.hold_ticks = point.hold_ticks;
        snapshot.control_points.push_back(saved);
    }
    snapshot.entities = entities_.snapshot();

    players_.for_each([&](const Player& player) {
        snapshot.players.push_back(player);
    });
    std::sort(snapshot.players.begin(), snapshot.players.end(), [](const Player& a, const Player& b) {
        return entity_less(a.id, b.id);
    });

    projectiles_.for_each([&](const Projectile& projectile) {
        snapshot.projectiles.push_back(projectile);
    });
    std::sort(
        snapshot.projectiles.begin(),
        snapshot.projectiles.end(),
        [](const Projectile& a, const Projectile& b) {
            return entity_less(a.id, b.id);
        });

    drops_.for_each([&](const Drop& drop) {
        snapshot.drops.push_back(drop);
    });
    std::sort(
        snapshot.drops.begin(),
        snapshot.drops.end(),
        [](const Drop& a, const Drop& b) {
            return entity_less(a.id, b.id);
        });

    snapshot.supply_spawners = supply_spawner_states_;
    snapshot.supplies = supplies_;
}

bool World::restore(const WorldSnapshot& snapshot, std::string* error) {
    if (snapshot.version != kSnapshotVersion) {
        if (error) {
            *error = "unsupported world snapshot version";
        }
        return false;
    }
    if (snapshot.players.empty() || snapshot.players.size() > kMaxPlayers) {
        if (error) {
            *error = "invalid snapshot player count";
        }
        return false;
    }
    if (snapshot.projectiles.size() > kSnapshotMaxProjectiles) {
        if (error) {
            *error = "invalid snapshot projectile count";
        }
        return false;
    }
    if (snapshot.drops.size() > kSnapshotMaxDrops) {
        if (error) {
            *error = "invalid snapshot drop count";
        }
        return false;
    }
    if (snapshot.supplies.size() > kSnapshotMaxSupplies ||
        snapshot.supply_spawners.size() > kSnapshotMaxSupplySpawners ||
        snapshot.supply_spawners.size() !=
            map_.supply_spawners.size()) {
        if (error) {
            *error = "invalid snapshot supply count";
        }
        return false;
    }
    if (snapshot.match_ticks > kSnapshotMaxMatchTicks ||
        snapshot.score_limit == 0 ||
        snapshot.winner > 3 ||
        snapshot.game_mode > static_cast<std::uint8_t>(GameMode::Crystal) ||
        snapshot.objective_owner > 2 ||
        snapshot.objective_capturing_team > 2 ||
        snapshot.objective_progress > kMapMaxObjectiveTicks ||
        snapshot.objective_score_ticks > kMapMaxObjectiveTicks) {
        if (error) {
            *error = "invalid snapshot match state";
        }
        return false;
    }
    const bool control_mode =
        snapshot.game_mode == static_cast<std::uint8_t>(GameMode::Control);
    if (!control_mode &&
        (!snapshot.control_points.empty() ||
         !snapshot.control_point_vacant_entries.empty() ||
         !snapshot.control_point_respawn_frames.empty() ||
         snapshot.control_points_recycled != 0)) {
        if (error) {
            *error = "non-control snapshot has control points";
        }
        return false;
    }
    if (snapshot.game_mode == static_cast<std::uint8_t>(GameMode::Tdm) &&
        (snapshot.objective_owner != kObjectiveNeutral ||
         snapshot.objective_capturing_team != kObjectiveNeutral ||
         snapshot.objective_progress != 0 ||
         snapshot.objective_score_ticks != 0)) {
        if (error) {
            *error = "tdm snapshot has objective state";
        }
        return false;
    }
    if (control_mode && !control_points_enabled_) {
        if (error) {
            *error = "control mode requires a map objective";
        }
        return false;
    }
    if (control_mode) {
        if (snapshot.control_points.size() >
            control_point_config_.instance_limit) {
            if (error) {
                *error = "invalid control point count";
            }
            return false;
        }
        for (const ControlPointSnapshot& point : snapshot.control_points) {
            if (point.def >= control_point_config_.points.size() ||
                point.entry >= control_point_config_.entries.size()) {
                if (error) {
                    *error = "invalid control point reference";
                }
                return false;
            }
            const std::int16_t capture_ticks = static_cast<std::int16_t>(
                control_point_config_.points[point.def].capture_ticks);
            const std::int32_t magnitude =
                point.ticks < 0 ? -point.ticks : point.ticks;
            if (magnitude > capture_ticks) {
                if (error) {
                    *error = "invalid control point progress";
                }
                return false;
            }
        }
        for (const std::uint16_t entry : snapshot.control_point_vacant_entries) {
            if (entry >= control_point_config_.entries.size()) {
                if (error) {
                    *error = "invalid control point vacated entry";
                }
                return false;
            }
        }
    }
    if (snapshot.game_mode != static_cast<std::uint8_t>(GameMode::Crystal) &&
        !snapshot.drops.empty()) {
        if (error) {
            *error = "non-crystal snapshot has drops";
        }
        return false;
    }
    if (snapshot.game_mode == static_cast<std::uint8_t>(GameMode::Crystal) &&
        map_.drop_depots.empty()) {
        if (error) {
            *error = "crystal mode requires map drop depots";
        }
        return false;
    }

    EntityAllocator allocator;
    if (!allocator.restore(snapshot.entities, error)) {
        return false;
    }
    if (allocator.live_count() !=
        snapshot.players.size() + snapshot.projectiles.size() +
            snapshot.drops.size()) {
        if (error) {
            *error = "snapshot entity component count mismatch";
        }
        return false;
    }

    Pcg32 rng;
    if (!rng.restore(snapshot.rng)) {
        if (error) {
            *error = "invalid snapshot rng state";
        }
        return false;
    }

    ComponentPool<Player> players;
    std::array<std::uint8_t, kMaxPlayers> used_slots{};
    for (const Player& source : snapshot.players) {
        if (!source.id.valid() || !allocator.alive(source.id) || players.get(source.id)) {
            if (error) {
                *error = "invalid snapshot player id";
            }
            return false;
        }
        if (source.slot >= kMaxPlayers || used_slots[source.slot] ||
            source.team > 1 || source.weapon >= content().weapon_count() ||
            source.hero >= content().hero_count() || source.bot > 1 ||
            source.alive > 1 || source.grounded > 1 ||
            source.kills > kSnapshotMaxKills ||
            source.deaths > kSnapshotMaxKills ||
            source.shield > kSnapshotMaxShield ||
            source.damage_dealt > kSnapshotMaxDamage ||
            source.blocked_damage > kSnapshotMaxDamage) {
            if (error) {
                *error = "invalid snapshot player fields";
            }
            return false;
        }
        used_slots[source.slot] = 1;
        players.add(source.id, source);
    }

    ComponentPool<Projectile> projectiles;
    for (const Projectile& source : snapshot.projectiles) {
        if (!source.id.valid() || !allocator.alive(source.id) ||
            players.get(source.id) || projectiles.get(source.id)) {
            if (error) {
                *error = "invalid snapshot projectile id";
            }
            return false;
        }
        if (!source.owner.valid() || source.team > 1 || source.explosive > 1) {
            if (error) {
                *error = "invalid snapshot projectile fields";
            }
            return false;
        }
        projectiles.add(source.id, source);
    }

    ComponentPool<Drop> drops;
    for (const Drop& source : snapshot.drops) {
        if (!source.id.valid() || !allocator.alive(source.id) ||
            players.get(source.id) || projectiles.get(source.id) ||
            drops.get(source.id)) {
            if (error) {
                *error = "invalid snapshot drop id";
            }
            return false;
        }
        const bool carried =
            source.state == static_cast<std::uint8_t>(DropState::Carried);
        if (source.drop_type >= content().drop_count() ||
            source.state > static_cast<std::uint8_t>(DropState::Loose) ||
            source.carrier.valid() != carried ||
            (carried && !players.get(source.carrier))) {
            if (error) {
                *error = "invalid snapshot drop fields";
            }
            return false;
        }
        drops.add(source.id, source);
    }

    std::vector<std::uint8_t> used_supply_spawners(
        map_.supply_spawners.size(), 0);
    for (const SupplyInstance& source : snapshot.supplies) {
        if (source.spawner >= map_.supply_spawners.size() ||
            used_supply_spawners[source.spawner] != 0 ||
            source.type >= content().supply_count() ||
            !map_.bounds.contains(source.position)) {
            if (error) {
                *error = "invalid snapshot supply fields";
            }
            return false;
        }
        used_supply_spawners[source.spawner] = 1;
    }

    entities_ = std::move(allocator);
    players_ = std::move(players);
    projectiles_ = std::move(projectiles);
    drops_ = std::move(drops);
    supply_spawner_states_ = snapshot.supply_spawners;
    supplies_ = snapshot.supplies;
    rng_ = rng;
    scores_ = snapshot.scores;
    tick_ = snapshot.tick;
    match_ticks_ = snapshot.match_ticks;
    score_limit_ = snapshot.score_limit;
    winner_ = snapshot.winner;
    game_mode_ = static_cast<GameMode>(snapshot.game_mode);
    control_points_.clear();
    cp_vacant_entries_ = snapshot.control_point_vacant_entries;
    cp_respawn_frames_ = snapshot.control_point_respawn_frames;
    cp_recycled_ = snapshot.control_points_recycled != 0;
    for (const ControlPointSnapshot& saved : snapshot.control_points) {
        ControlPointState point;
        point.def = saved.def;
        point.entry = saved.entry;
        point.ticks = saved.ticks;
        point.hold_ticks = saved.hold_ticks;
        control_points_.push_back(point);
    }
    player_count_ = static_cast<int>(snapshot.players.size());
    bot_mask_ = 0;
    players_.for_each([&](const Player& player) {
        if (player.bot != 0 && player.slot < 16) {
            bot_mask_ |= static_cast<std::uint16_t>(1U << player.slot);
        }
    });
    if (match_duration_ < match_ticks_) {
        match_duration_ = match_ticks_;
    }
    return true;
}

std::uint64_t World::hash() const {
    WorldSnapshot snapshot;
    save(snapshot);
    return snapshot_hash(snapshot);
}

} // namespace nsync::core

