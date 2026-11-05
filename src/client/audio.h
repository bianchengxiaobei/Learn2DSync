#pragma once

#include "core/gadgets.h"

#include <memory>
#include <string_view>

namespace nsync::shared {
class ResourceManager;
}

namespace nsync::client {

enum class GadgetSoundKind {
    Activate,
    Impact,
    Explode,
};

enum class AudioCue {
    UiClick,
    UiHover,
    WeaponSwitch,
    Reload,
    ShotRifle,
    ShotShotgun,
    ShotRocket,
    ShotGrenade,
    ShotSmg,
    ShotBurst,
    ProjectileImpact,
    Hit,
    ShieldHit,
    Explosion,
    Footstep,
    Death,
    Respawn,
    Ability,
    CrystalPickup,
    CrystalDeliver,
    MatchEnd,
};

[[nodiscard]] AudioCue weapon_shot_cue(std::string_view weapon_id);

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool initialize(
        bool enabled,
        float master_volume,
        const shared::ResourceManager* resources = nullptr);
    bool set_enabled(bool enabled);
    void shutdown();

    void set_master_volume(float master_volume);
    [[nodiscard]] bool available() const { return available_; }

    void play(AudioCue cue, float gain = 1.0f);
    void play_at(AudioCue cue, float x, float y, float gain = 1.0f);
    void play_gadget(
        core::GadgetType gadget,
        GadgetSoundKind kind,
        float x,
        float y,
        float gain = 1.0f);
    void set_listener(float x, float y);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool available_ = false;
};

} // namespace nsync::client
