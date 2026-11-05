#include "client/audio.h"

#include "shared/log.h"
#include "shared/resource.h"

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nsync::client {
namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr std::size_t kMaxVoices = 32;
constexpr std::size_t kQueueCapacity = 256;
constexpr std::size_t kDrainCapacity = 32;
constexpr float kSoundDistance = 17.0f;
constexpr float kPi = 3.14159265358979323846f;

enum class ClipId : std::size_t {
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
    Count,
};

constexpr std::size_t kClipCount = static_cast<std::size_t>(ClipId::Count);
constexpr std::size_t kCueCount = 32;

struct Clip {
    std::vector<float> samples;
    float volume = 1.0f;
};

struct PendingSound {
    AudioCue cue = AudioCue::UiClick;
    const Clip* clip = nullptr;
    int group = -1;
    float left = 0.0f;
    float right = 0.0f;
    float pitch = 1.0f;
};

struct Voice {
    const Clip* clip = nullptr;
    AudioCue cue = AudioCue::UiClick;
    int group = -1;
    float cursor = 0.0f;
    float step = 1.0f;
    float left = 0.0f;
    float right = 0.0f;
    bool active = false;
};

std::uint32_t next_random(std::uint32_t& state) {
    state = state * 1664525U + 1013904223U;
    return state;
}

float next_noise(std::uint32_t& state) {
    return static_cast<float>(next_random(state) >> 8) /
               static_cast<float>(1U << 24U) *
               2.0f -
           1.0f;
}

void apply_fade(std::vector<float>& samples, float seconds) {
    const std::size_t fade = std::min(
        samples.size() / 2,
        static_cast<std::size_t>(seconds * kSampleRate));
    if (fade == 0) {
        return;
    }
    for (std::size_t i = 0; i < fade; ++i) {
        const float gain = static_cast<float>(i) /
            static_cast<float>(fade);
        samples[i] *= gain;
        samples[samples.size() - 1 - i] *= gain;
    }
}

void normalize(std::vector<float>& samples) {
    float peak = 0.0f;
    for (float sample : samples) {
        peak = std::max(peak, std::abs(sample));
    }
    if (peak < 0.0001f) {
        return;
    }
    const float gain = 0.88f / peak;
    for (float& sample : samples) {
        sample *= gain;
    }
}

void add_click(
    std::vector<float>& samples,
    std::uint32_t& rng,
    float start_seconds,
    float frequency,
    float amplitude,
    float decay) {
    const std::size_t start = static_cast<std::size_t>(
        std::max(0.0f, start_seconds) * kSampleRate);
    if (start >= samples.size()) {
        return;
    }
    const std::size_t length = std::min(
        samples.size() - start,
        static_cast<std::size_t>(0.06f * kSampleRate));
    for (std::size_t i = 0; i < length; ++i) {
        const float t = static_cast<float>(i) /
            static_cast<float>(kSampleRate);
        const float noise = next_noise(rng);
        const float tone = std::sin(2.0f * kPi * frequency * t);
        samples[start + i] +=
            (noise * 0.55f + tone * 0.45f) *
            std::exp(-t * decay) * amplitude;
    }
}

std::vector<float> make_shot(
    std::uint32_t& rng,
    int variant,
    float duration,
    float base_frequency,
    float brightness,
    float decay) {
    const int count = std::max(1, static_cast<int>(duration * kSampleRate));
    std::vector<float> samples(static_cast<std::size_t>(count), 0.0f);
    float low = 0.0f;
    float phase = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) /
            static_cast<float>(kSampleRate);
        const float noise = next_noise(rng);
        low += (noise - low) * 0.2f;
        const float frequency = base_frequency *
            (1.0f + 0.05f * static_cast<float>(variant)) *
            (1.0f - 0.25f * t / duration);
        phase += 2.0f * kPi * frequency /
            static_cast<float>(kSampleRate);
        const float attack = std::min(1.0f, t / 0.003f);
        const float envelope = attack * std::exp(-t * decay);
        samples[static_cast<std::size_t>(i)] =
            (std::sin(phase) * 0.55f +
             low * 0.35f +
             noise * std::exp(-t * 65.0f) * brightness) *
            envelope;
    }
    apply_fade(samples, 0.004f);
    return samples;
}

std::vector<float> make_impact(std::uint32_t& rng, int variant) {
    const float duration = 0.075f;
    const int count = static_cast<int>(duration * kSampleRate);
    std::vector<float> samples(static_cast<std::size_t>(count), 0.0f);
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) /
            static_cast<float>(kSampleRate);
        const float noise = next_noise(rng);
        const float envelope = std::exp(-t * 75.0f);
        samples[static_cast<std::size_t>(i)] =
            (noise * 0.8f +
             std::sin(2.0f * kPi * (1100.0f + 90.0f * variant) * t) *
                 0.25f) *
            envelope;
    }
    apply_fade(samples, 0.003f);
    return samples;
}

std::vector<float> make_hit(std::uint32_t& rng) {
    const float duration = 0.14f;
    const int count = static_cast<int>(duration * kSampleRate);
    std::vector<float> samples(static_cast<std::size_t>(count), 0.0f);
    float low = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) /
            static_cast<float>(kSampleRate);
        const float noise = next_noise(rng);
        low += (noise - low) * 0.12f;
        const float envelope = std::exp(-t * 22.0f);
        samples[static_cast<std::size_t>(i)] =
            (std::sin(2.0f * kPi * 82.0f * t) * 0.65f +
             low * 0.5f) *
            envelope;
    }
    apply_fade(samples, 0.004f);
    return samples;
}

