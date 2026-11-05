#include "core/snapshot.h"
#include "core/world.h"

#include "core/byte_io.h"
#include "core/hash.h"
#include "core/heroes.h"
#include "core/weapons.h"

#include <algorithm>
#include <utility>

namespace nsync::core {
namespace {

void write_entity(ByteWriter& writer, EntityId id) {
    writer.u32(id.raw());
}

bool read_entity(ByteReader& reader, EntityId& id) {
    std::uint32_t raw = 0;
    if (!reader.u32(raw)) {
        return false;
    }
    id = EntityId::from_raw(raw);
    return true;
}

void write_vec2(ByteWriter& writer, Vec2 value) {
    writer.u32(static_cast<std::uint32_t>(value.x.raw()));
    writer.u32(static_cast<std::uint32_t>(value.y.raw()));
}

bool read_vec2(ByteReader& reader, Vec2& value) {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    if (!reader.u32(x) || !reader.u32(y)) {
        return false;
    }
    value = Vec2{Fix32::from_raw(static_cast<std::int32_t>(x)),
                 Fix32::from_raw(static_cast<std::int32_t>(y))};
    return true;
}

void write_player(ByteWriter& writer, const Player& player) {
    write_entity(writer, player.id);
    write_vec2(writer, player.position);
    write_vec2(writer, player.velocity);
    write_vec2(writer, player.aim);
    writer.u32(static_cast<std::uint32_t>(player.health.raw()));
    writer.u16(player.shield);
    for (const std::uint16_t cooldown : player.gadget_cooldowns) {
        writer.u16(cooldown);
    }
    for (const std::uint16_t active : player.gadget_active_ticks) {
        writer.u16(active);
    }
    writer.u16(player.invulnerable_ticks);
    writer.u16(player.heal_ticks);
    writer.u16(player.heal_interval);
    writer.u16(player.slow_ticks);
    writer.u16(player.stun_ticks);
    writer.u8(player.gadget_latch);
    writer.u8(player.fire_latch);
    writer.u16(player.shield_regen_delay);
    writer.u16(player.buttons);
    writer.u16(player.fire_cooldown);
    writer.u16(player.dispersion_ticks);
    writer.u16(player.warmup_timer);
    writer.u16(player.reload_timer);
    writer.u16(player.respawn_timer);
    writer.u16(player.ammo);
    writer.u16(player.weapon_switch_cooldown);
    writer.u16(player.kills);
    writer.u16(player.deaths);
    writer.u16(player.depot_hold_ticks);
    writer.u16(player.hold_score_timer);
    writer.u16(player.crystals_delivered);
    writer.u32(player.damage_dealt);
    writer.u32(player.blocked_damage);
    writer.u8(player.carried_drops);
    writer.u8(player.slot);
    writer.u8(player.team);
    writer.u8(player.weapon);
    writer.u8(player.hero);
    writer.u8(player.bot);
    writer.u8(player.alive);
    writer.u8(player.grounded);
    writer.u8(player.drop_through_ticks);
    writer.u8(player.pad_boost);
    writer.u8(player.pad_boost_ticks);
    writer.i8(player.facing);
}

bool read_player(ByteReader& reader, Player& player, std::string* error) {
    std::uint32_t health = 0;
    if (!read_entity(reader, player.id) ||
        !read_vec2(reader, player.position) ||
        !read_vec2(reader, player.velocity) ||
        !read_vec2(reader, player.aim) ||
        !reader.u32(health) ||
        !reader.u16(player.shield) ||
        !reader.u16(player.gadget_cooldowns[0]) ||
        !reader.u16(player.gadget_cooldowns[1]) ||
        !reader.u16(player.gadget_cooldowns[2]) ||
        !reader.u16(player.gadget_active_ticks[0]) ||
        !reader.u16(player.gadget_active_ticks[1]) ||
        !reader.u16(player.gadget_active_ticks[2]) ||
        !reader.u16(player.invulnerable_ticks) ||
        !reader.u16(player.heal_ticks) ||
        !reader.u16(player.heal_interval) ||
        !reader.u16(player.slow_ticks) ||
        !reader.u16(player.stun_ticks) ||
        !reader.u8(player.gadget_latch) ||
        !reader.u8(player.fire_latch) ||
        !reader.u16(player.shield_regen_delay) ||
        !reader.u16(player.buttons) ||
        !reader.u16(player.fire_cooldown) ||
        !reader.u16(player.dispersion_ticks) ||
        !reader.u16(player.warmup_timer) ||
        !reader.u16(player.reload_timer) ||
        !reader.u16(player.respawn_timer) ||
        !reader.u16(player.ammo) ||
        !reader.u16(player.weapon_switch_cooldown) ||
        !reader.u16(player.kills) ||
        !reader.u16(player.deaths) ||
        !reader.u16(player.depot_hold_ticks) ||
        !reader.u16(player.hold_score_timer) ||
        !reader.u16(player.crystals_delivered) ||
        !reader.u32(player.damage_dealt) ||
        !reader.u32(player.blocked_damage) ||
        !reader.u8(player.carried_drops) ||
        !reader.u8(player.slot) ||
        !reader.u8(player.team) ||
        !reader.u8(player.weapon) ||
        !reader.u8(player.hero) ||
        !reader.u8(player.bot) ||
        !reader.u8(player.alive) ||
        !reader.u8(player.grounded) ||
        !reader.u8(player.drop_through_ticks) ||
        !reader.u8(player.pad_boost) ||
        !reader.u8(player.pad_boost_ticks) ||
        !reader.i8(player.facing)) {
        if (error) {
            *error = "truncated player snapshot";
        }
        return false;
    }
    player.health = Fix32::from_raw(static_cast<std::int32_t>(health));
    if (!player.id.valid() || player.team > 1 || player.weapon >= content().weapon_count() ||
        player.hero >= content().hero_count() || player.bot > 1 || player.alive > 1 ||
        player.grounded > 1 || player.drop_through_ticks > 3 ||
        player.gadget_latch > 0x07U ||
        player.fire_latch > 1 || player.pad_boost_ticks > 10 ||
        player.dispersion_ticks > 6000 || player.warmup_timer > 6000 ||
        player.ammo > 512 ||
        player.depot_hold_ticks > 3600 ||
        player.hold_score_timer > 3600 ||
        player.carried_drops > 8 ||
        player.kills > kSnapshotMaxKills ||
        player.deaths > kSnapshotMaxKills ||
        player.shield > kSnapshotMaxShield ||
        player.damage_dealt > kSnapshotMaxDamage ||
        player.blocked_damage > kSnapshotMaxDamage) {
        if (error) {
            *error = "invalid player snapshot fields";
        }
        return false;
    }
    return true;
}

void write_projectile(ByteWriter& writer, const Projectile& projectile) {
    write_entity(writer, projectile.id);
    write_entity(writer, projectile.owner);
    write_vec2(writer, projectile.position);
    write_vec2(writer, projectile.velocity);
    write_vec2(writer, projectile.origin);
    writer.u32(static_cast<std::uint32_t>(projectile.half_size.raw()));
    writer.u32(static_cast<std::uint32_t>(projectile.range.raw()));
    writer.u32(static_cast<std::uint32_t>(projectile.falloff_start.raw()));
    writer.u32(static_cast<std::uint32_t>(projectile.falloff_end.raw()));
    writer.u32(static_cast<std::uint32_t>(projectile.traveled.raw()));
    writer.u32(static_cast<std::uint32_t>(projectile.explosion_radius.raw()));
    writer.u32(static_cast<std::uint32_t>(
        projectile.blast_falloff_start.raw()));
    writer.u32(static_cast<std::uint32_t>(
        projectile.blast_falloff_end.raw()));
    writer.u32(static_cast<std::uint32_t>(
        projectile.blast_self_ratio.raw()));
    writer.u32(static_cast<std::uint32_t>(
        projectile.blast_friend_ratio.raw()));
    writer.u32(static_cast<std::uint32_t>(
        projectile.explosion_impulse.raw()));
    writer.u32(static_cast<std::uint32_t>(
        projectile.explosion_ground_impulse.raw()));
    writer.u32(static_cast<std::uint32_t>(
        projectile.impulse_falloff_start.raw()));
    writer.u32(static_cast<std::uint32_t>(
        projectile.impulse_falloff_end.raw()));
    writer.u32(static_cast<std::uint32_t>(
        projectile.critical_ratio.raw()));
    writer.u32(static_cast<std::uint32_t>(projectile.gravity.raw()));
    writer.u32(static_cast<std::uint32_t>(projectile.detonator_hp.raw()));
    writer.u32(static_cast<std::uint32_t>(projectile.detonator_hp_step.raw()));
    writer.u32(static_cast<std::uint32_t>(
        projectile.detonate_damping.raw()));
    writer.u32(static_cast<std::uint32_t>(
        projectile.normal_velocity.raw()));
    writer.u32(static_cast<std::uint32_t>(projectile.damping_rate.raw()));
    writer.u16(projectile.damage);
    writer.u16(projectile.explosion_damage);
    writer.u16(projectile.life_ticks);
    writer.u16(projectile.slow_ticks);
    writer.u16(projectile.stun_ticks);
    writer.u8(projectile.team);
    writer.u8(projectile.explosive);
    writer.u8(projectile.terrain_bounces);
    writer.u8(projectile.explode_on_expire);
    writer.u8(projectile.platform_puncher);
    writer.u8(projectile.shell_impact);
    writer.u8(projectile.damage_sensor);
    writer.u8(projectile.ignore_wall);
    writer.u8(projectile.gadget);
}

bool read_projectile(ByteReader& reader, Projectile& projectile, std::string* error) {
    std::uint32_t half_size = 0;
    std::uint32_t range = 0;
    std::uint32_t falloff_start = 0;
    std::uint32_t falloff_end = 0;
    std::uint32_t traveled = 0;
    std::uint32_t explosion_radius = 0;
    std::uint32_t blast_falloff_start = 0;
    std::uint32_t blast_falloff_end = 0;
    std::uint32_t blast_self_ratio = 0;
    std::uint32_t blast_friend_ratio = 0;
    std::uint32_t explosion_impulse = 0;
    std::uint32_t explosion_ground_impulse = 0;
    std::uint32_t impulse_falloff_start = 0;
    std::uint32_t impulse_falloff_end = 0;
    std::uint32_t critical_ratio = 0;
    std::uint32_t gravity = 0;
    std::uint32_t detonator_hp = 0;
    std::uint32_t detonator_hp_step = 0;
    std::uint32_t detonate_damping = 0;
    std::uint32_t normal_velocity = 0;
    std::uint32_t damping_rate = 0;
    if (!read_entity(reader, projectile.id) ||
        !read_entity(reader, projectile.owner) ||
        !read_vec2(reader, projectile.position) ||
        !read_vec2(reader, projectile.velocity) ||
        !read_vec2(reader, projectile.origin) ||
        !reader.u32(half_size) ||
        !reader.u32(range) ||
        !reader.u32(falloff_start) ||
        !reader.u32(falloff_end) ||
        !reader.u32(traveled) ||
        !reader.u32(explosion_radius) ||
        !reader.u32(blast_falloff_start) ||
        !reader.u32(blast_falloff_end) ||
        !reader.u32(blast_self_ratio) ||
        !reader.u32(blast_friend_ratio) ||
        !reader.u32(explosion_impulse) ||
        !reader.u32(explosion_ground_impulse) ||
        !reader.u32(impulse_falloff_start) ||
        !reader.u32(impulse_falloff_end) ||
        !reader.u32(critical_ratio) ||
        !reader.u32(gravity) ||
        !reader.u32(detonator_hp) ||
        !reader.u32(detonator_hp_step) ||
        !reader.u32(detonate_damping) ||
        !reader.u32(normal_velocity) ||
        !reader.u32(damping_rate) ||
        !reader.u16(projectile.damage) ||
        !reader.u16(projectile.explosion_damage) ||
        !reader.u16(projectile.life_ticks) ||
        !reader.u16(projectile.slow_ticks) ||
        !reader.u16(projectile.stun_ticks) ||
        !reader.u8(projectile.team) ||
        !reader.u8(projectile.explosive) ||
        !reader.u8(projectile.terrain_bounces) ||
        !reader.u8(projectile.explode_on_expire) ||
        !reader.u8(projectile.platform_puncher) ||
        !reader.u8(projectile.shell_impact) ||
        !reader.u8(projectile.damage_sensor) ||
        !reader.u8(projectile.ignore_wall) ||
        !reader.u8(projectile.gadget)) {
        if (error) {
            *error = "truncated projectile snapshot";
        }
        return false;
    }
    projectile.half_size = Fix32::from_raw(static_cast<std::int32_t>(half_size));
    projectile.range = Fix32::from_raw(static_cast<std::int32_t>(range));
    projectile.falloff_start =
        Fix32::from_raw(static_cast<std::int32_t>(falloff_start));
    projectile.falloff_end =
        Fix32::from_raw(static_cast<std::int32_t>(falloff_end));
    projectile.traveled = Fix32::from_raw(static_cast<std::int32_t>(traveled));
    projectile.explosion_radius =
        Fix32::from_raw(static_cast<std::int32_t>(explosion_radius));
    projectile.blast_falloff_start =
        Fix32::from_raw(static_cast<std::int32_t>(blast_falloff_start));
    projectile.blast_falloff_end =
        Fix32::from_raw(static_cast<std::int32_t>(blast_falloff_end));
    projectile.blast_self_ratio =
        Fix32::from_raw(static_cast<std::int32_t>(blast_self_ratio));
    projectile.blast_friend_ratio =
        Fix32::from_raw(static_cast<std::int32_t>(blast_friend_ratio));
    projectile.explosion_impulse =
        Fix32::from_raw(static_cast<std::int32_t>(explosion_impulse));
    projectile.explosion_ground_impulse =
        Fix32::from_raw(static_cast<std::int32_t>(explosion_ground_impulse));
    projectile.impulse_falloff_start =
        Fix32::from_raw(static_cast<std::int32_t>(impulse_falloff_start));
    projectile.impulse_falloff_end =
        Fix32::from_raw(static_cast<std::int32_t>(impulse_falloff_end));
    projectile.critical_ratio =
        Fix32::from_raw(static_cast<std::int32_t>(critical_ratio));
    projectile.gravity = Fix32::from_raw(static_cast<std::int32_t>(gravity));
    projectile.detonator_hp =
        Fix32::from_raw(static_cast<std::int32_t>(detonator_hp));
    projectile.detonator_hp_step =
        Fix32::from_raw(static_cast<std::int32_t>(detonator_hp_step));
    projectile.detonate_damping =
        Fix32::from_raw(static_cast<std::int32_t>(detonate_damping));
    projectile.normal_velocity =
        Fix32::from_raw(static_cast<std::int32_t>(normal_velocity));
    projectile.damping_rate =
        Fix32::from_raw(static_cast<std::int32_t>(damping_rate));
    if (!projectile.id.valid() || projectile.team > 1 || projectile.explosive > 1 ||
        (projectile.terrain_bounces > 8 &&
         projectile.terrain_bounces != 255) ||
        projectile.explode_on_expire > 1 ||
        projectile.platform_puncher > 1 ||
        projectile.gadget > static_cast<std::uint8_t>(GadgetType::Count) ||
        projectile.range < Fix32::zero() ||
        projectile.falloff_start < Fix32::zero() ||
        projectile.falloff_start > Fix32::one() ||
        projectile.falloff_end < Fix32::zero() ||
        projectile.falloff_end > Fix32::one() ||
        projectile.blast_falloff_start < Fix32::zero() ||
        projectile.blast_falloff_start > Fix32::one() ||
        projectile.blast_falloff_end < Fix32::zero() ||
        projectile.blast_falloff_end > Fix32::one() ||
        projectile.blast_self_ratio < Fix32::zero() ||
        projectile.blast_self_ratio > Fix32::one() ||
        projectile.blast_friend_ratio < Fix32::zero() ||
        projectile.blast_friend_ratio > Fix32::one() ||
        projectile.explosion_impulse < Fix32::zero() ||
        projectile.explosion_ground_impulse < Fix32::zero() ||
        projectile.impulse_falloff_start < Fix32::zero() ||
        projectile.impulse_falloff_start > Fix32::one() ||
        projectile.impulse_falloff_end < Fix32::zero() ||
        projectile.impulse_falloff_end > Fix32::one() ||
        projectile.critical_ratio < Fix32::zero() ||
        projectile.critical_ratio > Fix32::from_int(10) ||
        projectile.detonator_hp < Fix32::zero() ||
        projectile.detonator_hp_step < Fix32::zero() ||
        projectile.detonate_damping < Fix32::zero() ||
        projectile.normal_velocity < Fix32::zero() ||
        projectile.damping_rate < Fix32::zero() ||
        projectile.damping_rate > Fix32::one() ||
        projectile.shell_impact > 1 || projectile.damage_sensor > 1 ||
        projectile.ignore_wall > 1 ||
        projectile.traveled < Fix32::zero()) {
        if (error) {
            *error = "invalid projectile snapshot fields";
        }
        return false;
    }
    return true;
}

void write_drop(ByteWriter& writer, const Drop& drop) {
    write_entity(writer, drop.id);
    write_entity(writer, drop.carrier);
    write_vec2(writer, drop.position);
    write_vec2(writer, drop.home);
    writer.u16(drop.respawn_timer);
    writer.u16(drop.vacant_ticks);
    writer.u8(drop.drop_type);
    writer.u8(drop.state);
}

bool read_drop(ByteReader& reader, Drop& drop, std::string* error) {
    if (!read_entity(reader, drop.id) ||
        !read_entity(reader, drop.carrier) ||
        !read_vec2(reader, drop.position) ||
        !read_vec2(reader, drop.home) ||
        !reader.u16(drop.respawn_timer) ||
        !reader.u16(drop.vacant_ticks) ||
        !reader.u8(drop.drop_type) ||
        !reader.u8(drop.state)) {
        if (error) {
            *error = "truncated drop snapshot";
        }
        return false;
    }
    if (!drop.id.valid() ||
        drop.drop_type >= content().drop_count() ||
        drop.state > static_cast<std::uint8_t>(DropState::Loose)) {
        if (error) {
            *error = "invalid drop snapshot fields";
        }
        return false;
    }
    return true;
}

bool validate_allocator_state(
    const EntityAllocatorState& state,
    std::string* error) {
    EntityAllocator allocator;
    return allocator.restore(state, error);
}

} // namespace

std::vector<std::uint8_t> encode_snapshot_payload(const WorldSnapshot& snapshot) {
    std::vector<Player> players = snapshot.players;
    std::sort(players.begin(), players.end(), [](const Player& a, const Player& b) {
        return entity_less(a.id, b.id);
    });
    std::vector<Projectile> projectiles = snapshot.projectiles;
    std::sort(
        projectiles.begin(),
        projectiles.end(),
        [](const Projectile& a, const Projectile& b) {
            return entity_less(a.id, b.id);
        });
    std::vector<Drop> drops = snapshot.drops;
    std::sort(
        drops.begin(),
        drops.end(),
        [](const Drop& a, const Drop& b) {
            return entity_less(a.id, b.id);
        });

    ByteWriter writer;
    writer.reserve(
        64 +
        snapshot.entities.generations.size() * 2 +
        snapshot.entities.free_list.size() * 4 +
        players.size() * 76 +
        projectiles.size() * 100 +
        drops.size() * 32);
    writer.u32(snapshot.version);
    writer.u32(snapshot.tick);
    writer.u32(snapshot.match_ticks);
    writer.u64(snapshot.rng.state);
    writer.u64(snapshot.rng.increment);
    writer.i32(snapshot.scores[0]);
    writer.i32(snapshot.scores[1]);
    writer.u16(snapshot.score_limit);
    writer.u8(snapshot.winner);
    writer.u8(snapshot.game_mode);
    writer.u8(snapshot.objective_owner);
    writer.u8(snapshot.objective_capturing_team);
    writer.u16(snapshot.objective_progress);
    writer.u16(snapshot.objective_score_ticks);

    writer.u32(snapshot.entities.next_index);
    writer.u32(static_cast<std::uint32_t>(snapshot.entities.generations.size()));
    for (const std::uint16_t generation : snapshot.entities.generations) {
        writer.u16(generation);
    }
    writer.u32(static_cast<std::uint32_t>(snapshot.entities.free_list.size()));
    for (const std::uint32_t index : snapshot.entities.free_list) {
        writer.u32(index);
    }

    writer.u32(static_cast<std::uint32_t>(players.size()));
    for (const Player& player : players) {
        write_player(writer, player);
    }

    writer.u32(static_cast<std::uint32_t>(projectiles.size()));
    for (const Projectile& projectile : projectiles) {
        write_projectile(writer, projectile);
    }

    writer.u32(static_cast<std::uint32_t>(drops.size()));
    for (const Drop& drop : drops) {
        write_drop(writer, drop);
    }

    return writer.data();
}

std::vector<std::uint8_t> encode_snapshot(const WorldSnapshot& snapshot) {
    const std::vector<std::uint8_t> payload = encode_snapshot_payload(snapshot);
    ByteWriter writer;
    writer.reserve(4 + payload.size());
    writer.u32(static_cast<std::uint32_t>(payload.size()));
    writer.bytes(payload.data(), payload.size());
    return writer.data();
}

bool decode_snapshot(
    const std::uint8_t* data,
    std::size_t size,
    WorldSnapshot& snapshot,
    std::string* error) {
    ByteReader reader(data, size);
    std::uint32_t payload_size = 0;
    if (!reader.u32(payload_size) || payload_size != reader.remaining()) {
        if (error) {
            *error = "invalid snapshot length prefix";
        }
        return false;
    }

    WorldSnapshot result;
    if (!reader.u32(result.version)) {
        if (error) {
            *error = "truncated snapshot header";
        }
        return false;
    }
    if (result.version != kSnapshotVersion) {
        if (error) {
            *error = "unsupported snapshot version";
        }
        return false;
    }
    if (!reader.u32(result.tick) ||
        !reader.u32(result.match_ticks) ||
        !reader.u64(result.rng.state) ||
        !reader.u64(result.rng.increment) ||
        !reader.i32(result.scores[0]) ||
        !reader.i32(result.scores[1]) ||
        !reader.u16(result.score_limit) ||
        !reader.u8(result.winner) ||
        !reader.u8(result.game_mode) ||
        !reader.u8(result.objective_owner) ||
        !reader.u8(result.objective_capturing_team) ||
        !reader.u16(result.objective_progress) ||
        !reader.u16(result.objective_score_ticks)) {
        if (error) {
            *error = "truncated snapshot header";
        }
        return false;
    }
    if (result.match_ticks > kSnapshotMaxMatchTicks ||
        result.score_limit == 0 ||
        result.winner > 3 ||
        result.game_mode > 2 ||
        result.objective_owner > 2 ||
        result.objective_capturing_team > 2 ||
        result.objective_progress > kObjectiveCaptureTicks ||
        result.objective_score_ticks > kObjectiveScoreTicks) {
        if (error) {
            *error = "invalid snapshot match state";
        }
        return false;
    }

    if (!reader.u32(result.entities.next_index)) {
        if (error) {
            *error = "truncated entity allocator";
        }
        return false;
    }
    std::uint32_t generation_count = 0;
    if (!reader.u32(generation_count) || generation_count > EntityId::kMaxEntities) {
        if (error) {
            *error = "invalid entity generation count";
        }
        return false;
    }
    result.entities.generations.resize(generation_count);
    for (std::uint32_t index = 0; index < generation_count; ++index) {
        if (!reader.u16(result.entities.generations[index])) {
            if (error) {
                *error = "truncated entity generations";
            }
            return false;
        }
    }

    std::uint32_t free_count = 0;
    if (!reader.u32(free_count) || free_count > generation_count) {
        if (error) {
            *error = "invalid entity free list count";
        }
        return false;
    }
    result.entities.free_list.resize(free_count);
    for (std::uint32_t index = 0; index < free_count; ++index) {
        if (!reader.u32(result.entities.free_list[index])) {
            if (error) {
                *error = "truncated entity free list";
            }
            return false;
        }
    }

    if (!validate_allocator_state(result.entities, error)) {
        return false;
    }

    std::uint32_t player_count = 0;
    if (!reader.u32(player_count) || player_count > kSnapshotMaxPlayers) {
        if (error) {
            *error = "invalid player count";
        }
        return false;
    }
    result.players.resize(player_count);
    for (std::uint32_t index = 0; index < player_count; ++index) {
        if (!read_player(reader, result.players[index], error)) {
            return false;
        }
    }

    std::uint32_t projectile_count = 0;
    if (!reader.u32(projectile_count) || projectile_count > kSnapshotMaxProjectiles) {
        if (error) {
            *error = "invalid projectile count";
        }
        return false;
    }
    result.projectiles.resize(projectile_count);
    for (std::uint32_t index = 0; index < projectile_count; ++index) {
        if (!read_projectile(reader, result.projectiles[index], error)) {
            return false;
        }
    }

    std::uint32_t drop_count = 0;
    if (!reader.u32(drop_count) || drop_count > kSnapshotMaxDrops) {
        if (error) {
            *error = "invalid drop count";
        }
        return false;
    }
    result.drops.resize(drop_count);
    for (std::uint32_t index = 0; index < drop_count; ++index) {
        if (!read_drop(reader, result.drops[index], error)) {
            return false;
        }
    }

    if (!reader.at_end()) {
        if (error) {
            *error = "snapshot has trailing bytes";
        }
        return false;
    }
    Pcg32 rng;
    if (!rng.restore(result.rng)) {
        if (error) {
            *error = "invalid snapshot rng state";
        }
        return false;
    }

    snapshot = std::move(result);
    return true;
}

bool decode_snapshot(
    const std::vector<std::uint8_t>& data,
    WorldSnapshot& snapshot,
    std::string* error) {
    return decode_snapshot(data.data(), data.size(), snapshot, error);
}

std::uint64_t snapshot_hash_payload(
    const std::uint8_t* payload,
    std::size_t size) {
    return hash64_bytes(payload, size);
}

bool snapshot_hash_encoded(
    const std::vector<std::uint8_t>& encoded,
    std::uint64_t& hash) {
    if (encoded.size() < 4) {
        return false;
    }
    const std::uint32_t payload_size =
        static_cast<std::uint32_t>(encoded[0]) |
        (static_cast<std::uint32_t>(encoded[1]) << 8U) |
        (static_cast<std::uint32_t>(encoded[2]) << 16U) |
        (static_cast<std::uint32_t>(encoded[3]) << 24U);
    if (payload_size != encoded.size() - 4) {
        return false;
    }
    hash = snapshot_hash_payload(encoded.data() + 4, payload_size);
    return true;
}

std::uint64_t snapshot_hash(const WorldSnapshot& snapshot) {
    const std::vector<std::uint8_t> payload = encode_snapshot_payload(snapshot);
    return snapshot_hash_payload(payload.data(), payload.size());
}

} // namespace nsync::core
