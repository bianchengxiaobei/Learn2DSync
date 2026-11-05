#include "client/world_renderer.h"

#include "client/image_loader.h"
#include "core/aim.h"
#include "core/content.h"
#include "core/heroes.h"
#include "core/json.h"
#include "shared/log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nsync::client {

bool parse_hero_art_mode(
    std::string_view value,
    HeroArtMode& mode) {
    if (value == "auto") {
        mode = HeroArtMode::Auto;
        return true;
    }
    if (value == "placeholder") {
        mode = HeroArtMode::Placeholder;
        return true;
    }
    if (value == "original") {
        mode = HeroArtMode::Original;
        return true;
    }
    return false;
}

const char* hero_art_mode_name(HeroArtMode mode) {
    switch (mode) {
        case HeroArtMode::Auto: return "auto";
        case HeroArtMode::Placeholder: return "placeholder";
        case HeroArtMode::Original: return "original";
    }
    return "auto";
}

namespace {

// Debug: NSYNC_RENDER_TRACE=<file> writes one CSV line per drawn player:
// time,frame_dt,sim_tick,slot,local,sim_x,render_x,correction_x,camera_x,vx
std::FILE* render_trace_file() {
    static std::FILE* file = [] {
        const char* path = std::getenv("NSYNC_RENDER_TRACE");
        if (path == nullptr || *path == '\0') {
            return static_cast<std::FILE*>(nullptr);
        }
        std::FILE* opened = nullptr;
        if (fopen_s(&opened, path, "w") != 0) {
            return static_cast<std::FILE*>(nullptr);
        }
        return opened;
    }();
    return file;
}

struct SpriteVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
};

struct Color {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
};

constexpr Color kBackground{13, 22, 34, 255};
constexpr Color kSolid{52, 66, 86, 255};
constexpr Color kPlatform{91, 120, 148, 255};
constexpr Color kJumpPad{255, 166, 74, 255};
constexpr Color kSpawn{255, 209, 102, 90};
constexpr Color kProjectile{255, 221, 92, 255};
constexpr Color kRocket{255, 137, 61, 255};
constexpr Color kHealthBack{20, 25, 34, 255};
constexpr Color kHealthFill{86, 216, 133, 255};
constexpr Color kShieldFill{89, 202, 255, 255};
constexpr Color kTeamBlue{71, 159, 255, 255};
constexpr Color kTeamRed{255, 91, 99, 255};
constexpr Color kObjectiveNeutral{255, 255, 255, 38};
constexpr Color kObjectiveBlue{71, 159, 255, 64};
constexpr Color kObjectiveRed{255, 91, 99, 64};
constexpr Color kCrystal{120, 230, 255, 255};
constexpr Color kDepotNeutral{255, 255, 255, 30};
constexpr Color kDepotBlue{71, 159, 255, 54};
constexpr Color kDepotRed{255, 91, 99, 54};
constexpr Color kMuzzleCore{255, 248, 205, 255};
constexpr Color kMuzzleRay{255, 196, 92, 255};
constexpr Color kImpactRay{255, 214, 130, 255};
constexpr Color kExplosionCore{255, 236, 170, 255};
constexpr Color kExplosionRing{255, 154, 66, 255};
constexpr Color kHitHealth{255, 91, 99, 255};
constexpr Color kHitShield{89, 202, 255, 255};
constexpr float kPi = 3.14159265358979323846f;

Color gadget_color(core::GadgetType type) {
    switch (type) {
        case core::GadgetType::RubyBlade:
            return Color{255, 84, 120, 255};
        case core::GadgetType::PowerBattery:
            return Color{120, 232, 255, 255};
        case core::GadgetType::MagneticNozzle:
            return Color{255, 182, 84, 255};
        case core::GadgetType::FragGrenade:
            return Color{146, 232, 120, 255};
        case core::GadgetType::StaticShield:
            return Color{92, 202, 255, 255};
        case core::GadgetType::Kickstart:
            return Color{120, 182, 255, 255};
        case core::GadgetType::RocketLaunch:
            return Color{255, 150, 70, 255};
        case core::GadgetType::ProtectiveDroid:
            return Color{94, 232, 190, 255};
        case core::GadgetType::FirstAid:
            return Color{126, 255, 146, 255};
        case core::GadgetType::Injection:
            return Color{176, 255, 120, 255};
        case core::GadgetType::RocketSalvo:
            return Color{255, 122, 74, 255};
        case core::GadgetType::TurboPack:
            return Color{110, 212, 255, 255};
        case core::GadgetType::Toughness:
            return Color{255, 222, 122, 255};
        case core::GadgetType::ConcussionGrenade:
            return Color{202, 142, 255, 255};
        case core::GadgetType::FragMine:
            return Color{255, 212, 92, 255};
        case core::GadgetType::Neutralizer:
            return Color{236, 246, 255, 255};
        case core::GadgetType::Boomerang:
            return Color{112, 240, 222, 255};
        case core::GadgetType::Trap:
            return Color{255, 232, 112, 255};
        case core::GadgetType::DoubleShotSystem:
            return Color{255, 172, 92, 255};
        case core::GadgetType::Sprayer:
            return Color{152, 255, 152, 255};
        case core::GadgetType::Chemicals:
            return Color{172, 255, 92, 255};
        case core::GadgetType::Bloodletter:
            return Color{255, 92, 112, 255};
        case core::GadgetType::Chakram:
            return Color{255, 202, 92, 255};
        case core::GadgetType::Dematerializator:
            return Color{182, 162, 255, 255};
        case core::GadgetType::Mutator:
            return Color{212, 132, 255, 255};
        case core::GadgetType::DemonicFace:
            return Color{255, 112, 202, 255};
        case core::GadgetType::FireTrap:
            return Color{255, 122, 62, 255};
        case core::GadgetType::Avatar:
            return Color{255, 92, 82, 255};
        case core::GadgetType::HyperAccelerator:
            return Color{92, 172, 255, 255};
        case core::GadgetType::ArmCyberProsthesis:
            return Color{242, 122, 255, 255};
        case core::GadgetType::Inhibitor:
            return Color{202, 232, 255, 255};
        default:
            return Color{255, 255, 255, 255};
    }
}

Color projectile_color(const core::Projectile& projectile) {
    if (projectile.gadget != 0 &&
        projectile.gadget <=
            static_cast<std::uint8_t>(core::GadgetType::Count)) {
        return gadget_color(
            static_cast<core::GadgetType>(projectile.gadget - 1));
    }
    return projectile.explosive != 0
        ? kRocket
        : kProjectile;
}

Color gadget_tag_color(std::uint8_t tag) {
    if (tag != 0 &&
        tag <= static_cast<std::uint8_t>(core::GadgetType::Count)) {
        return gadget_color(
            static_cast<core::GadgetType>(tag - 1));
    }
    return kExplosionRing;
}

bool gadget_projectile_is_round(std::uint8_t tag) {
    if (tag == 0 ||
        tag > static_cast<std::uint8_t>(core::GadgetType::Count)) {
        return false;
    }
    switch (static_cast<core::GadgetType>(tag - 1)) {
        case core::GadgetType::FragGrenade:
        case core::GadgetType::StaticShield:
        case core::GadgetType::ConcussionGrenade:
        case core::GadgetType::FragMine:
        case core::GadgetType::Trap:
        case core::GadgetType::Sprayer:
        case core::GadgetType::Chemicals:
        case core::GadgetType::DemonicFace:
        case core::GadgetType::FireTrap:
        case core::GadgetType::ArmCyberProsthesis:
            return true;
        default:
            return false;
    }
}

bool gadget_projectile_spins(std::uint8_t tag) {
    if (tag == 0 ||
        tag > static_cast<std::uint8_t>(core::GadgetType::Count)) {
        return false;
    }
    const core::GadgetType type =
        static_cast<core::GadgetType>(tag - 1);
    return type == core::GadgetType::Boomerang ||
           type == core::GadgetType::Chakram;
}

enum class GadgetVisualKind : std::uint8_t {
    Projectile,
    Melee,
    Dash,
    Heal,
    Cleanse,
    Buff,
    Immunity,
};

GadgetVisualKind gadget_visual_kind(core::GadgetType type) {
    if (core::gadget_is_projectile(type)) {
        return GadgetVisualKind::Projectile;
    }
    switch (type) {
        case core::GadgetType::RubyBlade:
            return GadgetVisualKind::Melee;
        case core::GadgetType::Kickstart:
        case core::GadgetType::TurboPack:
        case core::GadgetType::HyperAccelerator:
            return GadgetVisualKind::Dash;
        case core::GadgetType::FirstAid:
        case core::GadgetType::Injection:
            return GadgetVisualKind::Heal;
        case core::GadgetType::Neutralizer:
        case core::GadgetType::Inhibitor:
            return GadgetVisualKind::Cleanse;
        case core::GadgetType::PowerBattery:
        case core::GadgetType::Dematerializator:
        case core::GadgetType::Toughness:
            return GadgetVisualKind::Immunity;
        default:
            return GadgetVisualKind::Buff;
    }
}

float to_float(core::Fix32 value) {
    return static_cast<float>(value.raw()) / static_cast<float>(core::Fix32::kOne);
}

core::Fix32 from_float(float value) {
    return core::Fix32::from_raw(
        static_cast<std::int32_t>(
            value * static_cast<float>(core::Fix32::kOne)));
}

Color fade(Color color, float factor) {
    return Color{
        static_cast<std::uint8_t>(color.r * factor),
        static_cast<std::uint8_t>(color.g * factor),
        static_cast<std::uint8_t>(color.b * factor),
        color.a};
}

Color with_alpha(Color color, float factor) {
    return Color{
        color.r,
        color.g,
        color.b,
        static_cast<std::uint8_t>(
            std::clamp(
                static_cast<float>(color.a) * factor,
                0.0f,
                255.0f))};
}

bgfx::VertexLayout sprite_layout() {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    return layout;
}

class QuadBatch {
public:
    QuadBatch(
        const Camera& camera,
        int width,
        int height,
        std::vector<SpriteVertex>& vertices)
        : camera_(camera), width_(width), height_(height), vertices_(vertices) {}

    void rect(const core::AABB& box, Color color) {
        sprite(box, 0.0f, 0.0f, 1.0f, 1.0f, color);
    }

    void sprite(
        const core::AABB& box,
        float u0,
        float v0,
        float u1,
        float v1,
        Color color) {
        const float left =
            (to_float(box.min.x) - camera_.x) * camera_.pixels_per_unit +
            static_cast<float>(width_) * 0.5f;
        const float right =
            (to_float(box.max.x) - camera_.x) * camera_.pixels_per_unit +
            static_cast<float>(width_) * 0.5f;
        const float top =
            (to_float(box.min.y) - camera_.y) * camera_.pixels_per_unit +
            static_cast<float>(height_) * 0.5f;
        const float bottom =
            (to_float(box.max.y) - camera_.y) * camera_.pixels_per_unit +
            static_cast<float>(height_) * 0.5f;

        if (right < 0.0f || left > static_cast<float>(width_) ||
            bottom < 0.0f || top > static_cast<float>(height_)) {
            return;
        }

        vertex(left, top, u0, v0, color);
        vertex(right, top, u1, v0, color);
        vertex(right, bottom, u1, v1, color);
        vertex(left, top, u0, v0, color);
        vertex(right, bottom, u1, v1, color);
        vertex(left, bottom, u0, v1, color);
    }

    void textured_quad(
        const std::array<core::HeroPoint, 4>& corners,
        const std::array<core::HeroPoint, 4>& uvs,
        Color color) {
        std::array<float, 4> xs{};
        std::array<float, 4> ys{};
        float min_x = 1.0e30f;
        float max_x = -1.0e30f;
        float min_y = 1.0e30f;
        float max_y = -1.0e30f;
        for (std::size_t i = 0; i < corners.size(); ++i) {
            xs[i] =
                (corners[i].x - camera_.x) * camera_.pixels_per_unit +
                static_cast<float>(width_) * 0.5f;
            ys[i] =
                (corners[i].y - camera_.y) * camera_.pixels_per_unit +
                static_cast<float>(height_) * 0.5f;
            min_x = std::min(min_x, xs[i]);
            max_x = std::max(max_x, xs[i]);
            min_y = std::min(min_y, ys[i]);
            max_y = std::max(max_y, ys[i]);
        }
        if (max_x < 0.0f ||
            min_x > static_cast<float>(width_) ||
            max_y < 0.0f ||
            min_y > static_cast<float>(height_)) {
            return;
        }
        vertex(xs[0], ys[0], uvs[0].x, uvs[0].y, color);
        vertex(xs[1], ys[1], uvs[1].x, uvs[1].y, color);
        vertex(xs[2], ys[2], uvs[2].x, uvs[2].y, color);
        vertex(xs[0], ys[0], uvs[0].x, uvs[0].y, color);
        vertex(xs[2], ys[2], uvs[2].x, uvs[2].y, color);
        vertex(xs[3], ys[3], uvs[3].x, uvs[3].y, color);
    }

    void disc(
        const core::Vec2& center,
        float radius_world,
        int segments,
        Color color) {
        const float cx =
            (to_float(center.x) - camera_.x) * camera_.pixels_per_unit +
            static_cast<float>(width_) * 0.5f;
        const float cy =
            (to_float(center.y) - camera_.y) * camera_.pixels_per_unit +
            static_cast<float>(height_) * 0.5f;
        const float radius = radius_world * camera_.pixels_per_unit;
        if (radius <= 0.0f ||
            cx + radius < 0.0f ||
            cx - radius > static_cast<float>(width_) ||
            cy + radius < 0.0f ||
            cy - radius > static_cast<float>(height_)) {
            return;
        }
        const int count = std::max(6, segments);
        for (int i = 0; i < count; ++i) {
            const float a0 =
                static_cast<float>(i) *
                2.0f * kPi /
                static_cast<float>(count);
            const float a1 =
                static_cast<float>(i + 1) *
                2.0f * kPi /
                static_cast<float>(count);
            vertex(cx, cy, 0.5f, 0.5f, color);
            vertex(
                cx + std::cos(a0) * radius,
                cy + std::sin(a0) * radius,
                0.5f,
                0.5f,
                color);
            vertex(
                cx + std::cos(a1) * radius,
                cy + std::sin(a1) * radius,
                0.5f,
                0.5f,
                color);
        }
    }

    void segment(
        const core::Vec2& from,
        const core::Vec2& to,
        float width_pixels,
        Color color) {
        const float ax =
            (to_float(from.x) - camera_.x) * camera_.pixels_per_unit +
            static_cast<float>(width_) * 0.5f;
        const float ay =
            (to_float(from.y) - camera_.y) * camera_.pixels_per_unit +
            static_cast<float>(height_) * 0.5f;
        const float bx =
            (to_float(to.x) - camera_.x) * camera_.pixels_per_unit +
            static_cast<float>(width_) * 0.5f;
        const float by =
            (to_float(to.y) - camera_.y) * camera_.pixels_per_unit +
            static_cast<float>(height_) * 0.5f;
        const float dx = bx - ax;
        const float dy = by - ay;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.01f) {
            return;
        }
        const float half_width = std::max(0.5f, width_pixels * 0.5f);
        const float nx = -dy / length * half_width;
        const float ny = dx / length * half_width;

        const float min_x = std::min(ax, bx) - half_width;
        const float max_x = std::max(ax, bx) + half_width;
        const float min_y = std::min(ay, by) - half_width;
        const float max_y = std::max(ay, by) + half_width;
        if (max_x < 0.0f || min_x > static_cast<float>(width_) ||
            max_y < 0.0f || min_y > static_cast<float>(height_)) {
            return;
        }

        const float a1x = ax + nx;
        const float a1y = ay + ny;
        const float b1x = bx + nx;
        const float b1y = by + ny;
        const float b2x = bx - nx;
        const float b2y = by - ny;
        const float a2x = ax - nx;
        const float a2y = ay - ny;
        vertex(a1x, a1y, 0.0f, 0.0f, color);
        vertex(b1x, b1y, 1.0f, 0.0f, color);
        vertex(b2x, b2y, 1.0f, 1.0f, color);
        vertex(a1x, a1y, 0.0f, 0.0f, color);
        vertex(b2x, b2y, 1.0f, 1.0f, color);
        vertex(a2x, a2y, 0.0f, 1.0f, color);
    }