std::vector<float> make_shield_hit(std::uint32_t& rng) {
    const float duration = 0.18f;
    const int count = static_cast<int>(duration * kSampleRate);
    std::vector<float> samples(static_cast<std::size_t>(count), 0.0f);
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) /
            static_cast<float>(kSampleRate);
        const float noise = next_noise(rng);
        const float envelope = std::exp(-t * 18.0f);
        samples[static_cast<std::size_t>(i)] =
            (std::sin(2.0f * kPi * 920.0f * t) * 0.42f +
             std::sin(2.0f * kPi * 1370.0f * t) * 0.18f +
             noise * 0.22f) *
            envelope;
    }
    apply_fade(samples, 0.004f);
    return samples;
}

std::vector<float> make_explosion(std::uint32_t& rng) {
    const float duration = 0.65f;
    const int count = static_cast<int>(duration * kSampleRate);
    std::vector<float> samples(static_cast<std::size_t>(count), 0.0f);
    float low = 0.0f;
    float phase = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) /
            static_cast<float>(kSampleRate);
        const float noise = next_noise(rng);
        low += (noise - low) * 0.09f;
        const float frequency = 58.0f - 20.0f * (t / duration);
        phase += 2.0f * kPi * frequency /
            static_cast<float>(kSampleRate);
        const float envelope = std::exp(-t * 4.2f);
        samples[static_cast<std::size_t>(i)] =
            (std::sin(phase) * 0.58f +
             low * 0.48f +
             noise * std::exp(-t * 32.0f) * 0.5f) *
            envelope;
    }
    apply_fade(samples, 0.006f);
    return samples;
}

std::vector<float> make_footstep(std::uint32_t& rng, int variant) {
    const float duration = 0.1f;
    const int count = static_cast<int>(duration * kSampleRate);
    std::vector<float> samples(static_cast<std::size_t>(count), 0.0f);
    float low = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) /
            static_cast<float>(kSampleRate);
        const float noise = next_noise(rng);
        low += (noise - low) * 0.2f;
        const float envelope = std::exp(-t * 28.0f);
        samples[static_cast<std::size_t>(i)] =
            (low * 0.55f +
             std::sin(2.0f * kPi * (92.0f + 8.0f * variant) * t) *
                 0.45f) *
            envelope;
    }
    apply_fade(samples, 0.004f);
    return samples;
}

std::vector<float> make_death(std::uint32_t& rng) {
    const float duration = 0.5f;
    const int count = static_cast<int>(duration * kSampleRate);
    std::vector<float> samples(static_cast<std::size_t>(count), 0.0f);
    float phase = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) /
            static_cast<float>(kSampleRate);
        const float noise = next_noise(rng);
        const float frequency = 230.0f - 160.0f * (t / duration);
        phase += 2.0f * kPi * frequency /
            static_cast<float>(kSampleRate);
        const float envelope =
            std::min(1.0f, t / 0.01f) * std::exp(-t * 5.0f);
        samples[static_cast<std::size_t>(i)] =
            (std::sin(phase) * 0.6f + noise * 0.2f) * envelope;
    }
    apply_fade(samples, 0.006f);
    return samples;
}

std::vector<float> make_respawn() {
    const float duration = 0.4f;
    const int count = static_cast<int>(duration * kSampleRate);
    std::vector<float> samples(static_cast<std::size_t>(count), 0.0f);
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) /
            static_cast<float>(kSampleRate);
        const float first = std::sin(2.0f * kPi * 430.0f * t) *
            std::exp(-t * 9.0f);
        const float second = t > 0.12f
            ? std::sin(2.0f * kPi * 650.0f * (t - 0.12f)) *
                  std::exp(-(t - 0.12f) * 8.0f)
            : 0.0f;
        samples[static_cast<std::size_t>(i)] =
            (first * 0.45f + second * 0.35f) *
            std::min(1.0f, t / 0.008f);
    }
    apply_fade(samples, 0.006f);
    return samples;
}

std::vector<float> make_switch(std::uint32_t& rng) {
    const float duration = 0.13f;
    std::vector<float> samples(
        static_cast<std::size_t>(duration * kSampleRate),
        0.0f);
    add_click(samples, rng, 0.004f, 1450.0f, 0.7f, 85.0f);
    add_click(samples, rng, 0.055f, 880.0f, 0.5f, 70.0f);
    apply_fade(samples, 0.003f);
    return samples;
}

std::vector<float> make_reload(std::uint32_t& rng) {
    const float duration = 0.45f;
    std::vector<float> samples(
        static_cast<std::size_t>(duration * kSampleRate),
        0.0f);
    add_click(samples, rng, 0.01f, 1100.0f, 0.55f, 80.0f);
    add_click(samples, rng, 0.14f, 720.0f, 0.6f, 65.0f);
    add_click(samples, rng, 0.29f, 980.0f, 0.5f, 75.0f);
    apply_fade(samples, 0.004f);
    return samples;
}

