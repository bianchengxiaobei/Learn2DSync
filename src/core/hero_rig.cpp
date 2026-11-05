#include "core/hero_rig.h"

#include "core/json.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <utility>

namespace nsync::core {
namespace {

bool read_text_file(
    const std::filesystem::path& path,
    std::string& text) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    text.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    return true;
}

bool read_u32(const JsonValue* value, std::uint32_t& result) {
    if (value == nullptr || value->type != JsonValue::Type::Number) {
        return false;
    }
    const double number = value->number_value;
    if (!std::isfinite(number) ||
        std::floor(number) != number ||
        number < 0.0 ||
        number > static_cast<double>(0xFFFFFFFFU)) {
        return false;
    }
    result = static_cast<std::uint32_t>(number);
    return true;
}

bool read_int(const JsonValue* value, int& result) {
    if (value == nullptr || value->type != JsonValue::Type::Number) {
        return false;
    }
    const double number = value->number_value;
    if (!std::isfinite(number) ||
        std::floor(number) != number ||
        number < -32768.0 ||
        number > 32767.0) {
        return false;
    }
    result = static_cast<int>(number);
    return true;
}

bool read_float(const JsonValue* value, float& result) {
    if (value == nullptr || value->type != JsonValue::Type::Number) {
        return false;
    }
    const double number = value->number_value;
    if (!std::isfinite(number) ||
        number < -100000.0 ||
        number > 100000.0) {
        return false;
    }
    result = static_cast<float>(number);
    return true;
}

bool read_string(
    const JsonValue* value,
    std::string& result,
    std::size_t max_length) {
    if (value == nullptr ||
        value->type != JsonValue::Type::String ||
        value->string_value.empty() ||
        value->string_value.size() > max_length) {
        return false;
    }
    result = value->string_value;
    return true;
}

bool read_bool(const JsonValue* value, bool& result) {
    if (value == nullptr || value->type != JsonValue::Type::Bool) {
        return false;
    }
    result = value->bool_value;
    return true;
}

bool read_point(const JsonValue* value, HeroPoint& result) {
    if (value == nullptr ||
        value->type != JsonValue::Type::Array ||
        value->array.size() != 2) {
        return false;
    }
    return read_float(&value->array[0], result.x) &&
        read_float(&value->array[1], result.y);
}

bool read_points4(
    const JsonValue* value,
    std::array<HeroPoint, 4>& result) {
    if (value == nullptr ||
        value->type != JsonValue::Type::Array ||
        value->array.size() != 4) {
        return false;
    }
    for (std::size_t i = 0; i < 4; ++i) {
        if (!read_point(&value->array[i], result[i])) {
            return false;
        }
    }
    return true;
}

bool finite(float value) {
    return std::isfinite(value);
}

bool point_finite(const HeroPoint& point) {
    return finite(point.x) && finite(point.y);
}

bool quad_finite(const std::array<HeroPoint, 4>& quad) {
    return point_finite(quad[0]) &&
        point_finite(quad[1]) &&
        point_finite(quad[2]) &&
        point_finite(quad[3]);
}

bool uv_valid(const std::array<HeroPoint, 4>& uv) {
    for (const HeroPoint& point : uv) {
        if (!point_finite(point) ||
            point.x < -0.01f || point.x > 1.01f ||
            point.y < -0.01f || point.y > 1.01f) {
            return false;
        }
    }
    return true;
}

bool parse_part(
    const JsonValue& value,
    HeroPart& part,
    std::string& error) {
    if (value.type != JsonValue::Type::Object) {
        error = "hero part must be an object";
        return false;
    }
    if (!read_string(value.find("role"), part.role, 32) ||
        !read_string(value.find("sprite"), part.sprite, 128) ||
        !read_int(value.find("z"), part.z) ||
        !read_point(value.find("pivot"), part.pivot) ||
        !read_points4(value.find("quad"), part.quad) ||
        !read_points4(value.find("uv"), part.uv)) {
        error = "hero part requires role, sprite, z, pivot, quad, uv";
        return false;
    }
    if (!point_finite(part.pivot) ||
        !quad_finite(part.quad) ||
        !uv_valid(part.uv)) {
        error = "hero part contains invalid coordinates: " + part.role;
        return false;
    }
    return true;
}

bool parse_weapon(
    const JsonValue& value,
    HeroWeapon& weapon,
    std::string& error) {
    if (value.type != JsonValue::Type::Object) {
        error = "hero weapon must be an object";
        return false;
    }
    if (!read_string(value.find("id"), weapon.id, 32) ||
        !read_string(value.find("sprite"), weapon.sprite, 128) ||
        !read_int(value.find("z"), weapon.z) ||
        !read_point(value.find("pivot"), weapon.pivot) ||
        !read_points4(value.find("quad"), weapon.quad) ||
        !read_points4(value.find("uv"), weapon.uv)) {
        error = "hero weapon requires id, sprite, z, pivot, quad, uv";
        return false;
    }
    if (!point_finite(weapon.pivot) ||
        !quad_finite(weapon.quad) ||
        !uv_valid(weapon.uv)) {
        error = "hero weapon contains invalid coordinates: " + weapon.id;
        return false;
    }
    return true;
}

bool parse_rig(
    const JsonValue& value,
    HeroRig& rig,
    std::string& error) {
    if (value.type != JsonValue::Type::Object) {
        error = "hero rig must be an object";
        return false;
    }
    if (!read_string(value.find("hero"), rig.hero, 32)) {
        error = "hero rig requires a hero id";
        return false;
    }
    if (const JsonValue* root_pivot = value.find("root_pivot");
        root_pivot != nullptr &&
        !read_point(root_pivot, rig.root_pivot)) {
        error = "hero rig has an invalid root_pivot: " + rig.hero;
        return false;
    }
    const JsonValue* parts_value = value.find("parts");
    if (parts_value == nullptr ||
        parts_value->type != JsonValue::Type::Array ||
        parts_value->array.empty() ||
        parts_value->array.size() > kMaxHeroParts) {
        error = "hero rig requires a non-empty parts array: " + rig.hero;
        return false;
    }
    rig.parts.reserve(parts_value->array.size());
    for (const JsonValue& part_value : parts_value->array) {
        HeroPart part;
        if (!parse_part(part_value, part, error)) {
            return false;
        }
        rig.parts.push_back(std::move(part));
    }
    std::sort(
        rig.parts.begin(),
        rig.parts.end(),
        [](const HeroPart& a, const HeroPart& b) {
            return a.z < b.z;
        });

    const JsonValue* weapons_value = value.find("weapons");
    if (weapons_value != nullptr) {
        if (weapons_value->type != JsonValue::Type::Array ||
            weapons_value->array.size() > kMaxHeroWeapons) {
            error = "hero rig has an invalid weapons array: " + rig.hero;
            return false;
        }
        rig.weapons.reserve(weapons_value->array.size());
        for (const JsonValue& weapon_value : weapons_value->array) {
            HeroWeapon weapon;
            if (!parse_weapon(weapon_value, weapon, error)) {
                return false;
            }
            if (std::any_of(
                    rig.weapons.begin(),
                    rig.weapons.end(),
                    [&](const HeroWeapon& existing) {
                        return existing.id == weapon.id;
                    })) {
                error = "duplicate hero weapon id: " + weapon.id;
                return false;
            }
            rig.weapons.push_back(std::move(weapon));
        }
    }
    return true;
}

bool parse_track(
    const JsonValue& value,
    HeroPoseTrack& track,
    std::string& error) {
    if (value.type != JsonValue::Type::Object) {
        error = "pose track must be an object";
        return false;
    }
    if (!read_string(value.find("role"), track.role, 32)) {
        error = "pose track requires a role";
        return false;
    }
    const JsonValue* fields[] = {
        value.find("offset_x"),
        value.find("offset_y"),
        value.find("sway_x"),
        value.find("sway_y"),
        value.find("rotation"),
        value.find("sway_rotation"),
        value.find("frequency"),
        value.find("phase"),
    };
    float* targets[] = {
        &track.offset_x,
        &track.offset_y,
        &track.sway_x,
        &track.sway_y,
        &track.rotation,
        &track.sway_rotation,
        &track.frequency,
        &track.phase,
    };
    for (std::size_t i = 0; i < 8; ++i) {
        if (fields[i] != nullptr && !read_float(fields[i], *targets[i])) {
            error = "pose track has an invalid numeric field: " + track.role;
            return false;
        }
    }
    return true;
}

bool parse_state(
    std::string name,
    const JsonValue& value,
    HeroPoseState& state,
    std::string& error) {
    if (value.type != JsonValue::Type::Object) {
        error = "pose state must be an object: " + name;
        return false;
    }
    state.name = std::move(name);
    if (!read_float(value.find("duration"), state.duration) ||
        state.duration <= 0.0f) {
        error = "pose state requires a positive duration: " + state.name;
        return false;
    }
    if (const JsonValue* loop = value.find("loop");
        loop != nullptr && !read_bool(loop, state.loop)) {
        error = "pose state has an invalid loop flag: " + state.name;
        return false;
    }
    const JsonValue* fields[] = {
        value.find("root_rotation_start"),
        value.find("root_rotation_end"),
        value.find("root_offset_x_start"),
        value.find("root_offset_x_end"),
        value.find("root_offset_y_start"),
        value.find("root_offset_y_end"),
        value.find("alpha_start"),
        value.find("alpha_end"),
    };
    float* targets[] = {
        &state.root_rotation_start,
        &state.root_rotation_end,
        &state.root_offset_x_start,
        &state.root_offset_x_end,
        &state.root_offset_y_start,
        &state.root_offset_y_end,
        &state.alpha_start,
        &state.alpha_end,
    };
    for (std::size_t i = 0; i < 8; ++i) {
        if (fields[i] != nullptr && !read_float(fields[i], *targets[i])) {
            error = "pose state has an invalid numeric field: " + state.name;
            return false;
        }
    }
    state.alpha_start = std::clamp(state.alpha_start, 0.0f, 1.0f);
    state.alpha_end = std::clamp(state.alpha_end, 0.0f, 1.0f);

    const JsonValue* tracks = value.find("tracks");
    if (tracks != nullptr) {
        if (tracks->type != JsonValue::Type::Array ||
            tracks->array.size() > kMaxHeroPoseTracks) {
            error = "pose state has an invalid tracks array: " + state.name;
            return false;
        }
        state.tracks.reserve(tracks->array.size());
        for (const JsonValue& track_value : tracks->array) {
            HeroPoseTrack track;
            if (!parse_track(track_value, track, error)) {
                return false;
            }
            state.tracks.push_back(std::move(track));
        }
    }
    return true;
}

} // namespace