private:
    void vertex(float x, float y, float u, float v, Color color) {
        SpriteVertex value;
        value.x = x;
        value.y = y;
        value.u = u;
        value.v = v;
        value.r = color.r;
        value.g = color.g;
        value.b = color.b;
        value.a = color.a;
        vertices_.push_back(value);
    }

    const Camera& camera_;
    int width_ = 0;
    int height_ = 0;
    std::vector<SpriteVertex>& vertices_;
};

float lerp_float(float from, float to, float amount) {
    return from + (to - from) * amount;
}

core::HeroPoint rotate_point(
    const core::HeroPoint& point,
    const core::HeroPoint& pivot,
    float radians) {
    const float dx = point.x - pivot.x;
    const float dy = point.y - pivot.y;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return core::HeroPoint{
        pivot.x + dx * cosine - dy * sine,
        pivot.y + dx * sine + dy * cosine};
}

core::Vec2 fallback_muzzle(
    core::Vec2 position,
    core::Vec2 aim,
    std::int8_t facing) {
    (void)aim;
    const core::Fix32 direction =
        facing < 0 ? -core::Fix32::one() : core::Fix32::one();
    return position +
        core::Vec2{
            direction * core::Fix32::from_ratio(9, 20),
            -core::Fix32::from_ratio(7, 20)};
}

struct RigPoseSample {
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float rotation = 0.0f;
};

const core::HeroPoseTrack* find_pose_track(
    const core::HeroPoseState& state,
    std::string_view role) {
    for (const core::HeroPoseTrack& track : state.tracks) {
        if (track.role == role) {
            return &track;
        }
    }
    return nullptr;
}

RigPoseSample sample_pose(
    const core::HeroPoseState& state,
    const core::HeroPoseRecoil& recoil,
    std::string_view role,
    float seconds,
    float recoil_pulse) {
    RigPoseSample sample;
    if (const core::HeroPoseTrack* track = find_pose_track(state, role);
        track != nullptr) {
        const float angle =
            2.0f * kPi * track->frequency * seconds +
            track->phase;
        const float wave = std::sin(angle);
        sample.offset_x = track->offset_x + track->sway_x * wave;
        sample.offset_y = track->offset_y + track->sway_y * wave;
        sample.rotation =
            (track->rotation + track->sway_rotation * wave) *
            kPi /
            180.0f;
    }
    if (role == "weapon" && recoil_pulse > 0.0f) {
        sample.offset_x += recoil.offset_x * recoil_pulse;
        sample.offset_y += recoil.offset_y * recoil_pulse;
        sample.rotation +=
            recoil.rotation_deg * recoil_pulse * kPi / 180.0f;
    }
    return sample;
}

void draw_rig_piece(
    QuadBatch& batch,
    const core::Atlas& atlas,
    const core::HeroRig& rig,
    const core::HeroPoseState& pose,
    const core::HeroPoseRecoil& recoil,
    std::string_view role,
    std::string_view sprite,
    const std::array<core::HeroPoint, 4>& quad,
    const std::array<core::HeroPoint, 4>& local_uv,
    const core::HeroPoint& pivot,
    float seconds,
    float recoil_pulse,
    float extra_rotation,
    float root_rotation,
    float root_offset_x,
    float root_offset_y,
    float origin_x,
    float origin_y,
    bool flip,
    std::uint8_t alpha,
    QuadBatch* flash_batch,
    std::uint8_t flash_alpha) {
    const core::AtlasFrame* frame = atlas.find(sprite);
    if (frame == nullptr) {
        return;
    }
    const RigPoseSample sample = sample_pose(
        pose,
        recoil,
        role,
        seconds,
        recoil_pulse);
    const float u0 =
        static_cast<float>(frame->x) /
        static_cast<float>(atlas.width);
    const float v0 =
        static_cast<float>(frame->y) /
        static_cast<float>(atlas.height);
    const float u1 =
        static_cast<float>(frame->x + frame->width) /
        static_cast<float>(atlas.width);
    const float v1 =
        static_cast<float>(frame->y + frame->height) /
        static_cast<float>(atlas.height);

    std::array<core::HeroPoint, 4> corners{};
    std::array<core::HeroPoint, 4> uvs{};
    const core::HeroPoint root_pivot = rig.root_pivot;
    for (std::size_t i = 0; i < 4; ++i) {
        core::HeroPoint point =
            rotate_point(quad[i], pivot, sample.rotation + extra_rotation);
        point.x += sample.offset_x;
        point.y += sample.offset_y;
        point = rotate_point(point, root_pivot, root_rotation);
        point.x += root_offset_x;
        point.y += root_offset_y;
        if (flip) {
            point.x = -point.x;
        }
        // The rig is authored in Unity's Y-up space. NSYNC's world and
        // screen projection use Y-down, so map the local Y at the end while
        // keeping pose math readable.
        point.x += origin_x;
        point.y = origin_y - point.y;
        corners[i] = point;
        uvs[i] = core::HeroPoint{
            u0 + (u1 - u0) * local_uv[i].x,
            v0 + (v1 - v0) * local_uv[i].y};
    }

    batch.textured_quad(
        corners,
        uvs,
        Color{255, 255, 255, alpha});
    if (flash_batch != nullptr && flash_alpha > 0) {
        flash_batch->textured_quad(
            corners,
            uvs,
            Color{255, 255, 255, flash_alpha});
    }
}

void draw_atlas_billboard(
    QuadBatch& batch,
    const core::Atlas& atlas,
    std::string_view sprite,
    float x,
    float y,
    float size,
    float rotation,
    Color color) {
    const core::AtlasFrame* frame = atlas.find(sprite);
    if (frame == nullptr) {
        return;
    }
    const float half = std::max(0.01f, size * 0.5f);
    const float c = std::cos(rotation);
    const float s = std::sin(rotation);
    const auto corner = [&](float lx, float ly) {
        return core::HeroPoint{
            x + lx * c - ly * s,
            y + lx * s + ly * c};
    };
    const std::array<core::HeroPoint, 4> corners{
        corner(-half, -half),
        corner(half, -half),
        corner(half, half),
        corner(-half, half)};
    const float u0 =
        static_cast<float>(frame->x) / static_cast<float>(atlas.width);
    const float v0 =
        static_cast<float>(frame->y) / static_cast<float>(atlas.height);
    const float u1 =
        static_cast<float>(frame->x + frame->width) /
        static_cast<float>(atlas.width);
    const float v1 =
        static_cast<float>(frame->y + frame->height) /
        static_cast<float>(atlas.height);
    const std::array<core::HeroPoint, 4> uvs{
        core::HeroPoint{u0, v0},
        core::HeroPoint{u1, v0},
        core::HeroPoint{u1, v1},
        core::HeroPoint{u0, v1}};
    batch.textured_quad(corners, uvs, color);
}

void draw_atlas_box(
    QuadBatch& batch,
    const core::Atlas& atlas,
    const core::AtlasFrame& frame,
    const core::AABB& box,
    Color color) {
    const float u0 =
        static_cast<float>(frame.x) / static_cast<float>(atlas.width);
    const float v0 =
        static_cast<float>(frame.y) / static_cast<float>(atlas.height);
    const float u1 =
        static_cast<float>(frame.x + frame.width) /
        static_cast<float>(atlas.width);
    const float v1 =
        static_cast<float>(frame.y + frame.height) /
        static_cast<float>(atlas.height);
    batch.sprite(box, u0, v0, u1, v1, color);
}

// NGUI draws the health fill as Type.Tiled: the 14x9 tile repeats
// horizontally and the UISlider clips the result. The repeated tile seams
// are the vertical dividers visible on the original health bar.
void draw_tiled_bar_fill(
    QuadBatch& batch,
    const core::Atlas& atlas,
    const core::AtlasFrame& frame,
    float left,
    float top,
    float width,
    float tile_width,
    float height,
    Color color) {
    if (width <= 0.0f || tile_width <= 0.0f || height <= 0.0f) {
        return;
    }
    const float u0 =
        static_cast<float>(frame.x) / static_cast<float>(atlas.width);
    const float v0 =
        static_cast<float>(frame.y) / static_cast<float>(atlas.height);
    const float u1 =
        static_cast<float>(frame.x + frame.width) /
        static_cast<float>(atlas.width);
    const float v1 =
        static_cast<float>(frame.y + frame.height) /
        static_cast<float>(atlas.height);
    const float right = left + width;
    float x = left;
    while (x < right - 1.0e-4f) {
        const float tile_right = std::min(right, x + tile_width);
        const float fraction = std::clamp(
            (tile_right - x) / tile_width,
            0.0f,
            1.0f);
        batch.sprite(
            core::AABB{
                core::Vec2{from_float(x), from_float(top)},
                core::Vec2{
                    from_float(tile_right),
                    from_float(top + height)}},
            u0,
            v0,
            u0 + (u1 - u0) * fraction,
            v1,
            color);
        x = tile_right;
    }
}

// NGUI shield fill is Type.Sliced: keep the border caps at their authored
// pixel size and stretch only the middle, then clip to the current value.
void draw_sliced_bar_fill(
    QuadBatch& batch,
    const core::Atlas& atlas,
    const core::AtlasFrame& frame,
    float left,
    float top,
    float clip_width,
    float full_width,
    float height,
    int border,
    Color color) {
    if (clip_width <= 0.0f || full_width <= 0.0f ||
        height <= 0.0f || border <= 0) {
        return;
    }
    const float scale =
        height / static_cast<float>(frame.height);
    const float cap = static_cast<float>(border) * scale;
    const float sprite_width = static_cast<float>(frame.width);
    struct Segment {
        float source_begin = 0.0f;
        float source_end = 0.0f;
        float dest_begin = 0.0f;
        float dest_end = 0.0f;
    };
    const Segment segments[3]{
        {static_cast<float>(frame.x),
         static_cast<float>(frame.x + border),
         left,
         left + cap},
        {static_cast<float>(frame.x + border),
         static_cast<float>(frame.x) + sprite_width - border,
         left + cap,
         left + full_width - cap},
        {static_cast<float>(frame.x) + sprite_width - border,
         static_cast<float>(frame.x) + sprite_width,
         left + full_width - cap,
         left + full_width}};
    const float clip_right = left + clip_width;
    const float v0 =
        static_cast<float>(frame.y) / static_cast<float>(atlas.height);
    const float v1 =
        static_cast<float>(frame.y + frame.height) /
        static_cast<float>(atlas.height);
    for (const Segment& segment : segments) {
        if (segment.dest_end <= segment.dest_begin + 1.0e-4f ||
            segment.dest_begin >= clip_right - 1.0e-4f) {
            continue;
        }
        const float dest_right =
            std::min(segment.dest_end, clip_right);
        const float fraction =
            (dest_right - segment.dest_begin) /
            (segment.dest_end - segment.dest_begin);
        const float u0 =
            segment.source_begin / static_cast<float>(atlas.width);
        const float u1 =
            (segment.source_begin +
             (segment.source_end - segment.source_begin) * fraction) /
            static_cast<float>(atlas.width);
        batch.sprite(
            core::AABB{
                core::Vec2{
                    from_float(segment.dest_begin),
                    from_float(top)},
                core::Vec2{
                    from_float(dest_right),
                    from_float(top + height)}},
            u0,
            v0,
            u1,
            v1,
            color);
    }
}

bool draw_original_hero(
    QuadBatch& batch,
    const core::Atlas& atlas,
    const core::HeroRig& rig,
    const core::HeroPoseSet& poses,
    core::AnimationState state,
    std::string_view weapon_id,
    core::Vec2 aim,
    float state_seconds,
    float recoil_pulse,
    float origin_x,
    float origin_y,
    bool flip,
    core::Vec2* muzzle,
    QuadBatch* flash_batch,
    std::uint8_t flash_alpha) {
    const char* state_name = core::animation_state_name(state);
    const core::HeroPoseState* pose = poses.find(state_name);
    if (pose == nullptr) {
        pose = poses.find("idle");
    }
    if (pose == nullptr) {
        return false;
    }

    const float seconds = pose->loop
        ? state_seconds
        : std::min(state_seconds, pose->duration);
    const float progress = std::clamp(
        seconds / std::max(0.001f, pose->duration),
        0.0f,
        1.0f);
    const float root_rotation =
        lerp_float(pose->root_rotation_start, pose->root_rotation_end, progress) *
        kPi /
        180.0f;
    const float root_offset_x =
        lerp_float(pose->root_offset_x_start, pose->root_offset_x_end, progress);
    const float root_offset_y =
        lerp_float(pose->root_offset_y_start, pose->root_offset_y_end, progress);
    const float alpha_value = std::clamp(
        lerp_float(pose->alpha_start, pose->alpha_end, progress),
        0.0f,
        1.0f);
    if (alpha_value <= 0.001f) {
        return false;
    }
    const std::uint8_t alpha = static_cast<std::uint8_t>(
        std::clamp(alpha_value * 255.0f, 0.0f, 255.0f));

    float weapon_rotation = 0.0f;
    const float aim_x = to_float(aim.x);
    const float aim_y = to_float(aim.y);
    if (aim_x != 0.0f || aim_y != 0.0f) {
        const float local_x = flip ? -aim_x : aim_x;
        const float local_y = -aim_y;
        weapon_rotation = std::atan2(local_y, local_x) - kPi;
    }

    const core::HeroWeapon* weapon = nullptr;
    for (const core::HeroWeapon& candidate : rig.weapons) {
        if (candidate.id == weapon_id) {
            weapon = &candidate;
            break;
        }
    }

    bool has_muzzle = false;
    if (muzzle != nullptr && weapon != nullptr) {
        std::size_t first = 0;
        std::size_t second = 0;
        for (std::size_t index = 1; index < weapon->quad.size(); ++index) {
            if (weapon->quad[index].x < weapon->quad[first].x) {
                first = index;
                second = index;
            } else if (
                second == first &&
                weapon->quad[index].x == weapon->quad[first].x) {
                second = index;
            }
        }
        const core::HeroPoint local{
            (weapon->quad[first].x + weapon->quad[second].x) * 0.5f,
            (weapon->quad[first].y + weapon->quad[second].y) * 0.5f};
        const RigPoseSample weapon_sample = sample_pose(
            *pose,
            poses.recoil,
            "weapon",
            seconds,
            recoil_pulse);
        core::HeroPoint point = rotate_point(
            local,
            weapon->pivot,
            weapon_sample.rotation + weapon_rotation);
        point.x += weapon_sample.offset_x;
        point.y += weapon_sample.offset_y;
        point = rotate_point(point, rig.root_pivot, root_rotation);
        point.x += root_offset_x;
        point.y += root_offset_y;
        if (flip) {
            point.x = -point.x;
        }
        point.x += origin_x;
        point.y = origin_y - point.y;
        *muzzle = core::Vec2{from_float(point.x), from_float(point.y)};
        has_muzzle = true;
    }

    bool weapon_drawn = weapon == nullptr;
    for (const core::HeroPart& part : rig.parts) {
        if (!weapon_drawn && part.z > weapon->z) {
            draw_rig_piece(
                batch,
                atlas,
                rig,
                *pose,
                poses.recoil,
                "weapon",
                weapon->sprite,
                weapon->quad,
                weapon->uv,
                weapon->pivot,
                seconds,
                recoil_pulse,
                weapon_rotation,
                root_rotation,
                root_offset_x,
                root_offset_y,
                origin_x,
                origin_y,
                flip,
                alpha,
                flash_batch,
                flash_alpha);
            weapon_drawn = true;
        }
        draw_rig_piece(
            batch,
            atlas,
            rig,
            *pose,
            poses.recoil,
            part.role,
            part.sprite,
            part.quad,
            part.uv,
            part.pivot,
            seconds,
            recoil_pulse,
            0.0f,
            root_rotation,
            root_offset_x,
            root_offset_y,
            origin_x,
            origin_y,
                flip,
                alpha,
                flash_batch,
                flash_alpha);
    }
    if (!weapon_drawn) {
        draw_rig_piece(
            batch,
            atlas,
            rig,
            *pose,
            poses.recoil,
            "weapon",
            weapon->sprite,
            weapon->quad,
            weapon->uv,
            weapon->pivot,
            seconds,
            recoil_pulse,
            weapon_rotation,
            root_rotation,
            root_offset_x,
            root_offset_y,
            origin_x,
            origin_y,
                flip,
                alpha,
                flash_batch,
                flash_alpha);
    }
    return has_muzzle;
}