std::vector<float> make_ability() {
    const float duration = 0.4f;
    const int count = static_cast<int>(duration * kSampleRate);
    std::vector<float> samples(static_cast<std::size_t>(count), 0.0f);
    float phase = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) /
            static_cast<float>(kSampleRate);
        const float frequency = 260.0f + 640.0f * (t / duration);
        phase += 2.0f * kPi * frequency /
            static_cast<float>(kSampleRate);
        const float envelope =
            std::min(1.0f, t / 0.015f) * std::exp(-t * 4.5f);
        samples[static_cast<std::size_t>(i)] =
            (std::sin(phase) * 0.45f +
             std::sin(phase * 2.0f) * 0.12f) *
            envelope;
    }
    apply_fade(samples, 0.008f);
    return samples;
}

std::vector<float> make_crystal_pickup() {
    const float duration = 0.18f;
    const int count = static_cast<int>(duration * kSampleRate);
    std::vector<float> samples(static_cast<std::size_t>(count), 0.0f);
    float phase = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) /
            static_cast<float>(kSampleRate);
        const float frequency = 880.0f + 900.0f * (t / duration);
        phase += 2.0f * kPi * frequency /
            static_cast<float>(kSampleRate);
        const float envelope =
            std::min(1.0f, t / 0.004f) * std::exp(-t * 22.0f);
        samples[static_cast<std::size_t>(i)] =
            (std::sin(phase) * 0.42f +
             std::sin(phase * 2.0f) * 0.16f) *
            envelope;
    }
    apply_fade(samples, 0.004f);
    return samples;
}

std::vector<float> make_crystal_deliver() {
    const float duration = 0.55f;
    const int count = static_cast<int>(duration * kSampleRate);
    std::vector<float> samples(static_cast<std::size_t>(count), 0.0f);
    constexpr std::array<float, 3> notes{659.25f, 880.0f, 1318.51f};
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) /
            static_cast<float>(kSampleRate);
        float value = 0.0f;
        for (std::size_t note = 0; note < notes.size(); ++note) {
            const float start = static_cast<float>(note) * 0.09f;
            if (t < start) {
                continue;
            }
            const float local = t - start;
            value += std::sin(2.0f * kPi * notes[note] * local) *
                std::exp(-local * 7.0f) * 0.28f;
        }
        samples[static_cast<std::size_t>(i)] =
            value * std::min(1.0f, t / 0.01f);
    }
    apply_fade(samples, 0.01f);
    return samples;
}

std::vector<float> make_match_end() {
    const float duration = 0.9f;
    const int count = static_cast<int>(duration * kSampleRate);
    std::vector<float> samples(static_cast<std::size_t>(count), 0.0f);
    constexpr std::array<float, 3> notes{196.0f, 293.66f, 392.0f};
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) /
            static_cast<float>(kSampleRate);
        float value = 0.0f;
        for (float note : notes) {
            value += std::sin(2.0f * kPi * note * t) * 0.26f;
        }
        samples[static_cast<std::size_t>(i)] =
            value * std::exp(-t * 3.2f) *
            std::min(1.0f, t / 0.02f);
    }
    apply_fade(samples, 0.012f);
    return samples;
}

std::vector<float> make_ui_click(std::uint32_t& rng) {
    std::vector<float> samples(
        static_cast<std::size_t>(0.045f * kSampleRate),
        0.0f);
    add_click(samples, rng, 0.001f, 1050.0f, 0.75f, 110.0f);
    add_click(samples, rng, 0.012f, 700.0f, 0.35f, 90.0f);
    apply_fade(samples, 0.002f);
    return samples;
}

std::vector<float> make_ui_hover() {
    std::vector<float> samples(
        static_cast<std::size_t>(0.03f * kSampleRate),
        0.0f);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const float t = static_cast<float>(i) /
            static_cast<float>(kSampleRate);
        samples[i] = std::sin(2.0f * kPi * 640.0f * t) *
            std::exp(-t * 120.0f) * 0.3f;
    }
    apply_fade(samples, 0.002f);
    return samples;
}

constexpr std::size_t kGadgetTypeCount =
    static_cast<std::size_t>(core::GadgetType::Count);
constexpr std::size_t kGadgetSoundKinds = 3;

struct GadgetSoundNames {
    const char* activate = nullptr;
    std::array<const char*, 3> impact{};
    std::array<const char*, 3> explode{};
};