bool HeroRigSet::load_json(std::string_view json, std::string* error) {
    JsonValue root;
    std::string parse_error;
    if (!parse_json(json, root, &parse_error)) {
        if (error) {
            *error = "hero rig json: " + parse_error;
        }
        return false;
    }
    if (root.type != JsonValue::Type::Object) {
        if (error) {
            *error = "hero rig must be a json object";
        }
        return false;
    }

    HeroRigSet next;
    std::uint32_t version = 0;
    if (!read_u32(root.find("version"), version) || version != kHeroRigVersion) {
        if (error) {
            *error = "unsupported hero rig version";
        }
        return false;
    }
    if (const JsonValue* atlas = root.find("atlas"); atlas != nullptr) {
        if (!read_string(atlas, next.atlas, 256)) {
            if (error) {
                *error = "hero rig has an invalid atlas path";
            }
            return false;
        }
    }
    if (const JsonValue* image = root.find("image"); image != nullptr) {
        if (!read_string(image, next.image, 256)) {
            if (error) {
                *error = "hero rig has an invalid image path";
            }
            return false;
        }
    }
    if (const JsonValue* height = root.find("height");
        height != nullptr &&
        (!read_float(height, next.height) ||
         next.height < 0.5f ||
         next.height > 10.0f)) {
        if (error) {
            *error = "hero rig height must be between 0.5 and 10";
        }
        return false;
    }

    const JsonValue* heroes = root.find("heroes");
    if (heroes == nullptr ||
        heroes->type != JsonValue::Type::Array ||
        heroes->array.empty() ||
        heroes->array.size() > kMaxHeroRigs) {
        if (error) {
            *error = "hero rig requires a non-empty heroes array";
        }
        return false;
    }
    next.heroes.reserve(heroes->array.size());
    for (const JsonValue& hero_value : heroes->array) {
        HeroRig rig;
        if (!parse_rig(hero_value, rig, parse_error)) {
            if (error) {
                *error = parse_error;
            }
            return false;
        }
        if (next.find(rig.hero) != nullptr) {
            if (error) {
                *error = "duplicate hero rig id: " + rig.hero;
            }
            return false;
        }
        next.heroes.push_back(std::move(rig));
    }

    if (!next.valid(&parse_error)) {
        if (error) {
            *error = parse_error;
        }
        return false;
    }

    if (error) {
        error->clear();
    }
    *this = std::move(next);
    return true;
}

