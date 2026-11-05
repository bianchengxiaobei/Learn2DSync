#include "core/content.h"

#include "core/byte_io.h"
#include "core/hash.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

namespace nsync::core {
namespace {

constexpr std::size_t kMaxJsonDepth = 32;
constexpr std::size_t kMaxContentIdLength = 64;

bool is_space(char value) {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

bool is_digit(char value) {
    return value >= '0' && value <= '9';
}

struct ParsedContent {
    std::uint32_t version = 0;
    std::vector<HeroStats> heroes;
    std::vector<WeaponStats> weapons;
    std::vector<DropStats> drops;
    CrystalRules crystal_rules;
};

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    bool parse(ParsedContent& content, std::string* error) {
        skip_whitespace();
        if (!consume('{')) {
            return finish(error, "expected content object");
        }

        bool has_version = false;
        bool has_heroes = false;
        bool has_weapons = false;
        skip_whitespace();
        if (consume('}')) {
            return finish(error, "empty content object");
        }

        for (;;) {
            std::string key;
            if (!parse_string(key)) {
                return finish(error);
            }
            skip_whitespace();
            if (!consume(':')) {
                return finish(error, "expected ':' after content key");
            }
            skip_whitespace();

            bool ok = true;
            if (key == "version") {
                ok = parse_u32(content.version);
                has_version = true;
            } else if (key == "heroes") {
                ok = parse_heroes(content.heroes);
                has_heroes = true;
            } else if (key == "weapons") {
                ok = parse_weapons(content.weapons);
                has_weapons = true;
            } else if (key == "drops") {
                ok = parse_drops(content.drops);
            } else if (key == "mode_rules") {
                ok = parse_mode_rules(content.crystal_rules);
            } else {
                ok = skip_value(0);
            }
            if (!ok) {
                return finish(error);
            }

            skip_whitespace();
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                return finish(error, "expected ',' or '}' in content object");
            }
            skip_whitespace();
        }

        skip_whitespace();
        if (position_ != text_.size()) {
            return finish(error, "trailing data after content object");
        }
        if (!has_version || !has_heroes || !has_weapons) {
            return finish(error, "content requires version, heroes and weapons");
        }
        if (content.version != kContentVersion) {
            return finish(error, "unsupported content version");
        }
        if (!validate(content)) {
            return finish(error);
        }
        if (error) {
            error->clear();
        }
        return true;
    }

private:
    bool finish(std::string* error) {
        if (error) {
            *error = error_.empty() ? "invalid content json" : error_;
        }
        return false;
    }

    bool finish(std::string* error, std::string message) {
        fail(std::move(message));
        return finish(error);
    }

    bool fail(std::string message) {
        if (error_.empty()) {
            error_ = std::move(message) + " at byte " +
                     std::to_string(position_);
        }
        return false;
    }

    void skip_whitespace() {
        while (position_ < text_.size() && is_space(text_[position_])) {
            ++position_;
        }
    }

    bool consume(char expected) {
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    bool parse_string(std::string& value) {
        skip_whitespace();
        if (!consume('"')) {
            return fail("expected string");
        }

        std::string result;
        while (position_ < text_.size()) {
            const unsigned char character =
                static_cast<unsigned char>(text_[position_++]);
            if (character == '"') {
                value = std::move(result);
                return true;
            }
            if (character < 0x20U) {
                return fail("control character in string");
            }
            if (character != '\\') {
                result.push_back(static_cast<char>(character));
                continue;
            }

            if (position_ >= text_.size()) {
                return fail("truncated string escape");
            }
            const char escape = text_[position_++];
            switch (escape) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    std::uint32_t codepoint = 0;
                    for (int index = 0; index < 4; ++index) {
                        if (position_ >= text_.size()) {
                            return fail("truncated unicode escape");
                        }
                        const char digit = text_[position_++];
                        codepoint <<= 4U;
                        if (digit >= '0' && digit <= '9') {
                            codepoint |= static_cast<std::uint32_t>(digit - '0');
                        } else if (digit >= 'a' && digit <= 'f') {
                            codepoint |= static_cast<std::uint32_t>(
                                digit - 'a' + 10);
                        } else if (digit >= 'A' && digit <= 'F') {
                            codepoint |= static_cast<std::uint32_t>(
                                digit - 'A' + 10);
                        } else {
                            return fail("invalid unicode escape");
                        }
                    }
                    if (codepoint <= 0x7FU) {
                        result.push_back(static_cast<char>(codepoint));
                    } else if (codepoint <= 0x7FFU) {
                        result.push_back(static_cast<char>(
                            0xC0U | (codepoint >> 6U)));
                        result.push_back(static_cast<char>(
                            0x80U | (codepoint & 0x3FU)));
                    } else {
                        result.push_back(static_cast<char>(
                            0xE0U | (codepoint >> 12U)));
                        result.push_back(static_cast<char>(
                            0x80U | ((codepoint >> 6U) & 0x3FU)));
                        result.push_back(static_cast<char>(
                            0x80U | (codepoint & 0x3FU)));
                    }
                    break;
                }
                default:
                    return fail("unknown string escape");
            }
        }
        return fail("unterminated string");
    }

    bool parse_number_token(std::string_view& token) {
        skip_whitespace();
        const std::size_t start = position_;
        if (position_ < text_.size() &&
            (text_[position_] == '-' || text_[position_] == '+')) {
            ++position_;
        }
        bool has_digits = false;
        while (position_ < text_.size() && is_digit(text_[position_])) {
            has_digits = true;
            ++position_;
        }
        if (position_ < text_.size() && text_[position_] == '.') {
            ++position_;
            bool has_fraction = false;
            while (position_ < text_.size() && is_digit(text_[position_])) {
                has_fraction = true;
                ++position_;
            }
            if (!has_fraction) {
                return fail("invalid number fraction");
            }
        }
        if (!has_digits) {
            return fail("expected number");
        }
        token = text_.substr(start, position_ - start);
        return true;
    }

    bool parse_u32(std::uint32_t& value) {
        skip_whitespace();
        if (position_ >= text_.size() || !is_digit(text_[position_])) {
            return fail("expected unsigned integer");
        }
        std::uint64_t result = 0;
        while (position_ < text_.size() && is_digit(text_[position_])) {
            result = result * 10U +
                     static_cast<std::uint64_t>(text_[position_] - '0');
            if (result > std::numeric_limits<std::uint32_t>::max()) {
                return fail("integer out of range");
            }
            ++position_;
        }
        value = static_cast<std::uint32_t>(result);
        return true;
    }

    bool parse_fix32(Fix32& value) {
        std::string_view token;
        if (!parse_number_token(token)) {
            return false;
        }
        if (!parse_fix32_decimal(token, value)) {
            return fail("invalid content number");
        }
        return true;
    }

    bool parse_ability_type(HeroAbilityType& ability) {
        std::string value;
        if (!parse_string(value)) {
            return false;
        }
        if (value == "dash") {
            ability = HeroAbilityType::Dash;
        } else if (value == "shield") {
            ability = HeroAbilityType::Shield;
        } else if (value == "overcharge") {
            ability = HeroAbilityType::Overcharge;
        } else {
            return fail("unknown hero ability");
        }
        return true;
    }