constexpr std::array<GadgetSoundNames, kGadgetTypeCount> kGadgetSounds{{
    {"sword_whoosh", {"sword_hit"}, {}},                    // RubyBlade
    {"energy_battery", {}, {}},                             // PowerBattery
    {"activate_gain_gadget", {}, {}},                       // MagneticNozzle
    {"weapon_grenade_throw",
     {"grenade_rebound01", "grenade_rebound02", "grenade_rebound03"},
     {"grenade_explosion01", "grenade_explosion02"}},      // FragGrenade
    {"electroshock", {}, {"electroshock_expl"}},            // StaticShield
    {"turbo", {}, {}},                                      // Kickstart
    {"rocket_launch_shot", {}, {"rocket_launch_explosion"}},// RocketLaunch
    {"protective_droid_activation", {"protective_droid_damage"}, {}},
                                                            // ProtectiveDroid
    {"firstaid", {}, {}},                                   // FirstAid
    {"injection_active", {}, {}},                           // Injection
    {"rocket_burst",
     {},
     {"rocket_burst_expl01", "rocket_burst_expl02", "rocket_burst_expl03"}},
                                                            // RocketSalvo
    {"jetpack", {}, {}},                                    // TurboPack
    {"aura_defence", {}, {}},                               // Toughness
    {"weapon_grenade_throw",
     {"grenade_rebound01", "grenade_rebound02", "grenade_rebound03"},
     {"stunning_grenade_expl"}},                            // ConcussionGrenade
    {"mine_install",
     {},
     {"antipersonal_mine_expl01", "antipersonal_mine_expl02"}},
                                                            // FragMine
    {"neutralizator", {}, {}},                              // Neutralizer
    {"boomerang", {"boomerang_hit01", "boomerang_hit02"}, {}},// Boomerang
    {"trap", {}, {"trap_activate"}},                        // Trap
    {"activate_gain_gadget", {}, {}},                       // DoubleShotSystem
    {"nebulizer", {}, {"nebulizer"}},                       // Sprayer
    {"chemicals", {}, {"chemicals"}},                       // Chemicals
    {"bloodreleaser", {}, {}},                              // Bloodletter
    {"chakram",
     {"metal_collision01", "metal_collision02", "metal_collision03"},
     {}},                                                   // Chakram
    {"dematerializator", {}, {}},                           // Dematerializator
    {"activate_gain_gadget", {}, {}},                       // Mutator
    {"fire_face", {}, {}},                                  // DemonicFace
    {"fire_mark_install", {}, {"fire_mark_expl"}},          // FireTrap
    {"avatar", {}, {}},                                     // Avatar
    {"turbo", {}, {}},                                      // HyperAccelerator
    {"hand_prothesis", {}, {}},                             // ArmCyberProsthesis
    {"anti_pain", {}, {}},                                  // Inhibitor
}};

std::optional<std::vector<float>> decode_wav(
    const std::vector<std::uint8_t>& bytes) {
    SDL_IOStream* io =
        SDL_IOFromConstMem(bytes.data(), bytes.size());
    if (io == nullptr) {
        return std::nullopt;
    }
    SDL_AudioSpec source{};
    std::uint8_t* buffer = nullptr;
    std::uint32_t buffer_size = 0;
    if (!SDL_LoadWAV_IO(io, true, &source, &buffer, &buffer_size)) {
        return std::nullopt;
    }
    SDL_AudioSpec target{};
    target.format = SDL_AUDIO_F32;
    target.channels = 1;
    target.freq = kSampleRate;
    SDL_AudioStream* converter =
        SDL_CreateAudioStream(&source, &target);
    if (converter == nullptr) {
        SDL_free(buffer);
        return std::nullopt;
    }
    SDL_PutAudioStreamData(
        converter,
        buffer,
        static_cast<int>(buffer_size));
    SDL_FlushAudioStream(converter);
    SDL_free(buffer);

    std::vector<float> samples;
    while (true) {
        const int available = SDL_GetAudioStreamAvailable(converter);
        if (available <= 0) {
            break;
        }
        const std::size_t offset = samples.size();
        samples.resize(
            offset + static_cast<std::size_t>(available) / sizeof(float));
        const int got = SDL_GetAudioStreamData(
            converter,
            samples.data() + offset,
            available);
        if (got <= 0) {
            samples.resize(offset);
            break;
        }
        samples.resize(
            offset + static_cast<std::size_t>(got) / sizeof(float));
    }
    SDL_DestroyAudioStream(converter);
    if (samples.empty()) {
        return std::nullopt;
    }
    normalize(samples);
    return samples;
}

ClipId clip_for(AudioCue cue) {
    switch (cue) {
        case AudioCue::UiClick: return ClipId::UiClick;
        case AudioCue::UiHover: return ClipId::UiHover;
        case AudioCue::WeaponSwitch: return ClipId::WeaponSwitch;
        case AudioCue::Reload: return ClipId::Reload;
        case AudioCue::ShotRifle: return ClipId::ShotRifle;
        case AudioCue::ShotShotgun: return ClipId::ShotShotgun;
        case AudioCue::ShotRocket: return ClipId::ShotRocket;
        case AudioCue::ShotGrenade: return ClipId::ShotGrenade;
        case AudioCue::ShotSmg: return ClipId::ShotSmg;
        case AudioCue::ShotBurst: return ClipId::ShotBurst;
        case AudioCue::ProjectileImpact: return ClipId::ProjectileImpact;
        case AudioCue::Hit: return ClipId::Hit;
        case AudioCue::ShieldHit: return ClipId::ShieldHit;
        case AudioCue::Explosion: return ClipId::Explosion;
        case AudioCue::Footstep: return ClipId::Footstep;
        case AudioCue::Death: return ClipId::Death;
        case AudioCue::Respawn: return ClipId::Respawn;
        case AudioCue::Ability: return ClipId::Ability;
        case AudioCue::CrystalPickup: return ClipId::CrystalPickup;
        case AudioCue::CrystalDeliver: return ClipId::CrystalDeliver;
        case AudioCue::MatchEnd: return ClipId::MatchEnd;
    }
    return ClipId::UiClick;
}

