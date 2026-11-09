#pragma once

#include "core/entity.h"
#include "core/geometry.h"
#include "core/input_command.h"
#include "core/map.h"
#include "core/rng.h"
#include "core/sim_types.h"
#include "core/snapshot.h"
#include "core/content.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace nsync::core {

inline constexpr int kTickRate = 60;
inline constexpr int kMaxPlayers = 10;
inline constexpr std::uint32_t kDefaultMatchDurationTicks = 5U * 60U * kTickRate;
inline constexpr std::uint16_t kDefaultScoreLimit = 30;
inline constexpr std::uint16_t kObjectiveCaptureTicks =
    kMapDefaultObjectiveCaptureTicks;
inline constexpr std::uint16_t kObjectiveScoreTicks =
    kMapDefaultObjectiveScoreTicks;
inline constexpr std::uint8_t kObjectiveNeutral = 0;

enum class GameMode : std::uint8_t {
    Tdm = 0,
    Control = 1,
    Crystal = 2,
};

struct ControlPointState {
    std::uint8_t def = 0;
    std::uint16_t entry = 0;
    std::int16_t ticks = 0;
    std::uint16_t hold_ticks = 0;
};

class World {
public:
    World();

    void reset(std::uint64_t seed = 1, int player_count = 2);
    void step(const InputFrame& input);

    void set_map(const Map& map);
    void set_bot_slots(std::uint16_t mask);
    void set_match_rules(
        std::uint32_t duration_ticks = kDefaultMatchDurationTicks,
        std::uint16_t score_limit = kDefaultScoreLimit);
    void set_game_mode(GameMode mode);
    [[nodiscard]] const Map& map() const { return map_; }
    [[nodiscard]] std::uint32_t tick() const { return tick_; }
    [[nodiscard]] int player_count() const { return player_count_; }
    [[nodiscard]] const Pcg32& rng() const { return rng_; }
    [[nodiscard]] const std::array<std::int32_t, 2>& scores() const { return scores_; }
    [[nodiscard]] std::uint32_t match_ticks() const { return match_ticks_; }
    [[nodiscard]] std::uint16_t score_limit() const { return score_limit_; }
    [[nodiscard]] std::uint8_t winner() const { return winner_; }
    [[nodiscard]] bool match_over() const { return winner_ != 0; }
    [[nodiscard]] GameMode game_mode() const { return game_mode_; }
    [[nodiscard]] bool has_control_points() const {
        return control_points_enabled_;
    }
    [[nodiscard]] const ControlPointGenerator& control_point_config() const {
        return control_point_config_;
    }
    [[nodiscard]] const std::vector<ControlPointState>& control_points() const {
        return control_points_;
    }
    [[nodiscard]] AABB control_point_area(
        const ControlPointState& point) const;
    [[nodiscard]] std::int16_t control_point_capture_ticks(
        const ControlPointState& point) const;
    [[nodiscard]] std::uint16_t control_point_score_ticks_needed(
        const ControlPointState& point) const;
    [[nodiscard]] const ControlPointState* primary_control_point() const {
        return control_points_.empty() ? nullptr : &control_points_.front();
    }
    [[nodiscard]] std::int16_t objective_capture_ticks() const {
        const ControlPointState* point = primary_control_point();
        if (point != nullptr) {
            return control_point_capture_ticks(*point);
        }
        return map_.objective.has_value()
            ? static_cast<std::int16_t>(map_.objective->capture_ticks)
            : 0;
    }
    [[nodiscard]] std::uint16_t objective_score_ticks_needed() const {
        const ControlPointState* point = primary_control_point();
        if (point != nullptr) {
            return control_point_score_ticks_needed(*point);
        }
        return map_.objective.has_value() ? map_.objective->score_ticks : 0;
    }
    [[nodiscard]] std::uint8_t objective_owner() const {
        const ControlPointState* point = primary_control_point();
        return point == nullptr ? kObjectiveNeutral : point_owner(*point);
    }
    [[nodiscard]] std::uint8_t objective_capturing_team() const {
        const ControlPointState* point = primary_control_point();
        if (point == nullptr || point->ticks == 0) {
            return kObjectiveNeutral;
        }
        return point->ticks > 0 ? 1 : 2;
    }
    [[nodiscard]] std::uint16_t objective_progress() const {
        const ControlPointState* point = primary_control_point();
        if (point == nullptr) {
            return 0;
        }
        const std::int32_t ticks = point->ticks;
        return static_cast<std::uint16_t>(ticks < 0 ? -ticks : ticks);
    }
    [[nodiscard]] std::uint16_t objective_score_ticks() const {
        const ControlPointState* point = primary_control_point();
        return point == nullptr ? 0 : point->hold_ticks;
    }
    [[nodiscard]] std::uint8_t point_owner(
        const ControlPointState& point) const;
    [[nodiscard]] ComponentPool<Player>& players() { return players_; }
    [[nodiscard]] const ComponentPool<Player>& players() const { return players_; }
    [[nodiscard]] const ComponentPool<Projectile>& projectiles() const {
        return projectiles_;
    }
    [[nodiscard]] const ComponentPool<Drop>& drops() const { return drops_; }
    [[nodiscard]] const std::vector<SupplyInstance>& supplies() const {
        return supplies_;
    }
    [[nodiscard]] const std::vector<SupplySpawnerState>& supply_spawners() const {
        return supply_spawner_states_;
    }