    bool parse_fix32_pair(Fix32& x, Fix32& y) {
        skip_whitespace();
        if (!consume('[')) {
            return fail("expected pair array");
        }
        skip_whitespace();
        if (!parse_fix32(x)) {
            return false;
        }
        skip_whitespace();
        if (!consume(',')) {
            return fail("expected ',' in pair array");
        }
        skip_whitespace();
        if (!parse_fix32(y)) {
            return false;
        }
        skip_whitespace();
        if (!consume(']')) {
            return fail("expected ']' after pair");
        }
        return true;
    }

    bool parse_hero(HeroStats& hero) {
        skip_whitespace();
        if (!consume('{')) {
            return fail("expected hero object");
        }

        bool has_id = false;
        bool has_health = false;
        bool has_shield = false;
        bool has_run_speed = false;
        bool has_jump_speed = false;
        bool has_ability = false;
        bool has_ability_name = false;
        bool has_ability_cooldown = false;
        bool has_ability_duration = false;
        bool has_ability_power = false;
        for (;;) {
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            skip_whitespace();
            if (!consume(':')) {
                return fail("expected ':' after hero key");
            }
            skip_whitespace();

            bool ok = true;
            if (key == "id") {
                ok = parse_string(hero.id);
                has_id = true;
            } else if (key == "health") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 4096U) {
                    ok = fail("hero health out of range");
                }
                hero.health = static_cast<std::uint16_t>(value);
                has_health = true;
            } else if (key == "shield") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 4096U) {
                    ok = fail("hero shield out of range");
                }
                hero.shield = static_cast<std::uint16_t>(value);
                has_shield = true;
            } else if (key == "run_speed") {
                ok = parse_fix32(hero.run_speed);
                has_run_speed = true;
            } else if (key == "jump_speed") {
                ok = parse_fix32(hero.jump_speed);
                has_jump_speed = true;
            } else if (key == "ability") {
                ok = parse_ability_type(hero.ability);
                has_ability = true;
            } else if (key == "ability_name") {
                ok = parse_string(hero.ability_name);
                has_ability_name = true;
            } else if (key == "ability_cooldown") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && (value == 0 || value > 65535U)) {
                    ok = fail("hero ability cooldown out of range");
                }
                hero.ability_cooldown = static_cast<std::uint16_t>(value);
                has_ability_cooldown = true;
            } else if (key == "ability_duration") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && (value == 0 || value > 65535U)) {
                    ok = fail("hero ability duration out of range");
                }
                hero.ability_duration = static_cast<std::uint16_t>(value);
                has_ability_duration = true;
            } else if (key == "ability_power") {
                ok = parse_fix32(hero.ability_power);
                has_ability_power = true;
            } else if (key == "surface_area") {
                ok = parse_fix32(hero.surface_area);
            } else if (key == "weapon") {
                ok = parse_string(hero.weapon);
            } else {
                ok = skip_value(0);
            }
            if (!ok) {
                return false;
            }

            skip_whitespace();
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                return fail("expected ',' or '}' in hero object");
            }
            skip_whitespace();
        }
        if (!has_id || !has_health || !has_shield || !has_run_speed ||
            !has_jump_speed || !has_ability || !has_ability_name ||
            !has_ability_cooldown || !has_ability_duration ||
            !has_ability_power) {
            return fail("hero object is missing required fields");
        }
        return true;
    }

    bool parse_weapon(WeaponStats& weapon) {
        skip_whitespace();
        if (!consume('{')) {
            return fail("expected weapon object");
        }

        bool has_id = false;
        bool has_magazine = false;
        bool has_fire_interval = false;
        bool has_reload_ticks = false;
        bool has_damage = false;
        bool has_pellets = false;
        bool has_projectile_life = false;
        bool has_projectile_speed = false;
        bool has_spread = false;
        bool has_half_size = false;
        bool has_explosion_radius = false;
        bool has_explosion_damage = false;
        bool has_explosive = false;
        for (;;) {
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            skip_whitespace();
            if (!consume(':')) {
                return fail("expected ':' after weapon key");
            }
            skip_whitespace();

            bool ok = true;
            if (key == "id") {
                ok = parse_string(weapon.id);
                has_id = true;
            } else if (key == "magazine") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 65535U) {
                    ok = fail("weapon magazine out of range");
                }
                weapon.magazine = static_cast<std::uint16_t>(value);
                has_magazine = true;
            } else if (key == "fire_interval") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 65535U) {
                    ok = fail("weapon fire interval out of range");
                }
                weapon.fire_interval = static_cast<std::uint16_t>(value);
                has_fire_interval = true;
            } else if (key == "reload_ticks") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 65535U) {
                    ok = fail("weapon reload ticks out of range");
                }
                weapon.reload_ticks = static_cast<std::uint16_t>(value);
                has_reload_ticks = true;
            } else if (key == "reload_size") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 65535U) {
                    ok = fail("weapon reload size out of range");
                }
                weapon.reload_size = static_cast<std::uint16_t>(value);
            } else if (key == "warmup_ticks") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 65535U) {
                    ok = fail("weapon warmup ticks out of range");
                }
                weapon.warmup_ticks = static_cast<std::uint16_t>(value);
            } else if (key == "damage") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 65535U) {
                    ok = fail("weapon damage out of range");
                }
                weapon.damage = static_cast<std::uint16_t>(value);
                has_damage = true;
            } else if (key == "pellets") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 65535U) {
                    ok = fail("weapon pellets out of range");
                }
                weapon.pellets = static_cast<std::uint16_t>(value);
                has_pellets = true;
            } else if (key == "projectile_life") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 65535U) {
                    ok = fail("weapon projectile life out of range");
                }
                weapon.projectile_life = static_cast<std::uint16_t>(value);
                has_projectile_life = true;
            } else if (key == "projectile_speed") {
                ok = parse_fix32(weapon.projectile_speed);
                has_projectile_speed = true;
            } else if (key == "spread") {
                ok = parse_fix32(weapon.spread);
                has_spread = true;
            } else if (key == "projectile_half_size") {
                ok = parse_fix32(weapon.projectile_half_size);
                has_half_size = true;
            } else if (key == "range") {
                ok = parse_fix32(weapon.range);
            } else if (key == "falloff_start") {
                ok = parse_fix32(weapon.falloff_start);
            } else if (key == "falloff_end") {
                ok = parse_fix32(weapon.falloff_end);
            } else if (key == "platform_puncher") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 1U) {
                    ok = fail("weapon platform puncher out of range");
                }
                weapon.platform_puncher = static_cast<std::uint8_t>(value);
            } else if (key == "terrain_bounces") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 8U) {
                    ok = fail("weapon terrain bounces out of range");
                }
                weapon.terrain_bounces = static_cast<std::uint8_t>(value);
            } else if (key == "explode_on_expire") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 1U) {
                    ok = fail("weapon explode on expire out of range");
                }
                weapon.explode_on_expire = static_cast<std::uint8_t>(value);
            } else if (key == "blast_falloff_start") {
                ok = parse_fix32(weapon.blast_falloff_start);
            } else if (key == "blast_falloff_end") {
                ok = parse_fix32(weapon.blast_falloff_end);
            } else if (key == "blast_self_ratio") {
                ok = parse_fix32(weapon.blast_self_ratio);
            } else if (key == "blast_friend_ratio") {
                ok = parse_fix32(weapon.blast_friend_ratio);
            } else if (key == "explosion_impulse") {
                ok = parse_fix32(weapon.explosion_impulse);
            } else if (key == "explosion_ground_impulse") {
                ok = parse_fix32(weapon.explosion_ground_impulse);
            } else if (key == "impulse_falloff_start") {
                ok = parse_fix32(weapon.impulse_falloff_start);
            } else if (key == "impulse_falloff_end") {
                ok = parse_fix32(weapon.impulse_falloff_end);
            } else if (key == "critical_chance") {
                ok = parse_fix32(weapon.critical_chance);
            } else if (key == "critical_ratio") {
                ok = parse_fix32(weapon.critical_ratio);
            } else if (key == "joint") {
                ok = parse_fix32_pair(weapon.joint_x, weapon.joint_y);
            } else if (key == "gun") {
                ok = parse_fix32_pair(weapon.gun_x, weapon.gun_y);
            } else if (key == "divergence") {
                ok = parse_fix32(weapon.divergence_degrees);
            } else if (key == "dispersion") {
                ok = parse_fix32(weapon.dispersion_degrees);
            } else if (key == "dispersion_time") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 65535U) {
                    ok = fail("weapon dispersion time out of range");
                }
                weapon.dispersion_ticks = static_cast<std::uint16_t>(value);
            } else if (key == "gravity") {
                ok = parse_fix32(weapon.gravity);
            } else if (key == "normal_velocity") {
                ok = parse_fix32(weapon.normal_velocity);
            } else if (key == "damping_rate") {
                ok = parse_fix32(weapon.damping_rate);
            } else if (key == "detonator_hp") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 65535U) {
                    ok = fail("weapon detonator hp out of range");
                }
                weapon.detonator_hp = static_cast<std::uint16_t>(value);
            } else if (key == "shell_impact") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 1U) {
                    ok = fail("weapon shell impact out of range");
                }
                weapon.shell_impact = static_cast<std::uint8_t>(value);
            } else if (key == "damage_sensor") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 1U) {
                    ok = fail("weapon damage sensor out of range");
                }
                weapon.damage_sensor = static_cast<std::uint8_t>(value);
            } else if (key == "wall_shot_action") {
                std::string value;
                ok = parse_string(value);
                if (ok) {
                    if (value == "detonate") {
                        weapon.wall_shot_action = WallShotAction::Detonate;
                    } else if (value == "bounce") {
                        weapon.wall_shot_action = WallShotAction::Bounce;
                    } else if (value == "skip") {
                        weapon.wall_shot_action = WallShotAction::Skip;
                    } else if (value == "ignore") {
                        weapon.wall_shot_action = WallShotAction::Ignore;
                    } else {
                        ok = fail("weapon wall shot action is invalid");
                    }
                }
            } else if (key == "explosion_radius") {
                ok = parse_fix32(weapon.explosion_radius);
                has_explosion_radius = true;
            } else if (key == "explosion_damage") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 65535U) {
                    ok = fail("weapon explosion damage out of range");
                }
                weapon.explosion_damage = static_cast<std::uint16_t>(value);
                has_explosion_damage = true;
            } else if (key == "explosive") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 1U) {
                    ok = fail("weapon explosive flag out of range");
                }
                weapon.explosive = static_cast<std::uint8_t>(value);
                has_explosive = true;
            } else {
                ok = skip_value(0);
            }
            if (!ok) {
                return false;
            }

            skip_whitespace();
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                return fail("expected ',' or '}' in weapon object");
            }
            skip_whitespace();
        }
        if (!has_id || !has_magazine || !has_fire_interval ||
            !has_reload_ticks || !has_damage || !has_pellets ||
            !has_projectile_life || !has_projectile_speed || !has_spread ||
            !has_half_size || !has_explosion_radius ||
            !has_explosion_damage || !has_explosive) {
            return fail("weapon object is missing required fields");
        }
        return true;
    }

    template <typename T, typename ParseFn>
    bool parse_array(std::vector<T>& output, ParseFn parse_item, const char* name) {
        skip_whitespace();
        if (!consume('[')) {
            return fail(std::string("expected ") + name + " array");
        }
        output.clear();
        skip_whitespace();
        if (consume(']')) {
            return true;
        }
        for (;;) {
            if (output.size() >= kMaxHeroes && std::string_view(name) == "heroes") {
                return fail("too many heroes");
            }
            if (output.size() >= kMaxWeapons &&
                std::string_view(name) == "weapons") {
                return fail("too many weapons");
            }
            if (output.size() >= kMaxDrops &&
                std::string_view(name) == "drops") {
                return fail("too many drops");
            }
            T item;
            if (!parse_item(item)) {
                return false;
            }
            output.push_back(std::move(item));
            skip_whitespace();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return fail(std::string("expected ',' or ']' in ") + name);
            }
            skip_whitespace();
        }
    }

    bool parse_heroes(std::vector<HeroStats>& output) {
        return parse_array(
            output,
            [this](HeroStats& value) { return parse_hero(value); },
            "heroes");
    }

    bool parse_weapons(std::vector<WeaponStats>& output) {
        return parse_array(
            output,
            [this](WeaponStats& value) { return parse_weapon(value); },
            "weapons");
    }

    bool parse_drop_kind(DropKind& kind) {
        std::string value;
        if (!parse_string(value)) {
            return false;
        }
        if (value == "crystal") {
            kind = DropKind::Crystal;
        } else {
            return fail("unknown drop kind");
        }
        return true;
    }

    bool parse_drop_pickup(DropPickup& pickup) {
        std::string value;
        if (!parse_string(value)) {
            return false;
        }
        if (value == "human") {
            pickup = DropPickup::Human;
        } else if (value == "enemy") {
            pickup = DropPickup::Enemy;
        } else if (value == "any") {
            pickup = DropPickup::Any;
        } else if (value == "none") {
            pickup = DropPickup::None;
        } else {
            return fail("unknown drop pickup rule");
        }
        return true;
    }

    bool parse_drop(DropStats& drop) {
        skip_whitespace();
        if (!consume('{')) {
            return fail("expected drop object");
        }
        bool has_id = false;
        for (;;) {
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            skip_whitespace();
            if (!consume(':')) {
                return fail("expected ':' after drop key");
            }
            skip_whitespace();

            bool ok = true;
            if (key == "id") {
                ok = parse_string(drop.id);
                has_id = true;
            } else if (key == "kind") {
                ok = parse_drop_kind(drop.kind);
            } else if (key == "pickup") {
                ok = parse_drop_pickup(drop.pickup);
            } else if (key == "respawn_ticks") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && (value == 0 || value > 65535U)) {
                    ok = fail("drop respawn_ticks out of range");
                }
                drop.respawn_ticks = static_cast<std::uint16_t>(value);
            } else if (key == "vacant_ticks") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 65535U) {
                    ok = fail("drop vacant_ticks out of range");
                }
                drop.vacant_ticks = static_cast<std::uint16_t>(value);
            } else if (key == "max_capacity") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && (value == 0 || value > 8U)) {
                    ok = fail("drop max_capacity out of range");
                }
                drop.max_capacity = static_cast<std::uint8_t>(value);
            } else if (key == "sprite") {
                ok = parse_string(drop.sprite);
            } else {
                ok = skip_value(0);
            }
            if (!ok) {
                return false;
            }

            skip_whitespace();
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                return fail("expected ',' or '}' in drop object");
            }
            skip_whitespace();
        }
        if (!has_id || drop.id.empty() ||
            drop.id.size() > kMaxContentIdLength ||
            drop.sprite.size() > kMaxContentIdLength) {
            return fail("drop object has invalid id or sprite");
        }
        return true;
    }

    bool parse_drops(std::vector<DropStats>& output) {
        return parse_array(
            output,
            [this](DropStats& value) { return parse_drop(value); },
            "drops");
    }

    bool parse_crystal_rules(CrystalRules& rules) {
        skip_whitespace();
        if (!consume('{')) {
            return fail("expected crystal rules object");
        }
        for (;;) {
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            skip_whitespace();
            if (!consume(':')) {
                return fail("expected ':' after crystal rule key");
            }
            skip_whitespace();

            bool ok = true;
            if (key == "deliver_ticks") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && (value == 0 || value > 3600U)) {
                    ok = fail("crystal deliver_ticks out of range");
                }
                rules.deliver_ticks = static_cast<std::uint16_t>(value);
            } else if (key == "score_per_delivery") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && (value == 0 || value > 100U)) {
                    ok = fail("crystal score_per_delivery out of range");
                }
                rules.score_per_delivery = static_cast<std::uint16_t>(value);
            } else if (key == "carry_limit") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && (value == 0 || value > 8U)) {
                    ok = fail("crystal carry_limit out of range");
                }
                rules.carry_limit = static_cast<std::uint8_t>(value);
            } else if (key == "attach_score") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 100U) {
                    ok = fail("crystal attach_score out of range");
                }
                rules.attach_score = static_cast<std::uint16_t>(value);
            } else if (key == "hold_score") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 100U) {
                    ok = fail("crystal hold_score out of range");
                }
                rules.hold_score = static_cast<std::uint16_t>(value);
            } else if (key == "hold_score_ticks") {
                std::uint32_t value = 0;
                ok = parse_u32(value);
                if (ok && value > 3600U) {
                    ok = fail("crystal hold_score_ticks out of range");
                }
                rules.hold_score_ticks = static_cast<std::uint16_t>(value);
            } else {
                ok = skip_value(0);
            }
            if (!ok) {
                return false;
            }

            skip_whitespace();
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                return fail("expected ',' or '}' in crystal rules");
            }
            skip_whitespace();
        }
        return true;
    }

    bool parse_mode_rules(CrystalRules& rules) {
        skip_whitespace();
        if (!consume('{')) {
            return fail("expected mode rules object");
        }
        for (;;) {
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            skip_whitespace();
            if (!consume(':')) {
                return fail("expected ':' after mode rule key");
            }
            skip_whitespace();

            bool ok = true;
            if (key == "crystal") {
                ok = parse_crystal_rules(rules);
            } else {
                ok = skip_value(0);
            }
            if (!ok) {
                return false;
            }

            skip_whitespace();
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                return fail("expected ',' or '}' in mode rules");
            }
            skip_whitespace();
        }
        return true;
    }

    bool skip_value(std::size_t depth) {
        if (depth > kMaxJsonDepth) {
            return fail("json nesting too deep");
        }
        skip_whitespace();
        if (position_ >= text_.size()) {
            return fail("unexpected end of json");
        }

        const char character = text_[position_];
        if (character == '"') {
            std::string ignored;
            return parse_string(ignored);
        }
        if (character == '[') {
            ++position_;
            skip_whitespace();
            if (consume(']')) {
                return true;
            }
            for (;;) {
                if (!skip_value(depth + 1)) {
                    return false;
                }
                skip_whitespace();
                if (consume(']')) {
                    return true;
                }
                if (!consume(',')) {
                    return fail("expected ',' or ']' in array");
                }
                skip_whitespace();
            }
        }
        if (character == '{') {
            ++position_;
            skip_whitespace();
            if (consume('}')) {
                return true;
            }
            for (;;) {
                std::string ignored;
                if (!parse_string(ignored)) {
                    return false;
                }
                skip_whitespace();
                if (!consume(':')) {
                    return fail("expected ':' in object");
                }
                if (!skip_value(depth + 1)) {
                    return false;
                }
                skip_whitespace();
                if (consume('}')) {
                    return true;
                }
                if (!consume(',')) {
                    return fail("expected ',' or '}' in object");
                }
                skip_whitespace();
            }
        }
        if (character == 't') {
            if (text_.substr(position_, 4) != "true") {
                return fail("invalid literal");
            }
            position_ += 4;
            return true;
        }
        if (character == 'f') {
            if (text_.substr(position_, 5) != "false") {
                return fail("invalid literal");
            }
            position_ += 5;
            return true;
        }
        if (character == 'n') {
            if (text_.substr(position_, 4) != "null") {
                return fail("invalid literal");
            }
            position_ += 4;
            return true;
        }

        std::string_view token;
        return parse_number_token(token);
    }

    bool validate(ParsedContent& content) {
        if (content.heroes.empty() || content.weapons.empty()) {
            return fail("content requires at least one hero and weapon");
        }
        if (content.heroes.size() > kMaxHeroes ||
            content.weapons.size() > kMaxWeapons ||
            content.drops.size() > kMaxDrops) {
            return fail("content exceeds hero/weapon/drop capacity");
        }

        for (std::size_t index = 0; index < content.heroes.size(); ++index) {
            HeroStats& hero = content.heroes[index];
            hero.type = static_cast<HeroType>(index);
            if (hero.id.empty() || hero.id.size() > kMaxContentIdLength) {
                return fail("hero id is invalid");
            }
            if (hero.health == 0 || hero.shield > 4096 ||
                hero.run_speed <= Fix32::zero() ||
                hero.jump_speed <= Fix32::zero() ||
                hero.jump_speed > Fix32::from_int(16) ||
                hero.ability_name.empty() ||
                hero.ability_name.size() > kMaxContentIdLength ||
                hero.ability_cooldown == 0 ||
                hero.ability_duration == 0 ||
                hero.ability_power <= Fix32::zero() ||
                hero.ability_power > Fix32::from_int(4096) ||
                hero.surface_area <= Fix32::zero() ||
                hero.surface_area > Fix32::from_int(16)) {
                return fail("hero stats are out of range");
            }
            for (std::size_t other = 0; other < index; ++other) {
                if (content.heroes[other].id == hero.id) {
                    return fail("duplicate hero id");
                }
            }
        }

        for (std::size_t index = 0; index < content.weapons.size(); ++index) {
            WeaponStats& weapon = content.weapons[index];
            weapon.type = static_cast<WeaponType>(index);
            if (weapon.id.empty() || weapon.id.size() > kMaxContentIdLength) {
                return fail("weapon id is invalid");
            }
            if (weapon.magazine == 0 || weapon.magazine > 512 ||
                weapon.fire_interval == 0 || weapon.reload_ticks > 6000 ||
                weapon.reload_size > weapon.magazine ||
                weapon.warmup_ticks > 6000 ||
                weapon.damage > 2000 || weapon.pellets == 0 ||
                weapon.pellets > 32 || weapon.projectile_life == 0 ||
                weapon.projectile_life > 3600 ||
                weapon.projectile_speed <= Fix32::zero() ||
                weapon.spread < Fix32::zero() ||
                weapon.projectile_half_size <= Fix32::zero() ||
                weapon.range < Fix32::zero() ||
                weapon.falloff_start < Fix32::zero() ||
                weapon.falloff_start > Fix32::one() ||
                weapon.falloff_end < Fix32::zero() ||
                weapon.falloff_end > Fix32::one() ||
                weapon.explosion_radius < Fix32::zero() ||
                weapon.divergence_degrees < Fix32::zero() ||
                weapon.divergence_degrees > Fix32::from_int(180) ||
                weapon.dispersion_degrees < Fix32::zero() ||
                weapon.dispersion_degrees > Fix32::from_int(360) ||
                weapon.dispersion_ticks > 6000 ||
                weapon.explosive > 1 || weapon.platform_puncher > 1 ||
                weapon.terrain_bounces > 8 ||
                weapon.explode_on_expire > 1 ||
                weapon.gravity < Fix32::zero() ||
                weapon.gravity > Fix32::from_int(10) ||
                weapon.normal_velocity < Fix32::zero() ||
                weapon.normal_velocity > Fix32::from_int(10) ||
                weapon.damping_rate < Fix32::zero() ||
                weapon.damping_rate > Fix32::one() ||
                weapon.shell_impact > 1 || weapon.damage_sensor > 1 ||
                static_cast<std::uint8_t>(weapon.wall_shot_action) > 3 ||
                weapon.blast_falloff_start < Fix32::zero() ||
                weapon.blast_falloff_start > Fix32::one() ||
                weapon.blast_falloff_end < Fix32::zero() ||
                weapon.blast_falloff_end > Fix32::one() ||
                weapon.blast_self_ratio < Fix32::zero() ||
                weapon.blast_self_ratio > Fix32::one() ||
                weapon.blast_friend_ratio < Fix32::zero() ||
                weapon.blast_friend_ratio > Fix32::one() ||
                weapon.explosion_impulse < Fix32::zero() ||
                weapon.explosion_ground_impulse < Fix32::zero() ||
                weapon.impulse_falloff_start < Fix32::zero() ||
                weapon.impulse_falloff_start > Fix32::one() ||
                weapon.impulse_falloff_end < Fix32::zero() ||
                weapon.impulse_falloff_end > Fix32::one() ||
                weapon.critical_chance < Fix32::zero() ||
                weapon.critical_chance > Fix32::one() ||
                weapon.critical_ratio < Fix32::zero() ||
                weapon.critical_ratio > Fix32::from_int(10) ||
                (weapon.explosive != 0 &&
                 weapon.explosion_radius <= Fix32::zero())) {
                return fail("weapon stats are out of range");
            }
            for (std::size_t other = 0; other < index; ++other) {
                if (content.weapons[other].id == weapon.id) {
                    return fail("duplicate weapon id");
                }
            }
        }

        bool has_crystal_drop = false;
        for (std::size_t index = 0; index < content.drops.size(); ++index) {
            DropStats& drop = content.drops[index];
            if (drop.id.empty() || drop.id.size() > kMaxContentIdLength ||
                drop.sprite.size() > kMaxContentIdLength ||
                drop.respawn_ticks == 0 || drop.vacant_ticks > 3600 ||
                drop.max_capacity == 0 || drop.max_capacity > 8) {
                return fail("drop stats are out of range");
            }
            if (drop.id == "crystal") {
                if (drop.kind != DropKind::Crystal) {
                    return fail("crystal drop has the wrong kind");
                }
                has_crystal_drop = true;
            }
            for (std::size_t other = 0; other < index; ++other) {
                if (content.drops[other].id == drop.id) {
                    return fail("duplicate drop id");
                }
            }
        }
        if (!has_crystal_drop) {
            if (content.drops.size() >= kMaxDrops) {
                return fail("missing crystal drop and no capacity left");
            }
            DropStats crystal;
            crystal.kind = DropKind::Crystal;
            crystal.id = "crystal";
            crystal.pickup = DropPickup::Any;
            crystal.respawn_ticks = 600;
            crystal.vacant_ticks = 30;
            crystal.max_capacity = 3;
            crystal.sprite = "crystal_blue_small";
            content.drops.push_back(std::move(crystal));
        }
        if (content.crystal_rules.deliver_ticks == 0 ||
            content.crystal_rules.deliver_ticks > 3600 ||
            content.crystal_rules.score_per_delivery == 0 ||
            content.crystal_rules.score_per_delivery > 100 ||
            content.crystal_rules.carry_limit == 0 ||
            content.crystal_rules.carry_limit > 8 ||
            content.crystal_rules.attach_score > 100 ||
            content.crystal_rules.hold_score > 100 ||
            content.crystal_rules.hold_score_ticks > 3600) {
            return fail("crystal mode rules are out of range");
        }

        for (HeroStats& hero : content.heroes) {
            if (hero.weapon.empty()) {
                hero.weapon = "rifle";
            }
            const auto match = std::find_if(
                content.weapons.begin(),
                content.weapons.end(),
                [&](const WeaponStats& weapon) {
                    return weapon.id == hero.weapon;
                });
            if (match == content.weapons.end()) {
                return fail("hero weapon is unknown");
            }
        }
        return true;
    }

    std::string_view text_;
    std::size_t position_ = 0;
    std::string error_;
};