int max_voices_for(AudioCue cue) {
    switch (cue) {
        case AudioCue::UiClick:
        case AudioCue::UiHover:
            return 4;
        case AudioCue::Footstep:
        case AudioCue::ProjectileImpact:
            return 8;
        case AudioCue::Explosion:
            return 6;
        case AudioCue::ShotRifle:
        case AudioCue::ShotShotgun:
        case AudioCue::ShotRocket:
        case AudioCue::ShotGrenade:
        case AudioCue::ShotSmg:
        case AudioCue::ShotBurst:
            return 12;
        default:
            return 6;
    }
}

std::int16_t to_s16(float sample) {
    const float clamped = std::clamp(sample, -1.0f, 1.0f);
    return static_cast<std::int16_t>(
        std::lround(clamped * 32767.0f));
}

} // namespace

AudioCue weapon_shot_cue(std::string_view weapon_id) {
    if (weapon_id == "shotgun") {
        return AudioCue::ShotShotgun;
    }
    if (weapon_id == "rocket") {
        return AudioCue::ShotRocket;
    }
    if (weapon_id == "grenade") {
        return AudioCue::ShotGrenade;
    }
    if (weapon_id == "smg") {
        return AudioCue::ShotSmg;
    }
    if (weapon_id == "burst") {
        return AudioCue::ShotBurst;
    }
    return AudioCue::ShotRifle;
}

struct AudioEngine::Impl {
    std::array<std::vector<Clip>, kClipCount> clips;
    std::array<
        std::array<std::vector<const Clip*>, kGadgetSoundKinds>,
        kGadgetTypeCount>
        gadget_clips;
    std::vector<std::unique_ptr<Clip>> gadget_clip_cache;
    std::unordered_map<std::string, const Clip*> gadget_clip_lookup;
    std::array<int, kGadgetSoundKinds> active_gadget_groups{};
    const shared::ResourceManager* resources = nullptr;
    std::array<Voice, kMaxVoices> voices{};
    std::array<int, kCueCount> active_cues{};
    std::array<PendingSound, kQueueCapacity> queue{};
    std::mutex queue_mutex;
    std::size_t queue_head = 0;
    std::size_t queue_tail = 0;
    SDL_AudioStream* stream = nullptr;
    bool owns_audio = false;
    bool enabled = false;
    std::atomic<float> master{0.8f};
    float listener_x = 0.0f;
    float listener_y = 0.0f;
    float output_frequency = static_cast<float>(kSampleRate);
    std::uint32_t rng = 0x51A7E3D9U;
    std::uint32_t callback_rng = 0x9E3779B9U;
    std::vector<std::int16_t> mix_buffer;
    std::uint32_t enqueued = 0;
    std::uint32_t dropped = 0;
    std::uint64_t rendered_frames = 0;

    void build_clips() {
        for (auto& variants : clips) {
            variants.clear();
        }
        voices = {};
        active_cues.fill(0);
        active_gadget_groups.fill(0);

        const auto add = [&](ClipId id, float volume,
                             std::vector<float> samples) {
            normalize(samples);
            clips[static_cast<std::size_t>(id)].push_back(
                Clip{std::move(samples), volume});
        };

        for (int variant = 0; variant < 3; ++variant) {
            add(
                ClipId::ShotRifle,
                0.82f,
                make_shot(
                    rng,
                    variant,
                    0.14f,
                    180.0f,
                    0.72f,
                    28.0f));
            add(
                ClipId::ShotShotgun,
                0.92f,
                make_shot(
                    rng,
                    variant,
                    0.24f,
                    105.0f,
                    0.9f,
                    19.0f));
            add(
                ClipId::ShotSmg,
                0.58f,
                make_shot(
                    rng,
                    variant,
                    0.075f,
                    240.0f,
                    0.82f,
                    42.0f));
            add(
                ClipId::ShotBurst,
                0.64f,
                make_shot(
                    rng,
                    variant,
                    0.095f,
                    205.0f,
                    0.76f,
                    36.0f));
            add(
                ClipId::ShotRocket,
                0.82f,
                make_shot(
                    rng,
                    variant,
                    0.3f,
                    90.0f,
                    0.45f,
                    15.0f));
            add(
                ClipId::ShotGrenade,
                0.62f,
                make_shot(
                    rng,
                    variant,
                    0.18f,
                    132.0f,
                    0.55f,
                    23.0f));
            add(
                ClipId::ProjectileImpact,
                0.52f,
                make_impact(rng, variant));
            add(
                ClipId::Footstep,
                0.38f,
                make_footstep(rng, variant));
        }

        add(ClipId::Hit, 0.8f, make_hit(rng));
        add(ClipId::ShieldHit, 0.62f, make_shield_hit(rng));
        add(ClipId::Explosion, 0.95f, make_explosion(rng));
        add(ClipId::Death, 0.82f, make_death(rng));
        add(ClipId::Respawn, 0.64f, make_respawn());
        add(ClipId::WeaponSwitch, 0.56f, make_switch(rng));
        add(ClipId::Reload, 0.52f, make_reload(rng));
        add(ClipId::Ability, 0.72f, make_ability());
        add(ClipId::CrystalPickup, 0.62f, make_crystal_pickup());
        add(ClipId::CrystalDeliver, 0.82f, make_crystal_deliver());
        add(ClipId::MatchEnd, 0.75f, make_match_end());
        add(ClipId::UiClick, 0.46f, make_ui_click(rng));
        add(ClipId::UiHover, 0.2f, make_ui_hover());
    }

