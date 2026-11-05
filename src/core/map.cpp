#include "core/map.h"

#include "core/byte_io.h"
#include "core/hash.h"

#include <cctype>
#include <fstream>
#include <limits>
#include <utility>

namespace nsync::core {
namespace {

constexpr std::size_t kMaxJsonDepth = 32;

bool is_space(char value) {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

bool is_digit(char value) {
    return value >= '0' && value <= '9';
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    bool parse(Map& map, std::string* error) {
        skip_whitespace();
        bool has_id = false;
        bool has_bounds = false;
        bool has_spawns = false;

        if (!consume('{')) {
            return fail("expected map object");
        }
        skip_whitespace();
        if (consume('}')) {
            return fail("empty map object");
        }

        for (;;) {
            std::string key;
            if (!parse_string(key)) {
                return finish(error);
            }
            skip_whitespace();
            if (!consume(':')) {
                return fail("expected ':' after object key");
            }
            skip_whitespace();

            bool ok = true;
            if (key == "id") {
                ok = parse_string(map.id);
                has_id = true;
            } else if (key == "bounds") {
                ok = parse_bounds(map.bounds);
                has_bounds = true;
            } else if (key == "solids") {
                ok = parse_shape_list(map.solids);
            } else if (key == "platforms") {
                ok = parse_shape_list(map.one_way_platforms);
            } else if (key == "platform_hatchway") {
                ok = parse_bool_list(map.platform_hatchway);
            } else if (key == "jump_pads") {
                ok = parse_jump_pads(map.jump_pads);
            } else if (key == "objective") {
                AABB objective;
                ok = parse_rect(objective);
                if (ok) {
                    map.objective = objective;
                }
            } else if (key == "spawns") {
                ok = parse_spawns(map.spawns);
                has_spawns = true;
            } else if (key == "drop_spawns") {
                ok = parse_drop_spawns(map.drop_spawns);
            } else if (key == "drop_depots") {
                ok = parse_drop_depots(map.drop_depots);
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
                return fail("expected ',' or '}' in map object");
            }
            skip_whitespace();
        }

        skip_whitespace();
        if (position_ != text_.size()) {
            fail("trailing data after map object");
            return finish(error);
        }
        if (!has_id || !has_bounds || !has_spawns) {
            fail("map requires id, bounds and spawns");
            return finish(error);
        }
        if (map.platform_hatchway.empty()) {
            map.platform_hatchway.assign(
                map.one_way_platforms.size(), 1);
        } else if (map.platform_hatchway.size() !=
                   map.one_way_platforms.size()) {
            fail("platform hatchway count mismatch");
            return finish(error);
        }
        std::string validation_error;
        if (!map.valid(&validation_error)) {
            fail(validation_error);
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
            *error = error_.empty() ? "invalid map json" : error_;
        }
        return false;
    }

    bool fail(std::string message) {
        if (error_.empty()) {
            error_ = std::move(message) + " at byte " + std::to_string(position_);
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
                            codepoint |= static_cast<std::uint32_t>(digit - 'a' + 10);
                        } else if (digit >= 'A' && digit <= 'F') {
                            codepoint |= static_cast<std::uint32_t>(digit - 'A' + 10);
                        } else {
                            return fail("invalid unicode escape");
                        }
                    }
                    if (codepoint <= 0x7FU) {
                        result.push_back(static_cast<char>(codepoint));
                    } else if (codepoint <= 0x7FFU) {
                        result.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
                        result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
                    } else {
                        result.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
                        result.push_back(
                            static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
                        result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
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

    bool parse_number(Fix32& value) {
        std::string_view token;
        if (!parse_number_token(token)) {
            return false;
        }
        if (!parse_fix32_decimal(token, value)) {
            return fail("invalid map number");
        }
        return true;
    }

    bool parse_vec2(Vec2& value) {
        skip_whitespace();
        if (!consume('[')) {
            return fail("expected vec2 array");
        }
        if (!parse_number(value.x)) {
            return false;
        }
        skip_whitespace();
        if (!consume(',')) {
            return fail("expected ',' in vec2");
        }
        if (!parse_number(value.y)) {
            return false;
        }
        skip_whitespace();
        if (!consume(']')) {
            return fail("expected ']' after vec2");
        }
        return true;
    }

    bool parse_bounds(AABB& value) {
        skip_whitespace();
        if (!consume('[')) {
            return fail("expected bounds array");
        }
        Vec2 origin;
        Vec2 size;
        if (!parse_number(origin.x)) {
            return false;
        }
        skip_whitespace();
        if (!consume(',')) {
            return fail("expected ',' in bounds");
        }
        if (!parse_number(origin.y)) {
            return false;
        }
        skip_whitespace();
        if (!consume(',')) {
            return fail("expected ',' in bounds");
        }
        if (!parse_number(size.x)) {
            return false;
        }
        skip_whitespace();
        if (!consume(',')) {
            return fail("expected ',' in bounds");
        }
        if (!parse_number(size.y)) {
            return false;
        }
        skip_whitespace();
        if (!consume(']')) {
            return fail("expected ']' after bounds");
        }
        if (size.x <= Fix32::zero() || size.y <= Fix32::zero()) {
            return fail("bounds size must be positive");
        }
        value = AABB{origin, origin + size};
        return true;
    }

    bool parse_rect(AABB& value) {
        skip_whitespace();
        if (consume('[')) {
            Vec2 origin;
            Vec2 size;
            if (!parse_number(origin.x)) {
                return false;
            }
            skip_whitespace();
            if (!consume(',')) {
                return fail("expected ',' in rect");
            }
            if (!parse_number(origin.y)) {
                return false;
            }
            skip_whitespace();
            if (!consume(',')) {
                return fail("expected ',' in rect");
            }
            if (!parse_number(size.x)) {
                return false;
            }
            skip_whitespace();
            if (!consume(',')) {
                return fail("expected ',' in rect");
            }
            if (!parse_number(size.y)) {
                return false;
            }
            skip_whitespace();
            if (!consume(']')) {
                return fail("expected ']' after rect");
            }
            if (size.x <= Fix32::zero() || size.y <= Fix32::zero()) {
                return fail("rect size must be positive");
            }
            value = AABB{origin, origin + size};
            return true;
        }

        if (!consume('{')) {
            return fail("expected rect object");
        }
        bool has_rect = false;
        for (;;) {
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            skip_whitespace();
            if (!consume(':')) {
                return fail("expected ':' after rect key");
            }
            skip_whitespace();
            if (key == "rect") {
                if (!parse_rect(value)) {
                    return false;
                }
                has_rect = true;
            } else if (!skip_value(0)) {
                return false;
            }
            skip_whitespace();
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                return fail("expected ',' or '}' in rect object");
            }
            skip_whitespace();
        }
        if (!has_rect) {
            return fail("rect object requires rect");
        }
        return true;
    }

    bool parse_shape_list(std::vector<AABB>& value) {
        skip_whitespace();
        if (!consume('[')) {
            return fail("expected shape array");
        }
        value.clear();
        skip_whitespace();
        if (consume(']')) {
            return true;
        }
        for (;;) {
            if (value.size() >= kMapMaxShapes) {
                return fail("too many map shapes");
            }
            AABB shape;
            if (!parse_rect(shape)) {
                return false;
            }
            value.push_back(shape);
            skip_whitespace();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return fail("expected ',' or ']' in shape array");
            }
            skip_whitespace();
        }
    }

    bool parse_bool_list(std::vector<std::uint8_t>& value) {
        skip_whitespace();
        if (!consume('[')) {
            return fail("expected bool array");
        }
        value.clear();
        skip_whitespace();
        if (consume(']')) {
            return true;
        }
        for (;;) {
            if (value.size() >= kMapMaxShapes) {
                return fail("too many map flags");
            }
            skip_whitespace();
            bool flag = false;
            if (text_.substr(position_, 4) == "true") {
                position_ += 4;
                flag = true;
            } else if (text_.substr(position_, 5) == "false") {
                position_ += 5;
            } else {
                return fail("expected true or false");
            }
            value.push_back(flag ? 1 : 0);
            skip_whitespace();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return fail("expected ',' or ']' in bool array");
            }
            skip_whitespace();
        }
    }

    bool parse_jump_pads(std::vector<JumpPad>& value) {
        skip_whitespace();
        if (!consume('[')) {
            return fail("expected jump pad array");
        }
        value.clear();
        skip_whitespace();
        if (consume(']')) {
            return true;
        }
        for (;;) {
            if (value.size() >= kMapMaxShapes) {
                return fail("too many map jump pads");
            }
            skip_whitespace();
            if (!consume('{')) {
                return fail("expected jump pad object");
            }
            JumpPad pad;
            bool has_rect = false;
            bool has_velocity = false;
            for (;;) {
                std::string key;
                if (!parse_string(key)) {
                    return false;
                }
                skip_whitespace();
                if (!consume(':')) {
                    return fail("expected ':' after jump pad key");
                }
                skip_whitespace();
                bool ok = true;
                if (key == "rect") {
                    ok = parse_rect(pad.bounds);
                    has_rect = true;
                } else if (key == "velocity") {
                    ok = parse_number(pad.velocity);
                    has_velocity = true;
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
                    return fail("expected ',' or '}' in jump pad");
                }
                skip_whitespace();
            }
            if (!has_rect || !has_velocity ||
                pad.velocity <= Fix32::zero()) {
                return fail("jump pad requires rect and positive velocity");
            }
            value.push_back(pad);
            skip_whitespace();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return fail("expected ',' or ']' in jump pad array");
            }
            skip_whitespace();
        }
    }

