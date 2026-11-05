#include "core/aim.h"

#include <algorithm>
#include <cmath>

namespace nsync::core {
namespace {

constexpr double kPi = 3.14159265358979323846;

double to_double(Fix32 value) {
    return static_cast<double>(value.raw()) /
           static_cast<double>(Fix32::kOne);
}

Fix32 from_double(double value) {
    return Fix32::from_raw(static_cast<std::int32_t>(
        std::llround(value * static_cast<double>(Fix32::kOne))));
}

} // namespace

Vec2 aim_target_pointer(
    const WeaponStats& weapon,
    Vec2 center,
    Vec2 mark) {
    const double center_x = to_double(center.x);
    const double center_y = -to_double(center.y);
    double mark_x = to_double(mark.x);
    const double mark_y = -to_double(mark.y);
    const double joint_x = to_double(weapon.joint_x);
    const double joint_y = to_double(weapon.joint_y);
    const double gun_x = to_double(weapon.gun_x);
    const double gun_y = to_double(weapon.gun_y);

    int sign = 1;
    if (mark_x >= center_x) {
        sign = 1;
    } else {
        mark_x = center_x + (center_x - mark_x);
        sign = -1;
    }

    const double origin_x = center_x + joint_x;
    const double origin_y = center_y + joint_y;
    double delta_x = mark_x - origin_x;
    double delta_y = mark_y - origin_y;
    double distance = std::sqrt(delta_x * delta_x + delta_y * delta_y);
    if (distance < 1e-14) {
        return Vec2{Fix32::one(), Fix32::zero()};
    }

    double angle =
        std::asin(std::clamp(delta_y / distance, -1.0, 1.0));
    if (delta_x < 0.0) {
        angle = kPi - angle;
    }

    const double minimum_distance =
        std::sqrt(2.0 * (gun_x * gun_x + gun_y * gun_y));
    if (distance < minimum_distance) {
        const double scale = minimum_distance / distance;
        delta_x *= scale;
        delta_y *= scale;
        distance = minimum_distance;
    }

    angle -= std::asin(std::clamp(gun_y / distance, -1.0, 1.0));
    const double out_x = static_cast<double>(sign) * std::cos(angle);
    const double out_y = std::sin(angle);
    return Vec2{from_double(out_x), from_double(-out_y)};
}

Vec2 aim_muzzle_point(
    const WeaponStats& weapon,
    Vec2 center,
    Vec2 aim) {
    return aim_muzzle_point(
        weapon.joint_x,
        weapon.joint_y,
        weapon.gun_x,
        weapon.gun_y,
        center,
        aim);
}

Vec2 aim_muzzle_point(
    Fix32 joint_x,
    Fix32 joint_y,
    Fix32 gun_x,
    Fix32 gun_y,
    Vec2 center,
    Vec2 aim) {
    const Fix32 aim_x = aim.x;
    const Fix32 aim_y = -aim.y;
    Fix32 x = joint_x;
    Fix32 y = joint_y;
    x += Fix32::abs(aim_x) * gun_x - aim_y * gun_y;
    y += aim_y * gun_x + Fix32::abs(aim_x) * gun_y;
    if (aim_x < Fix32::zero()) {
        x = -x;
    }
    return Vec2{center.x + x, center.y - y};
}

} // namespace nsync::core
