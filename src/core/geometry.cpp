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

namespace {

Fix32 clamp01(Fix32 value) {
    return Fix32::min(Fix32::max(value, Fix32::zero()), Fix32::one());
}

} // namespace

bool swept_point_hits_segment(
    const Segment& path,
    const Segment& segment,
    Fix32 radius,
    Fix32& hit_t,
    Vec2& normal,
    bool one_sided) {
    hit_t = Fix32::one();
    normal = {};

    const Fix32 radius_sq = radius * radius;
    const Vec2 move = path.b - path.a;
    const Fix32 move_sq = move.length_sq();
    const Vec2 ab = segment.b - segment.a;
    const Fix32 ab_sq = ab.length_sq();
    bool found = false;

    const auto consider = [&](Fix32 t, Vec2 candidate_normal) {
        if (t < Fix32::zero() || t > Fix32::one()) {
            return;
        }
        if (!found || t < hit_t) {
            found = true;
            hit_t = t;
            normal = candidate_normal;
        }
    };

    Vec2 edge_normal{};
    bool have_edge_normal = false;

    if (ab_sq > Fix32::zero()) {
        const Fix32 ab_length = Fix32::sqrt(ab_sq);
        const Vec2 along = ab / ab_length;
        const Vec2 side{-along.y, along.x};
        if (one_sided && !(move.dot(side) < Fix32::zero())) {
            return false;
        }
        const Fix32 start_side = (path.a - segment.a).dot(side);
        const Fix32 end_side = (path.b - segment.a).dot(side);
        edge_normal = start_side >= Fix32::zero()
            ? side
            : Vec2{-side.x, -side.y};
        have_edge_normal = true;

        Fix32 t{};
        bool crossing = false;
        if (start_side > radius && end_side <= radius) {
            const Fix32 span = start_side - end_side;
            if (span > Fix32::zero()) {
                t = (start_side - radius) / span;
                crossing = true;
            }
        } else if (start_side < -radius && end_side >= -radius) {
            const Fix32 span = end_side - start_side;
            if (span > Fix32::zero()) {
                t = (-radius - start_side) / span;
                crossing = true;
            }
        } else if (Fix32::abs(start_side) <= radius) {
            const bool toward = start_side >= Fix32::zero()
                ? end_side < start_side
                : end_side > start_side;
            if (toward) {
                t = Fix32::zero();
                crossing = true;
            }
        }
        if (crossing) {
            t = clamp01(t);
            const Vec2 contact = path.a + move * t;
            const Fix32 along_contact = (contact - segment.a).dot(along);
            if (along_contact >= Fix32::zero() &&
                along_contact <= ab_length) {
                const Vec2 direction =
                    start_side >= Fix32::zero() ? side : Vec2{-side.x, -side.y};
                consider(t, direction);
            }
        }
    }

    const auto sweep_endpoint = [&](Vec2 center) {
        if (!have_edge_normal ||
            !(move.dot(edge_normal) < Fix32::zero())) {
            return;
        }
        if (move_sq <= Fix32::from_raw(64)) {
            const Vec2 offset = path.a - center;
            if (offset.length_sq() <= radius_sq) {
                Vec2 direction = offset.normalized();
                if (direction == Vec2{}) {
                    direction = Vec2{Fix32::zero(), -Fix32::one()};
                }
                consider(Fix32::zero(), direction);
            }
            return;
        }
        const Vec2 offset = path.a - center;
        const Fix32 projection = offset.dot(move) / move_sq;
        Fix32 t_closest = Fix32::zero() - projection;
        t_closest = Fix32::min(
            Fix32::max(t_closest, Fix32::from_int(-1000)),
            Fix32::from_int(1000));
        const Vec2 closest = path.a + move * t_closest;
        const Vec2 delta = closest - center;
        if (Fix32::abs(delta.x) > radius || Fix32::abs(delta.y) > radius) {
            return;
        }
        const Fix32 distance_sq = delta.length_sq();
        if (distance_sq > radius_sq) {
            return;
        }
        const Fix32 half_span = Fix32::sqrt((radius_sq - distance_sq) / move_sq);
        const Fix32 enter = t_closest - half_span;
        const Fix32 exit = t_closest + half_span;
        Fix32 t{};
        if (enter >= Fix32::zero()) {
            t = enter;
        } else if (exit >= Fix32::zero()) {
            if (offset.dot(move) >= Fix32::zero()) {
                return;
            }
            t = Fix32::zero();
        } else {
            return;
        }
        const Vec2 contact = path.a + move * t;
        Vec2 direction = (contact - center).normalized();
        if (direction == Vec2{}) {
            direction = Vec2{Fix32::zero(), -Fix32::one()};
        }
        consider(t, direction);
    };

    sweep_endpoint(segment.a);
    sweep_endpoint(segment.b);

    return found;
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