void append_string(ByteWriter& writer, const std::string& value) {
    writer.u32(static_cast<std::uint32_t>(value.size()));
    writer.bytes(value.data(), value.size());
}

} // namespace

ContentDatabase::ContentDatabase() {
    reset_defaults();
}

void ContentDatabase::reset_defaults() {
    hero_count_ = 10;
    heroes_[0] = HeroStats{
        HeroType::Scout,
        "scout",
        100,
        50,
        Fix32::from_ratio(689, 6000),
        Fix32::from_ratio(13, 80),
        HeroAbilityType::Dash,
        "KICKSTART",
        420,
        12,
        Fix32::from_ratio(7, 20),
        Fix32::from_ratio(908, 1000),
        "rocket",
    };
    heroes_[1] = HeroStats{
        HeroType::Gunner,
        "gunner",
        140,
        70,
        Fix32::from_ratio(559, 6000),
        Fix32::from_ratio(169, 1000),
        HeroAbilityType::Shield,
        "POWER BATTERY",
        600,
        30,
        Fix32::from_int(40),
        Fix32::from_ratio(1498, 1000),
        "smg",
    };
    heroes_[2] = HeroStats{
        HeroType::Assault,
        "assault",
        120,
        60,
        Fix32::from_ratio(611, 6000),
        Fix32::from_ratio(169, 1000),
        HeroAbilityType::Overcharge,
        "OVERCHARGE",
        600,
        180,
        Fix32::from_ratio(3, 2),
        Fix32::from_ratio(1498, 1000),
        "shotgun",
    };
    heroes_[3] = HeroStats{
        HeroType::Grenadier,
        "grenadier",
        130,
        60,
        Fix32::from_ratio(98, 1000),
        Fix32::from_ratio(169, 1000),
        HeroAbilityType::Overcharge,
        "OVERCHARGE",
        620,
        150,
        Fix32::from_ratio(14, 10),
        Fix32::from_ratio(10605, 10000),
        "grenade",
    };
    heroes_[4] = HeroStats{
        HeroType::Cyborg,
        "cyborg",
        110,
        55,
        Fix32::from_ratio(108, 1000),
        Fix32::from_ratio(172, 1000),
        HeroAbilityType::Dash,
        "KICKSTART",
        420,
        12,
        Fix32::from_ratio(35, 100),
        Fix32::from_ratio(908, 1000),
        "burst",
    };
    heroes_[5] = HeroStats{
        HeroType::Hermit,
        "hermit",
        125,
        65,
        Fix32::from_ratio(95, 1000),
        Fix32::from_ratio(168, 1000),
        HeroAbilityType::Shield,
        "POWER BATTERY",
        620,
        36,
        Fix32::from_int(44),
        Fix32::from_ratio(1498, 1000),
        "grenade",
    };
    heroes_[6] = HeroStats{
        HeroType::Predator,
        "predator",
        145,
        75,
        Fix32::from_ratio(9, 100),
        Fix32::from_ratio(166, 1000),
        HeroAbilityType::Overcharge,
        "OVERCHARGE",
        660,
        160,
        Fix32::from_ratio(16, 10),
        Fix32::from_ratio(1498, 1000),
        "smg",
    };
    heroes_[7] = HeroStats{
        HeroType::Pyromancer,
        "pyromancer",
        105,
        45,
        Fix32::from_ratio(112, 1000),
        Fix32::from_ratio(171, 1000),
        HeroAbilityType::Dash,
        "KICKSTART",
        400,
        14,
        Fix32::from_ratio(4, 10),
        Fix32::from_ratio(1498, 1000),
        "rocket",
    };
    heroes_[8] = HeroStats{
        HeroType::Soldier,
        "soldier",
        115,
        55,
        Fix32::from_ratio(104, 1000),
        Fix32::from_ratio(169, 1000),
        HeroAbilityType::Shield,
        "POWER BATTERY",
        580,
        28,
        Fix32::from_int(38),
        Fix32::from_ratio(1498, 1000),
        "rifle",
    };
    heroes_[9] = HeroStats{
        HeroType::Warrioress,
        "warrioress",
        120,
        60,
        Fix32::from_ratio(107, 1000),
        Fix32::from_ratio(173, 1000),
        HeroAbilityType::Dash,
        "KICKSTART",
        430,
        12,
        Fix32::from_ratio(36, 100),
        Fix32::from_ratio(10605, 10000),
        "shotgun",
    };

    weapon_count_ = 6;
    weapons_[0] = WeaponStats{
        .type = WeaponType::Rifle,
        .id = "rifle",
        .magazine = 15,
        .fire_interval = 12,
        .reload_ticks = 108,
        .reload_size = 0,
        .warmup_ticks = 0,
        .damage = 16,
        .pellets = 1,
        .projectile_life = 40,
        .projectile_speed = Fix32::from_ratio(19, 30),
        .spread = Fix32::zero(),
        .projectile_half_size = Fix32::from_ratio(1, 10),
        .range = Fix32::from_int(20),
        .falloff_start = Fix32::one(),
        .falloff_end = Fix32::one(),
        .explosion_radius = Fix32::zero(),
        .explosion_damage = 0,
        .explosive = 0,
        .platform_puncher = 0,
        .joint_x = Fix32::from_ratio(3, 50),
        .joint_y = Fix32::from_ratio(21, 100),
        .gun_x = Fix32::from_ratio(51, 50),
        .gun_y = Fix32::from_ratio(21, 100),
        .divergence_degrees = Fix32::zero(),
        .dispersion_degrees = Fix32::from_int(4),
        .dispersion_ticks = 28,
    };

    weapons_[1] = WeaponStats{
        .type = WeaponType::Shotgun,
        .id = "shotgun",
        .magazine = 6,
        .fire_interval = 72,
        .reload_ticks = 84,
        .reload_size = 2,
        .warmup_ticks = 0,
        .damage = 13,
        .pellets = 7,
        .projectile_life = 2,
        .projectile_speed = Fix32::from_ratio(19, 5),
        .spread = Fix32::zero(),
        .projectile_half_size = Fix32::from_ratio(1, 10),
        .range = Fix32::from_ratio(38, 5),
        .falloff_start = Fix32::from_ratio(1, 2),
        .falloff_end = Fix32::from_ratio(1, 2),
        .explosion_radius = Fix32::zero(),
        .explosion_damage = 0,
        .explosive = 0,
        .platform_puncher = 0,
        .joint_x = Fix32::from_ratio(1, 250),
        .joint_y = Fix32::from_ratio(31, 200),
        .gun_x = Fix32::from_ratio(61, 100),
        .gun_y = Fix32::from_ratio(-1, 50),
        .divergence_degrees = Fix32::from_int(13),
        .dispersion_degrees = Fix32::from_int(9),
        .dispersion_ticks = 80,
    };

    weapons_[2] = WeaponStats{
        .type = WeaponType::Rocket,
        .id = "rocket",
        .magazine = 5,
        .fire_interval = 48,
        .reload_ticks = 108,
        .reload_size = 0,
        .warmup_ticks = 0,
        .damage = 36,
        .pellets = 1,
        .projectile_life = 100,
        .projectile_speed = Fix32::from_ratio(1, 5),
        .spread = Fix32::zero(),
        .projectile_half_size = Fix32::from_ratio(1, 5),
        .range = Fix32::from_int(20),
        .falloff_start = Fix32::one(),
        .falloff_end = Fix32::one(),
        .explosion_radius = Fix32::from_int(2),
        .explosion_damage = 18,
        .explosive = 1,
        .platform_puncher = 0,
        .joint_x = Fix32::from_ratio(27, 500),
        .joint_y = Fix32::from_ratio(1, 5),
        .gun_x = Fix32::from_ratio(77, 100),
        .gun_y = Fix32::from_ratio(-1, 20),
        .divergence_degrees = Fix32::zero(),
        .dispersion_degrees = Fix32::zero(),
        .dispersion_ticks = 0,
    };

    weapons_[3] = WeaponStats{
        .type = WeaponType::Smg,
        .id = "smg",
        .magazine = 40,
        .fire_interval = 8,
        .reload_ticks = 132,
        .reload_size = 0,
        .warmup_ticks = 40,
        .damage = 13,
        .pellets = 1,
        .projectile_life = 2,
        .projectile_speed = Fix32::from_int(8),
        .spread = Fix32::zero(),
        .projectile_half_size = Fix32::from_ratio(3, 100),
        .range = Fix32::from_int(16),
        .falloff_start = Fix32::from_ratio(7, 10),
        .falloff_end = Fix32::from_ratio(7, 10),
        .explosion_radius = Fix32::zero(),
        .explosion_damage = 0,
        .explosive = 0,
        .platform_puncher = 1,
        .joint_x = Fix32::from_ratio(1, 5),
        .joint_y = Fix32::from_ratio(-9, 100),
        .gun_x = Fix32::from_ratio(57, 50),
        .gun_y = Fix32::from_ratio(-3, 10),
        .divergence_degrees = Fix32::zero(),
        .dispersion_degrees = Fix32::from_int(6),
        .dispersion_ticks = 40,
    };

    weapons_[4] = WeaponStats{
        .type = WeaponType::Burst,
        .id = "burst",
        .magazine = 6,
        .fire_interval = 48,
        .reload_ticks = 120,
        .reload_size = 0,
        .warmup_ticks = 0,
        .damage = 60,
        .pellets = 1,
        .projectile_life = 60,
        .projectile_speed = Fix32::from_ratio(1, 3),
        .spread = Fix32::zero(),
        .projectile_half_size = Fix32::from_ratio(3, 20),
        .range = Fix32::from_int(20),
        .falloff_start = Fix32::one(),
        .falloff_end = Fix32::one(),
        .explosion_radius = Fix32::zero(),
        .explosion_damage = 0,
        .explosive = 0,
        .platform_puncher = 0,
        .terrain_bounces = 2,
        .joint_x = Fix32::from_ratio(287, 1000),
        .joint_y = Fix32::from_ratio(282, 1000),
        .gun_x = Fix32::from_ratio(83, 100),
        .gun_y = Fix32::from_ratio(-21, 100),
        .divergence_degrees = Fix32::zero(),
        .dispersion_degrees = Fix32::zero(),
        .dispersion_ticks = 0,
    };

    weapons_[5] = WeaponStats{
        .type = WeaponType::Grenade,
        .id = "grenade",
        .magazine = 5,
        .fire_interval = 84,
        .reload_ticks = 120,
        .reload_size = 0,
        .warmup_ticks = 0,
        .damage = 0,
        .pellets = 1,
        .projectile_life = 90,
        .projectile_speed = Fix32::from_ratio(1, 5),
        .spread = Fix32::zero(),
        .projectile_half_size = Fix32::from_ratio(1, 5),
        .range = Fix32::from_int(20),
        .falloff_start = Fix32::one(),
        .falloff_end = Fix32::one(),
        .explosion_radius = Fix32::from_int(2),
        .explosion_damage = 60,
        .explosive = 1,
        .platform_puncher = 0,
        .joint_x = Fix32::from_ratio(9, 200),
        .joint_y = Fix32::from_ratio(-6, 125),
        .gun_x = Fix32::one(),
        .gun_y = Fix32::from_ratio(97, 500),
        .divergence_degrees = Fix32::zero(),
        .dispersion_degrees = Fix32::zero(),
        .dispersion_ticks = 0,
    };

    weapons_[2].explode_on_expire = 1;
    weapons_[2].blast_falloff_start = Fix32::from_ratio(55, 100);
    weapons_[2].blast_falloff_end = Fix32::from_ratio(60, 100);
    weapons_[2].blast_self_ratio = Fix32::from_ratio(70, 100);
    weapons_[2].blast_friend_ratio = Fix32::zero();
    weapons_[2].explosion_impulse = Fix32::from_int(400);
    weapons_[2].explosion_ground_impulse = Fix32::from_int(100);
    weapons_[2].impulse_falloff_start = Fix32::one();
    weapons_[2].impulse_falloff_end = Fix32::one();

    weapons_[5].terrain_bounces = 2;
    weapons_[5].explode_on_expire = 1;
    weapons_[5].blast_falloff_start = Fix32::from_ratio(55, 100);
    weapons_[5].blast_falloff_end = Fix32::from_ratio(60, 100);
    weapons_[5].blast_self_ratio = Fix32::from_ratio(70, 100);
    weapons_[5].blast_friend_ratio = Fix32::zero();
    weapons_[5].explosion_impulse = Fix32::from_int(1000);
    weapons_[5].explosion_ground_impulse = Fix32::from_int(300);
    weapons_[5].impulse_falloff_start = Fix32::from_ratio(1, 2);
    weapons_[5].impulse_falloff_end = Fix32::from_ratio(3, 10);

    weapons_[2].detonator_hp = 40;
    weapons_[2].damage_sensor = 1;
    weapons_[3].wall_shot_action = WallShotAction::Skip;
    weapons_[4].wall_shot_action = WallShotAction::Bounce;
    weapons_[4].detonator_hp = 1;
    weapons_[4].shell_impact = 1;
    weapons_[5].gravity = Fix32::from_ratio(981, 360000);
    weapons_[5].normal_velocity = Fix32::from_ratio(1, 120);
    weapons_[5].detonator_hp = 40;
    weapons_[5].shell_impact = 1;
    weapons_[5].damage_sensor = 1;
    weapons_[5].wall_shot_action = WallShotAction::Bounce;

    drop_count_ = 1;
    drops_[0] = DropStats{
        DropKind::Crystal,
        "crystal",
        DropPickup::Any,
        600,
        30,
        3,
        "crystal_blue_small",
    };
    crystal_rules_ = CrystalRules{};
    crystal_rules_.deliver_ticks = 60;
    crystal_rules_.score_per_delivery = 1;
    crystal_rules_.attach_score = 0;
    crystal_rules_.hold_score = 0;
    crystal_rules_.hold_score_ticks = 0;
    crystal_rules_.carry_limit = 3;
}