    const Clip* load_gadget_clip(std::string_view name, float volume) {
        if (name.empty() || resources == nullptr) {
            return nullptr;
        }
        const std::string key{name};
        if (const auto found = gadget_clip_lookup.find(key);
            found != gadget_clip_lookup.end()) {
            return found->second;
        }
        const auto bytes =
            resources->read("audio/gadgets/" + key + ".wav");
        if (!bytes) {
            return nullptr;
        }
        const auto samples = decode_wav(*bytes);
        if (!samples) {
            NSYNC_LOG_WARN("audio", "cannot decode gadget clip: ", key);
            return nullptr;
        }
        auto clip = std::make_unique<Clip>();
        clip->samples = *samples;
        clip->volume = volume;
        const Clip* pointer = clip.get();
        gadget_clip_cache.push_back(std::move(clip));
        gadget_clip_lookup.emplace(key, pointer);
        return pointer;
    }

    void load_gadget_sounds() {
        for (auto& gadget : gadget_clips) {
            for (auto& variants : gadget) {
                variants.clear();
            }
        }
        gadget_clip_cache.clear();
        gadget_clip_lookup.clear();
        if (resources == nullptr) {
            return;
        }
        std::size_t loaded = 0;
        std::size_t wanted = 0;
        for (std::size_t gadget = 0; gadget < kGadgetTypeCount; ++gadget) {
            const GadgetSoundNames& names = kGadgetSounds[gadget];
            const auto add = [&](std::size_t kind,
                                 const char* name,
                                 float volume) {
                if (name == nullptr) {
                    return;
                }
                ++wanted;
                if (const Clip* clip = load_gadget_clip(name, volume)) {
                    gadget_clips[gadget][kind].push_back(clip);
                    ++loaded;
                }
            };
            add(0, names.activate, 0.9f);
            for (const char* name : names.impact) {
                add(1, name, 0.68f);
            }
            for (const char* name : names.explode) {
                add(2, name, 1.0f);
            }
        }
        NSYNC_LOG_INFO(
            "audio",
            "loaded original gadget clips: ",
            loaded,
            "/",
            wanted);
    }

    void enqueue(AudioCue cue, float left, float right, float pitch) {
        if (!enabled || stream == nullptr) {
            return;
        }
        const float master_gain = master.load(std::memory_order_relaxed);
        left *= master_gain;
        right *= master_gain;
        if (left <= 0.0001f && right <= 0.0001f) {
            return;
        }
        const std::size_t next = (queue_head + 1) % kQueueCapacity;
        std::lock_guard lock(queue_mutex);
        if (next == queue_tail) {
            ++dropped;
            return;
        }
        PendingSound pending;
        pending.cue = cue;
        pending.left = left;
        pending.right = right;
        pending.pitch = pitch;
        queue[queue_head] = pending;
        queue_head = next;
        ++enqueued;
    }

    void enqueue_clip(
        const Clip* clip,
        int group,
        float left,
        float right,
        float pitch) {
        if (!enabled || stream == nullptr || clip == nullptr) {
            return;
        }
        const float master_gain = master.load(std::memory_order_relaxed);
        left *= master_gain;
        right *= master_gain;
        if (left <= 0.0001f && right <= 0.0001f) {
            return;
        }
        const std::size_t next = (queue_head + 1) % kQueueCapacity;
        std::lock_guard lock(queue_mutex);
        if (next == queue_tail) {
            ++dropped;
            return;
        }
        PendingSound pending;
        pending.clip = clip;
        pending.group = group;
        pending.left = left;
        pending.right = right;
        pending.pitch = pitch;
        queue[queue_head] = pending;
        queue_head = next;
        ++enqueued;
    }

    float random_unit() {
        return static_cast<float>(next_random(rng) >> 8) /
            static_cast<float>(1U << 24U);
    }

    void play_centered(AudioCue cue, float gain) {
        const float channel = gain * 0.70710678f;
        enqueue(cue, channel, channel, 1.0f);
    }

