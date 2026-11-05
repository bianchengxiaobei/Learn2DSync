#pragma once

#include "core/content.h"
#include "core/fix32.h"

namespace nsync::core {

Vec2 aim_target_pointer(
    const WeaponStats& weapon,
    Vec2 center,
    Vec2 mark);

Vec2 aim_muzzle_point(
    const WeaponStats& weapon,
    Vec2 center,
    Vec2 aim);

Vec2 aim_muzzle_point(
    Fix32 joint_x,
    Fix32 joint_y,
    Fix32 gun_x,
    Fix32 gun_y,
    Vec2 center,
    Vec2 aim);

} // namespace nsync::core