bool ContentDatabase::load_json(std::string_view text, std::string* error) {
    ParsedContent parsed;
    Parser parser(text);
    if (!parser.parse(parsed, error)) {
        return false;
    }

    ContentDatabase result;
    result.hero_count_ = parsed.heroes.size();
    result.weapon_count_ = parsed.weapons.size();
    result.drop_count_ = parsed.drops.size();
    result.crystal_rules_ = parsed.crystal_rules;
    for (std::size_t index = 0; index < result.hero_count_; ++index) {
        result.heroes_[index] = std::move(parsed.heroes[index]);
    }
    for (std::size_t index = 0; index < result.weapon_count_; ++index) {
        result.weapons_[index] = std::move(parsed.weapons[index]);
    }
    for (std::size_t index = 0; index < result.drop_count_; ++index) {
        result.drops_[index] = std::move(parsed.drops[index]);
    }
    *this = std::move(result);
    return true;
}

bool ContentDatabase::load_file(
    const std::filesystem::path& path,
    std::string* error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        if (error) {
            *error = "failed to open content file: " + path.string();
        }
        return false;
    }
    const std::streampos end = input.tellg();
    if (end < 0) {
        if (error) {
            *error = "failed to size content file: " + path.string();
        }
        return false;
    }
    std::string text(static_cast<std::size_t>(end), '\0');
    input.seekg(0, std::ios::beg);
    if (!text.empty() &&
        !input.read(text.data(), static_cast<std::streamsize>(text.size()))) {
        if (error) {
            *error = "failed to read content file: " + path.string();
        }
        return false;
    }
    return load_json(text, error);
}