bool HeroRigSet::load_file(
    const std::filesystem::path& path,
    std::string* error) {
    std::string text;
    if (!read_text_file(path, text)) {
        if (error) {
            *error = "failed to read hero rig: " + path.string();
        }
        return false;
    }
    return load_json(text, error);
}

bool HeroRigSet::valid(std::string* error) const {
    if (atlas.empty() || image.empty() ||
        heroes.empty() ||
        heroes.size() > kMaxHeroRigs ||
        height < 0.5f ||
        height > 10.0f) {
        if (error) {
            *error = "invalid hero rig header";
        }
        return false;
    }
    for (const HeroRig& rig : heroes) {
        if (rig.hero.empty() ||
            rig.parts.empty() ||
            rig.parts.size() > kMaxHeroParts ||
            !point_finite(rig.root_pivot)) {
            if (error) {
                *error = "invalid hero rig: " + rig.hero;
            }
            return false;
        }
        for (const HeroPart& part : rig.parts) {
            if (part.role.empty() ||
                part.sprite.empty() ||
                !point_finite(part.pivot) ||
                !quad_finite(part.quad) ||
                !uv_valid(part.uv)) {
                if (error) {
                    *error = "invalid hero part: " + part.sprite;
                }
                return false;
            }
        }
        for (const HeroWeapon& weapon : rig.weapons) {
            if (weapon.id.empty() ||
                weapon.sprite.empty() ||
                !point_finite(weapon.pivot) ||
                !quad_finite(weapon.quad) ||
                !uv_valid(weapon.uv)) {
                if (error) {
                    *error = "invalid hero weapon: " + weapon.id;
                }
                return false;
            }
        }
    }
    if (error) {
        error->clear();
    }
    return true;
}