    void play_spatial(AudioCue cue, float x, float y, float gain) {
        const float dx = x - listener_x;
        const float dy = y - listener_y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance >= kSoundDistance) {
            return;
        }
        const float attenuation =
            std::clamp(1.0f - distance / kSoundDistance, 0.0f, 1.0f);
        const float pan = std::clamp(
            (1.0f - attenuation) * (dx < 0.0f ? -1.0f : 1.0f),
            -1.0f,
            1.0f);
        const float angle = (pan + 1.0f) * 0.25f * kPi;
        float pitch = 1.0f;
        if (cue != AudioCue::UiClick &&
            cue != AudioCue::UiHover &&
            cue != AudioCue::MatchEnd) {
            pitch = 0.97f + random_unit() * 0.06f;
        }
        enqueue(
            cue,
            attenuation * std::cos(angle) * gain,
            attenuation * std::sin(angle) * gain,
            pitch);
    }

    void play_gadget(
        core::GadgetType gadget,
        GadgetSoundKind kind,
        float x,
        float y,
        float gain) {
        const std::size_t gadget_index =
            static_cast<std::size_t>(gadget);
        const std::size_t kind_index = static_cast<std::size_t>(kind);
        if (gadget_index >= gadget_clips.size() ||
            kind_index >= kGadgetSoundKinds) {
            return;
        }
        const auto& variants = gadget_clips[gadget_index][kind_index];
        if (variants.empty()) {
            switch (kind) {
                case GadgetSoundKind::Activate:
                    play_spatial(AudioCue::Ability, x, y, gain);
                    break;
                case GadgetSoundKind::Impact:
                    play_spatial(AudioCue::ProjectileImpact, x, y, gain);
                    break;
                case GadgetSoundKind::Explode:
                    play_spatial(AudioCue::Explosion, x, y, gain);
                    break;
            }
            return;
        }
        const float dx = x - listener_x;
        const float dy = y - listener_y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance >= kSoundDistance) {
            return;
        }
        const float attenuation =
            std::clamp(1.0f - distance / kSoundDistance, 0.0f, 1.0f);
        const float pan = std::clamp(
            (1.0f - attenuation) * (dx < 0.0f ? -1.0f : 1.0f),
            -1.0f,
            1.0f);
        const float angle = (pan + 1.0f) * 0.25f * kPi;
        const float pitch = 0.97f + random_unit() * 0.06f;
        const Clip* clip = variants[
            static_cast<std::size_t>(next_random(rng)) % variants.size()];
        enqueue_clip(
            clip,
            static_cast<int>(kind_index),
            attenuation * std::cos(angle) * gain,
            attenuation * std::sin(angle) * gain,
            pitch);
    }

    void render(SDL_AudioStream* output, int additional_amount) {
        if (additional_amount <= 0) {
            return;
        }
        const std::size_t frame_bytes =
            static_cast<std::size_t>(kChannels) * sizeof(std::int16_t);
        const std::size_t frames =
            static_cast<std::size_t>(additional_amount) / frame_bytes;
        if (frames == 0) {
            return;
        }
        rendered_frames += frames;
        const std::size_t sample_count =
            frames * static_cast<std::size_t>(kChannels);
        if (mix_buffer.size() < sample_count) {
            mix_buffer.resize(sample_count);
        }
        std::fill(
            mix_buffer.begin(),
            mix_buffer.begin() + static_cast<std::ptrdiff_t>(sample_count),
            0);

        std::array<PendingSound, kDrainCapacity> incoming{};
        std::size_t incoming_count = 0;
        {
            std::lock_guard lock(queue_mutex);
            while (queue_tail != queue_head &&
                   incoming_count < incoming.size()) {
                incoming[incoming_count++] = queue[queue_tail];
                queue_tail = (queue_tail + 1) % kQueueCapacity;
            }
        }

        for (std::size_t i = 0; i < incoming_count; ++i) {
            const PendingSound& pending = incoming[i];
            const Clip* selected = nullptr;
            int group = -1;
            if (pending.clip != nullptr) {
                if (pending.group < 0 ||
                    static_cast<std::size_t>(pending.group) >=
                        active_gadget_groups.size() ||
                    active_gadget_groups[
                        static_cast<std::size_t>(pending.group)] >= 8) {
                    continue;
                }
                selected = pending.clip;
                group = pending.group;
            } else {
                const std::size_t cue_index =
                    static_cast<std::size_t>(pending.cue);
                if (cue_index >= active_cues.size() ||
                    active_cues[cue_index] >= max_voices_for(pending.cue)) {
                    continue;
                }
                const std::size_t clip_index =
                    static_cast<std::size_t>(clip_for(pending.cue));
                const auto& variants = clips[clip_index];
                if (variants.empty()) {
                    continue;
                }
                selected = &variants[
                    static_cast<std::size_t>(next_random(callback_rng)) %
                    variants.size()];
            }
            Voice* voice = nullptr;
            for (Voice& candidate : voices) {
                if (!candidate.active) {
                    voice = &candidate;
                    break;
                }
            }
            if (voice == nullptr) {
                continue;
            }
            voice->clip = selected;
            voice->cue = pending.cue;
            voice->group = group;
            voice->cursor = 0.0f;
            voice->step =
                static_cast<float>(kSampleRate) / output_frequency *
                pending.pitch;
            voice->left = pending.left * selected->volume;
            voice->right = pending.right * selected->volume;
            voice->active = true;
            if (group >= 0) {
                ++active_gadget_groups[static_cast<std::size_t>(group)];
            } else {
                ++active_cues[static_cast<std::size_t>(pending.cue)];
            }
        }

        for (std::size_t frame = 0; frame < frames; ++frame) {
            float left = 0.0f;
            float right = 0.0f;
            for (Voice& voice : voices) {
                if (!voice.active) {
                    continue;
                }
                const Clip& clip = *voice.clip;
                if (voice.cursor >=
                    static_cast<float>(clip.samples.size())) {
                    voice.active = false;
                    if (voice.group >= 0) {
                        const std::size_t group =
                            static_cast<std::size_t>(voice.group);
                        if (group < active_gadget_groups.size() &&
                            active_gadget_groups[group] > 0) {
                            --active_gadget_groups[group];
                        }
                    } else {
                        const std::size_t cue_index =
                            static_cast<std::size_t>(voice.cue);
                        if (cue_index < active_cues.size() &&
                            active_cues[cue_index] > 0) {
                            --active_cues[cue_index];
                        }
                    }
                    continue;
                }
                const float sample = clip.samples[
                    static_cast<std::size_t>(voice.cursor)];
                left += sample * voice.left;
                right += sample * voice.right;
                voice.cursor += voice.step;
            }
            mix_buffer[frame * 2] = to_s16(left);
            mix_buffer[frame * 2 + 1] = to_s16(right);
        }

        SDL_PutAudioStreamData(
            output,
            mix_buffer.data(),
            static_cast<int>(sample_count * sizeof(std::int16_t)));
    }

    static void SDLCALL stream_callback(
        void* userdata,
        SDL_AudioStream* stream,
        int additional_amount,
        int total_amount) {
        (void)total_amount;
        static_cast<Impl*>(userdata)->render(stream, additional_amount);
    }

    void quit_audio_subsystem() {
        if (owns_audio) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            owns_audio = false;
        }
    }

    bool start(bool requested, float master_volume) {
        master.store(std::clamp(master_volume, 0.0f, 1.0f));
        if (!requested) {
            NSYNC_LOG_INFO("audio", "disabled by configuration");
            return false;
        }
        if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                NSYNC_LOG_WARN(
                    "audio",
                    "SDL audio init failed: ",
                    SDL_GetError());
                return false;
            }
            owns_audio = true;
        }

        SDL_AudioSpec device_format{};
        int sample_frames = 0;
        if (!SDL_GetAudioDeviceFormat(
                SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                &device_format,
                &sample_frames)) {
            NSYNC_LOG_WARN(
                "audio",
                "no playback device: ",
                SDL_GetError());
            quit_audio_subsystem();
            return false;
        }

        SDL_AudioSpec format{};
        format.format = SDL_AUDIO_S16;
        format.channels = kChannels;
        format.freq = device_format.freq > 0
            ? device_format.freq
            : kSampleRate;
        output_frequency = static_cast<float>(format.freq);
        build_clips();
        load_gadget_sounds();
        stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
            &format,
            &Impl::stream_callback,
            this);
        if (stream == nullptr) {
            NSYNC_LOG_WARN(
                "audio",
                "failed to open playback stream: ",
                SDL_GetError());
            quit_audio_subsystem();
            return false;
        }
        if (!SDL_ResumeAudioStreamDevice(stream)) {
            NSYNC_LOG_WARN(
                "audio",
                "failed to resume playback: ",
                SDL_GetError());
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
            quit_audio_subsystem();
            return false;
        }
        enabled = true;
        NSYNC_LOG_INFO(
            "audio",
            "ready: ",
            format.freq,
            " Hz, ",
            format.channels,
            " channels, ",
            clips[static_cast<std::size_t>(ClipId::ShotRifle)].size(),
            " rifle variants");
        return true;
    }

    void stop() {
        const bool was_enabled = enabled;
        enabled = false;
        if (stream != nullptr) {
            SDL_PauseAudioStreamDevice(stream);
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
        }
        {
            std::lock_guard lock(queue_mutex);
            queue_head = 0;
            queue_tail = 0;
        }
        for (Voice& voice : voices) {
            voice = {};
        }
        active_cues.fill(0);
        active_gadget_groups.fill(0);
        if (was_enabled) {
            NSYNC_LOG_DEBUG(
                "audio",
                "stopped: queued=",
                enqueued,
                " dropped=",
                dropped,
                " rendered_frames=",
                rendered_frames);
        }
        quit_audio_subsystem();
    }
};