const HeroStats& ContentDatabase::hero(std::size_t index) const {
    return heroes_[index < hero_count_ ? index : 0];
}

const WeaponStats& ContentDatabase::weapon(std::size_t index) const {
    return weapons_[index < weapon_count_ ? index : 0];
}

const DropStats& ContentDatabase::drop(std::size_t index) const {
    return drops_[index < drop_count_ ? index : 0];
}

const HeroStats* ContentDatabase::find_hero(std::string_view id) const {
    for (std::size_t index = 0; index < hero_count_; ++index) {
        if (heroes_[index].id == id) {
            return &heroes_[index];
        }
    }
    return nullptr;
}

const WeaponStats* ContentDatabase::find_weapon(std::string_view id) const {
    for (std::size_t index = 0; index < weapon_count_; ++index) {
        if (weapons_[index].id == id) {
            return &weapons_[index];
        }
    }
    return nullptr;
}

const DropStats* ContentDatabase::find_drop(std::string_view id) const {
    for (std::size_t index = 0; index < drop_count_; ++index) {
        if (drops_[index].id == id) {
            return &drops_[index];
        }
    }
    return nullptr;
}

std::uint8_t ContentDatabase::find_drop_type(std::string_view id) const {
    for (std::size_t index = 0; index < drop_count_; ++index) {
        if (drops_[index].id == id) {
            return static_cast<std::uint8_t>(index);
        }
    }
    return 0;
}

