#pragma once

#include "core/entity.h"
#include "core/geometry.h"
#include "core/input_command.h"
#include "core/map.h"
#include "core/rng.h"
#include "core/sim_types.h"
#include "core/snapshot.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace nsync::core {

inline constexpr int kTickRate = 60;
inline constexpr int kMaxPlayers = 10;
inline constexpr std::uint32_t kDefaultMatchDurationTicks = 5U * 60U * kTickRate;
inline constexpr std::uint16_t kDefaultScoreLimit = 30;
inline constexpr std::uint16_t kObjectiveCaptureTicks = 180;
inline constexpr std::uint16_t kObjectiveScoreTicks = 300;
inline constexpr std::uint8_t kObjectiveNeutral = 0;

enum class GameMode : std::uint8_t {
    Tdm = 0,
    Control = 1,
    Crystal = 2,
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
    [[nodiscard]] std::uint8_t objective_owner() const { return objective_owner_; }
    [[nodiscard]] std::uint8_t objective_capturing_team() const {
        return objective_capturing_team_;
    }
    [[nodiscard]] std::uint16_t objective_progress() const {
        return objective_progress_;
    }
    [[nodiscard]] std::uint16_t objective_score_ticks() const {
        return objective_score_ticks_;
    }
    [[nodiscard]] ComponentPool<Player>& players() { return players_; }
    [[nodiscard]] const ComponentPool<Player>& players() const { return players_; }
    [[nodiscard]] const ComponentPool<Projectile>& projectiles() const {
        return projectiles_;
    }
    [[nodiscard]] const ComponentPool<Drop>& drops() const { return drops_; }

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
    void update_drops();
    void clear_drops();
    void spawn_drops_from_map();
    void drop_carried_drops(Player& player);
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
    EntityAllocator entities_;
    ComponentPool<Player> players_;
    ComponentPool<Projectile> projectiles_;
    ComponentPool<Drop> drops_;
    Pcg32 rng_;
    std::array<std::int32_t, 2> scores_{};
    std::uint32_t tick_ = 0;
    std::uint32_t match_ticks_ = kDefaultMatchDurationTicks;
    std::uint32_t match_duration_ = kDefaultMatchDurationTicks;
    std::uint16_t score_limit_ = kDefaultScoreLimit;
    GameMode game_mode_ = GameMode::Tdm;
    std::uint8_t objective_owner_ = kObjectiveNeutral;
    std::uint8_t objective_capturing_team_ = kObjectiveNeutral;
    std::uint16_t objective_progress_ = 0;
    std::uint16_t objective_score_ticks_ = 0;
    std::uint16_t bot_mask_ = 0;
    std::uint8_t winner_ = 0;
    int player_count_ = 0;
};

void equip_hero_default_weapon(Player& player);

} // namespace nsync::core
