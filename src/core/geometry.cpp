#include "core/geometry.h"

namespace nsync::core {
namespace {

bool clip_axis(Fix32 direction, Fix32 offset, Fix32& t_min, Fix32& t_max) {
    if (direction == Fix32::zero()) {
        return offset >= Fix32::zero();
    }

    const Fix32 ratio = offset / direction;
    if (direction < Fix32::zero()) {
        if (ratio > t_max) {
            return false;
        }
        if (ratio > t_min) {
            t_min = ratio;
        }
    } else {
        if (ratio < t_min) {
            return false;
        }
        if (ratio < t_max) {
            t_max = ratio;
        }
    }
    return true;
}

} // namespace

bool segment_intersects_aabb(const Segment& segment, const AABB& box) {
    Fix32 t_min;
    Fix32 t_max;
    return segment_intersects_aabb(segment, box, t_min, t_max);
}

bool segment_intersects_aabb(
    const Segment& segment,
    const AABB& box,
    Fix32& t_min,
    Fix32& t_max) {
    t_min = Fix32::zero();
    t_max = Fix32::one();

    const Vec2 delta = segment.b - segment.a;
    if (!clip_axis(-delta.x, segment.a.x - box.min.x, t_min, t_max)) {
        return false;
    }
    if (!clip_axis(delta.x, box.max.x - segment.a.x, t_min, t_max)) {
        return false;
    }
    if (!clip_axis(-delta.y, segment.a.y - box.min.y, t_min, t_max)) {
        return false;
    }
    if (!clip_axis(delta.y, box.max.y - segment.a.y, t_min, t_max)) {
        return false;
    }
    return t_min <= t_max;
}

} // namespace nsync::core
