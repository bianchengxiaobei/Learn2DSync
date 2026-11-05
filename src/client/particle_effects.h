#pragma once

#include "core/gadgets.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>

namespace nsync::client {

struct ParticleLayer {
    const char* sprite = nullptr;
    std::uint8_t count = 1;
    float lifetime = 0.4f;
    float lifetime_variance = 0.0f;
    float speed = 0.0f;
    float speed_variance = 0.0f;
    float size_start = 0.4f;
    float size_end = 0.4f;
    float spread = 6.2831853f;
    float angle = 0.0f;
    float drag = 0.0f;
    float gravity = 0.0f;
    float spin = 0.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float delay = 0.0f;
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
    bool additive = true;
    bool stretch = false;
    bool ring = false;
    bool follow = false;
};

struct GadgetParticles {
    std::span<const ParticleLayer> activate;
    std::span<const ParticleLayer> explode;
    std::span<const ParticleLayer> impact;
    std::span<const ParticleLayer> aura;
    float aura_rate = 0.0f;
};

namespace particle_detail {

inline constexpr float kUp = -1.5707963f;
inline constexpr float kFull = 6.2831853f;

inline constexpr std::array<ParticleLayer, 0> kEmpty{};

inline constexpr ParticleLayer kSlashSparks[] = {
    {"smallSharpPoint", 5, 0.22f, 0.08f, 5.0f, 2.0f, 0.07f, 0.03f,
     kFull, 0.0f, 2.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 210, 170, 255,
     true, true, false, false},
    {"flash", 1, 0.12f, 0.0f, 0.0f, 0.0f, 0.28f, 0.12f, kFull, 0.0f,
     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 235, 210, 220, true, false,
     false, false},
};

inline constexpr ParticleLayer kExplosionBig[] = {
    {"flash", 1, 0.18f, 0.03f, 0.0f, 0.0f, 1.4f, 0.7f, kFull, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 238, 205, 255, true, false,
    false, false},
    {"smokyShockwave", 1, 0.34f, 0.05f, 0.0f, 0.0f, 0.55f, 3.0f, kFull,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 220, 180, 220, true,
    false, true, false},
    {"smallSharpPoint", 14, 0.38f, 0.16f, 6.5f, 3.5f, 0.12f, 0.04f, kFull,
    0.0f, 1.8f, 5.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 200, 130, 255, true,
    true, false, false},
    {"dirtySmoke", 7, 0.95f, 0.3f, 1.5f, 0.8f, 0.65f, 1.8f, kFull, 0.0f,
    1.0f, -1.0f, 0.6f, 0.0f, 0.0f, 0.02f, 60, 55, 52, 190, false, false,
    false, false},
    {"singleStone", 6, 0.75f, 0.2f, 5.5f, 2.5f, 0.13f, 0.08f, kFull, 0.0f,
    0.8f, 9.0f, 5.0f, 0.0f, 0.0f, 0.0f, 120, 115, 105, 255, false, false,
    false, false},
};

inline constexpr ParticleLayer kExplosionSmall[] = {
    {"flash", 1, 0.14f, 0.02f, 0.0f, 0.0f, 0.9f, 0.45f, kFull, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 235, 200, 255, true, false,
    false, false},
    {"smokyShockwave", 1, 0.26f, 0.04f, 0.0f, 0.0f, 0.4f, 2.0f, kFull,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 215, 175, 210, true,
    false, true, false},
    {"smallSharpPoint", 9, 0.3f, 0.12f, 5.0f, 2.8f, 0.09f, 0.03f, kFull,
    0.0f, 1.5f, 4.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 195, 125, 255, true,
    true, false, false},
    {"dirtySmoke", 4, 0.7f, 0.25f, 1.1f, 0.6f, 0.45f, 1.2f, kFull, 0.0f,
    1.0f, -0.8f, 0.5f, 0.0f, 0.0f, 0.02f, 65, 60, 55, 170, false, false,
    false, false},
};

inline constexpr ParticleLayer kFlashExplosion[] = {
    {"flash", 1, 0.16f, 0.0f, 0.0f, 0.0f, 1.5f, 0.8f, kFull, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 220, 245, 255, 255, true, false, false,
    false},
    {"smokyShockwave", 1, 0.36f, 0.04f, 0.0f, 0.0f, 0.5f, 3.4f, kFull,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 170, 230, 255, 230, true,
    false, true, false},
    {"Bolt", 6, 0.24f, 0.1f, 4.5f, 2.0f, 0.35f, 0.12f, kFull, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 190, 240, 255, 255, true, true,
    false, false},
    {"CFX_T_Anim7_Electricity", 5, 0.3f, 0.12f, 2.5f, 1.5f, 0.35f, 0.15f,
    kFull, 0.0f, 1.0f, 0.0f, 8.0f, 0.0f, 0.0f, 0.0f, 210, 245, 255, 255,
    true, false, false, false},
    {"FlashExplosion", 7, 0.32f, 0.1f, 3.2f, 1.8f, 0.18f, 0.06f, kFull,
    0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 235, 250, 255, 240, true,
    true, false, false},
};

inline constexpr ParticleLayer kFireExplosion[] = {
    {"FireGlowTwoColor", 2, 0.5f, 0.12f, 0.8f, 0.4f, 1.1f, 0.5f, kFull,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 150, 60, 255, true,
    false, false, false},
    {"FireSmoke", 5, 1.0f, 0.3f, 1.2f, 0.7f, 0.55f, 1.5f, kFull, 0.0f,
    0.6f, -1.4f, 0.5f, 0.0f, 0.0f, 0.03f, 55, 45, 40, 190, false, false,
    false, false},
    {"smallSharpPoint", 10, 0.4f, 0.15f, 5.5f, 2.5f, 0.1f, 0.03f, kFull,
    kUp, 1.2f, 3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 175, 80, 255, true,
    true, false, false},
    {"Wave6", 3, 0.55f, 0.15f, 0.0f, 0.0f, 0.7f, 1.4f, kFull, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.02f, 255, 140, 50, 220, true, false, false,
    false},
};

inline constexpr ParticleLayer kMetalImpact[] = {
    {"flash", 1, 0.1f, 0.0f, 0.0f, 0.0f, 0.45f, 0.25f, kFull, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 245, 220, 255, true, false, false,
    false},
    {"smallSharpPoint", 8, 0.28f, 0.1f, 4.5f, 2.0f, 0.08f, 0.03f, kFull,
    0.0f, 2.0f, 4.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 210, 160, 255, true,
    true, false, false},
    {"smokeImpact", 3, 0.45f, 0.12f, 1.0f, 0.5f, 0.4f, 0.9f, kFull, 0.0f,
    1.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.02f, 90, 85, 80, 180, false, false,
    false, false},
    {"singleStone", 4, 0.5f, 0.15f, 4.0f, 1.8f, 0.1f, 0.06f, kFull, 0.0f,
    0.6f, 7.0f, 4.0f, 0.0f, 0.0f, 0.0f, 120, 115, 105, 255, false, false,
    false, false},
};

inline constexpr ParticleLayer kMuzzle[] = {
    {"flash", 1, 0.09f, 0.02f, 0.0f, 0.0f, 0.35f, 0.18f, kFull, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 235, 200, 255, true, false,
    false, false},
    {"shortMuzzleFires", 1, 0.12f, 0.02f, 0.0f, 0.0f, 0.5f, 0.3f, kFull,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 190, 120, 240, true,
    false, false, false},
    {"smallSharpPoint", 4, 0.18f, 0.06f, 3.5f, 1.5f, 0.06f, 0.02f, kFull,
    -1.5707963f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 200, 140, 255,
    true, true, false, false},
};

inline constexpr ParticleLayer kFireMuzzle[] = {
    {"FireGlowTwoColor", 1, 0.22f, 0.04f, 0.0f, 0.0f, 0.75f, 0.35f, kFull,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 150, 60, 255, true,
    false, false, false},
    {"FireSmoke", 2, 0.45f, 0.12f, 0.8f, 0.4f, 0.4f, 0.9f, kFull, kUp,
    0.6f, -0.5f, 0.0f, 0.0f, 0.0f, 0.02f, 70, 60, 50, 160, false, false,
    false, false},
    {"smallSharpPoint", 5, 0.25f, 0.08f, 4.0f, 1.5f, 0.07f, 0.02f, kFull,
    0.0f, 1.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 190, 100, 255, true,
    true, false, false},
};

inline constexpr ParticleLayer kSprayerBurst[] = {
    {"flash", 1, 0.12f, 0.0f, 0.0f, 0.0f, 0.7f, 0.35f, kFull, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 220, 180, 255, true, false, false,
    false},
    {"smallSharpPoint", 10, 0.3f, 0.1f, 5.0f, 2.0f, 0.08f, 0.03f, kFull,
    0.0f, 1.5f, 3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 120, 90, 255, true,
    true, false, false},
    {"detailSmoke", 6, 0.6f, 0.2f, 1.2f, 0.6f, 0.5f, 1.2f, kFull, 0.0f,
    0.8f, -1.0f, 0.4f, 0.0f, 0.0f, 0.02f, 120, 80, 70, 180, false, false,
    false, false},
    {"FlashExplosion", 5, 0.35f, 0.12f, 3.5f, 1.8f, 0.16f, 0.05f, kFull,
    0.0f, 1.2f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 90, 70, 230, true,
    true, false, false},
};

inline constexpr ParticleLayer kChemBurst[] = {
    {"EnergyBall2", 1, 0.5f, 0.08f, 0.0f, 0.0f, 1.0f, 1.5f, kFull, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 120, 220, 90, 220, true, false,
    false, false},
    {"dirtySmoke", 8, 1.0f, 0.3f, 1.0f, 0.5f, 0.6f, 1.7f, kFull, 0.0f,
    0.8f, -0.2f, 0.5f, 0.0f, 0.0f, 0.03f, 120, 180, 90, 180, false, false,
    false, false},
    {"small_glowdot_sprite", 10, 0.7f, 0.2f, 2.0f, 1.0f, 0.08f, 0.03f,
    kFull, 0.0f, 0.4f, -1.2f, 0.0f, 0.0f, 0.0f, 0.0f, 160, 255, 120, 255,
    true, false, false, false},
};

inline constexpr ParticleLayer kMineBurst[] = {
    {"flash", 1, 0.12f, 0.0f, 0.0f, 0.0f, 0.7f, 0.35f, kFull, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 200, 120, 255, true, false, false,
    false},
    {"smallSharpPoint", 6, 0.25f, 0.08f, 4.0f, 1.5f, 0.07f, 0.02f, kFull,
    -1.5707963f, 1.2f, 3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 190, 100, 255,
    true, true, false, false},
};

inline constexpr ParticleLayer kTrapAura[] = {
    {"pentagram", 1, 0.6f, 0.0f, 0.0f, 0.0f, 0.8f, 1.1f, kFull, 0.0f,
    0.0f, 0.0f, 1.5f, 0.0f, 0.0f, 0.0f, 255, 120, 70, 220, true, false,
    false, false},
    {"FireGlowTwoColor", 1, 0.4f, 0.1f, 0.0f, 0.0f, 0.5f, 0.9f, kFull,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 150, 60, 200, true,
    false, false, false},
};

inline constexpr ParticleLayer kHealAura[] = {
    {"Heal", 1, 0.7f, 0.15f, 1.2f, 0.5f, 0.22f, 0.1f, kFull, -1.5707963f,
    0.6f, -0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 120, 255, 150, 255, true, false,
    false, false},
    {"HealBright", 1, 0.8f, 0.2f, 0.8f, 0.4f, 0.28f, 0.12f, kFull,
    -1.5707963f, 0.5f, -0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 150, 255, 170, 255,
    true, false, false, false},
    {"small_glowdot_sprite", 3, 0.6f, 0.15f, 1.2f, 0.4f, 0.1f, 0.04f,
    kFull, -1.5707963f, 0.4f, -0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 130, 255,
    160, 255, true, false, false, false},
};

inline constexpr ParticleLayer kShieldAura[] = {
    {"ShieldAlfa", 1, 1.0f, 0.0f, 0.0f, 0.0f, 0.9f, 1.35f, kFull, 0.0f,
    0.0f, 0.0f, 0.7f, 0.0f, 0.0f, 0.0f, 120, 210, 255, 170, true, false,
    true, false},
    {"Bolt", 2, 0.45f, 0.15f, 1.5f, 0.8f, 0.22f, 0.1f, kFull, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 170, 230, 255, 220, true, false, false,
    false},
    {"CFX2_T_Glow", 1, 0.7f, 0.1f, 0.0f, 0.0f, 0.6f, 1.0f, kFull, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 120, 200, 255, 160, true, false,
    false, false},
};

inline constexpr ParticleLayer kPowerBurst[] = {
    {"flash", 1, 0.16f, 0.0f, 0.0f, 0.0f, 1.15f, 0.55f, kFull, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 210, 240, 255, 255, true, false,
    false, false},
    {"smokyShockwave", 1, 0.32f, 0.04f, 0.0f, 0.0f, 0.5f, 2.8f, kFull,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 150, 215, 255, 220, true,
    false, true, false},
    {"CFX3_T_AuraRays", 1, 0.36f, 0.05f, 0.0f, 0.0f, 0.75f, 1.35f, kFull,
    0.0f, 0.0f, 0.0f, 1.2f, 0.0f, 0.0f, 0.0f, 170, 225, 255, 230, true,
    false, true, false},
    {"TeleportParticles", 6, 0.45f, 0.15f, 1.8f, 0.8f, 0.11f, 0.04f, kFull,
    -1.5707963f, 0.6f, -0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 190, 230, 255, 240,
    true, false, false, false},
};

inline constexpr ParticleLayer kPowerAura[] = {
    {"CFX3_T_AuraRays", 1, 0.8f, 0.1f, 0.0f, 0.0f, 0.8f, 1.2f, kFull,
    0.0f, 0.0f, 0.0f, 0.8f, 0.0f, 0.0f, 0.0f, 130, 200, 255, 190, true,
    false, true, false},
    {"TeleportParticles", 4, 0.6f, 0.2f, 1.4f, 0.7f, 0.12f, 0.05f, kFull,
    -1.5707963f, 0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 150, 220, 255, 230,
    true, false, false, false},
    {"CFX2_T_Glow", 1, 0.9f, 0.1f, 0.0f, 0.0f, 0.7f, 1.1f, kFull, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 140, 190, 255, 150, true, false,
    false, false},
};

inline constexpr ParticleLayer kNeutralAura[] = {
    {"Circle", 1, 0.9f, 0.1f, 0.0f, 0.0f, 0.8f, 1.6f, kFull, 0.0f, 0.0f,
    0.0f, 0.4f, 0.0f, 0.0f, 0.0f, 180, 240, 255, 180, true, false, true,
    false},
    {"CFX2_T_GlowSoft", 1, 0.8f, 0.1f, 0.0f, 0.0f, 0.9f, 1.3f, kFull,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 160, 230, 255, 190, true,
    false, false, false},
    {"FlashExplosion", 4, 0.5f, 0.15f, 2.0f, 1.0f, 0.1f, 0.04f, kFull,
    0.0f, 1.0f, -0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 210, 245, 255, 230, true,
    false, false, false},
};

inline constexpr ParticleLayer kAvatarAura[] = {
    {"EnergyBall2", 1, 0.6f, 0.1f, 0.0f, 0.0f, 1.0f, 1.4f, kFull, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 120, 40, 210, true, false,
    false, false},
    {"FireGlowTwoColor", 1, 0.5f, 0.1f, 0.0f, 0.0f, 0.9f, 1.2f, kFull,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 150, 60, 220, true,
    false, false, false},
    {"FireSmoke", 4, 0.9f, 0.25f, 1.0f, 0.5f, 0.5f, 1.3f, kFull, kUp,
    0.6f, -1.2f, 0.0f, 0.0f, 0.0f, 0.02f, 60, 45, 38, 170, false, false,
    false, false},
    {"CFX3_T_GlowStar", 3, 0.7f, 0.15f, 1.6f, 0.7f, 0.12f, 0.05f, kFull,
    -1.5707963f, 0.4f, -0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 180, 80, 230,
    true, false, false, false},
};

inline constexpr ParticleLayer kKickstartAura[] = {
    {"cloud_type_a", 1, 0.7f, 0.12f, 0.0f, 0.0f, 0.8f, 1.3f, kFull, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 210, 120, 200, true, false,
    false, false},
    {"GlowParticle", 4, 0.55f, 0.15f, 2.2f, 1.0f, 0.14f, 0.05f, kFull,
    -1.5707963f, 0.5f, -0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 230, 150, 245,
    true, false, false, false},
    {"Default-Particle", 1, 0.9f, 0.1f, 0.0f, 0.0f, 0.7f, 1.1f, kFull,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 220, 140, 170, true,
    false, false, false},
};

inline constexpr ParticleLayer kDematerialAura[] = {
    {"CFX3_T_IceSmoke", 4, 0.8f, 0.2f, 1.2f, 0.6f, 0.45f, 1.1f, kFull,
    -1.5707963f, 0.4f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 140, 210, 255, 190,
    true, false, false, false},
    {"CFX3_T_AuraRays", 1, 0.7f, 0.1f, 0.0f, 0.0f, 0.7f, 1.2f, kFull,
    0.0f, 0.0f, 0.0f, 0.6f, 0.0f, 0.0f, 0.0f, 150, 220, 255, 200, true,
    false, true, false},
    {"CFX3_T_RayStraight", 6, 0.4f, 0.12f, 3.0f, 1.5f, 0.08f, 0.03f, kFull,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 190, 235, 255, 240, true,
    true, false, false},
};

inline constexpr ParticleLayer kDoubleAura[] = {
    {"CFX3_T_GlowStar", 4, 0.6f, 0.15f, 1.8f, 0.8f, 0.14f, 0.05f, kFull,
    -1.5707963f, 0.4f, -0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 160, 60, 240,
    true, false, false, false},
    {"EnergyBall2", 1, 0.6f, 0.1f, 0.0f, 0.0f, 0.9f, 1.3f, kFull, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 130, 50, 190, true, false,
    false, false},
    {"Default-Particle", 1, 0.8f, 0.1f, 0.0f, 0.0f, 0.8f, 1.2f, kFull,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 150, 60, 170, true,
    false, false, false},
};

inline constexpr ParticleLayer kVampireAura[] = {
    {"Heal", 1, 0.6f, 0.15f, 1.2f, 0.5f, 0.2f, 0.09f, kFull, -1.5707963f,
    0.5f, -0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 80, 80, 240, true, false,
    false, false},
    {"Lines", 3, 0.5f, 0.15f, 2.0f, 0.8f, 0.12f, 0.04f, kFull,
    -1.5707963f, 0.4f, -0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 90, 90, 230,
    true, true, false, false},
    {"EnergyBall2", 1, 0.7f, 0.1f, 0.0f, 0.0f, 0.7f, 1.1f, kFull, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 70, 70, 180, true, false,
    false, false},
};

inline constexpr ParticleLayer kFirstAidAura[] = {
    {"cross_particle_1", 3, 0.7f, 0.2f, 1.4f, 0.6f, 0.16f, 0.07f, kFull,
    -1.5707963f, 0.4f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 120, 255, 150, 255,
    true, false, false, false},
    {"CFX2_T_Glow", 1, 0.9f, 0.1f, 0.0f, 0.0f, 0.8f, 1.2f, kFull, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 130, 255, 160, 160, true, false,
    false, false},
};

inline constexpr ParticleLayer kFistBurst[] = {
    {"flash", 1, 0.14f, 0.0f, 0.0f, 0.0f, 0.8f, 0.4f, kFull, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 230, 170, 255, true, false, false,
    false},
    {"MuzzlefireSnipe", 1, 0.2f, 0.03f, 0.0f, 0.0f, 0.9f, 0.3f, kFull,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 200, 120, 230, true,
    false, false, false},
    {"CFXM3_T_HollowCircle", 1, 0.35f, 0.05f, 0.0f, 0.0f, 0.5f, 2.2f, kFull,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 220, 150, 220, true,
    false, true, false},
    {"CFX2_T_Glow", 1, 0.3f, 0.05f, 0.0f, 0.0f, 0.6f, 0.9f, kFull, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 200, 120, 200, true, false,
    false, false},
    {"Fist3", 1, 0.4f, 0.05f, 0.0f, 0.0f, 0.7f, 0.6f, kFull, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 240, 200, 230, true, false, false,
    false},
};

inline constexpr ParticleLayer kDroidAura[] = {
    {"HollowCircle", 1, 0.9f, 0.1f, 0.0f, 0.0f, 0.8f, 1.5f, kFull, 0.0f,
    0.0f, 0.0f, 0.4f, 0.0f, 0.0f, 0.0f, 120, 255, 170, 190, true, false,
    true, false},
    {"Bolt", 2, 0.4f, 0.12f, 1.4f, 0.7f, 0.2f, 0.09f, kFull, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 160, 255, 190, 220, true, false, false,
    false},
    {"Default-Particle", 2, 0.7f, 0.2f, 1.2f, 0.6f, 0.5f, 0.9f, kFull, kUp,
    0.4f, -0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 130, 255, 180, 170, true, false,
    false, false},
};

inline constexpr ParticleLayer kJetAura[] = {
    {"FireGlowTwoColor", 2, 0.35f, 0.1f, 1.5f, 0.8f, 0.45f, 0.9f, kFull,
    1.5707963f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 150, 60, 240,
    true, false, false, false},
    {"FireSmoke", 3, 0.7f, 0.2f, 1.0f, 0.5f, 0.4f, 0.9f, kFull,
    1.5707963f, 0.6f, 0.4f, 0.0f, 0.0f, 0.0f, 0.02f, 60, 50, 45, 170,
    false, false, false, false},
    {"ParticleStretched2", 4, 0.4f, 0.12f, 2.2f, 1.0f, 0.14f, 0.05f, kFull,
    1.5707963f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 200, 120, 230,
    true, true, false, false},
};

inline constexpr ParticleLayer kFireTrapAura[] = {
    {"pentagram", 1, 0.6f, 0.0f, 0.0f, 0.0f, 0.9f, 1.3f, kFull, 0.0f,
    0.0f, 0.0f, 1.8f, 0.0f, 0.0f, 0.0f, 255, 110, 50, 230, true, false,
    false, false},
    {"FireGlowTwoColor", 2, 0.45f, 0.1f, 0.6f, 0.3f, 0.5f, 0.9f, kFull,
    -1.5707963f, 0.5f, -0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 150, 60, 230,
    true, false, false, false},
    {"smallSharpPoint", 3, 0.4f, 0.12f, 2.5f, 1.0f, 0.08f, 0.03f, kFull,
    -1.5707963f, 0.8f, -0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 190, 90, 240,
    true, true, false, false},
};

inline constexpr ParticleLayer kChemAura[] = {
    {"EnergyBall2", 1, 0.8f, 0.1f, 0.0f, 0.0f, 0.9f, 1.3f, kFull, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 120, 220, 90, 180, true, false,
    false, false},
    {"dirtySmoke", 5, 0.9f, 0.25f, 0.9f, 0.4f, 0.5f, 1.3f, kFull, kUp,
    0.6f, -0.4f, 0.0f, 0.0f, 0.0f, 0.02f, 110, 170, 80, 160, false, false,
    false, false},
};

inline constexpr ParticleLayer kSprayAura[] = {
    {"FlashExplosion", 4, 0.45f, 0.15f, 2.2f, 1.0f, 0.12f, 0.05f, kFull,
    0.0f, 1.2f, -0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 255, 120, 90, 220, true,
    true, false, false},
    {"detailSmoke", 3, 0.7f, 0.2f, 0.8f, 0.4f, 0.4f, 1.0f, kFull, 0.0f,
    0.8f, -0.8f, 0.0f, 0.0f, 0.0f, 0.02f, 120, 85, 70, 160, false, false,
    false, false},
};

inline constexpr ParticleLayer kSilenceAura[] = {
    {"CFX2_T_GlowSoft", 1, 0.8f, 0.1f, 0.0f, 0.0f, 0.8f, 1.2f, kFull,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 140, 255, 180, 180, true,
    false, false, false},
    {"small_glowdot_sprite", 3, 0.6f, 0.15f, 1.2f, 0.5f, 0.1f, 0.04f,
    kFull, -1.5707963f, 0.4f, -0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 140, 255,
    170, 230, true, false, false, false},
};

} // namespace particle_detail

inline constexpr GadgetParticles kGadgetParticles[] = {
    // RubyBlade
    {std::span<const ParticleLayer>{particle_detail::kSlashSparks}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kSlashSparks}, std::span<const ParticleLayer>{particle_detail::kEmpty}, 0.0f},
    // PowerBattery
    {std::span<const ParticleLayer>{particle_detail::kPowerBurst}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kPowerAura}, 26.0f},
    // MagneticNozzle
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kShieldAura}, 22.0f},
    // FragGrenade
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kExplosionBig},
     std::span<const ParticleLayer>{particle_detail::kMetalImpact}, std::span<const ParticleLayer>{particle_detail::kEmpty}, 0.0f},
    // StaticShield
    {std::span<const ParticleLayer>{particle_detail::kFlashExplosion}, std::span<const ParticleLayer>{particle_detail::kFlashExplosion},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kShieldAura}, 30.0f},
    // Kickstart
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kKickstartAura}, 24.0f},
    // RocketLaunch
    {std::span<const ParticleLayer>{particle_detail::kMuzzle}, std::span<const ParticleLayer>{particle_detail::kExplosionSmall},
     std::span<const ParticleLayer>{particle_detail::kMetalImpact}, std::span<const ParticleLayer>{particle_detail::kEmpty}, 0.0f},
    // ProtectiveDroid
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kDroidAura}, 22.0f},
    // FirstAid
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kFirstAidAura}, 20.0f},
    // Injection
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kHealAura}, 24.0f},
    // RocketSalvo
    {std::span<const ParticleLayer>{particle_detail::kMuzzle}, std::span<const ParticleLayer>{particle_detail::kExplosionSmall},
     std::span<const ParticleLayer>{particle_detail::kMetalImpact}, std::span<const ParticleLayer>{particle_detail::kEmpty}, 0.0f},
    // TurboPack
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kJetAura}, 28.0f},
    // Toughness
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kShieldAura}, 22.0f},
    // ConcussionGrenade
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kFlashExplosion},
     std::span<const ParticleLayer>{particle_detail::kMetalImpact}, std::span<const ParticleLayer>{particle_detail::kEmpty}, 0.0f},
    // FragMine
    {std::span<const ParticleLayer>{particle_detail::kMineBurst}, std::span<const ParticleLayer>{particle_detail::kExplosionBig},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kEmpty}, 0.0f},
    // Neutralizer
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kNeutralAura}, 26.0f},
    // Boomerang
    {std::span<const ParticleLayer>{particle_detail::kMuzzle}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kMetalImpact}, std::span<const ParticleLayer>{particle_detail::kEmpty}, 0.0f},
    // Trap
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kMineBurst}, std::span<const ParticleLayer>{particle_detail::kTrapAura}, 20.0f},
    // DoubleShotSystem
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kDoubleAura}, 22.0f},
    // Sprayer
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kSprayerBurst},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kSprayAura}, 18.0f},
    // Chemicals
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kChemBurst},
     std::span<const ParticleLayer>{particle_detail::kChemBurst}, std::span<const ParticleLayer>{particle_detail::kChemAura}, 18.0f},
    // Bloodletter
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kVampireAura}, 22.0f},
    // Chakram
    {std::span<const ParticleLayer>{particle_detail::kMuzzle}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kMetalImpact}, std::span<const ParticleLayer>{particle_detail::kEmpty}, 0.0f},
    // Dematerializator
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kDematerialAura}, 26.0f},
    // Mutator
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kDoubleAura}, 22.0f},
    // DemonicFace
    {std::span<const ParticleLayer>{particle_detail::kFireMuzzle}, std::span<const ParticleLayer>{particle_detail::kFireExplosion},
     std::span<const ParticleLayer>{particle_detail::kMetalImpact}, std::span<const ParticleLayer>{particle_detail::kEmpty}, 0.0f},
    // FireTrap
    {std::span<const ParticleLayer>{particle_detail::kFireMuzzle}, std::span<const ParticleLayer>{particle_detail::kFireExplosion},
     std::span<const ParticleLayer>{particle_detail::kMetalImpact}, std::span<const ParticleLayer>{particle_detail::kFireTrapAura}, 22.0f},
    // Avatar
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kAvatarAura}, 30.0f},
    // HyperAccelerator
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kKickstartAura}, 30.0f},
    // ArmCyberProsthesis
    {std::span<const ParticleLayer>{particle_detail::kFistBurst}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kMetalImpact}, std::span<const ParticleLayer>{particle_detail::kEmpty}, 0.0f},
    // Inhibitor
    {std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kEmpty},
     std::span<const ParticleLayer>{particle_detail::kEmpty}, std::span<const ParticleLayer>{particle_detail::kSilenceAura}, 22.0f},
};

inline constexpr std::size_t kGadgetParticleCount =
    static_cast<std::size_t>(core::GadgetType::Count);
static_assert(std::size(kGadgetParticles) == kGadgetParticleCount);

[[nodiscard]] inline const GadgetParticles& gadget_particles(
    core::GadgetType type) {
    const std::size_t index = static_cast<std::size_t>(type);
    return index < kGadgetParticleCount ? kGadgetParticles[index]
                                        : kGadgetParticles[0];
}

} // namespace nsync::client
