#pragma once

#include "core/animation.h"
#include "core/atlas.h"
#include "core/entity.h"
#include "core/geometry.h"
#include "core/hero_rig.h"
#include "core/world.h"
#include "client/particle_effects.h"
#include "shared/resource.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nsync::client {

inline constexpr float kWorldPixelsPerUnit = 100.0f;
inline constexpr float kWorldCameraMaxScreenHeight = 900.0f;

enum class HeroArtMode : std::uint8_t {
    Auto = 0,
    Placeholder,
    Original,
};

[[nodiscard]] bool parse_hero_art_mode(
    std::string_view value,
    HeroArtMode& mode);
[[nodiscard]] const char* hero_art_mode_name(HeroArtMode mode);

struct Camera {
    float x = 0.0f;
    float y = 0.0f;
    float pixels_per_unit = kWorldPixelsPerUnit;
};

class WorldRenderer {
public:
    bool initialize(const shared::ResourceManager& resources);
    bool reload_visuals(const shared::ResourceManager& resources);
    bool load_map_art(
        const shared::ResourceManager& resources,
        std::string_view map_id);
    void clear_map_art();
    [[nodiscard]] bool map_art_camera_bounds(core::AABB& bounds) const;
    void set_hero_art_mode(HeroArtMode mode) { hero_art_mode_ = mode; }
    void set_blood_enabled(bool enabled) { blood_enabled_ = enabled; }
    void reset_player_visuals();
    // Presentation-only correction blending: `offset` is
    // (old simulated position - new simulated position) of a player whose
    // state was corrected by rollback/snapshot at the current tick.
    void add_position_correction(core::EntityId player, core::Vec2 offset);
    bool run_original_hero_benchmark(
        int players,
        int frames,
        std::string* error = nullptr);
    void shutdown();
    [[nodiscard]] const std::string& atlas_image_path() const {
        return atlas_.image;
    }
    [[nodiscard]] const std::string& original_image_path() const {
        return original_image_path_;
    }
    [[nodiscard]] bool original_ready() const {
        return original_ready_;
    }
    void draw(
        bgfx::ProgramHandle program,
        bgfx::UniformHandle sampler,
        const core::World& world,
        core::EntityId local_player,
        const Camera& camera,
        int width,
        int height,
        float delta_seconds);

private:
    enum class EffectKind : std::uint8_t {
        Muzzle,
        Impact,
        Hit,
        Explosion,
        Tracer,
        Pulse,
        Slash,
        Streak,
    };

    struct EffectVisual {
        EffectKind kind = EffectKind::Impact;
        core::Vec2 position{};
        core::Vec2 direction{};
        float age_seconds = 0.0f;
        float life_seconds = 0.0f;
        float radius = 0.0f;
        std::uint8_t r = 255;
        std::uint8_t g = 255;
        std::uint8_t b = 255;
        std::uint8_t a = 255;
    };

    struct ProjectileVisual {
        std::vector<core::Vec2> trail;
        core::Vec2 last_position{};
        core::Vec2 offset{};
        std::uint32_t start_tick = 0;
        float explosion_radius = 0.0f;
        float trail_accum = 0.0f;
        std::uint8_t explosive = 0;
        std::uint8_t gadget = 0;
    };

    struct ParticleVisual {
        const ParticleLayer* layer = nullptr;
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float age = 0.0f;
        float life = 0.5f;
        float delay = 0.0f;
        float rotation = 0.0f;
        float spin = 0.0f;
        float size_start = 0.2f;
        float size_end = 0.2f;
        std::uint8_t r = 255;
        std::uint8_t g = 255;
        std::uint8_t b = 255;
        std::uint8_t a = 255;
        core::EntityId follow{};
        bool follows = false;
        bool additive = true;
        bool stretch = false;
        bool ring = false;
    };

    struct MapArtVertex {
        float x = 0.0f;
        float y = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        std::uint8_t r = 255;
        std::uint8_t g = 255;
        std::uint8_t b = 255;
        std::uint8_t a = 255;
    };

    struct MapArtLayer {
        int order = 0;
        float parallax = 0.0f;
        bool additive = false;
        std::vector<MapArtVertex> vertices;
    };