void draw_team_ring(
    QuadBatch& batch,
    const core::Player& player,
    Color color) {
    const float x = to_float(player.position.x);
    const float y = to_float(player.position.y) + 0.88f;
    constexpr int segments = 20;
    for (int i = 0; i < segments; ++i) {
        const float a0 =
            static_cast<float>(i) *
            2.0f * kPi /
            static_cast<float>(segments);
        const float a1 =
            static_cast<float>(i + 1) *
            2.0f * kPi /
            static_cast<float>(segments);
        batch.segment(
            core::Vec2{
                from_float(x + std::cos(a0) * 0.42f),
                from_float(y + std::sin(a0) * 0.10f)},
            core::Vec2{
                from_float(x + std::cos(a1) * 0.42f),
                from_float(y + std::sin(a1) * 0.10f)},
            2.0f,
            with_alpha(color, 0.42f));
    }
}

} // namespace

bool WorldRenderer::initialize(const shared::ResourceManager& resources) {
    shutdown();
    const std::uint32_t white = 0xFFFFFFFFU;
    white_texture_ = bgfx::createTexture2D(
        1,
        1,
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP |
            BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_MIP_POINT,
        bgfx::copy(&white, sizeof(white)));
    if (!bgfx::isValid(white_texture_)) {
        return false;
    }

    reload_visuals(resources);
    return true;
}

void WorldRenderer::load_crystal_texture(
    const shared::ResourceManager& resources) {
    const auto bytes = resources.read("sprites/crystal_blue_small.png");
    if (!bytes) {
        NSYNC_LOG_WARN(
            "client",
            "crystal sprite missing: sprites/crystal_blue_small.png");
        return;
    }
    RgbaImage image;
    std::string error;
    if (!load_rgba_image(
            bytes->data(),
            bytes->size(),
            image,
            error)) {
        NSYNC_LOG_WARN("client", "crystal sprite decode failed: ", error);
        return;
    }
    if (image.width <= 0 || image.height <= 0 ||
        image.width > 0xFFFF || image.height > 0xFFFF) {
        NSYNC_LOG_WARN("client", "crystal sprite has invalid dimensions");
        return;
    }
    const bgfx::TextureHandle next = bgfx::createTexture2D(
        static_cast<std::uint16_t>(image.width),
        static_cast<std::uint16_t>(image.height),
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP |
            BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_MIP_POINT,
        bgfx::copy(
            image.pixels.data(),
            static_cast<std::uint32_t>(image.pixels.size())));
    if (!bgfx::isValid(next)) {
        NSYNC_LOG_WARN("client", "crystal sprite texture creation failed");
        return;
    }
    if (bgfx::isValid(crystal_texture_)) {
        bgfx::destroy(crystal_texture_);
    }
    crystal_texture_ = next;
    crystal_ready_ = true;
}

void WorldRenderer::load_bag_atlas(
    const shared::ResourceManager& resources) {
    const auto text =
        resources.read_text("sprites/crystal_bag.atlas.json");
    if (!text) {
        NSYNC_LOG_WARN(
            "client",
            "crystal bag atlas missing: sprites/crystal_bag.atlas.json");
        return;
    }
    core::Atlas next;
    std::string error;
    if (!next.load_json(*text, &error)) {
        NSYNC_LOG_WARN(
            "client",
            "failed to load crystal bag atlas: ",
            error);
        return;
    }
    const auto bytes = resources.read(next.image);
    if (!bytes) {
        NSYNC_LOG_WARN(
            "client",
            "crystal bag image missing: ",
            next.image);
        return;
    }
    RgbaImage image;
    if (!load_rgba_image(
            bytes->data(),
            bytes->size(),
            image,
            error)) {
        NSYNC_LOG_WARN(
            "client",
            "crystal bag image decode failed: ",
            error);
        return;
    }
    if (image.width != next.width || image.height != next.height) {
        NSYNC_LOG_WARN(
            "client",
            "crystal bag image dimensions do not match atlas");
        return;
    }
    const bgfx::TextureHandle next_texture = bgfx::createTexture2D(
        next.width,
        next.height,
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP |
            BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_MIP_POINT,
        bgfx::copy(
            image.pixels.data(),
            static_cast<std::uint32_t>(image.pixels.size())));
    if (!bgfx::isValid(next_texture)) {
        NSYNC_LOG_WARN(
            "client",
            "crystal bag texture creation failed");
        return;
    }
    if (bgfx::isValid(bag_texture_)) {
        bgfx::destroy(bag_texture_);
    }
    bag_atlas_ = std::move(next);
    bag_texture_ = next_texture;
    bag_ready_ = true;
    NSYNC_LOG_INFO(
        "client",
        "loaded crystal bag atlas: frames=",
        bag_atlas_.frames.size(),
        " image=",
        bag_atlas_.width,
        "x",
        bag_atlas_.height);
}

void WorldRenderer::load_health_bar_atlas(
    const shared::ResourceManager& resources) {
    const auto text =
        resources.read_text("sprites/health_bar.atlas.json");
    if (!text) {
        NSYNC_LOG_WARN(
            "client",
            "health bar atlas missing: sprites/health_bar.atlas.json");
        return;
    }
    core::Atlas next;
    std::string error;
    if (!next.load_json(*text, &error)) {
        NSYNC_LOG_WARN(
            "client",
            "failed to load health bar atlas: ",
            error);
        return;
    }
    const auto bytes = resources.read(next.image);
    if (!bytes) {
        NSYNC_LOG_WARN(
            "client",
            "health bar image missing: ",
            next.image);
        return;
    }
    RgbaImage image;
    if (!load_rgba_image(
            bytes->data(),
            bytes->size(),
            image,
            error)) {
        NSYNC_LOG_WARN(
            "client",
            "health bar image decode failed: ",
            error);
        return;
    }
    if (image.width != next.width || image.height != next.height) {
        NSYNC_LOG_WARN(
            "client",
            "health bar image dimensions do not match atlas");
        return;
    }
    const bgfx::TextureHandle next_texture = bgfx::createTexture2D(
        next.width,
        next.height,
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP |
            BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_MIP_POINT,
        bgfx::copy(
            image.pixels.data(),
            static_cast<std::uint32_t>(image.pixels.size())));
    if (!bgfx::isValid(next_texture)) {
        NSYNC_LOG_WARN(
            "client",
            "health bar texture creation failed");
        return;
    }
    if (bgfx::isValid(health_bar_texture_)) {
        bgfx::destroy(health_bar_texture_);
    }
    health_bar_atlas_ = std::move(next);
    health_bar_texture_ = next_texture;
    health_bar_ready_ = true;
    NSYNC_LOG_INFO(
        "client",
        "loaded health bar atlas: frames=",
        health_bar_atlas_.frames.size(),
        " image=",
        health_bar_atlas_.width,
        "x",
        health_bar_atlas_.height);
}

void WorldRenderer::load_particle_atlas(
    const shared::ResourceManager& resources) {
    const auto text = resources.read_text("sprites/particles.atlas.json");
    if (!text) {
        NSYNC_LOG_WARN(
            "client",
            "particle atlas missing: sprites/particles.atlas.json");
        return;
    }
    core::Atlas next;
    std::string error;
    if (!next.load_json(*text, &error)) {
        NSYNC_LOG_WARN(
            "client",
            "failed to load particle atlas: ",
            error);
        return;
    }
    const auto bytes = resources.read(next.image);
    if (!bytes) {
        NSYNC_LOG_WARN(
            "client",
            "failed to read particle atlas image: ",
            next.image);
        return;
    }
    RgbaImage image;
    if (!load_rgba_image(
            bytes->data(),
            bytes->size(),
            image,
            error)) {
        NSYNC_LOG_WARN(
            "client",
            "failed to decode particle atlas image: ",
            error);
        return;
    }
    if (image.width != next.width || image.height != next.height) {
        NSYNC_LOG_WARN(
            "client",
            "particle atlas dimensions do not match metadata");
        return;
    }
    const bgfx::TextureHandle next_texture = bgfx::createTexture2D(
        next.width,
        next.height,
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP |
            BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_MIP_POINT,
        bgfx::copy(
            image.pixels.data(),
            static_cast<std::uint32_t>(image.pixels.size())));
    if (!bgfx::isValid(next_texture)) {
        NSYNC_LOG_WARN("client", "failed to create particle atlas texture");
        return;
    }
    if (bgfx::isValid(particle_texture_)) {
        bgfx::destroy(particle_texture_);
    }
    particle_atlas_ = std::move(next);
    particle_texture_ = next_texture;
    particle_atlas_ready_ = true;
    NSYNC_LOG_INFO(
        "client",
        "loaded particle atlas: frames=",
        particle_atlas_.frames.size(),
        " image=",
        particle_atlas_.width,
        "x",
        particle_atlas_.height);
}

bool WorldRenderer::reload_visuals(
    const shared::ResourceManager& resources) {
    load_crystal_texture(resources);
    load_bag_atlas(resources);
    load_health_bar_atlas(resources);
    load_particle_atlas(resources);
    const bool had_visuals = atlas_ready_;
    const auto fail = [&](std::string message) {
        NSYNC_LOG_WARN(
            "client",
            message,
            had_visuals
                ? "; keeping previous hero visuals"
                : "; using colored player boxes");
        return false;
    };

    core::Atlas next_atlas;
    core::AnimationSet next_animations;
    std::string error;

    const auto atlas_text = resources.read_text("sprites/heroes.atlas.json");
    if (!atlas_text) {
        return fail("sprites/heroes.atlas.json missing");
    }
    if (!next_atlas.load_json(*atlas_text, &error)) {
        return fail("failed to load hero atlas: " + error);
    }

    const auto image_bytes = resources.read(next_atlas.image);
    if (!image_bytes) {
        return fail("failed to read atlas image: " + next_atlas.image);
    }

    RgbaImage image;
    if (!load_rgba_image(
            image_bytes->data(),
            image_bytes->size(),
            image,
            error)) {
        return fail("failed to decode atlas image: " + error);
    }
    if (image.width != next_atlas.width ||
        image.height != next_atlas.height) {
        return fail(
            "atlas image dimensions do not match metadata: " +
            std::to_string(image.width) + "x" +
            std::to_string(image.height) + " vs " +
            std::to_string(next_atlas.width) + "x" +
            std::to_string(next_atlas.height));
    }

    const auto animation_text = resources.read_text("data/animations.json");
    if (!animation_text) {
        return fail("data/animations.json missing");
    }
    if (!next_animations.load_json(*animation_text, &error)) {
        return fail("failed to load animations: " + error);
    }

    const bgfx::TextureHandle next_texture = bgfx::createTexture2D(
        next_atlas.width,
        next_atlas.height,
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP |
            BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_MIP_POINT,
        bgfx::copy(
            image.pixels.data(),
            static_cast<std::uint32_t>(image.pixels.size())));
    if (!bgfx::isValid(next_texture)) {
        return fail("failed to create hero atlas texture");
    }

    if (bgfx::isValid(atlas_texture_)) {
        bgfx::destroy(atlas_texture_);
    }
    atlas_ = std::move(next_atlas);
    animations_ = std::move(next_animations);
    atlas_texture_ = next_texture;
    atlas_ready_ = true;
    visuals_.clear();
    clear_effects();

    NSYNC_LOG_INFO(
        "client",
        "loaded hero atlas: frames=",
        atlas_.frames.size(),
        " clips=",
        animations_.clips.size(),
        " image=",
        atlas_.width,
        "x",
        atlas_.height);

    if (hero_art_mode_ == HeroArtMode::Placeholder) {
        if (bgfx::isValid(original_texture_)) {
            bgfx::destroy(original_texture_);
            original_texture_ = BGFX_INVALID_HANDLE;
        }
        original_atlas_ = {};
        hero_rigs_ = {};
        hero_poses_ = {};
        original_image_path_.clear();
        original_ready_ = false;
        return true;
    }

    const auto fail_original = [&](std::string message) {
        NSYNC_LOG_WARN(
            "client",
            "original hero art unavailable: ",
            message,
            original_ready_
                ? "; keeping previous original visuals"
                : "; using placeholder");
        if (!original_ready_) {
            if (bgfx::isValid(original_texture_)) {
                bgfx::destroy(original_texture_);
                original_texture_ = BGFX_INVALID_HANDLE;
            }
            original_atlas_ = {};
            hero_rigs_ = {};
            hero_poses_ = {};
            original_image_path_.clear();
        }
        return true;
    };

    std::string original_error;
    core::HeroRigSet next_rigs;
    const auto rig_text = resources.read_text("data/hero_rigs.json");
    if (!rig_text) {
        return fail_original("data/hero_rigs.json missing");
    }
    if (!next_rigs.load_json(*rig_text, &original_error)) {
        return fail_original("failed to load hero rigs: " + original_error);
    }

    core::HeroPoseSet next_poses;
    const auto pose_text = resources.read_text("data/hero_poses.json");
    if (!pose_text) {
        return fail_original("data/hero_poses.json missing");
    }
    if (!next_poses.load_json(*pose_text, &original_error)) {
        return fail_original("failed to load hero poses: " + original_error);
    }

    core::Atlas next_original_atlas;
    const auto original_atlas_text =
        resources.read_text(next_rigs.atlas);
    if (!original_atlas_text) {
        return fail_original(
            "failed to read original atlas: " + next_rigs.atlas);
    }
    if (!next_original_atlas.load_json(
            *original_atlas_text,
            &original_error)) {
        return fail_original(
            "failed to load original atlas: " + original_error);
    }

    for (const core::HeroRig& rig : next_rigs.heroes) {
        for (const core::HeroPart& part : rig.parts) {
            if (next_original_atlas.find(part.sprite) == nullptr) {
                return fail_original(
                    "part sprite missing from atlas: " + part.sprite);
            }
        }
        for (const core::HeroWeapon& weapon : rig.weapons) {
            if (next_original_atlas.find(weapon.sprite) == nullptr) {
                return fail_original(
                    "weapon sprite missing from atlas: " + weapon.sprite);
            }
        }
    }

    const auto original_image_bytes =
        resources.read(next_original_atlas.image);
    if (!original_image_bytes) {
        return fail_original(
            "failed to read original atlas image: " +
            next_original_atlas.image);
    }
    RgbaImage original_image;
    if (!load_rgba_image(
            original_image_bytes->data(),
            original_image_bytes->size(),
            original_image,
            original_error)) {
        return fail_original(
            "failed to decode original atlas image: " + original_error);
    }
    if (original_image.width != next_original_atlas.width ||
        original_image.height != next_original_atlas.height) {
        return fail_original(
            "original atlas image dimensions do not match metadata");
    }

    const bgfx::TextureHandle next_original_texture =
        bgfx::createTexture2D(
            next_original_atlas.width,
            next_original_atlas.height,
            false,
            1,
            bgfx::TextureFormat::RGBA8,
            BGFX_SAMPLER_U_CLAMP |
                BGFX_SAMPLER_V_CLAMP |
                BGFX_SAMPLER_MIP_POINT,
            bgfx::copy(
                original_image.pixels.data(),
                static_cast<std::uint32_t>(
                    original_image.pixels.size())));
    if (!bgfx::isValid(next_original_texture)) {
        return fail_original(
            "failed to create original hero atlas texture");
    }

    if (bgfx::isValid(original_texture_)) {
        bgfx::destroy(original_texture_);
    }
    original_atlas_ = std::move(next_original_atlas);
    hero_rigs_ = std::move(next_rigs);
    hero_poses_ = std::move(next_poses);
    original_texture_ = next_original_texture;
    original_image_path_ = original_atlas_.image;
    original_ready_ = true;

    NSYNC_LOG_INFO(
        "client",
        "loaded original hero art: heroes=",
        hero_rigs_.heroes.size(),
        " atlas=",
        original_atlas_.width,
        "x",
        original_atlas_.height,
        " poses=",
        hero_poses_.states.size(),
        " mode=",
        hero_art_mode_name(hero_art_mode_));
    return true;
}