    bool parse_spawns(std::array<Vec2, kMapSpawnCount>& value) {
        skip_whitespace();
        if (!consume('[')) {
            return fail("expected spawns array");
        }
        for (std::size_t index = 0; index < value.size(); ++index) {
            if (!parse_vec2(value[index])) {
                return false;
            }
            skip_whitespace();
            if (index + 1 < value.size()) {
                if (!consume(',')) {
                    return fail("expected exactly 10 spawns");
                }
            }
        }
        skip_whitespace();
        if (!consume(']')) {
            return fail("expected ']' after spawns");
        }
        return true;
    }

    bool parse_u16(std::uint16_t& value) {
        Fix32 number;
        if (!parse_number(number)) {
            return false;
        }
        const std::int32_t integer = number.to_int_trunc();
        if (number != Fix32::from_int(integer)) {
            return fail("expected integer");
        }
        if (integer < 0 || integer > 65535) {
            return fail("integer out of range");
        }
        value = static_cast<std::uint16_t>(integer);
        return true;
    }

    bool parse_drop_id(std::string& value) {
        if (!parse_string(value)) {
            return false;
        }
        if (value.empty()) {
            return fail("drop id is empty");
        }
        if (value.size() > kMapMaxDropIdLength) {
            return fail("drop id is too long");
        }
        return true;
    }

