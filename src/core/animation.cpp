#include "core/animation.h"

#include "core/input_command.h"
#include "core/json.h"

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
    if (std::floor(number) != number ||
        number < 0.0 ||
        number > static_cast<double>(0xFFFFFFFFU)) {
        return false;
    }
    result = static_cast<std::uint32_t>(number);
    return true;
}

bool read_bool(const JsonValue* value, bool& result) {
    if (value == nullptr || value->type != JsonValue::Type::Bool) {
        return false;
    }
    result = value->bool_value;
    return true;
}

} // namespace

bool AnimationSet::load_json(std::string_view json, std::string* error) {
    JsonValue root;
    std::string parse_error;
    if (!parse_json(json, root, &parse_error)) {
        if (error) {
            *error = "animation json: " + parse_error;
        }
        return false;
    }
    if (root.type != JsonValue::Type::Object) {
        if (error) {
            *error = "animation data must be a json object";
        }
        return false;
    }

    std::uint32_t version = 0;
    if (!read_u32(root.find("version"), version) ||
        version != kAnimationVersion) {
        if (error) {
            *error = "unsupported animation version";
        }
        return false;
    }

    const JsonValue* clips_value = root.find("clips");
    if (clips_value == nullptr ||
        clips_value->type != JsonValue::Type::Array ||
        clips_value->array.empty() ||
        clips_value->array.size() > kMaxAnimationClips) {
        if (error) {
            *error = "animation data requires a non-empty clips array";
        }
        return false;
    }

    AnimationSet next;
    next.clips.reserve(clips_value->array.size());
    for (const JsonValue& clip_value : clips_value->array) {
        if (clip_value.type != JsonValue::Type::Object) {
            if (error) {
                *error = "animation clip must be an object";
            }
            return false;
        }
        const JsonValue* name_value = clip_value.find("name");
        if (name_value == nullptr ||
            name_value->type != JsonValue::Type::String ||
            name_value->string_value.empty() ||
            name_value->string_value.size() > 128) {
            if (error) {
                *error = "animation clip requires a valid name";
            }
            return false;
        }

        AnimationClip clip;
        clip.name = name_value->string_value;

        std::uint32_t fps = 0;
        if (!read_u32(clip_value.find("fps"), fps) ||
            fps == 0 ||
            fps > 60) {
            if (error) {
                *error = "animation clip " + clip.name +
                         " requires fps in 1..60";
            }
            return false;
        }
        clip.fps = static_cast<std::uint8_t>(fps);

        if (clip_value.find("loop") != nullptr &&
            !read_bool(clip_value.find("loop"), clip.loop)) {
            if (error) {
                *error = "animation clip " + clip.name +
                         " has an invalid loop flag";
            }
            return false;
        }

        const JsonValue* frames_value = clip_value.find("frames");
        if (frames_value == nullptr ||
            frames_value->type != JsonValue::Type::Array ||
            frames_value->array.empty() ||
            frames_value->array.size() > kMaxAnimationFrames) {
            if (error) {
                *error = "animation clip " + clip.name +
                         " requires a non-empty frames array";
            }
            return false;
        }

        clip.frames.reserve(frames_value->array.size());
        for (const JsonValue& frame_value : frames_value->array) {
            if (frame_value.type != JsonValue::Type::String ||
                frame_value.string_value.empty() ||
                frame_value.string_value.size() > 128) {
                if (error) {
                    *error = "animation clip " + clip.name +
                             " has an invalid frame name";
                }
                return false;
            }
            clip.frames.push_back(frame_value.string_value);
        }
        if (next.find(clip.name) != nullptr) {
            if (error) {
                *error = "duplicate animation clip: " + clip.name;
            }
            return false;
        }
        next.clips.push_back(std::move(clip));
    }

    std::string validation_error;
    if (!next.valid(&validation_error)) {
        if (error) {
            *error = validation_error;
        }
        return false;
    }

    *this = std::move(next);
    if (error) {
        error->clear();
    }
    return true;
}

bool AnimationSet::load_file(
    const std::filesystem::path& path,
    std::string* error) {
    std::string text;
    if (!read_text_file(path, text)) {
        if (error) {
            *error = "failed to read animation data: " + path.string();
        }
        return false;
    }
    return load_json(text, error);
}

bool AnimationSet::valid(std::string* error) const {
    if (clips.empty() || clips.size() > kMaxAnimationClips) {
        if (error) {
            *error = "invalid animation clip count";
        }
        return false;
    }

    for (std::size_t i = 0; i < clips.size(); ++i) {
        const AnimationClip& clip = clips[i];
        if (clip.name.empty() ||
            clip.fps == 0 ||
            clip.fps > 60 ||
            clip.frames.empty() ||
            clip.frames.size() > kMaxAnimationFrames) {
            if (error) {
                *error = "invalid animation clip: " + clip.name;
            }
            return false;
        }
        for (const std::string& frame : clip.frames) {
            if (frame.empty() || frame.size() > 128) {
                if (error) {
                    *error = "invalid frame in animation clip: " + clip.name;
                }
                return false;
            }
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (clips[j].name == clip.name) {
                if (error) {
                    *error = "duplicate animation clip: " + clip.name;
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

const AnimationClip* AnimationSet::find(std::string_view name) const {
    for (const AnimationClip& clip : clips) {
        if (clip.name == name) {
            return &clip;
        }
    }
    return nullptr;
}

const char* animation_state_name(AnimationState state) {
    switch (state) {
        case AnimationState::Idle: return "idle";
        case AnimationState::Run: return "run";
        case AnimationState::Jump: return "jump";
        case AnimationState::Fall: return "fall";
        case AnimationState::Shoot: return "shoot";
        case AnimationState::Hurt: return "hurt";
        case AnimationState::Death: return "death";
    }
    return "idle";
}

std::string animation_clip_name(
    std::string_view hero_id,
    AnimationState state) {
    return std::string(hero_id) + "." + animation_state_name(state);
}

AnimationState select_animation_state(
    const Player& player,
    bool recently_hurt) {
    if (player.alive == 0) {
        return AnimationState::Death;
    }
    if (recently_hurt) {
        return AnimationState::Hurt;
    }
    if (button_down(player.buttons, kButtonFire)) {
        return AnimationState::Shoot;
    }
    if (player.grounded == 0) {
        return player.velocity.y < Fix32::zero()
            ? AnimationState::Jump
            : AnimationState::Fall;
    }
    if (player.velocity.x > Fix32::from_ratio(1, 100) ||
        player.velocity.x < -Fix32::from_ratio(1, 100)) {
        return AnimationState::Run;
    }
    return AnimationState::Idle;
}

} // namespace nsync::core