bool WorldRenderer::load_map_art(
    const shared::ResourceManager& resources,
    std::string_view map_id) {
    const std::string id(map_id);
    if (id.empty()) {
        clear_map_art();
        return false;
    }

    const std::string path = "maps/" + id + "_art.json";
    const auto text = resources.read_text(path);
    if (!text) {
        if (map_art_id_ != id) {
            clear_map_art();
        }
        return false;
    }

    const auto fail = [&](std::string message) {
        NSYNC_LOG_WARN(
            "client",
            "map art unavailable for ",
            id,
            ": ",
            message,
            map_art_ready_ && map_art_id_ == id
                ? "; keeping previous map art"
                : "");
        if (!map_art_ready_ || map_art_id_ != id) {
            clear_map_art();
        }
        return false;
    };

    core::JsonValue root;
    std::string error;
    if (!core::parse_json(*text, root, &error)) {
        return fail("json: " + error);
    }
    if (root.type != core::JsonValue::Type::Object) {
        return fail("root must be an object");
    }

    const core::JsonValue* version = root.find("version");
    if (version == nullptr ||
        version->type != core::JsonValue::Type::Number ||
        version->number_value != 1.0) {
        return fail("unsupported version");
    }

    const core::JsonValue* image_value = root.find("image");
    if (image_value == nullptr ||
        image_value->type != core::JsonValue::Type::String ||
        image_value->string_value.empty()) {
        return fail("image path missing");
    }
    const std::string& image_path = image_value->string_value;

    const core::JsonValue* layers_value = root.find("layers");
    if (layers_value == nullptr ||
        layers_value->type != core::JsonValue::Type::Array) {
        return fail("layers array missing");
    }

    const auto read_number = [](const core::JsonValue* value, double& out) {
        if (value == nullptr ||
            value->type != core::JsonValue::Type::Number) {
            return false;
        }
        out = value->number_value;
        return true;
    };

    std::vector<MapArtLayer> next_layers;
    next_layers.reserve(layers_value->array.size());
    for (const core::JsonValue& layer_value : layers_value->array) {
        if (layer_value.type != core::JsonValue::Type::Object) {
            return fail("layer must be an object");
        }
        double order = 0.0;
        double parallax = 0.0;
        if (!read_number(layer_value.find("order"), order) ||
            !read_number(layer_value.find("parallax"), parallax)) {
            return fail("layer order/parallax missing");
        }
        const core::JsonValue* vertices_value =
            layer_value.find("vertices");
        if (vertices_value == nullptr ||
            vertices_value->type != core::JsonValue::Type::Array ||
            vertices_value->array.size() % 8U != 0U) {
            return fail("layer vertices must be a multiple of 8 numbers");
        }

        MapArtLayer layer;
        layer.order = static_cast<int>(std::lround(order));
        layer.parallax = static_cast<float>(parallax);
        if (const core::JsonValue* additive_value =
                layer_value.find("additive")) {
            if (additive_value->type != core::JsonValue::Type::Bool) {
                return fail("layer additive must be a bool");
            }
            layer.additive = additive_value->bool_value;
        }
        layer.vertices.reserve(vertices_value->array.size() / 8U);
        for (std::size_t i = 0; i < vertices_value->array.size(); i += 8U) {
            double x = 0.0;
            double y = 0.0;
            double u = 0.0;
            double v = 0.0;
            double r = 255.0;
            double g = 255.0;
            double b = 255.0;
            double a = 255.0;
            if (!read_number(&vertices_value->array[i], x) ||
                !read_number(&vertices_value->array[i + 1], y) ||
                !read_number(&vertices_value->array[i + 2], u) ||
                !read_number(&vertices_value->array[i + 3], v) ||
                !read_number(&vertices_value->array[i + 4], r) ||
                !read_number(&vertices_value->array[i + 5], g) ||
                !read_number(&vertices_value->array[i + 6], b) ||
                !read_number(&vertices_value->array[i + 7], a)) {
                return fail("layer vertex contains a non-number");
            }
            const auto clamp_byte = [](double value) {
                return static_cast<std::uint8_t>(
                    std::clamp(std::lround(value), 0L, 255L));
            };
            MapArtVertex vertex;
            vertex.x = static_cast<float>(x);
            vertex.y = static_cast<float>(y);
            vertex.u = static_cast<float>(u);
            vertex.v = static_cast<float>(v);
            vertex.r = clamp_byte(r);
            vertex.g = clamp_byte(g);
            vertex.b = clamp_byte(b);
            vertex.a = clamp_byte(a);
            layer.vertices.push_back(vertex);
        }
        if (!layer.vertices.empty()) {
            next_layers.push_back(std::move(layer));
        }
    }

    std::stable_sort(
        next_layers.begin(),
        next_layers.end(),
        [](const MapArtLayer& left, const MapArtLayer& right) {
            return left.order < right.order;
        });

    const auto image_bytes = resources.read(image_path);
    if (!image_bytes) {
        return fail("failed to read " + image_path);
    }
    RgbaImage image;
    if (!load_rgba_image(
            image_bytes->data(),
            image_bytes->size(),
            image,
            error)) {
        return fail("failed to decode image: " + error);
    }
    if (image.width <= 0 || image.height <= 0 ||
        image.width > 0xFFFF || image.height > 0xFFFF) {
        return fail("invalid image dimensions");
    }

    const bgfx::TextureHandle next_texture = bgfx::createTexture2D(
        static_cast<std::uint16_t>(image.width),
        static_cast<std::uint16_t>(image.height),
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP |
            BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_MIP_POINT,
        bgfx::copy(
            image.pixels.data(),
            static_cast<std::uint32_t>(image.pixels.size())));
    if (!bgfx::isValid(next_texture)) {
        return fail("failed to create bgfx texture");
    }

    core::AABB camera_bounds{};
    bool has_camera_bounds = false;
    if (const core::JsonValue* bounds_value = root.find("camera_bounds")) {
        if (bounds_value->type == core::JsonValue::Type::Array &&
            bounds_value->array.size() == 4U) {
            double x = 0.0;
            double y = 0.0;
            double w = 0.0;
            double h = 0.0;
            if (read_number(&bounds_value->array[0], x) &&
                read_number(&bounds_value->array[1], y) &&
                read_number(&bounds_value->array[2], w) &&
                read_number(&bounds_value->array[3], h) &&
                w > 0.0 &&
                h > 0.0) {
                camera_bounds = core::AABB{
                    core::Vec2{from_float(static_cast<float>(x)),
                               from_float(static_cast<float>(y))},
                    core::Vec2{from_float(static_cast<float>(x + w)),
                               from_float(static_cast<float>(y + h))}};
                has_camera_bounds = true;
            }
        }
    }

    if (bgfx::isValid(map_art_texture_)) {
        bgfx::destroy(map_art_texture_);
    }
    map_art_texture_ = next_texture;
    map_art_layers_ = std::move(next_layers);
    map_art_id_ = id;
    map_art_ready_ = !map_art_layers_.empty();
    map_art_has_camera_bounds_ = has_camera_bounds;
    map_art_camera_bounds_ = camera_bounds;
    map_art_reference_valid_ = false;

    std::size_t vertex_count = 0;
    for (const MapArtLayer& layer : map_art_layers_) {
        vertex_count += layer.vertices.size();
    }
    NSYNC_LOG_INFO(
        "client",
        "loaded original map art: ",
        id,
        " layers=",
        map_art_layers_.size(),
        " vertices=",
        vertex_count,
        " image=",
        image.width,
        "x",
        image.height);
    return map_art_ready_;
}

void WorldRenderer::clear_map_art() {
    if (bgfx::isValid(map_art_texture_)) {
        bgfx::destroy(map_art_texture_);
        map_art_texture_ = BGFX_INVALID_HANDLE;
    }
    map_art_layers_.clear();
    map_art_id_.clear();
    map_art_ready_ = false;
    map_art_has_camera_bounds_ = false;
    map_art_camera_bounds_ = {};
    map_art_reference_valid_ = false;
}

bool WorldRenderer::map_art_camera_bounds(core::AABB& bounds) const {
    if (!map_art_has_camera_bounds_) {
        return false;
    }
    bounds = map_art_camera_bounds_;
    return true;
}

void WorldRenderer::reset_player_visuals() {
    visuals_.clear();
    has_interp_frame_ = false;
    clear_effects();
}

void WorldRenderer::add_position_correction(
    core::EntityId player,
    core::Vec2 offset) {
    const auto found = visuals_.find(player.raw());
    if (found == visuals_.end() || !found->second.has_render_position) {
        return;
    }
    PlayerVisualState& visual = found->second;
    visual.correction_x += to_float(offset.x);
    visual.correction_y += to_float(offset.y);
    // Never carry more than a few units of hidden error; beyond that a
    // visible snap is the honest presentation.
    constexpr float kMaxCorrection = 4.0f;
    if (std::abs(visual.correction_x) > kMaxCorrection ||
        std::abs(visual.correction_y) > kMaxCorrection) {
        visual.correction_x = 0.0f;
        visual.correction_y = 0.0f;
    }
}

bool WorldRenderer::run_original_hero_benchmark(
    int players,
    int frames,
    std::string* error) {
    if (!original_ready_ || hero_rigs_.heroes.empty() ||
        hero_poses_.states.empty()) {
        if (error != nullptr) {
            *error = "original hero art is not loaded";
        }
        return false;
    }

    players = std::clamp(players, 1, core::kMaxPlayers);
    frames = std::clamp(frames, 60, 100000);
    constexpr int width = 1280;
    constexpr int height = 720;
    constexpr int warmup_frames = 60;
    constexpr double budget_us = 500.0;

    Camera camera;
    const std::array<core::AnimationState, 5> states{
        core::AnimationState::Idle,
        core::AnimationState::Run,
        core::AnimationState::Jump,
        core::AnimationState::Fall,
        core::AnimationState::Shoot,
    };
    const std::array<const char*, 6> weapons{
        "rifle", "shotgun", "rocket", "smg", "burst", "grenade"};

    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(frames));
    std::uint64_t checksum = 0;

    for (int frame = 0; frame < frames + warmup_frames; ++frame) {
        const auto start = std::chrono::steady_clock::now();
        std::vector<SpriteVertex> vertices;
        std::vector<SpriteVertex> flash_vertices;
        vertices.reserve(4096);
        flash_vertices.reserve(4096);
        QuadBatch batch(camera, width, height, vertices);
        QuadBatch flash_batch(
            camera,
            width,
            height,
            flash_vertices);

        for (int index = 0; index < players; ++index) {
            const core::HeroRig& rig =
                hero_rigs_.heroes[
                    static_cast<std::size_t>(index) %
                    hero_rigs_.heroes.size()];
            const core::AnimationState state =
                states[
                    (static_cast<std::size_t>(frame) +
                     static_cast<std::size_t>(index)) %
                    states.size()];
            const float seconds =
                static_cast<float>(
                    (frame + index) % 120) /
                60.0f;
            const float x =
                -4.0f + static_cast<float>(index % 5) * 2.0f;
            const float y =
                static_cast<float>(index / 5) * 2.0f;
            draw_original_hero(
                batch,
                original_atlas_,
                rig,
                hero_poses_,
                state,
                weapons[
                    (static_cast<std::size_t>(frame) +
                     static_cast<std::size_t>(index)) %
                    weapons.size()],
                core::Vec2{
                    (index % 2) != 0 ? core::Fix32::one()
                                     : core::Fix32::from_int(-1),
                    core::Fix32::zero()},
                seconds,
                state == core::AnimationState::Shoot ? 1.0f : 0.0f,
                x,
                y,
                (index % 2) != 0,
                nullptr,
                &flash_batch,
                180);
        }

        const auto end = std::chrono::steady_clock::now();
        checksum += static_cast<std::uint64_t>(
            vertices.size() + flash_vertices.size());
        if (frame >= warmup_frames) {
            samples.push_back(
                std::chrono::duration<double, std::micro>(end - start)
                    .count());
        }
    }

    std::sort(samples.begin(), samples.end());
    double total_us = 0.0;
    for (const double sample : samples) {
        total_us += sample;
    }
    const double average_us =
        samples.empty() ? 0.0 : total_us / static_cast<double>(samples.size());
    const std::size_t p95_index = samples.empty()
        ? 0
        : std::min(
              samples.size() - 1,
              static_cast<std::size_t>(
                  static_cast<double>(samples.size()) * 0.95));
    const double p95_us = samples.empty() ? 0.0 : samples[p95_index];
    const double max_us = samples.empty() ? 0.0 : samples.back();
    std::size_t max_parts = 0;
    for (const core::HeroRig& rig : hero_rigs_.heroes) {
        max_parts = std::max(max_parts, rig.parts.size());
    }
    const std::size_t draws_per_frame =
        static_cast<std::size_t>(players) * (max_parts + 1U);
    const bool pass = average_us <= budget_us;

    NSYNC_LOG_INFO(
        "client",
        "hero render benchmark: players=",
        players,
        " parts=",
        max_parts + 1U,
        " draws=",
        draws_per_frame,
        " frames=",
        frames,
        " flash=all",
        " avg_us=",
        average_us,
        " p95_us=",
        p95_us,
        " max_us=",
        max_us,
        " budget_us=",
        budget_us,
        " result=",
        pass ? "PASS" : "FAIL",
        " checksum=",
        checksum);
    if (!pass) {
        NSYNC_LOG_WARN(
            "client",
            "hero render benchmark exceeded CPU budget");
    }
    return true;
}

void WorldRenderer::add_effect(const EffectVisual& effect) {
    if (effects_.size() >= 256) {
        effects_.erase(effects_.begin());
    }
    effects_.push_back(effect);
}

void WorldRenderer::add_shake(float strength) {
    shake_strength_ = std::max(shake_strength_, strength);
}

void WorldRenderer::clear_effects() {
    projectile_visuals_.clear();
    effects_.clear();
    particles_.clear();
    shake_strength_ = 0.0f;
    recoil_x_ = 0.0f;
    recoil_y_ = 0.0f;
}

float WorldRenderer::next_particle_random() {
    particle_rng_ = particle_rng_ * 1664525U + 1013904223U;
    return static_cast<float>(particle_rng_ >> 8) /
        static_cast<float>(1U << 24U);
}

void WorldRenderer::spawn_particles(
    std::span<const ParticleLayer> layers,
    float x,
    float y,
    float direction_angle,
    core::EntityId owner) {
    if (!particle_atlas_ready_ || layers.empty()) {
        return;
    }
    constexpr float kFullTurn = 6.2831853f;
    for (const ParticleLayer& layer : layers) {
        if (layer.sprite == nullptr ||
            particle_atlas_.find(layer.sprite) == nullptr) {
            continue;
        }
        for (std::uint8_t i = 0; i < layer.count; ++i) {
            float angle = layer.angle;
            if (layer.spread >= kFullTurn - 0.01f) {
                angle += next_particle_random() * kFullTurn;
            } else {
                angle +=
                    (next_particle_random() * 2.0f - 1.0f) *
                    layer.spread * 0.5f;
            }
            if (layer.angle == 0.0f && direction_angle != 0.0f) {
                angle += direction_angle;
            }
            const float speed =
                std::max(
                    0.0f,
                    layer.speed +
                        (next_particle_random() * 2.0f - 1.0f) *
                            layer.speed_variance);
            const float size_scale =
                0.85f + next_particle_random() * 0.3f;

            ParticleVisual particle;
            particle.layer = &layer;
            particle.x =
                x + layer.offset_x +
                (next_particle_random() * 2.0f - 1.0f) * 0.05f;
            particle.y =
                y + layer.offset_y +
                (next_particle_random() * 2.0f - 1.0f) * 0.05f;
            if (layer.ring) {
                particle.vx = 0.0f;
                particle.vy = 0.0f;
            } else {
                particle.vx = std::cos(angle) * speed;
                particle.vy = std::sin(angle) * speed;
            }
            particle.life = std::max(
                0.05f,
                layer.lifetime +
                    (next_particle_random() * 2.0f - 1.0f) *
                        layer.lifetime_variance);
            particle.delay = layer.delay;
            particle.rotation =
                layer.stretch ? angle : next_particle_random() * kFullTurn;
            particle.spin = layer.spin;
            particle.size_start = layer.size_start * size_scale;
            particle.size_end = layer.size_end * size_scale;
            particle.r = layer.r;
            particle.g = layer.g;
            particle.b = layer.b;
            particle.a = layer.a;
            particle.additive = layer.additive;
            particle.stretch = layer.stretch;
            particle.ring = layer.ring;
            particle.follows = layer.follow;
            particle.follow = owner;
            particles_.push_back(particle);
            if (particles_.size() > 2400) {
                particles_.erase(
                    particles_.begin(),
                    particles_.begin() +
                        static_cast<std::ptrdiff_t>(
                            particles_.size() - 2400));
            }
        }
    }
}

void WorldRenderer::spawn_gadget_particles(
    core::GadgetType gadget,
    bool explode,
    float x,
    float y,
    float direction_angle,
    core::EntityId owner) {
    const GadgetParticles& definition = gadget_particles(gadget);
    if (explode) {
        spawn_particles(
            definition.explode.empty() ? definition.impact
                                       : definition.explode,
            x,
            y,
            direction_angle,
            owner);
    } else {
        spawn_particles(
            definition.impact,
            x,
            y,
            direction_angle,
            owner);
    }
}