const HeroRig* HeroRigSet::find(std::string_view hero_id) const {
    for (const HeroRig& rig : heroes) {
        if (rig.hero == hero_id) {
            return &rig;
        }
    }
    return nullptr;
}

bool HeroPoseSet::load_json(std::string_view json, std::string* error) {
    JsonValue root;
    std::string parse_error;
    if (!parse_json(json, root, &parse_error)) {
        if (error) {
            *error = "hero pose json: " + parse_error;
        }
        return false;
    }
    if (root.type != JsonValue::Type::Object) {
        if (error) {
            *error = "hero pose must be a json object";
        }
        return false;
    }

    HeroPoseSet next;
    std::uint32_t version = 0;
    if (!read_u32(root.find("version"), version) || version != kHeroPoseVersion) {
        if (error) {
            *error = "unsupported hero pose version";
        }
        return false;
    }

    const JsonValue* states = root.find("states");
    if (states == nullptr ||
        states->type != JsonValue::Type::Object ||
        states->object.empty() ||
        states->object.size() > kMaxHeroPoseStates) {
        if (error) {
            *error = "hero pose requires a non-empty states object";
        }
        return false;
    }
    next.states.reserve(states->object.size());
    for (const auto& [name, state_value] : states->object) {
        HeroPoseState state;
        if (!parse_state(name, state_value, state, parse_error)) {
            if (error) {
                *error = parse_error;
            }
            return false;
        }
        if (next.find(state.name) != nullptr) {
            if (error) {
                *error = "duplicate pose state: " + state.name;
            }
            return false;
        }
        next.states.push_back(std::move(state));
    }

    if (const JsonValue* recoil = root.find("recoil"); recoil != nullptr) {
        if (recoil->type != JsonValue::Type::Object ||
            !read_float(recoil->find("offset_x"), next.recoil.offset_x) ||
            !read_float(recoil->find("offset_y"), next.recoil.offset_y) ||
            !read_float(recoil->find("rotation_deg"), next.recoil.rotation_deg) ||
            !read_float(recoil->find("decay"), next.recoil.decay) ||
            next.recoil.decay < 0.0f) {
            if (error) {
                *error = "hero pose recoil is invalid";
            }
            return false;
        }
    }

    if (!next.valid(&parse_error)) {
        if (error) {
            *error = parse_error;
        }
        return false;
    }

    if (error) {
        error->clear();
    }
    *this = std::move(next);
    return true;
}

bool HeroPoseSet::load_file(
    const std::filesystem::path& path,
    std::string* error) {
    std::string text;
    if (!read_text_file(path, text)) {
        if (error) {
            *error = "failed to read hero pose: " + path.string();
        }
        return false;
    }
    return load_json(text, error);
}

bool HeroPoseSet::valid(std::string* error) const {
    if (states.empty() || states.size() > kMaxHeroPoseStates) {
        if (error) {
            *error = "invalid hero pose state count";
        }
        return false;
    }
    for (const HeroPoseState& state : states) {
        if (state.name.empty() ||
            state.duration <= 0.0f ||
            state.tracks.size() > kMaxHeroPoseTracks) {
            if (error) {
                *error = "invalid pose state: " + state.name;
            }
            return false;
        }
        for (const HeroPoseTrack& track : state.tracks) {
            if (track.role.empty() ||
                !finite(track.offset_x) ||
                !finite(track.offset_y) ||
                !finite(track.sway_x) ||
                !finite(track.sway_y) ||
                !finite(track.rotation) ||
                !finite(track.sway_rotation) ||
                !finite(track.frequency) ||
                !finite(track.phase)) {
                if (error) {
                    *error = "invalid pose track: " + track.role;
                }
                return false;
            }
        }
    }
    if (find("idle") == nullptr) {
        if (error) {
            *error = "hero pose has no idle state";
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

const HeroPoseState* HeroPoseSet::find(std::string_view name) const {
    for (const HeroPoseState& state : states) {
        if (state.name == name) {
            return &state;
        }
    }
    return nullptr;
}

} // namespace nsync::core