AudioEngine::AudioEngine()
    : impl_(std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::initialize(
    bool enabled,
    float master_volume,
    const shared::ResourceManager* resources) {
    if (impl_ == nullptr) {
        return false;
    }
    if (resources != nullptr) {
        impl_->resources = resources;
    }
    if (available_) {
        return true;
    }
    available_ = impl_->start(enabled, master_volume);
    return available_;
}

bool AudioEngine::set_enabled(bool enabled) {
    if (impl_ == nullptr) {
        return false;
    }
    if (!enabled) {
        impl_->stop();
        available_ = false;
        return false;
    }
    if (available_) {
        return true;
    }
    available_ = impl_->start(true, impl_->master.load());
    return available_;
}

void AudioEngine::shutdown() {
    if (impl_ != nullptr) {
        impl_->stop();
    }
    available_ = false;
}

void AudioEngine::set_master_volume(float master_volume) {
    if (impl_ != nullptr) {
        impl_->master.store(std::clamp(master_volume, 0.0f, 1.0f));
    }
}

void AudioEngine::play(AudioCue cue, float gain) {
    if (!available_ || impl_ == nullptr) {
        return;
    }
    impl_->play_centered(cue, std::clamp(gain, 0.0f, 2.0f));
}

void AudioEngine::play_at(AudioCue cue, float x, float y, float gain) {
    if (!available_ || impl_ == nullptr) {
        return;
    }
    impl_->play_spatial(cue, x, y, std::clamp(gain, 0.0f, 2.0f));
}

void AudioEngine::play_gadget(
    core::GadgetType gadget,
    GadgetSoundKind kind,
    float x,
    float y,
    float gain) {
    if (!available_ || impl_ == nullptr) {
        return;
    }
    impl_->play_gadget(
        gadget,
        kind,
        x,
        y,
        std::clamp(gain, 0.0f, 2.0f));
}

void AudioEngine::set_listener(float x, float y) {
    if (impl_ != nullptr) {
        impl_->listener_x = x;
        impl_->listener_y = y;
    }
}

} // namespace nsync::client