    void save(WorldSnapshot& snapshot) const;
    bool restore(const WorldSnapshot& snapshot, std::string* error = nullptr);
    [[nodiscard]] std::uint64_t hash() const;

private:
    void spawn_player(std::uint8_t slot);
    void update_player(Player& player);
    void respawn_player(Player& player);
    void activate_gadget(Player& player, std::size_t slot);
    void melee_attack(Player& player, const GadgetStats& gadget);
    void spawn_gadget_projectile(
        Player& player,
        const GadgetStats& gadget);
    void fire(Player& player);
    void update_projectiles();
    void resolve_projectile_collisions();
    void apply_damage(Player& player, std::uint16_t damage, EntityId attacker);
    void explode(const Projectile& projectile);
    [[nodiscard]] bool wall_shot_blocked(
        const Player& player,
        Vec2 muzzle,
        Fix32 half_size,
        WallShotAction action,
        bool platform_puncher) const;
    [[nodiscard]] static bool projectile_in_range(
        const Projectile& projectile);
    void update_match_state();
    void update_objective();
    void rebuild_control_point_config();
    void reset_control_points();
    void update_control_points();
    void process_control_points();
    void schedule_control_point_respawns();
    void spawn_control_point();
    void remove_control_point(std::size_t index);
    void award_control_point_score(std::uint8_t team, std::int16_t amount);
    void update_drops();
    void clear_drops();
    void spawn_drops_from_map();
    void drop_carried_drops(Player& player);
    void reset_supplies();
    void update_supplies();
    void spawn_supply(std::size_t spawner_index);
    void apply_supply(Player& player, const SupplyStats& supply);
    [[nodiscard]] bool spawner_has_supply(
        std::size_t spawner_index) const;
    [[nodiscard]] bool player_accepts_drop(
        const Player& player,
        const Drop& drop) const;
    [[nodiscard]] bool depot_accepts_drop(
        const DropDepot& depot,
        const Drop& drop) const;
    [[nodiscard]] const DropDepot* player_depot(const Player& player) const;
    [[nodiscard]] PlayerCommand make_bot_command(const Player& bot) const;
    [[nodiscard]] Vec2 decode_aim(const InputCommand& command, std::int8_t facing) const;
    [[nodiscard]] Vec2 spawn_position(std::uint8_t slot) const;
    [[nodiscard]] static AABB player_box(const Player& player);

    Map map_ = Map::default_arena();
    std::vector<AABB> collision_solids_;
    std::vector<AABB> projectile_solids_;
    std::vector<Segment> projectile_platform_segments_;
    EntityAllocator entities_;
    ComponentPool<Player> players_;
    ComponentPool<Projectile> projectiles_;
    ComponentPool<Drop> drops_;
    std::vector<SupplySpawnerState> supply_spawner_states_;
    std::vector<SupplyInstance> supplies_;
    Pcg32 rng_;
    std::array<std::int32_t, 2> scores_{};
    std::uint32_t tick_ = 0;
    std::uint32_t match_ticks_ = kDefaultMatchDurationTicks;
    std::uint32_t match_duration_ = kDefaultMatchDurationTicks;
    std::uint16_t score_limit_ = kDefaultScoreLimit;
    GameMode game_mode_ = GameMode::Tdm;
    ControlPointGenerator control_point_config_{};
    bool control_points_enabled_ = false;
    bool control_points_legacy_ = false;
    // Each point owns one signed capture counter: positive for team 1,
    // negative for team 2. Ownership is derived, so an enemy stepping onto a
    // held point immediately clears the previous owner.
    std::vector<ControlPointState> control_points_;
    std::vector<std::uint16_t> cp_vacant_entries_;
    std::vector<std::uint32_t> cp_respawn_frames_;
    bool cp_recycled_ = false;
    std::uint16_t bot_mask_ = 0;
    std::uint8_t winner_ = 0;
    int player_count_ = 0;
};

void equip_hero_default_weapon(Player& player);

} // namespace nsync::core