    bool parse_string_list(std::vector<std::string>& value) {
        skip_whitespace();
        if (!consume('[')) {
            return fail("expected string array");
        }
        value.clear();
        skip_whitespace();
        if (consume(']')) {
            return true;
        }
        for (;;) {
            if (value.size() >= kMapMaxDepotDrops) {
                return fail("too many depot drop ids");
            }
            std::string item;
            if (!parse_drop_id(item)) {
                return false;
            }
            value.push_back(std::move(item));
            skip_whitespace();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return fail("expected ',' or ']' in string array");
            }
            skip_whitespace();
        }
    }

    bool parse_drop_spawns(std::vector<DropSpawn>& value) {
        skip_whitespace();
        if (!consume('[')) {
            return fail("expected drop spawns array");
        }
        value.clear();
        skip_whitespace();
        if (consume(']')) {
            return true;
        }
        for (;;) {
            if (value.size() >= kMapMaxDropSpawns) {
                return fail("too many drop spawns");
            }
            skip_whitespace();
            if (!consume('{')) {
                return fail("expected drop spawn object");
            }
            DropSpawn spawn;
            bool has_drop = false;
            bool has_position = false;
            for (;;) {
                std::string key;
                if (!parse_string(key)) {
                    return false;
                }
                skip_whitespace();
                if (!consume(':')) {
                    return fail("expected ':' after drop spawn key");
                }
                skip_whitespace();
                bool ok = true;
                if (key == "drop" || key == "type") {
                    ok = parse_drop_id(spawn.drop_id);
                    has_drop = true;
                } else if (key == "at" || key == "pos") {
                    ok = parse_vec2(spawn.position);
                    has_position = true;
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
                    return fail("expected ',' or '}' in drop spawn");
                }
                skip_whitespace();
            }
            if (!has_drop || !has_position) {
                return fail("drop spawn requires drop and at");
            }
            value.push_back(std::move(spawn));
            skip_whitespace();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return fail("expected ',' or ']' in drop spawns array");
            }
            skip_whitespace();
        }
    }

    bool parse_drop_depots(std::vector<DropDepot>& value) {
        skip_whitespace();
        if (!consume('[')) {
            return fail("expected drop depots array");
        }
        value.clear();
        skip_whitespace();
        if (consume(']')) {
            return true;
        }
        for (;;) {
            if (value.size() >= kMapMaxDropDepots) {
                return fail("too many drop depots");
            }
            skip_whitespace();
            if (!consume('{')) {
                return fail("expected drop depot object");
            }
            DropDepot depot;
            bool has_team = false;
            bool has_rect = false;
            for (;;) {
                std::string key;
                if (!parse_string(key)) {
                    return false;
                }
                skip_whitespace();
                if (!consume(':')) {
                    return fail("expected ':' after drop depot key");
                }
                skip_whitespace();
                bool ok = true;
                if (key == "team") {
                    std::uint16_t team = 0;
                    ok = parse_u16(team);
                    if (ok && team > 2) {
                        ok = fail("drop depot team must be 0, 1 or 2");
                    }
                    if (ok) {
                        depot.team = team == 0
                            ? kDropDepotNeutral
                            : static_cast<std::uint8_t>(team - 1);
                        has_team = true;
                    }
                } else if (key == "rect") {
                    ok = parse_rect(depot.bounds);
                    has_rect = true;
                } else if (key == "deliver_ticks") {
                    ok = parse_u16(depot.deliver_ticks);
                } else if (key == "accepts" || key == "accepted_drops") {
                    ok = parse_string_list(depot.accepted_drops);
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
                    return fail("expected ',' or '}' in drop depot");
                }
                skip_whitespace();
            }
            if (!has_team || !has_rect) {
                return fail("drop depot requires team and rect");
            }
            value.push_back(std::move(depot));
            skip_whitespace();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return fail("expected ',' or ']' in drop depots array");
            }
            skip_whitespace();
        }
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
                std::string key;
                if (!parse_string(key)) {
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

    std::string_view text_;
    std::size_t position_ = 0;
    std::string error_;
};

} // namespace