void WorldRenderer::update_particles(
    float delta_seconds,
    const core::World& world) {
    if (particles_.empty()) {
        return;
    }
    for (ParticleVisual& particle : particles_) {
        if (particle.follows) {
            if (const core::Player* player =
                    world.players().get(particle.follow);
                player != nullptr && player->alive != 0) {
                particle.x =
                    to_float(player->position.x) +
                    particle.layer->offset_x;
                particle.y =
                    to_float(player->position.y) + 0.9f +
                    particle.layer->offset_y;
            }
        } else {
            const float drag = 1.0f /
                (1.0f + std::max(0.0f, particle.layer->drag) *
                            delta_seconds);
            particle.vx *= drag;
            particle.vy =
                particle.vy * drag +
                particle.layer->gravity * delta_seconds;
            particle.x += particle.vx * delta_seconds;
            particle.y += particle.vy * delta_seconds;
        }
        if (particle.delay > 0.0f) {
            particle.delay -= delta_seconds;
            continue;
        }
        particle.age += delta_seconds;
        particle.rotation += particle.spin * delta_seconds;
    }
    particles_.erase(
        std::remove_if(
            particles_.begin(),
            particles_.end(),
            [](const ParticleVisual& particle) {
                return particle.delay <= 0.0f &&
                    particle.age >= particle.life;
            }),
        particles_.end());
}

void WorldRenderer::shutdown() {
    clear_map_art();
    if (bgfx::isValid(atlas_texture_)) {
        bgfx::destroy(atlas_texture_);
        atlas_texture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(original_texture_)) {
        bgfx::destroy(original_texture_);
        original_texture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(particle_texture_)) {
        bgfx::destroy(particle_texture_);
        particle_texture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(white_texture_)) {
        bgfx::destroy(white_texture_);
        white_texture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(crystal_texture_)) {
        bgfx::destroy(crystal_texture_);
        crystal_texture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(bag_texture_)) {
        bgfx::destroy(bag_texture_);
        bag_texture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(health_bar_texture_)) {
        bgfx::destroy(health_bar_texture_);
        health_bar_texture_ = BGFX_INVALID_HANDLE;
    }
    crystal_ready_ = false;
    bag_ready_ = false;
    bag_atlas_ = {};
    health_bar_ready_ = false;
    health_bar_atlas_ = {};
    atlas_ = {};
    animations_ = {};
    particle_atlas_ = {};
    original_atlas_ = {};
    hero_rigs_ = {};
    hero_poses_ = {};
    original_image_path_.clear();
    atlas_ready_ = false;
    particle_atlas_ready_ = false;
    original_ready_ = false;
    visuals_.clear();
    clear_effects();
}