    struct PlayerVisualState {
        int last_health = -1;
        int last_shield = -1;
        int last_ammo = -1;
        std::uint8_t last_weapon = 0;
        bool last_alive = false;
        float hurt_seconds = 0.0f;
        float shoot_seconds = 0.0f;
        float state_seconds = 0.0f;
        core::AnimationState last_state = core::AnimationState::Idle;
        float render_x = 0.0f;
        float render_y = 0.0f;
        bool has_render_position = false;
        core::Vec2 muzzle{};
        bool has_muzzle = false;
        std::uint32_t interp_tick = 0;
        float interp_from_x = 0.0f;
        float interp_from_y = 0.0f;
        float interp_to_x = 0.0f;
        float interp_to_y = 0.0f;
        bool has_interp = false;
        float correction_x = 0.0f;
        float correction_y = 0.0f;
        std::array<std::uint16_t, core::kGadgetSlots>
            last_gadget_cooldowns{};
        std::array<float, core::kGadgetSlots> aura_accum{};
        bool has_gadget_state = false;
    };

    void add_effect(const EffectVisual& effect);
    void add_shake(float strength);
    void clear_effects();
    void load_crystal_texture(const shared::ResourceManager& resources);
    void load_bag_atlas(const shared::ResourceManager& resources);
    void load_health_bar_atlas(const shared::ResourceManager& resources);
    void load_particle_atlas(const shared::ResourceManager& resources);
    void load_projectile_atlas(const shared::ResourceManager& resources);
    void spawn_particles(
        std::span<const ParticleLayer> layers,
        float x,
        float y,
        float direction_angle,
        core::EntityId owner);
    void spawn_gadget_particles(
        core::GadgetType gadget,
        bool explode,
        float x,
        float y,
        float direction_angle,
        core::EntityId owner);
    void update_particles(float delta_seconds, const core::World& world);
    [[nodiscard]] float next_particle_random();

    bgfx::TextureHandle white_texture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle crystal_texture_ = BGFX_INVALID_HANDLE;
    bool crystal_ready_ = false;
    bgfx::TextureHandle bag_texture_ = BGFX_INVALID_HANDLE;
    core::Atlas bag_atlas_;
    bool bag_ready_ = false;
    bgfx::TextureHandle health_bar_texture_ = BGFX_INVALID_HANDLE;
    core::Atlas health_bar_atlas_;
    bool health_bar_ready_ = false;
    bgfx::TextureHandle atlas_texture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle original_texture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle map_art_texture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle particle_texture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle projectile_texture_ = BGFX_INVALID_HANDLE;
    core::Atlas projectile_atlas_;
    bool projectile_atlas_ready_ = false;
    std::string map_art_id_;
    std::vector<MapArtLayer> map_art_layers_;
    bool map_art_ready_ = false;
    bool map_art_has_camera_bounds_ = false;
    core::AABB map_art_camera_bounds_{};
    bool map_art_reference_valid_ = false;
    float map_art_reference_x_ = 0.0f;
    float map_art_reference_y_ = 0.0f;
    core::Atlas atlas_;
    core::AnimationSet animations_;
    core::Atlas particle_atlas_;
    bool particle_atlas_ready_ = false;
    std::vector<ParticleVisual> particles_;
    std::uint32_t particle_rng_ = 0x51A7E3D9U;
    core::Atlas original_atlas_;
    core::HeroRigSet hero_rigs_;
    core::HeroPoseSet hero_poses_;
    std::string original_image_path_;
    bool atlas_ready_ = false;
    bool original_ready_ = false;
    HeroArtMode hero_art_mode_ = HeroArtMode::Auto;
    bool blood_enabled_ = true;
    std::unordered_map<std::uint32_t, PlayerVisualState> visuals_;
    std::unordered_map<std::uint32_t, ProjectileVisual> projectile_visuals_;
    std::vector<EffectVisual> effects_;
    float presentation_seconds_ = 0.0f;
    // Global interpolation window, like ClientGame.Update in the original.
    std::uint32_t interp_frame_tick_ = 0;
    bool has_interp_frame_ = false;
    float interp_window_start_ = 0.0f;
    float interp_window_end_ = 0.0f;
    float interp_elapsed_ = 0.0f;
    float last_position_update_seconds_ = 0.0f;
    float shake_strength_ = 0.0f;
    float recoil_x_ = 0.0f;
    float recoil_y_ = 0.0f;
};

} // namespace nsync::client