Map Map::default_arena() {
    Map map;
    map.id = "arena_01";
    map.bounds = AABB{
        Vec2{Fix32::from_int(-14), Fix32::zero()},
        Vec2{Fix32::from_int(14), Fix32::from_int(16)}};
    map.solids = {
        AABB{
            Vec2{Fix32::from_int(-14), Fix32::from_int(11)},
            Vec2{Fix32::from_int(14), Fix32::from_int(12)}},
    };
    map.one_way_platforms = {
        AABB{
            Vec2{Fix32::from_int(-8), Fix32::from_ratio(15, 2)},
            Vec2{Fix32::from_int(-3), Fix32::from_int(8)}},
        AABB{
            Vec2{Fix32::from_int(3), Fix32::from_ratio(15, 2)},
            Vec2{Fix32::from_int(8), Fix32::from_int(8)}},
    };
    map.platform_hatchway = {1, 1};

    const Fix32 spawn_y = Fix32::from_ratio(103, 10);
    for (std::int32_t member = 0; member < 5; ++member) {
        const Fix32 offset = Fix32::from_int(member * 2);
        map.spawns[static_cast<std::size_t>(member)] =
            Vec2{Fix32::from_int(-5) - offset, spawn_y};
        map.spawns[static_cast<std::size_t>(member + 5)] =
            Vec2{Fix32::from_int(5) + offset, spawn_y};
    }
    map.drop_spawns = {
        DropSpawn{Vec2{Fix32::zero(), spawn_y}, "crystal"},
        DropSpawn{Vec2{Fix32::from_int(-4), spawn_y}, "crystal"},
        DropSpawn{Vec2{Fix32::from_int(4), spawn_y}, "crystal"},
    };
    map.drop_depots = {
        DropDepot{
            0,
            AABB{
                Vec2{Fix32::from_int(-14), Fix32::from_int(9)},
                Vec2{Fix32::from_ratio(-23, 2), Fix32::from_int(11)}},
            kMapDefaultDeliverTicks,
            {"crystal"}},
        DropDepot{
            1,
            AABB{
                Vec2{Fix32::from_ratio(23, 2), Fix32::from_int(9)},
                Vec2{Fix32::from_int(14), Fix32::from_int(11)}},
            kMapDefaultDeliverTicks,
            {"crystal"}},
    };
    return map;
}