void WorldRenderer::draw(
    bgfx::ProgramHandle program,
    bgfx::UniformHandle sampler,
    const core::World& world,
    core::EntityId local_player,
    const Camera& camera,
    int width,
    int height,
    float delta_seconds) {
    if (!bgfx::isValid(program) ||
        !bgfx::isValid(sampler) ||
        !bgfx::isValid(white_texture_) ||
        width <= 0 ||
        height <= 0) {
        return;
    }

    const float frame_delta =
        std::clamp(delta_seconds, 0.0f, 0.25f);
    update_particles(frame_delta, world);
    presentation_seconds_ += frame_delta;
    shake_strength_ *= std::exp(-10.0f * frame_delta);
    recoil_x_ *= std::exp(-14.0f * frame_delta);
    recoil_y_ *= std::exp(-14.0f * frame_delta);
    if (shake_strength_ < 0.0005f) {
        shake_strength_ = 0.0f;
    }
    for (EffectVisual& effect : effects_) {
        effect.age_seconds += frame_delta;
    }
    effects_.erase(
        std::remove_if(
            effects_.begin(),
            effects_.end(),
            [](const EffectVisual& effect) {
                return effect.age_seconds >= effect.life_seconds;
            }),
        effects_.end());

    Camera render_camera = camera;
    render_camera.x +=
        std::sin(presentation_seconds_ * 57.0f) * shake_strength_ +
        recoil_x_;
    render_camera.y +=
        std::cos(presentation_seconds_ * 71.0f) * shake_strength_ * 0.8f +
        recoil_y_;

    std::vector<SpriteVertex> world_vertices;
    std::vector<SpriteVertex> map_art_vertices;
    struct MapArtRange {
        std::size_t begin = 0;
        std::size_t end = 0;
        bool additive = false;
    };
    std::vector<MapArtRange> map_art_ranges;
    std::vector<SpriteVertex> sprite_vertices;
    std::vector<SpriteVertex> crystal_vertices;
    std::vector<SpriteVertex> bag_vertices;
    std::vector<SpriteVertex> health_bar_vertices;
    std::vector<SpriteVertex> original_vertices;
    std::vector<SpriteVertex> original_flash_vertices;
    std::vector<SpriteVertex> overlay_vertices;
    std::vector<SpriteVertex> effect_vertices;
    std::vector<SpriteVertex> particle_add_vertices;
    std::vector<SpriteVertex> particle_alpha_vertices;
    world_vertices.reserve(1024);
    sprite_vertices.reserve(512);
    crystal_vertices.reserve(128);
    bag_vertices.reserve(64);
    health_bar_vertices.reserve(192);
    original_vertices.reserve(2048);
    original_flash_vertices.reserve(1024);
    overlay_vertices.reserve(128);
    effect_vertices.reserve(512);
    particle_add_vertices.reserve(2048);
    particle_alpha_vertices.reserve(1024);
    QuadBatch batch(render_camera, width, height, world_vertices);
    QuadBatch sprite_batch(render_camera, width, height, sprite_vertices);
    QuadBatch crystal_batch(render_camera, width, height, crystal_vertices);
    QuadBatch bag_batch(render_camera, width, height, bag_vertices);
    QuadBatch health_bar_batch(
        render_camera,
        width,
        height,
        health_bar_vertices);
    QuadBatch original_batch(
        render_camera,
        width,
        height,
        original_vertices);
    QuadBatch original_flash_batch(
        render_camera,
        width,
        height,
        original_flash_vertices);
    QuadBatch overlay_batch(render_camera, width, height, overlay_vertices);
    QuadBatch effect_batch(render_camera, width, height, effect_vertices);
    QuadBatch particle_add_batch(
        render_camera,
        width,
        height,
        particle_add_vertices);
    QuadBatch particle_alpha_batch(
        render_camera,
        width,
        height,
        particle_alpha_vertices);

    const core::Map& map = world.map();
    if (map_art_ready_) {
        if (!map_art_reference_valid_) {
            map_art_reference_x_ = camera.x;
            map_art_reference_y_ = camera.y;
            map_art_reference_valid_ = true;
        }
        std::size_t art_vertex_count = 0;
        for (const MapArtLayer& layer : map_art_layers_) {
            art_vertex_count += layer.vertices.size();
        }
        map_art_vertices.reserve(art_vertex_count);
        map_art_ranges.reserve(map_art_layers_.size());
        for (const MapArtLayer& layer : map_art_layers_) {
            const std::size_t begin = map_art_vertices.size();
            const float offset_x =
                (render_camera.x - map_art_reference_x_) * layer.parallax;
            const float offset_y =
                (render_camera.y - map_art_reference_y_) * layer.parallax;
            for (const MapArtVertex& source : layer.vertices) {
                SpriteVertex value;
                value.x =
                    (source.x + offset_x - render_camera.x) *
                        render_camera.pixels_per_unit +
                    static_cast<float>(width) * 0.5f;
                value.y =
                    (source.y + offset_y - render_camera.y) *
                        render_camera.pixels_per_unit +
                    static_cast<float>(height) * 0.5f;
                value.u = source.u;
                value.v = source.v;
                value.r = source.r;
                value.g = source.g;
                value.b = source.b;
                value.a = source.a;
                map_art_vertices.push_back(value);
            }
            if (map_art_vertices.size() > begin) {
                map_art_ranges.push_back(
                    MapArtRange{
                        begin,
                        map_art_vertices.size(),
                        layer.additive});
            }
        }
    } else {
        batch.rect(map.bounds, kBackground);
        for (const core::AABB& solid : map.solids) {
            batch.rect(solid, kSolid);
        }
        for (const core::AABB& platform : map.one_way_platforms) {
            batch.rect(platform, kPlatform);
        }
        for (const core::JumpPad& pad : map.jump_pads) {
            batch.rect(pad.bounds, kJumpPad);
        }
    }
    if (world.game_mode() == core::GameMode::Control &&
        map.objective.has_value()) {
        const Color color = world.objective_owner() == 1
            ? kObjectiveBlue
            : (world.objective_owner() == 2 ? kObjectiveRed : kObjectiveNeutral);
        batch.rect(*map.objective, color);
    }
    const core::Player* local = world.players().get(local_player);
    if (world.game_mode() == core::GameMode::Crystal) {
        const float pulse =
            0.5f + 0.5f * std::sin(presentation_seconds_ * 6.0f);
        for (const core::DropDepot& depot : map.drop_depots) {
            const bool neutral =
                depot.team == core::kDropDepotNeutral;
            const Color base = neutral
                ? kDepotNeutral
                : (depot.team == 0 ? kDepotBlue : kDepotRed);
            batch.rect(depot.bounds, base);
            if (local != nullptr &&
                (neutral || depot.team == local->team)) {
                const std::uint8_t alpha = static_cast<std::uint8_t>(
                    18.0f + pulse * 34.0f);
                batch.rect(
                    depot.bounds.expanded(core::Fix32::from_ratio(3, 10)),
                    Color{base.r, base.g, base.b, alpha});
            }
        }

        const auto draw_crystal = [&](const core::Vec2& position,
                                      core::Fix32 half) {
            const core::AABB box = core::AABB::from_center(
                position,
                core::Vec2{half, half});
            if (crystal_ready_) {
                crystal_batch.sprite(
                    box,
                    0.0f,
                    0.0f,
                    1.0f,
                    1.0f,
                    Color{255, 255, 255, 255});
            } else {
                batch.rect(box, kCrystal);
            }
        };

        const core::Fix32 drop_half = core::Fix32::from_ratio(2, 5);
        world.drops().for_each([&](const core::Drop& drop) {
            if (drop.state ==
                static_cast<std::uint8_t>(core::DropState::Carried)) {
                return;
            }
            if (drop.state ==
                    static_cast<std::uint8_t>(core::DropState::Home) &&
                drop.respawn_timer > 0) {
                return;
            }
            draw_crystal(drop.position, drop_half);
        });

        if (local != nullptr && local->carried_drops > 0) {
            for (const core::DropDepot& depot : map.drop_depots) {
                if (depot.team != core::kDropDepotNeutral &&
                    depot.team != local->team) {
                    continue;
                }
                const core::Vec2 center = depot.bounds.center();
                const std::uint8_t alpha = static_cast<std::uint8_t>(
                    70.0f + pulse * 80.0f);
                const core::Fix32 beam_half =
                    core::Fix32::from_ratio(12, 100);
                const core::Fix32 top = center.y +
                    core::Fix32::from_int(4) +
                    core::Fix32::from_ratio(15, 10) *
                        core::Fix32::from_ratio(
                            static_cast<std::int32_t>(pulse * 1000.0f),
                            1000);
                batch.rect(
                    core::AABB{
                        core::Vec2{center.x - beam_half, center.y},
                        core::Vec2{center.x + beam_half, top}},
                    Color{255, 209, 102, alpha});
            }
        }
    }

    if (!map_art_ready_) {
        const core::Fix32 marker_half = core::Fix32::from_ratio(1, 5);
        for (const core::Vec2& spawn : map.spawns) {
            batch.rect(
                core::AABB{
                    spawn - core::Vec2{marker_half, marker_half},
                    spawn + core::Vec2{marker_half, marker_half}},
                kSpawn);
        }
    }

    const float local_x =
        local == nullptr ? 0.0f : to_float(local->position.x);
    const float local_y =
        local == nullptr ? 0.0f : to_float(local->position.y);

    std::vector<std::uint32_t> live_projectiles;
    live_projectiles.reserve(32);
    const std::uint32_t projectile_tick = world.tick();
    world.projectiles().for_each([&](const core::Projectile& projectile) {
        const std::uint32_t key = projectile.id.raw();
        live_projectiles.push_back(key);
        ProjectileVisual& visual = projectile_visuals_[key];
        const core::Vec2 simulated_start =
            projectile.position - projectile.velocity;
        if (visual.trail.empty()) {
            visual.start_tick = projectile_tick;
            core::Vec2 visual_start = simulated_start;
            const auto owner_visual = visuals_.find(projectile.owner.raw());
            if (owner_visual != visuals_.end() &&
                owner_visual->second.has_muzzle) {
                visual_start = owner_visual->second.muzzle;
            }
            visual.offset = visual_start - simulated_start;
            visual.trail.push_back(visual_start);
            const core::Vec2 display_position =
                projectile.position + visual.offset;
            if (display_position != visual.trail.back()) {
                visual.trail.push_back(display_position);
            }
        } else {
            const core::Vec2 display_position =
                projectile.position + visual.offset;
            if (projectile_tick > visual.start_tick &&
                visual.trail.back() != display_position) {
                visual.trail.push_back(display_position);
                if (visual.trail.size() > 10) {
                    visual.trail.erase(visual.trail.begin());
                }
            }
        }
        visual.last_position = projectile.position + visual.offset;
        visual.explosion_radius = to_float(projectile.explosion_radius);
        visual.explosive = projectile.explosive;
        visual.gadget = projectile.gadget;

        const Color shot_color = projectile_color(projectile);
        if (projectile.gadget != 0 && particle_atlas_ready_) {
            visual.trail_accum += frame_delta;
            constexpr float kTrailInterval = 0.035f;
            static constexpr ParticleLayer kTrailLayer{
                "small_glowdot_sprite", 1, 0.28f, 0.06f, 0.25f, 0.1f,
                0.16f, 0.04f, 6.2831853f, 0.0f, 1.6f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 255, 255, 255, 190, true, false,
                false, false};
            while (visual.trail_accum >= kTrailInterval) {
                visual.trail_accum -= kTrailInterval;
                ParticleVisual particle;
                particle.layer = &kTrailLayer;
                particle.x = to_float(visual.last_position.x);
                particle.y = to_float(visual.last_position.y);
                particle.life = kTrailLayer.lifetime;
                particle.size_start = kTrailLayer.size_start;
                particle.size_end = kTrailLayer.size_end;
                particle.r = shot_color.r;
                particle.g = shot_color.g;
                particle.b = shot_color.b;
                particle.a = 190;
                particle.additive = true;
                particles_.push_back(particle);
            }
        }
        const Color trail_color =
            projectile.gadget != 0
                ? shot_color
                : (projectile.explosive != 0
                       ? kRocket
                       : (projectile.team == 0 ? kTeamBlue : kTeamRed));
        if (visual.trail.size() > 1) {
            for (std::size_t i = 1; i < visual.trail.size(); ++i) {
                const float alpha =
                    0.25f +
                    0.75f *
                        static_cast<float>(i) /
                        static_cast<float>(visual.trail.size() - 1);
                batch.segment(
                    visual.trail[i - 1],
                    visual.trail[i],
                    projectile.explosive != 0 ? 3.0f : 2.0f,
                    with_alpha(trail_color, alpha));
            }
        }

        const core::Fix32 projectile_half = core::Fix32::max(
            projectile.half_size,
            core::Fix32::from_ratio(8, 100));
        const float half_world = to_float(projectile_half);
        if (gadget_projectile_spins(projectile.gadget)) {
            const float spin =
                presentation_seconds_ * 12.0f +
                static_cast<float>(projectile.id.raw() % 360U) *
                    0.0174533f;
            for (int blade = 0; blade < 2; ++blade) {
                const float angle =
                    spin +
                    static_cast<float>(blade) * kPi * 0.5f;
                const core::Vec2 arm{
                    from_float(std::cos(angle) * half_world * 2.2f),
                    from_float(std::sin(angle) * half_world * 2.2f)};
                batch.segment(
                    visual.last_position - arm,
                    visual.last_position + arm,
                    3.0f,
                    shot_color);
            }
            batch.disc(
                visual.last_position,
                half_world,
                12,
                with_alpha(shot_color, 0.8f));
        } else if (gadget_projectile_is_round(projectile.gadget)) {
            batch.disc(
                visual.last_position,
                half_world * 1.5f,
                14,
                shot_color);
        } else {
            const core::AABB box = core::AABB::from_center(
                visual.last_position,
                core::Vec2{projectile_half, projectile_half});
            batch.rect(box, shot_color);
        }
        if (particle_atlas_ready_ &&
            projectile.gadget ==
                static_cast<std::uint8_t>(core::GadgetType::StaticShield) +
                    1) {
            const float shield_x = to_float(visual.last_position.x);
            const float shield_y = to_float(visual.last_position.y);
            const float pulse =
                1.0f + 0.06f * std::sin(presentation_seconds_ * 8.0f);
            const float shield_size =
                std::max(1.0f, half_world * 2.2f) * pulse;
            draw_atlas_billboard(
                particle_add_batch,
                particle_atlas_,
                "CFX2_T_GlowSoft",
                shield_x,
                shield_y,
                shield_size * 1.7f,
                -presentation_seconds_ * 0.9f,
                Color{120, 200, 255, 120});
            draw_atlas_billboard(
                particle_add_batch,
                particle_atlas_,
                "ShieldAlfa",
                shield_x,
                shield_y,
                shield_size,
                presentation_seconds_ * 1.2f,
                Color{160, 225, 255, 210});
        }
    });

    for (auto it = projectile_visuals_.begin();
         it != projectile_visuals_.end();) {
        if (std::find(
                live_projectiles.begin(),
                live_projectiles.end(),
                it->first) != live_projectiles.end()) {
            ++it;
            continue;
        }
        const ProjectileVisual& visual = it->second;
        if (!visual.trail.empty()) {
            EffectVisual effect;
            effect.position = visual.last_position;
            if (visual.explosive != 0) {
                const Color blast_color =
                    gadget_tag_color(visual.gadget);
                effect.kind = EffectKind::Explosion;
                effect.life_seconds = 0.34f;
                effect.radius = std::max(0.6f, visual.explosion_radius);
                effect.r = blast_color.r;
                effect.g = blast_color.g;
                effect.b = blast_color.b;
                if (local != nullptr) {
                    const float dx =
                        local_x - to_float(visual.last_position.x);
                    const float dy =
                        local_y - to_float(visual.last_position.y);
                    const float distance =
                        std::sqrt(dx * dx + dy * dy);
                    const float range = effect.radius + 3.0f;
                    if (distance < range) {
                        add_shake(
                            0.10f *
                            (1.0f - distance / range));
                    }
                }
            } else {
                const Color impact_color =
                    visual.gadget != 0
                        ? gadget_tag_color(visual.gadget)
                        : kImpactRay;
                effect.kind = EffectKind::Impact;
                effect.life_seconds = 0.16f;
                effect.radius = 0.18f;
                effect.r = impact_color.r;
                effect.g = impact_color.g;
                effect.b = impact_color.b;
                if (visual.trail.size() > 1) {
                    effect.direction =
                        (visual.trail.back() -
                         visual.trail[visual.trail.size() - 2])
                            .normalized();
                }
                if (visual.trail.size() > 1) {
                    EffectVisual tracer;
                    tracer.kind = EffectKind::Tracer;
                    tracer.position =
                        visual.trail[visual.trail.size() - 2];
                    tracer.direction =
                        visual.trail.back() - tracer.position;
                    tracer.life_seconds = 0.08f;
                    tracer.radius = 2.5f;
                    tracer.r = kProjectile.r;
                    tracer.g = kProjectile.g;
                    tracer.b = kProjectile.b;
                    add_effect(tracer);
                }
            }
            add_effect(effect);
            if (visual.gadget != 0 && particle_atlas_ready_) {
                float direction_angle = 0.0f;
                if (visual.trail.size() > 1) {
                    const core::Vec2 delta =
                        visual.trail.back() -
                        visual.trail[visual.trail.size() - 2];
                    direction_angle = std::atan2(
                        to_float(delta.y),
                        to_float(delta.x));
                }
                spawn_gadget_particles(
                    static_cast<core::GadgetType>(visual.gadget - 1),
                    visual.explosive != 0,
                    to_float(visual.last_position.x),
                    to_float(visual.last_position.y),
                    direction_angle,
                    core::EntityId{});
            }
        }
        it = projectile_visuals_.erase(it);
    }

    const std::uint32_t sim_tick = world.tick();
    // Original ClientGame.Update / PhysicalArtifact.Interpolate:
    //   on a simulation update: Prev = CurPosition (what is on screen),
    //     Next = simulated position, start = previous frame time,
    //     end = now + 1/30, t = 0
    //   every frame: t += deltaTime;
    //     i = clamp(t / (end - start)); Cur = Prev * (1 - i) + Next * i
    // The previous port reset t *after* advancing time, so with one tick per
    // frame i was always 0 and characters froze, then jumped on frames
    // without a tick (the visible "walk stutter").
    constexpr float kInterpolationSeconds = 1.0f / 30.0f;
    const bool simulation_updated =
        !has_interp_frame_ || sim_tick != interp_frame_tick_;
    if (simulation_updated) {
        interp_frame_tick_ = sim_tick;
        interp_window_start_ = has_interp_frame_
            ? last_position_update_seconds_
            : presentation_seconds_ - frame_delta;
        interp_window_end_ = presentation_seconds_ + kInterpolationSeconds;
        interp_elapsed_ = 0.0f;
        has_interp_frame_ = true;
    }
    interp_elapsed_ += frame_delta;
    float interpolation = 1.0f;
    if (interp_window_end_ > interp_window_start_) {
        interpolation = std::clamp(
            interp_elapsed_ / (interp_window_end_ - interp_window_start_),
            0.0f,
            1.0f);
    }
    last_position_update_seconds_ = presentation_seconds_;
    // Rollback correction blend-out (~90% gone after 0.19s).
    const float correction_decay = std::exp(-12.0f * frame_delta);
    const int local_team =
        local != nullptr ? static_cast<int>(local->team) : -1;
    world.players().for_each([&](const core::Player& player) {
        const bool alive = player.alive != 0;
        PlayerVisualState& visual = visuals_[player.id.raw()];
        visual.correction_x *= correction_decay;
        visual.correction_y *= correction_decay;
        if (std::abs(visual.correction_x) < 0.002f) {
            visual.correction_x = 0.0f;
        }
        if (std::abs(visual.correction_y) < 0.002f) {
            visual.correction_y = 0.0f;
        }
        const float target_x =
            to_float(player.position.x) + visual.correction_x;
        const float target_y =
            to_float(player.position.y) + visual.correction_y;
        // Respawn / death / huge desync: snap instead of sliding.
        constexpr float kSnapDistance = 4.0f;
        const float to_dx = target_x - visual.render_x;
        const float to_dy = target_y - visual.render_y;
        const bool snap = !visual.has_render_position || !alive ||
            to_dx * to_dx + to_dy * to_dy > kSnapDistance * kSnapDistance;
        if (snap) {
            visual.correction_x = 0.0f;
            visual.correction_y = 0.0f;
            const float snap_x = to_float(player.position.x);
            const float snap_y = to_float(player.position.y);
            visual.render_x = snap_x;
            visual.render_y = snap_y;
            visual.interp_from_x = snap_x;
            visual.interp_from_y = snap_y;
            visual.interp_to_x = snap_x;
            visual.interp_to_y = snap_y;
            visual.interp_tick = sim_tick;
            visual.has_render_position = true;
            visual.has_interp = true;
        } else {
            if (simulation_updated || !visual.has_interp ||
                visual.interp_tick != sim_tick) {
                visual.interp_tick = sim_tick;
                visual.interp_from_x = visual.render_x;
                visual.interp_from_y = visual.render_y;
                visual.interp_to_x = target_x;
                visual.interp_to_y = target_y;
                visual.has_interp = true;
            }
            visual.render_x =
                visual.interp_from_x +
                (visual.interp_to_x - visual.interp_from_x) * interpolation;
            visual.render_y =
                visual.interp_from_y +
                (visual.interp_to_y - visual.interp_from_y) * interpolation;
        }
        const bool local = player.id == local_player;
        if (std::FILE* trace = render_trace_file()) {
            std::fprintf(
                trace,
                "%.5f,%.5f,%u,%u,%d,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                presentation_seconds_,
                frame_delta,
                sim_tick,
                static_cast<unsigned>(player.slot),
                local ? 1 : 0,
                to_float(player.position.x),
                visual.render_x,
                visual.correction_x,
                camera.x,
                to_float(player.velocity.x));
        }
        core::Player displayed = player;
        displayed.position = core::Vec2{
            from_float(visual.render_x),
            from_float(visual.render_y)};
        const core::AABB body = core::AABB::from_center(
            displayed.position,
            core::Vec2{
                core::Fix32::from_ratio(35, 100),
                core::Fix32::from_ratio(9, 10)});
        const int health_value =
            player.health.raw() / core::Fix32::kOne;
        const int shield_value = static_cast<int>(player.shield);
        const int ammo_value = static_cast<int>(player.ammo);
        const bool health_lost =
            visual.last_health >= 0 &&
            health_value < visual.last_health;
        const bool shield_lost =
            visual.last_shield >= 0 &&
            shield_value < visual.last_shield;
        const bool fired =
            visual.last_ammo >= 0 &&
            visual.last_weapon == player.weapon &&
            ammo_value < visual.last_ammo &&
            alive;
        const bool died = visual.last_alive && !alive;
        if (health_lost) {
            visual.hurt_seconds = 0.18f;
        } else {
            visual.hurt_seconds =
                std::max(0.0f, visual.hurt_seconds - frame_delta);
        }
        if (fired) {
            visual.shoot_seconds = 0.16f;
        } else if (visual.shoot_seconds > 0.0f) {
            const float decay = original_ready_
                ? hero_poses_.recoil.decay
                : 16.0f;
            visual.shoot_seconds *=
                std::exp(-std::max(0.0f, decay) * frame_delta);
            if (visual.shoot_seconds < 0.001f) {
                visual.shoot_seconds = 0.0f;
            }
        }

        const core::AnimationState state =
            core::select_animation_state(
                player,
                visual.hurt_seconds > 0.0f);
        if (state != visual.last_state) {
            visual.last_state = state;
            visual.state_seconds = 0.0f;
        } else {
            visual.state_seconds += frame_delta;
        }

        const core::HeroRig* original_rig = nullptr;
        if (original_ready_) {
            original_rig = hero_rigs_.find(
                core::hero_name(
                    static_cast<core::HeroType>(player.hero)));
        }
        const bool use_original = original_rig != nullptr;
        const bool flip_placeholder = player.facing < 0;
        const bool flip_original = player.facing > 0;

        const core::AtlasFrame* frame = nullptr;
        if (!use_original && atlas_ready_) {
            const std::string clip_name = core::animation_clip_name(
                core::hero_name(static_cast<core::HeroType>(player.hero)),
                state);
            const core::AnimationClip* clip =
                animations_.find(clip_name);
            if (clip == nullptr && state != core::AnimationState::Idle) {
                clip = animations_.find(core::animation_clip_name(
                    core::hero_name(static_cast<core::HeroType>(player.hero)),
                    core::AnimationState::Idle));
            }
            if (clip != nullptr) {
                std::uint32_t frame_index = 0;
                if (clip->loop) {
                    const std::uint32_t frame_interval =
                        std::max<std::uint32_t>(
                            1U,
                            60U / static_cast<std::uint32_t>(clip->fps));
                    frame_index =
                        (world.tick() / frame_interval +
                         player.id.index()) %
                        static_cast<std::uint32_t>(clip->frames.size());
                } else {
                    frame_index = std::min(
                        static_cast<std::uint32_t>(
                            visual.state_seconds *
                            static_cast<float>(clip->fps)),
                        static_cast<std::uint32_t>(
                            clip->frames.size() - 1U));
                }
                frame = atlas_.find(clip->frames[frame_index]);
            }
        }

        const Color team_color =
            player.team == 0 ? kTeamBlue : kTeamRed;
        if (alive) {
            draw_team_ring(batch, displayed, team_color);
        }

        for (std::size_t slot = 0; slot < core::kGadgetSlots; ++slot) {
            const core::GadgetStats& gadget = core::hero_gadget_stats(
                static_cast<core::HeroType>(player.hero), slot);
            const Color color = gadget_color(gadget.type);
            const bool activated =
                alive && visual.has_gadget_state &&
                player.gadget_cooldowns[slot] >
                    visual.last_gadget_cooldowns[slot] + 1 &&
                player.gadget_cooldowns[slot] + 2 >=
                    gadget.cooldown_ticks;
            if (activated) {
                const GadgetVisualKind kind =
                    gadget_visual_kind(gadget.type);
                EffectVisual effect;
                effect.position = displayed.position;
                effect.direction = player.aim;
                effect.r = color.r;
                effect.g = color.g;
                effect.b = color.b;
                switch (kind) {
                    case GadgetVisualKind::Melee:
                        effect.kind = EffectKind::Slash;
                        effect.life_seconds = 0.22f;
                        effect.radius = 1.35f;
                        break;
                    case GadgetVisualKind::Dash:
                        effect.kind = EffectKind::Streak;
                        effect.life_seconds = 0.22f;
                        effect.radius = 0.75f;
                        break;
                    case GadgetVisualKind::Heal:
                        effect.kind = EffectKind::Pulse;
                        effect.life_seconds = 0.55f;
                        effect.radius = 0.75f;
                        break;
                    case GadgetVisualKind::Cleanse:
                        effect.kind = EffectKind::Pulse;
                        effect.life_seconds = 0.42f;
                        effect.radius = 0.95f;
                        break;
                    case GadgetVisualKind::Immunity:
                        effect.kind = EffectKind::Pulse;
                        effect.life_seconds = 0.48f;
                        effect.radius = 0.8f;
                        break;
                    case GadgetVisualKind::Projectile:
                        effect.kind = EffectKind::Muzzle;
                        effect.life_seconds = 0.09f;
                        effect.radius = 0.28f;
                        break;
                    case GadgetVisualKind::Buff:
                        effect.kind = EffectKind::Pulse;
                        effect.life_seconds = 0.38f;
                        effect.radius = 0.62f;
                        break;
                }
                add_effect(effect);
                if (particle_atlas_ready_) {
                    const float aim_angle = std::atan2(
                        to_float(player.aim.y),
                        to_float(player.aim.x));
                    spawn_particles(
                        gadget_particles(gadget.type).activate,
                        to_float(displayed.position.x),
                        to_float(displayed.position.y) + 0.9f,
                        aim_angle,
                        player.id);
                }
            }
            visual.last_gadget_cooldowns[slot] =
                player.gadget_cooldowns[slot];
        }
        visual.has_gadget_state = true;

        if (alive) {
            const float aura_x = to_float(displayed.position.x);
            const float aura_y = to_float(displayed.position.y) + 0.88f;
            for (std::size_t slot = 0; slot < core::kGadgetSlots; ++slot) {
                if (player.gadget_active_ticks[slot] == 0) {
                    continue;
                }
                const core::GadgetStats& gadget = core::hero_gadget_stats(
                    static_cast<core::HeroType>(player.hero), slot);
                if (gadget.active_ticks == 0) {
                    continue;
                }
                const Color color = gadget_color(gadget.type);
                const float pulse =
                    0.86f +
                    0.14f *
                        std::sin(
                            presentation_seconds_ * 9.0f +
                            static_cast<float>(slot));
                constexpr int aura_segments = 22;
                for (int i = 0; i < aura_segments; ++i) {
                    const float a0 =
                        static_cast<float>(i) *
                        2.0f * kPi /
                        static_cast<float>(aura_segments);
                    const float a1 =
                        static_cast<float>(i + 1) *
                        2.0f * kPi /
                        static_cast<float>(aura_segments);
                    batch.segment(
                        core::Vec2{
                            from_float(
                                aura_x + std::cos(a0) * 0.46f * pulse),
                            from_float(
                                aura_y + std::sin(a0) * 0.12f * pulse)},
                        core::Vec2{
                            from_float(
                                aura_x + std::cos(a1) * 0.46f * pulse),
                            from_float(
                                aura_y + std::sin(a1) * 0.12f * pulse)},
                        2.0f,
                        with_alpha(color, 0.34f));
                }
                batch.disc(
                    displayed.position,
                    0.42f * pulse,
                    18,
                    with_alpha(color, 0.10f));
                const GadgetParticles& definition =
                    gadget_particles(gadget.type);
                if (particle_atlas_ready_ &&
                    !definition.aura.empty() &&
                    definition.aura_rate > 0.0f) {
                    visual.aura_accum[slot] +=
                        frame_delta * definition.aura_rate * 0.2f;
                    if (visual.aura_accum[slot] >= 1.0f) {
                        visual.aura_accum[slot] -= 1.0f;
                        spawn_particles(
                            definition.aura,
                            aura_x,
                            aura_y,
                            0.0f,
                            player.id);
                    }
                }
            }
        }

        if (shield_lost) {
            EffectVisual effect;
            effect.kind = EffectKind::Hit;
            effect.position = displayed.position;
            effect.life_seconds = 0.2f;
            effect.radius = 0.24f;
            effect.r = kHitShield.r;
            effect.g = kHitShield.g;
            effect.b = kHitShield.b;
            add_effect(effect);
            if (player.id == local_player) {
                add_shake(0.018f);
            }
        }
        if (health_lost) {
            if (blood_enabled_) {
                EffectVisual effect;
                effect.kind = EffectKind::Hit;
                effect.position = displayed.position;
                effect.life_seconds = 0.18f;
                effect.radius = 0.2f;
                effect.r = kHitHealth.r;
                effect.g = kHitHealth.g;
                effect.b = kHitHealth.b;
                add_effect(effect);
            }
            if (player.id == local_player) {
                add_shake(0.035f);
            }
        }
        if (died) {
            if (blood_enabled_) {
                EffectVisual effect;
                effect.kind = EffectKind::Explosion;
                effect.position = displayed.position;
                effect.life_seconds = 0.32f;
                effect.radius = 0.45f;
                effect.r = kHitHealth.r;
                effect.g = kHitHealth.g;
                effect.b = kHitHealth.b;
                add_effect(effect);
            }
            if (player.id == local_player) {
                add_shake(0.05f);
            }
        }

        visual.last_health = health_value;
        visual.last_shield = shield_value;
        visual.last_ammo = ammo_value;
        visual.last_weapon = player.weapon;
        visual.last_alive = alive;

        visual.has_muzzle = false;
        if (use_original) {
            const std::string weapon_id =
                player.weapon < core::content().weapon_count()
                    ? core::content().weapon(player.weapon).id
                    : std::string{};
            const float recoil_pulse = std::clamp(
                visual.shoot_seconds / 0.16f,
                0.0f,
                1.0f);
            const std::uint8_t flash_alpha = alive
                ? static_cast<std::uint8_t>(
                      std::clamp(
                          visual.hurt_seconds / 0.18f,
                          0.0f,
                          1.0f) *
                      180.0f)
                : 0;
            visual.has_muzzle = draw_original_hero(
                original_batch,
                original_atlas_,
                *original_rig,
                hero_poses_,
                state,
                weapon_id,
                player.aim,
                visual.state_seconds,
                recoil_pulse,
                to_float(displayed.position.x),
                to_float(displayed.position.y),
                flip_original,
                &visual.muzzle,
                flash_alpha > 0 ? &original_flash_batch : nullptr,
                flash_alpha);
        } else if (frame != nullptr && atlas_ready_) {
            const float sprite_height = 1.8f;
            const float sprite_width =
                sprite_height *
                static_cast<float>(frame->width) /
                static_cast<float>(frame->height);
            const float left =
                to_float(displayed.position.x) -
                to_float(frame->pivot_x) * sprite_width;
            const float top =
                to_float(body.max.y) -
                to_float(frame->pivot_y) * sprite_height;
            const core::AABB sprite_box{
                core::Vec2{from_float(left), from_float(top)},
                core::Vec2{
                    from_float(left + sprite_width),
                    from_float(top + sprite_height)}};

            const float u0 =
                static_cast<float>(frame->x) /
                static_cast<float>(atlas_.width);
            const float v0 =
                static_cast<float>(frame->y) /
                static_cast<float>(atlas_.height);
            const float u1 =
                static_cast<float>(frame->x + frame->width) /
                static_cast<float>(atlas_.width);
            const float v1 =
                static_cast<float>(frame->y + frame->height) /
                static_cast<float>(atlas_.height);
            sprite_batch.sprite(
                sprite_box,
                flip_placeholder ? u1 : u0,
                v0,
                flip_placeholder ? u0 : u1,
                v1,
                alive ? team_color : fade(team_color, 0.55f));
        } else if (alive) {
            batch.rect(body, team_color);
        } else {
            return;
        }

        if (!alive) {
            return;
        }

        if (visual.has_muzzle &&
            player.weapon < core::content().weapon_count()) {
            core::Vec2 direction = player.aim.normalized();
            if (direction == core::Vec2{}) {
                direction = core::Vec2{
                    player.facing < 0 ? -core::Fix32::one()
                                      : core::Fix32::one(),
                    core::Fix32::zero()};
            }
            const core::Vec2 perpendicular{-direction.y, direction.x};
            const core::Vec2 core_muzzle = core::aim_muzzle_point(
                core::content().weapon(player.weapon),
                displayed.position,
                player.aim);
            const core::Fix32 along =
                (visual.muzzle - displayed.position).dot(direction);
            const core::Fix32 offset =
                (core_muzzle - displayed.position).dot(perpendicular);
            visual.muzzle = displayed.position +
                direction * along + perpendicular * offset;
        }

        if (!visual.has_muzzle) {
            visual.muzzle = fallback_muzzle(
                displayed.position, player.aim, player.facing);
            visual.has_muzzle = true;
        }

        if (fired) {
            core::Vec2 direction = player.aim.normalized();
            if (direction == core::Vec2{}) {
                direction = core::Vec2{
                    player.facing < 0 ? -core::Fix32::one() : core::Fix32::one(),
                    core::Fix32::zero()};
            }
            core::Vec2 muzzle = visual.has_muzzle
                ? visual.muzzle
                : fallback_muzzle(
                      displayed.position, player.aim, player.facing);
            EffectVisual effect;
            effect.kind = EffectKind::Muzzle;
            effect.position = muzzle;
            effect.direction = direction;
            effect.life_seconds = 0.07f;
            effect.radius = 0.24f;
            effect.r = team_color.r;
            effect.g = team_color.g;
            effect.b = team_color.b;
            add_effect(effect);

            if (player.weapon < core::content().weapon_count()) {
                const core::WeaponStats& fired_weapon =
                    core::content().weapon(player.weapon);
                if (fired_weapon.explosive == 0 &&
                    fired_weapon.pellets <= 1) {
                    core::Vec2 tracer_end =
                        muzzle +
                        direction *
                            (fired_weapon.projectile_speed *
                             core::Fix32::from_int(2));
                    const core::Segment tracer_path{muzzle, tracer_end};
                    core::Fix32 tracer_hit{core::Fix32::one()};
                    bool tracer_blocked = false;
                    const auto clamp_tracer =
                        [&](const core::AABB& box) {
                            core::Fix32 t_min{};
                            core::Fix32 t_max{};
                            if (!core::segment_intersects_aabb(
                                    tracer_path, box, t_min, t_max)) {
                                return;
                            }
                            const core::Fix32 t = core::Fix32::min(
                                core::Fix32::max(
                                    t_min, core::Fix32::zero()),
                                core::Fix32::one());
                            if (t < tracer_hit) {
                                tracer_hit = t;
                                tracer_blocked = true;
                            }
                        };
                    for (const core::AABB& solid :
                         world.map().solids) {
                        clamp_tracer(solid);
                    }
                    for (std::size_t index = 0;
                         index < world.map().one_way_platforms.size();
                         ++index) {
                        const bool hatchway =
                            index >=
                                world.map().platform_hatchway.size() ||
                            world.map().platform_hatchway[index] != 0;
                        if (hatchway &&
                            fired_weapon.platform_puncher != 0) {
                            continue;
                        }
                        clamp_tracer(
                            world.map().one_way_platforms[index]);
                    }
                    world.players().for_each(
                        [&](const core::Player& other) {
                            if (other.alive == 0 ||
                                other.id == player.id ||
                                other.team == player.team) {
                                return;
                            }
                            clamp_tracer(core::AABB::from_center(
                                other.position,
                                core::Vec2{
                                    core::Fix32::from_ratio(35, 100),
                                    core::Fix32::from_ratio(7, 10)}));
                        });
                    if (tracer_blocked) {
                        tracer_end =
                            muzzle + (tracer_end - muzzle) * tracer_hit;
                    }
                    EffectVisual tracer;
                    tracer.kind = EffectKind::Tracer;
                    tracer.position = muzzle;
                    tracer.direction = tracer_end - muzzle;
                    tracer.life_seconds = 0.06f;
                    tracer.radius = 2.0f;
                    tracer.r = kProjectile.r;
                    tracer.g = kProjectile.g;
                    tracer.b = kProjectile.b;
                    add_effect(tracer);
                }
            }

            if (player.id == local_player) {
                add_shake(0.025f);
                recoil_x_ = std::clamp(
                    recoil_x_ - to_float(direction.x) * 0.035f,
                    -0.09f,
                    0.09f);
                recoil_y_ = std::clamp(
                    recoil_y_ - to_float(direction.y) * 0.035f,
                    -0.09f,
                    0.09f);
            }
        }

        const core::HeroStats& hero =
            core::hero_stats(static_cast<core::HeroType>(player.hero));
        const float health =
            std::clamp(
                to_float(player.health) /
                    static_cast<float>(hero.health),
                0.0f,
                1.0f);
        const int bag_count = static_cast<int>(player.carried_drops);
        const bool carrying = bag_count > 0;
        const float bar_bottom =
            to_float(displayed.position.y) -
            1.1f -
            (carrying ? 1.35f : 0.0f);
        if (health_bar_ready_) {
            // Original CharacterHealthBar colors: local player is green,
            // teammates are blue, enemies are orange.
            const char* color_name =
                player.id == local_player
                    ? "Green"
                    : (static_cast<int>(player.team) == local_team
                           ? "Blue"
                           : "Orange");
            const bool has_shield = hero.shield > 0;
            const std::string back_name =
                std::string(has_shield ? "barBack" : "barBackSingle") +
                color_name;
            const core::AtlasFrame* back =
                health_bar_atlas_.find(back_name);
            const core::AtlasFrame* life =
                health_bar_atlas_.find(
                    std::string("barLife") + color_name);
            const core::AtlasFrame* armor = has_shield
                ? health_bar_atlas_.find(
                      std::string("barArmor") + color_name)
                : nullptr;
            if (back != nullptr && life != nullptr) {
                // Prefab Container scale is 0.013; the fills use the
                // original 84px widget width inside the colored frame.
                constexpr float bar_scale = 0.013f;
                const float center_x = to_float(displayed.position.x);
                const float back_width =
                    static_cast<float>(back->width) * bar_scale;
                const float back_height =
                    static_cast<float>(back->height) * bar_scale;
                const float back_top = bar_bottom - back_height;
                draw_atlas_box(
                    health_bar_batch,
                    health_bar_atlas_,
                    *back,
                    core::AABB{
                        core::Vec2{
                            from_float(center_x - back_width * 0.5f),
                            from_float(back_top)},
                        core::Vec2{
                            from_float(center_x + back_width * 0.5f),
                            from_float(bar_bottom)}},
                    Color{255, 255, 255, 255});

                const float center_y =
                    back_top + back_height * 0.5f;
                const float fill_full_width = 84.0f * bar_scale;
                const float fill_left =
                    center_x - fill_full_width * 0.5f;
                const float life_height =
                    static_cast<float>(life->height) * bar_scale;
                // Double bar: health sits just below the center line,
                // shield occupies the slot above it (NGUI y-up +8px).
                const float life_top = has_shield
                    ? center_y
                    : center_y - life_height * 0.5f;
                if (health > 0.0f) {
                    const float fill_width =
                        fill_full_width * health;
                    const float tile_width =
                        static_cast<float>(life->width) * bar_scale;
                    draw_tiled_bar_fill(
                        health_bar_batch,
                        health_bar_atlas_,
                        *life,
                        fill_left,
                        life_top,
                        fill_width,
                        tile_width,
                        life_height,
                        Color{255, 255, 255, 255});
                }
                if (armor != nullptr && player.shield > 0) {
                    const float shield = std::clamp(
                        static_cast<float>(player.shield) /
                            static_cast<float>(hero.shield),
                        0.0f,
                        1.0f);
                    const float armor_height =
                        static_cast<float>(armor->height) * bar_scale;
                    const float armor_top =
                        center_y - 8.0f * bar_scale;
                    const float armor_width =
                        fill_full_width * shield;
                    if (shield > 0.0f) {
                        constexpr int kArmorBorder = 3;
                        draw_sliced_bar_fill(
                            health_bar_batch,
                            health_bar_atlas_,
                            *armor,
                            fill_left,
                            armor_top,
                            armor_width,
                            fill_full_width,
                            armor_height,
                            kArmorBorder,
                            Color{255, 255, 255, 255});
                    }
                }
            }
        } else {
            const core::Fix32 bar_width =
                core::Fix32::from_ratio(9, 10);
            const core::Fix32 bar_height =
                core::Fix32::from_ratio(1, 12);
            const core::Fix32 bar_y =
                displayed.position.y -
                core::Fix32::from_ratio(11, 10) -
                (carrying ? core::Fix32::from_ratio(27, 20)
                          : core::Fix32::zero());
            if (hero.shield > 0 && player.shield > 0) {
                const float shield = std::clamp(
                    static_cast<float>(player.shield) /
                        static_cast<float>(hero.shield),
                    0.0f,
                    1.0f);
                const core::Fix32 shield_height =
                    core::Fix32::from_ratio(1, 20);
                const core::Fix32 shield_y =
                    bar_y - core::Fix32::from_ratio(3, 20);
                const core::AABB shield_back{
                    core::Vec2{
                        displayed.position.x -
                            bar_width * core::Fix32::from_ratio(1, 2),
                        shield_y},
                    core::Vec2{
                        displayed.position.x +
                            bar_width * core::Fix32::from_ratio(1, 2),
                        shield_y + shield_height}};
                overlay_batch.rect(shield_back, kHealthBack);
                const core::Fix32 shield_fill_width =
                    bar_width * core::Fix32::from_raw(
                        static_cast<std::int32_t>(
                            shield * core::Fix32::kOne));
                overlay_batch.rect(
                    core::AABB{
                        core::Vec2{
                            shield_back.min.x,
                            shield_back.min.y},
                        core::Vec2{
                            shield_back.min.x + shield_fill_width,
                            shield_back.max.y}},
                    kShieldFill);
            }
            const core::AABB bar_back{
                core::Vec2{
                    displayed.position.x -
                        bar_width * core::Fix32::from_ratio(1, 2),
                    bar_y},
                core::Vec2{
                    displayed.position.x +
                        bar_width * core::Fix32::from_ratio(1, 2),
                    bar_y + bar_height}};
            overlay_batch.rect(bar_back, kHealthBack);
            if (health > 0.0f) {
                const core::Fix32 fill_width =
                    bar_width *
                    core::Fix32::from_raw(
                        static_cast<std::int32_t>(
                            health * core::Fix32::kOne));
                const core::AABB bar_fill{
                    core::Vec2{
                        bar_back.min.x,
                        bar_back.min.y},
                    core::Vec2{
                        bar_back.min.x + fill_width,
                        bar_back.max.y}};
                overlay_batch.rect(bar_fill, kHealthFill);
            }
        }

        if (bag_ready_ && carrying) {
            const char* frame_name = bag_count >= 3
                ? "bag_3"
                : (bag_count == 2 ? "bag_2" : "bag_1");
            const core::AtlasFrame* frame =
                bag_atlas_.find(frame_name);
            if (frame != nullptr) {
                constexpr float bag_height = 1.25f;
                const float bag_width =
                    bag_height *
                    static_cast<float>(frame->width) /
                    static_cast<float>(frame->height);
                const float half_width = bag_width * 0.5f;
                const core::Fix32 bag_bottom =
                    displayed.position.y -
                    core::Fix32::from_ratio(19, 20);
                const core::Fix32 bag_top =
                    bag_bottom - from_float(bag_height);
                const core::AABB bag_box{
                    core::Vec2{
                        displayed.position.x -
                            from_float(half_width),
                        bag_top},
                    core::Vec2{
                        displayed.position.x +
                            from_float(half_width),
                        bag_bottom}};
                const float u0 =
                    static_cast<float>(frame->x) /
                    static_cast<float>(bag_atlas_.width);
                const float v0 =
                    static_cast<float>(frame->y) /
                    static_cast<float>(bag_atlas_.height);
                const float u1 =
                    static_cast<float>(frame->x + frame->width) /
                    static_cast<float>(bag_atlas_.width);
                const float v1 =
                    static_cast<float>(frame->y + frame->height) /
                    static_cast<float>(bag_atlas_.height);
                bag_batch.sprite(
                    bag_box,
                    u0,
                    v0,
                    u1,
                    v1,
                    Color{255, 255, 255, 255});

                static constexpr std::uint8_t kDigits[10][5] = {
                    {7, 5, 5, 5, 7},
                    {2, 6, 2, 2, 7},
                    {7, 1, 7, 4, 7},
                    {7, 1, 7, 1, 7},
                    {5, 5, 7, 1, 1},
                    {7, 4, 7, 1, 7},
                    {7, 4, 7, 5, 7},
                    {7, 1, 1, 1, 1},
                    {7, 5, 7, 5, 7},
                    {7, 5, 7, 1, 7},
                };
                int value = std::clamp(bag_count, 1, 9);
                const std::uint8_t* glyph =
                    kDigits[value];
                constexpr float pixel = 0.075f;
                const float number_width = pixel * 3.0f;
                const float number_height = pixel * 5.0f;
                const float number_x =
                    to_float(displayed.position.x) -
                    number_width * 0.5f;
                const float number_y =
                    (to_float(bag_top) + to_float(bag_bottom)) * 0.5f -
                    number_height * 0.5f;
                for (int row = 0; row < 5; ++row) {
                    for (int column = 0; column < 3; ++column) {
                        if ((glyph[row] & (1U << (2 - column))) == 0) {
                            continue;
                        }
                        const float left =
                            number_x + static_cast<float>(column) * pixel;
                        const float top =
                            number_y + static_cast<float>(row) * pixel;
                        overlay_batch.rect(
                            core::AABB{
                                core::Vec2{
                                    from_float(left + 0.018f),
                                    from_float(top + 0.018f)},
                                core::Vec2{
                                    from_float(left + pixel + 0.018f),
                                    from_float(top + pixel + 0.018f)}},
                            Color{15, 24, 36, 230});
                        overlay_batch.rect(
                            core::AABB{
                                core::Vec2{
                                    from_float(left),
                                    from_float(top)},
                                core::Vec2{
                                    from_float(left + pixel),
                                    from_float(top + pixel)}},
                            Color{235, 250, 255, 255});
                    }
                }
            }
        }
    });

    for (const EffectVisual& effect : effects_) {
        const float progress = std::clamp(
            effect.age_seconds / std::max(0.001f, effect.life_seconds),
            0.0f,
            1.0f);
        const float fade = 1.0f - progress;
        const Color base{
            effect.r,
            effect.g,
            effect.b,
            effect.a};

        if (effect.kind == EffectKind::Muzzle) {
            const float dx = to_float(effect.direction.x);
            const float dy = to_float(effect.direction.y);
            const float length = std::sqrt(dx * dx + dy * dy);
            const float dir_x =
                length > 0.001f ? dx / length : 1.0f;
            const float dir_y =
                length > 0.001f ? dy / length : 0.0f;
            const float angle = std::atan2(dir_y, dir_x);
            const float ray_length =
                effect.radius * (1.0f - progress * 0.5f);
            for (const float spread : {-0.55f, 0.0f, 0.55f}) {
                const float a = angle + spread;
                effect_batch.segment(
                    effect.position,
                    effect.position +
                        core::Vec2{
                            from_float(std::cos(a) * ray_length),
                            from_float(std::sin(a) * ray_length)},
                    2.5f,
                    with_alpha(base, fade));
            }
            const float core = effect.radius * 0.22f * (1.0f - progress);
            const float x = to_float(effect.position.x);
            const float y = to_float(effect.position.y);
            effect_batch.rect(
                core::AABB{
                    core::Vec2{
                        from_float(x - core),
                        from_float(y - core)},
                    core::Vec2{
                        from_float(x + core),
                        from_float(y + core)}},
                with_alpha(kMuzzleCore, fade));
            continue;
        }

        if (effect.kind == EffectKind::Tracer) {
            effect_batch.segment(
                effect.position,
                effect.position + effect.direction,
                effect.radius * (1.0f - progress * 0.5f),
                with_alpha(base, fade));
            continue;
        }

        if (effect.kind == EffectKind::Pulse) {
            const float growth =
                1.0f - (1.0f - progress) * (1.0f - progress);
            const float radius =
                std::max(0.05f, effect.radius * growth);
            const int segments = 20;
            for (int i = 0; i < segments; ++i) {
                const float a0 =
                    static_cast<float>(i) *
                    2.0f * kPi /
                    static_cast<float>(segments);
                const float a1 =
                    static_cast<float>(i + 1) *
                    2.0f * kPi /
                    static_cast<float>(segments);
                effect_batch.segment(
                    effect.position +
                        core::Vec2{
                            from_float(std::cos(a0) * radius),
                            from_float(std::sin(a0) * radius)},
                    effect.position +
                        core::Vec2{
                            from_float(std::cos(a1) * radius),
                            from_float(std::sin(a1) * radius)},
                    2.5f,
                    with_alpha(base, fade));
            }
            effect_batch.disc(
                effect.position,
                radius * 0.45f,
                16,
                with_alpha(base, fade * 0.18f));
            continue;
        }

        if (effect.kind == EffectKind::Slash) {
            const float dx = to_float(effect.direction.x);
            const float dy = to_float(effect.direction.y);
            const float length = std::sqrt(dx * dx + dy * dy);
            const float angle =
                length > 0.001f ? std::atan2(dy, dx) : 0.0f;
            const int segments = 9;
            const float radius =
                effect.radius * (0.7f + 0.3f * progress);
            const float span = 1.9f;
            for (int i = 0; i < segments; ++i) {
                const float a0 =
                    angle - span * 0.5f +
                    span * static_cast<float>(i) /
                        static_cast<float>(segments);
                const float a1 =
                    angle - span * 0.5f +
                    span * static_cast<float>(i + 1) /
                        static_cast<float>(segments);
                effect_batch.segment(
                    effect.position +
                        core::Vec2{
                            from_float(std::cos(a0) * radius),
                            from_float(std::sin(a0) * radius)},
                    effect.position +
                        core::Vec2{
                            from_float(std::cos(a1) * radius),
                            from_float(std::sin(a1) * radius)},
                    3.0f,
                    with_alpha(base, fade));
            }
            continue;
        }

        if (effect.kind == EffectKind::Streak) {
            const float dx = to_float(effect.direction.x);
            const float dy = to_float(effect.direction.y);
            const float length = std::sqrt(dx * dx + dy * dy);
            const core::Vec2 direction =
                length > 0.001f
                    ? core::Vec2{
                          from_float(dx / length),
                          from_float(dy / length)}
                    : core::Vec2{core::Fix32::one(), core::Fix32::zero()};
            for (int i = 0; i < 4; ++i) {
                const float offset =
                    0.08f + 0.14f * static_cast<float>(i);
                const core::Vec2 start =
                    effect.position - direction * from_float(offset);
                effect_batch.segment(
                    start,
                    start - direction *
                        from_float(0.24f * effect.radius),
                    std::max(1.0f, 3.0f - static_cast<float>(i) * 0.5f),
                    with_alpha(base, fade * (1.0f - 0.2f * static_cast<float>(i))));
            }
            continue;
        }

        if (effect.kind == EffectKind::Impact ||
            effect.kind == EffectKind::Hit) {
            const int ray_count =
                effect.kind == EffectKind::Impact ? 7 : 5;
            const float ray_length =
                effect.radius * (1.0f - progress);
            for (int i = 0; i < ray_count; ++i) {
                const float a =
                    static_cast<float>(i) *
                        2.0f * kPi /
                        static_cast<float>(ray_count) +
                    0.35f;
                effect_batch.segment(
                    effect.position,
                    effect.position +
                        core::Vec2{
                            from_float(std::cos(a) * ray_length),
                            from_float(std::sin(a) * ray_length)},
                    2.0f,
                    with_alpha(base, fade));
            }
            const float core =
                effect.radius * 0.25f * (1.0f - progress);
            const float x = to_float(effect.position.x);
            const float y = to_float(effect.position.y);
            effect_batch.rect(
                core::AABB{
                    core::Vec2{
                        from_float(x - core),
                        from_float(y - core)},
                    core::Vec2{
                        from_float(x + core),
                        from_float(y + core)}},
                with_alpha(base, fade));
            continue;
        }

        if (effect.kind == EffectKind::Explosion) {
            const float growth =
                1.0f - (1.0f - progress) * (1.0f - progress);
            const float radius =
                std::max(0.08f, effect.radius * growth);
            effect_batch.disc(
                effect.position,
                radius * 0.55f,
                20,
                with_alpha(base, fade * 0.36f));
            effect_batch.disc(
                effect.position,
                radius * 0.27f,
                16,
                with_alpha(kExplosionCore, fade * 0.5f));
            const int segments = 24;
            for (int i = 0; i < segments; ++i) {
                const float a0 =
                    static_cast<float>(i) *
                    2.0f * kPi /
                    static_cast<float>(segments);
                const float a1 =
                    static_cast<float>(i + 1) *
                    2.0f * kPi /
                    static_cast<float>(segments);
                effect_batch.segment(
                    effect.position +
                        core::Vec2{
                            from_float(std::cos(a0) * radius),
                            from_float(std::sin(a0) * radius)},
                    effect.position +
                        core::Vec2{
                            from_float(std::cos(a1) * radius),
                            from_float(std::sin(a1) * radius)},
                    3.0f,
                    with_alpha(base, fade));
            }
            const int sparks = 8;
            for (int i = 0; i < sparks; ++i) {
                const float a =
                    static_cast<float>(i) *
                    2.0f * kPi /
                    static_cast<float>(sparks);
                effect_batch.segment(
                    effect.position +
                        core::Vec2{
                            from_float(
                                std::cos(a) * radius * 0.9f),
                            from_float(
                                std::sin(a) * radius * 0.9f)},
                    effect.position +
                        core::Vec2{
                            from_float(
                                std::cos(a) * radius * 1.14f),
                            from_float(
                                std::sin(a) * radius * 1.14f)},
                    2.0f,
                    with_alpha(base, fade * 0.8f));
            }
        }
    }

    if (particle_atlas_ready_ && !particles_.empty()) {
        for (const ParticleVisual& particle : particles_) {
            if (particle.delay > 0.0f ||
                particle.age >= particle.life ||
                particle.layer == nullptr) {
                continue;
            }
            const core::AtlasFrame* frame =
                particle_atlas_.find(particle.layer->sprite);
            if (frame == nullptr) {
                continue;
            }
            const float progress = std::clamp(
                particle.age / std::max(0.01f, particle.life),
                0.0f,
                1.0f);
            float fade = 1.0f;
            if (progress < 0.08f) {
                fade = progress / 0.08f;
            } else if (progress > 0.55f) {
                fade = (1.0f - progress) / 0.45f;
            }
            fade = std::clamp(fade, 0.0f, 1.0f);
            const float size = std::max(
                0.01f,
                lerp_float(
                    particle.size_start,
                    particle.size_end,
                    progress));
            float rotation = particle.rotation;
            if (particle.stretch) {
                const float speed =
                    std::sqrt(particle.vx * particle.vx +
                              particle.vy * particle.vy);
                if (speed > 0.05f) {
                    rotation = std::atan2(particle.vy, particle.vx);
                }
            }
            const float half = size * 0.5f;
            const float c = std::cos(rotation);
            const float s = std::sin(rotation);
            const auto corner = [&](float lx, float ly) {
                return core::HeroPoint{
                    particle.x + lx * c - ly * s,
                    particle.y + lx * s + ly * c};
            };
            const std::array<core::HeroPoint, 4> corners{
                corner(-half, -half),
                corner(half, -half),
                corner(half, half),
                corner(-half, half)};
            const float u0 =
                static_cast<float>(frame->x) /
                static_cast<float>(particle_atlas_.width);
            const float v0 =
                static_cast<float>(frame->y) /
                static_cast<float>(particle_atlas_.height);
            const float u1 =
                static_cast<float>(frame->x + frame->width) /
                static_cast<float>(particle_atlas_.width);
            const float v1 =
                static_cast<float>(frame->y + frame->height) /
                static_cast<float>(particle_atlas_.height);
            const std::array<core::HeroPoint, 4> uvs{
                core::HeroPoint{u0, v0},
                core::HeroPoint{u1, v0},
                core::HeroPoint{u1, v1},
                core::HeroPoint{u0, v1}};
            const int alpha = std::clamp(
                static_cast<int>(
                    std::lround(
                        static_cast<float>(particle.a) * fade)),
                0,
                255);
            const Color color{
                particle.r,
                particle.g,
                particle.b,
                static_cast<std::uint8_t>(alpha)};
            QuadBatch& target = particle.additive
                ? particle_add_batch
                : particle_alpha_batch;
            target.textured_quad(corners, uvs, color);
        }
    }

    if (world_vertices.empty() &&
        map_art_vertices.empty() &&
        sprite_vertices.empty() &&
        bag_vertices.empty() &&
        original_vertices.empty() &&
        original_flash_vertices.empty() &&
        overlay_vertices.empty() &&
        effect_vertices.empty() &&
        particle_add_vertices.empty() &&
        particle_alpha_vertices.empty()) {
        return;
    }

    const bgfx::VertexLayout layout = sprite_layout();
    const auto submit_range = [&](const std::vector<SpriteVertex>& vertices,
                                  std::size_t begin,
                                  std::size_t end,
                                  bgfx::TextureHandle texture,
                                  bool additive) {
        if (begin >= end || end > vertices.size()) {
            return;
        }
        constexpr std::size_t kMaxBatchVertices = 60000U;
        const std::uint64_t blend = additive
            ? BGFX_STATE_BLEND_FUNC(
                  BGFX_STATE_BLEND_SRC_ALPHA,
                  BGFX_STATE_BLEND_ONE)
            : BGFX_STATE_BLEND_ALPHA;
        for (std::size_t offset = begin;
             offset < end;
             offset += kMaxBatchVertices) {
            const std::size_t chunk_size = std::min(
                kMaxBatchVertices,
                end - offset);
            const std::uint32_t vertex_count =
                static_cast<std::uint32_t>(chunk_size);
            if (bgfx::getAvailTransientVertexBuffer(
                    vertex_count,
                    layout) < vertex_count) {
                return;
            }
            bgfx::TransientVertexBuffer buffer;
            bgfx::allocTransientVertexBuffer(
                &buffer,
                vertex_count,
                layout);
            std::memcpy(
                buffer.data,
                vertices.data() + offset,
                chunk_size * sizeof(SpriteVertex));

            bgfx::setState(
                BGFX_STATE_WRITE_RGB |
                BGFX_STATE_WRITE_A |
                blend);
            bgfx::setTexture(0, sampler, texture);
            bgfx::setVertexBuffer(0, &buffer);
            bgfx::submit(0, program);
        }
    };
    const auto submit = [&](const std::vector<SpriteVertex>& vertices,
                            bgfx::TextureHandle texture,
                            bool additive = false) {
        submit_range(
            vertices,
            0,
            vertices.size(),
            texture,
            additive);
    };

    if (map_art_ready_) {
        for (const MapArtRange& range : map_art_ranges) {
            submit_range(
                map_art_vertices,
                range.begin,
                range.end,
                map_art_texture_,
                range.additive);
        }
    }
    submit(world_vertices, white_texture_);
    if (crystal_ready_) {
        submit(crystal_vertices, crystal_texture_);
    }
    if (bag_ready_) {
        submit(bag_vertices, bag_texture_);
    }
    if (atlas_ready_) {
        submit(sprite_vertices, atlas_texture_);
    }
    if (original_ready_) {
        submit(original_vertices, original_texture_);
        submit(original_flash_vertices, original_texture_, true);
    }
    if (particle_atlas_ready_) {
        submit(particle_alpha_vertices, particle_texture_, false);
        submit(particle_add_vertices, particle_texture_, true);
    }
    if (health_bar_ready_) {
        submit(health_bar_vertices, health_bar_texture_);
    }
    submit(overlay_vertices, white_texture_);
    submit(effect_vertices, white_texture_, true);
}

} // namespace nsync::client