std::uint64_t ContentDatabase::fingerprint() const {
    ByteWriter writer;
    writer.u32(kContentVersion);
    writer.u32(static_cast<std::uint32_t>(hero_count_));
    for (std::size_t index = 0; index < hero_count_; ++index) {
        const HeroStats& hero = heroes_[index];
        writer.u8(static_cast<std::uint8_t>(hero.type));
        append_string(writer, hero.id);
        writer.u16(hero.health);
        writer.u16(hero.shield);
        writer.u32(static_cast<std::uint32_t>(hero.run_speed.raw()));
        writer.u32(static_cast<std::uint32_t>(hero.jump_speed.raw()));
        writer.u8(static_cast<std::uint8_t>(hero.ability));
        append_string(writer, hero.ability_name);
        writer.u16(hero.ability_cooldown);
        writer.u16(hero.ability_duration);
        writer.u32(static_cast<std::uint32_t>(hero.ability_power.raw()));
        writer.u32(static_cast<std::uint32_t>(hero.surface_area.raw()));
        append_string(writer, hero.weapon);
    }
    writer.u32(static_cast<std::uint32_t>(weapon_count_));
    for (std::size_t index = 0; index < weapon_count_; ++index) {
        const WeaponStats& weapon = weapons_[index];
        writer.u8(static_cast<std::uint8_t>(weapon.type));
        append_string(writer, weapon.id);
        writer.u16(weapon.magazine);
        writer.u16(weapon.fire_interval);
        writer.u16(weapon.reload_ticks);
        writer.u16(weapon.reload_size);
        writer.u16(weapon.warmup_ticks);
        writer.u16(weapon.damage);
        writer.u16(weapon.pellets);
        writer.u16(weapon.projectile_life);
        writer.u32(static_cast<std::uint32_t>(weapon.projectile_speed.raw()));
        writer.u32(static_cast<std::uint32_t>(weapon.spread.raw()));
        writer.u32(static_cast<std::uint32_t>(
            weapon.projectile_half_size.raw()));
        writer.u32(static_cast<std::uint32_t>(weapon.range.raw()));
        writer.u32(static_cast<std::uint32_t>(weapon.falloff_start.raw()));
        writer.u32(static_cast<std::uint32_t>(weapon.falloff_end.raw()));
        writer.u32(static_cast<std::uint32_t>(weapon.explosion_radius.raw()));
        writer.u16(weapon.explosion_damage);
        writer.u8(weapon.explosive);
        writer.u8(weapon.platform_puncher);
        writer.u8(weapon.terrain_bounces);
        writer.u8(weapon.explode_on_expire);
        writer.u32(static_cast<std::uint32_t>(
            weapon.blast_falloff_start.raw()));
        writer.u32(static_cast<std::uint32_t>(
            weapon.blast_falloff_end.raw()));
        writer.u32(static_cast<std::uint32_t>(weapon.blast_self_ratio.raw()));
        writer.u32(static_cast<std::uint32_t>(
            weapon.blast_friend_ratio.raw()));
        writer.u32(static_cast<std::uint32_t>(
            weapon.explosion_impulse.raw()));
        writer.u32(static_cast<std::uint32_t>(
            weapon.explosion_ground_impulse.raw()));
        writer.u32(static_cast<std::uint32_t>(
            weapon.impulse_falloff_start.raw()));
        writer.u32(static_cast<std::uint32_t>(
            weapon.impulse_falloff_end.raw()));
        writer.u32(static_cast<std::uint32_t>(weapon.critical_chance.raw()));
        writer.u32(static_cast<std::uint32_t>(weapon.critical_ratio.raw()));
        writer.u32(static_cast<std::uint32_t>(weapon.joint_x.raw()));
        writer.u32(static_cast<std::uint32_t>(weapon.joint_y.raw()));
        writer.u32(static_cast<std::uint32_t>(weapon.gun_x.raw()));
        writer.u32(static_cast<std::uint32_t>(weapon.gun_y.raw()));
        writer.u32(static_cast<std::uint32_t>(
            weapon.divergence_degrees.raw()));
        writer.u32(static_cast<std::uint32_t>(weapon.dispersion_degrees.raw()));
        writer.u16(weapon.dispersion_ticks);
        writer.u32(static_cast<std::uint32_t>(weapon.gravity.raw()));
        writer.u32(static_cast<std::uint32_t>(weapon.normal_velocity.raw()));
        writer.u32(static_cast<std::uint32_t>(weapon.damping_rate.raw()));
        writer.u16(weapon.detonator_hp);
        writer.u8(weapon.shell_impact);
        writer.u8(weapon.damage_sensor);
        writer.u8(static_cast<std::uint8_t>(weapon.wall_shot_action));
    }
    writer.u32(static_cast<std::uint32_t>(drop_count_));
    for (std::size_t index = 0; index < drop_count_; ++index) {
        const DropStats& drop = drops_[index];
        writer.u8(static_cast<std::uint8_t>(drop.kind));
        append_string(writer, drop.id);
        writer.u8(static_cast<std::uint8_t>(drop.pickup));
        writer.u16(drop.respawn_ticks);
        writer.u16(drop.vacant_ticks);
        writer.u8(drop.max_capacity);
        append_string(writer, drop.sprite);
    }
    writer.u16(crystal_rules_.deliver_ticks);
    writer.u16(crystal_rules_.score_per_delivery);
    writer.u16(crystal_rules_.attach_score);
    writer.u16(crystal_rules_.hold_score);
    writer.u16(crystal_rules_.hold_score_ticks);
    writer.u8(crystal_rules_.carry_limit);
    return hash64_bytes(writer.data().data(), writer.data().size());
}

ContentDatabase& content() {
    static ContentDatabase database;
    return database;
}

const HeroStats& hero_stats(HeroType type) {
    return content().hero(static_cast<std::size_t>(type));
}

const WeaponStats& weapon_stats(WeaponType type) {
    return content().weapon(static_cast<std::size_t>(type));
}

WeaponType hero_weapon(HeroType type) {
    const HeroStats& hero = hero_stats(type);
    const WeaponStats* weapon = content().find_weapon(hero.weapon);
    return weapon != nullptr ? weapon->type : WeaponType::Rifle;
}

WeaponType next_weapon(WeaponType type) {
    const std::size_t count = content().weapon_count();
    if (count == 0) {
        return WeaponType::Rifle;
    }
    return static_cast<WeaponType>(
        (static_cast<std::size_t>(type) + 1U) % count);
}

std::string_view hero_name(HeroType type) {
    return hero_stats(type).id;
}

std::string_view weapon_name(WeaponType type) {
    return weapon_stats(type).id;
}

} // namespace nsync::core
