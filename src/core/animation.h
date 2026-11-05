#pragma once

#include "core/sim_types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nsync::core {

inline constexpr std::uint32_t kAnimationVersion = 1;
inline constexpr std::size_t kMaxAnimationClips = 1024;
inline constexpr std::size_t kMaxAnimationFrames = 256;

enum class AnimationState : std::uint8_t {
    Idle = 0,
    Run,
    Jump,
    Fall,
    Shoot,
    Hurt,
    Death,
};

struct AnimationClip {
    std::string name;
    std::uint8_t fps = 8;
    bool loop = true;
    std::vector<std::string> frames;
};

struct AnimationSet {
    std::vector<AnimationClip> clips;

    bool load_json(std::string_view json, std::string* error = nullptr);
    bool load_file(
        const std::filesystem::path& path,
        std::string* error = nullptr);
    [[nodiscard]] bool valid(std::string* error = nullptr) const;
    [[nodiscard]] const AnimationClip* find(std::string_view name) const;
};

[[nodiscard]] const char* animation_state_name(AnimationState state);
[[nodiscard]] std::string animation_clip_name(
    std::string_view hero_id,
    AnimationState state);
[[nodiscard]] AnimationState select_animation_state(
    const Player& player,
    bool recently_hurt);

} // namespace nsync::core