bool Map::valid(std::string* error) const {
    const auto fail = [&](const char* message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    if (id.empty()) {
        return fail("map id is empty");
    }
    if (bounds.min.x >= bounds.max.x || bounds.min.y >= bounds.max.y) {
        return fail("map bounds are invalid");
    }
    if (solids.size() > kMapMaxShapes ||
        one_way_platforms.size() > kMapMaxShapes ||
        jump_pads.size() > kMapMaxShapes) {
        return fail("map has too many shapes");
    }
    for (const AABB& shape : solids) {
        if (shape.min.x >= shape.max.x || shape.min.y >= shape.max.y) {
            return fail("map solid is invalid");
        }
    }
    for (const AABB& shape : one_way_platforms) {
        if (shape.min.x >= shape.max.x || shape.min.y >= shape.max.y) {
            return fail("map platform is invalid");
        }
    }
    if (!platform_hatchway.empty() &&
        platform_hatchway.size() != one_way_platforms.size()) {
        return fail("map platform hatchway count mismatch");
    }
    for (std::uint8_t hatchway : platform_hatchway) {
        if (hatchway > 1) {
            return fail("map platform hatchway flag is invalid");
        }
    }
    for (const JumpPad& pad : jump_pads) {
        if (pad.bounds.min.x >= pad.bounds.max.x ||
            pad.bounds.min.y >= pad.bounds.max.y ||
            pad.velocity <= Fix32::zero()) {
            return fail("map jump pad is invalid");
        }
    }
    if (objective.has_value() &&
        (objective->min.x >= objective->max.x ||
         objective->min.y >= objective->max.y)) {
        return fail("map objective is invalid");
    }
    if (drop_spawns.size() > kMapMaxDropSpawns ||
        drop_depots.size() > kMapMaxDropDepots) {
        return fail("map has too many drop definitions");
    }
    for (const DropSpawn& spawn : drop_spawns) {
        if (spawn.drop_id.empty() ||
            spawn.drop_id.size() > kMapMaxDropIdLength) {
            return fail("map drop spawn id is invalid");
        }
        if (!bounds.contains(spawn.position)) {
            return fail("map drop spawn is outside bounds");
        }
    }
    for (const DropDepot& depot : drop_depots) {
        if (depot.team != kDropDepotNeutral && depot.team > 1) {
            return fail("map drop depot team is invalid");
        }
        if (depot.bounds.min.x >= depot.bounds.max.x ||
            depot.bounds.min.y >= depot.bounds.max.y) {
            return fail("map drop depot bounds are invalid");
        }
        if (!bounds.contains(depot.bounds.min) ||
            !bounds.contains(depot.bounds.max)) {
            return fail("map drop depot is outside bounds");
        }
        if (depot.deliver_ticks == 0 ||
            depot.deliver_ticks > kMapMaxDeliverTicks) {
            return fail("map drop depot deliver_ticks is invalid");
        }
        if (depot.accepted_drops.size() > kMapMaxDepotDrops) {
            return fail("map drop depot has too many accepted drops");
        }
        for (const std::string& drop_id : depot.accepted_drops) {
            if (drop_id.empty() ||
                drop_id.size() > kMapMaxDropIdLength) {
                return fail("map drop depot accepted drop id is invalid");
            }
        }
    }
    for (const Vec2& spawn : spawns) {
        if (!bounds.contains(spawn)) {
            return fail("map spawn is outside bounds");
        }
    }
    if (error) {
        error->clear();
    }
    return true;
}

bool load_map_json(std::string_view json, Map& map, std::string* error) {
    Map result;
    Parser parser(json);
    if (!parser.parse(result, error)) {
        return false;
    }
    map = std::move(result);
    return true;
}

std::vector<std::uint8_t> encode_map_bin(const Map& map) {
    if (!map.valid() || map.id.size() > 1024U) {
        return {};
    }

    ByteWriter writer;
    writer.u32(kMapBinaryMagic);
    writer.u16(kMapBinaryVersion);
    writer.u16(0);
    writer.u16(static_cast<std::uint16_t>(map.id.size()));
    writer.bytes(map.id.data(), map.id.size());

    const auto write_vec = [&writer](Vec2 value) {
        writer.u32(static_cast<std::uint32_t>(value.x.raw()));
        writer.u32(static_cast<std::uint32_t>(value.y.raw()));
    };
    const auto write_box = [&write_vec](const AABB& box) {
        write_vec(box.min);
        write_vec(box.max);
    };

    write_box(map.bounds);
    writer.u8(map.objective.has_value() ? 1 : 0);
    if (map.objective.has_value()) {
        write_box(*map.objective);
    }
    writer.u32(static_cast<std::uint32_t>(map.solids.size()));
    for (const AABB& box : map.solids) {
        write_box(box);
    }
    writer.u32(static_cast<std::uint32_t>(map.one_way_platforms.size()));
    for (std::size_t index = 0; index < map.one_way_platforms.size(); ++index) {
        write_box(map.one_way_platforms[index]);
        writer.u8(index < map.platform_hatchway.size()
                      ? map.platform_hatchway[index]
                      : 1);
    }
    writer.u32(static_cast<std::uint32_t>(map.jump_pads.size()));
    for (const JumpPad& pad : map.jump_pads) {
        write_box(pad.bounds);
        writer.u32(static_cast<std::uint32_t>(pad.velocity.raw()));
    }
    writer.u32(static_cast<std::uint32_t>(map.spawns.size()));
    for (Vec2 spawn : map.spawns) {
        write_vec(spawn);
    }
    writer.u32(static_cast<std::uint32_t>(map.drop_spawns.size()));
    for (const DropSpawn& spawn : map.drop_spawns) {
        write_vec(spawn.position);
        writer.u16(static_cast<std::uint16_t>(spawn.drop_id.size()));
        writer.bytes(spawn.drop_id.data(), spawn.drop_id.size());
    }
    writer.u32(static_cast<std::uint32_t>(map.drop_depots.size()));
    for (const DropDepot& depot : map.drop_depots) {
        writer.u8(depot.team);
        write_box(depot.bounds);
        writer.u16(depot.deliver_ticks);
        writer.u16(static_cast<std::uint16_t>(depot.accepted_drops.size()));
        for (const std::string& drop_id : depot.accepted_drops) {
            writer.u16(static_cast<std::uint16_t>(drop_id.size()));
            writer.bytes(drop_id.data(), drop_id.size());
        }
    }
    return writer.data();
}

bool decode_map_bin(
    const std::uint8_t* data,
    std::size_t size,
    Map& map,
    std::string* error) {
    const auto fail = [&](const char* message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    if (data == nullptr || size == 0) {
        return fail("empty map binary");
    }

    ByteReader reader(data, size);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t reserved = 0;
    std::uint16_t id_size = 0;
    if (!reader.u32(magic) || !reader.u16(version) ||
        !reader.u16(reserved) || !reader.u16(id_size)) {
        return fail("truncated map binary header");
    }
    if (version < 2 || version > kMapBinaryVersion) {
        return fail("unsupported map binary version");
    }
    if (magic != kMapBinaryMagic) {
        return fail("map binary magic mismatch");
    }
    if (reserved != 0 || id_size == 0 || id_size > 1024U ||
        reader.remaining() < id_size) {
        return fail("invalid map binary id");
    }

    Map decoded;
    decoded.id.resize(id_size);
    if (!reader.bytes(decoded.id.data(), decoded.id.size())) {
        return fail("truncated map binary id");
    }

    const auto read_vec = [&reader](Vec2& value) {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        if (!reader.u32(x) || !reader.u32(y)) {
            return false;
        }
        value = Vec2{
            Fix32::from_raw(static_cast<std::int32_t>(x)),
            Fix32::from_raw(static_cast<std::int32_t>(y))};
        return true;
    };
    const auto read_box = [&read_vec](AABB& box) {
        return read_vec(box.min) && read_vec(box.max);
    };

    if (!read_box(decoded.bounds)) {
        return fail("truncated map binary bounds");
    }
    std::uint8_t has_objective = 0;
    if (!reader.u8(has_objective) || has_objective > 1) {
        return fail("invalid map binary objective");
    }
    if (has_objective != 0) {
        AABB objective;
        if (!read_box(objective)) {
            return fail("truncated map binary objective");
        }
        decoded.objective = objective;
    }

    std::uint32_t solid_count = 0;
    if (!reader.u32(solid_count) || solid_count > kMapMaxShapes) {
        return fail("invalid map binary solid count");
    }
    decoded.solids.resize(solid_count);
    for (AABB& box : decoded.solids) {
        if (!read_box(box)) {
            return fail("truncated map binary solid");
        }
    }

    std::uint32_t platform_count = 0;
    if (!reader.u32(platform_count) || platform_count > kMapMaxShapes) {
        return fail("invalid map binary platform count");
    }
    decoded.one_way_platforms.resize(platform_count);
    decoded.platform_hatchway.assign(platform_count, 1);
    for (std::size_t index = 0; index < platform_count; ++index) {
        if (!read_box(decoded.one_way_platforms[index])) {
            return fail("truncated map binary platform");
        }
        if (version >= 4) {
            std::uint8_t hatchway = 0;
            if (!reader.u8(hatchway) || hatchway > 1) {
                return fail("invalid map binary platform hatchway");
            }
            decoded.platform_hatchway[index] = hatchway;
        }
    }

    if (version >= 3) {
        std::uint32_t jump_pad_count = 0;
        if (!reader.u32(jump_pad_count) ||
            jump_pad_count > kMapMaxShapes) {
            return fail("invalid map binary jump pad count");
        }
        decoded.jump_pads.resize(jump_pad_count);
        for (JumpPad& pad : decoded.jump_pads) {
            std::uint32_t velocity = 0;
            if (!read_box(pad.bounds) || !reader.u32(velocity)) {
                return fail("truncated map binary jump pad");
            }
            pad.velocity =
                Fix32::from_raw(static_cast<std::int32_t>(velocity));
        }
    }

    std::uint32_t spawn_count = 0;
    if (!reader.u32(spawn_count) ||
        spawn_count != static_cast<std::uint32_t>(kMapSpawnCount)) {
        return fail("invalid map binary spawn count");
    }
    for (Vec2& spawn : decoded.spawns) {
        if (!read_vec(spawn)) {
            return fail("truncated map binary spawn");
        }
    }

    if (version >= 5) {
        std::uint32_t drop_spawn_count = 0;
        if (!reader.u32(drop_spawn_count) ||
            drop_spawn_count > kMapMaxDropSpawns) {
            return fail("invalid map binary drop spawn count");
        }
        decoded.drop_spawns.resize(drop_spawn_count);
        for (DropSpawn& spawn : decoded.drop_spawns) {
            std::uint16_t drop_id_size = 0;
            if (!read_vec(spawn.position) || !reader.u16(drop_id_size) ||
                drop_id_size == 0 || drop_id_size > kMapMaxDropIdLength ||
                reader.remaining() < drop_id_size) {
                return fail("truncated map binary drop spawn");
            }
            spawn.drop_id.resize(drop_id_size);
            if (!reader.bytes(spawn.drop_id.data(), spawn.drop_id.size())) {
                return fail("truncated map binary drop spawn id");
            }
        }

        std::uint32_t depot_count = 0;
        if (!reader.u32(depot_count) ||
            depot_count > kMapMaxDropDepots) {
            return fail("invalid map binary drop depot count");
        }
        decoded.drop_depots.resize(depot_count);
        for (DropDepot& depot : decoded.drop_depots) {
            std::uint16_t accepted_count = 0;
            if (!reader.u8(depot.team) || !read_box(depot.bounds) ||
                !reader.u16(depot.deliver_ticks) ||
                !reader.u16(accepted_count) ||
                accepted_count > kMapMaxDepotDrops) {
                return fail("truncated map binary drop depot");
            }
            depot.accepted_drops.resize(accepted_count);
            for (std::string& drop_id : depot.accepted_drops) {
                std::uint16_t drop_id_size = 0;
                if (!reader.u16(drop_id_size) || drop_id_size == 0 ||
                    drop_id_size > kMapMaxDropIdLength ||
                    reader.remaining() < drop_id_size) {
                    return fail("truncated map binary depot drop id");
                }
                drop_id.resize(drop_id_size);
                if (!reader.bytes(drop_id.data(), drop_id.size())) {
                    return fail("truncated map binary depot drop id");
                }
            }
        }
    }

    if (reader.remaining() != 0) {
        return fail("trailing map binary data");
    }

    std::string validation_error;
    if (!decoded.valid(&validation_error)) {
        if (error) {
            *error = validation_error;
        }
        return false;
    }
    map = std::move(decoded);
    if (error) {
        error->clear();
    }
    return true;
}

bool load_map_file(
    const std::filesystem::path& path,
    Map& map,
    std::string* error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        if (error) {
            *error = "failed to open map: " + path.string();
        }
        return false;
    }
    const std::streampos end = input.tellg();
    if (end <= 0) {
        if (error) {
            *error = "map file is empty: " + path.string();
        }
        return false;
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!input.read(
            reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(data.size()))) {
        if (error) {
            *error = "failed to read map: " + path.string();
        }
        return false;
    }

    std::string extension = path.extension().string();
    for (char& character : extension) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    if (extension == ".bin") {
        return decode_map_bin(data.data(), data.size(), map, error);
    }
    return load_map_json(
        std::string_view(
            reinterpret_cast<const char*>(data.data()),
            data.size()),
        map,
        error);
}

std::uint64_t map_fingerprint(const Map& map) {
    ByteWriter writer;
    writer.u32(static_cast<std::uint32_t>(map.id.size()));
    writer.bytes(map.id.data(), map.id.size());
    auto write_vec = [&writer](Vec2 value) {
        writer.u32(static_cast<std::uint32_t>(value.x.raw()));
        writer.u32(static_cast<std::uint32_t>(value.y.raw()));
    };
    auto write_box = [&](const AABB& box) {
        write_vec(box.min);
        write_vec(box.max);
    };
    write_box(map.bounds);
    writer.u8(map.objective.has_value() ? 1 : 0);
    if (map.objective.has_value()) {
        write_box(*map.objective);
    }
    writer.u32(static_cast<std::uint32_t>(map.solids.size()));
    for (const AABB& box : map.solids) {
        write_box(box);
    }
    writer.u32(static_cast<std::uint32_t>(map.one_way_platforms.size()));
    for (std::size_t index = 0; index < map.one_way_platforms.size(); ++index) {
        write_box(map.one_way_platforms[index]);
        writer.u8(index < map.platform_hatchway.size()
                      ? map.platform_hatchway[index]
                      : 1);
    }
    writer.u32(static_cast<std::uint32_t>(map.jump_pads.size()));
    for (const JumpPad& pad : map.jump_pads) {
        write_box(pad.bounds);
        writer.u32(static_cast<std::uint32_t>(pad.velocity.raw()));
    }
    writer.u32(static_cast<std::uint32_t>(map.spawns.size()));
    for (Vec2 spawn : map.spawns) {
        write_vec(spawn);
    }
    writer.u32(static_cast<std::uint32_t>(map.drop_spawns.size()));
    for (const DropSpawn& spawn : map.drop_spawns) {
        write_vec(spawn.position);
        writer.u16(static_cast<std::uint16_t>(spawn.drop_id.size()));
        writer.bytes(spawn.drop_id.data(), spawn.drop_id.size());
    }
    writer.u32(static_cast<std::uint32_t>(map.drop_depots.size()));
    for (const DropDepot& depot : map.drop_depots) {
        writer.u8(depot.team);
        write_box(depot.bounds);
        writer.u16(depot.deliver_ticks);
        writer.u16(static_cast<std::uint16_t>(depot.accepted_drops.size()));
        for (const std::string& drop_id : depot.accepted_drops) {
            writer.u16(static_cast<std::uint16_t>(drop_id.size()));
            writer.bytes(drop_id.data(), drop_id.size());
        }
    }
    return hash64_bytes(writer.data().data(), writer.data().size());
}

} // namespace nsync::core
