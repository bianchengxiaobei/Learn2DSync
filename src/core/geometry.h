#pragma once

#include "core/fix32.h"

namespace nsync::core {

struct AABB {
    Vec2 min{};
    Vec2 max{};

    constexpr AABB() = default;
    constexpr AABB(Vec2 min_, Vec2 max_) : min(min_), max(max_) {}

    static constexpr AABB from_center(Vec2 center, Vec2 half_extents) {
        return {center - half_extents, center + half_extents};
    }

    constexpr Vec2 center() const { return (min + max) * Fix32::from_ratio(1, 2); }
    constexpr Vec2 half_extents() const { return (max - min) * Fix32::from_ratio(1, 2); }

    constexpr bool overlaps(const AABB& other) const {
        return min.x < other.max.x && max.x > other.min.x &&
               min.y < other.max.y && max.y > other.min.y;
    }

    constexpr bool contains(Vec2 point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y;
    }

    constexpr AABB expanded(Fix32 amount) const {
        return {min - Vec2{amount, amount}, max + Vec2{amount, amount}};
    }

    constexpr AABB translated(Vec2 offset) const {
        return {min + offset, max + offset};
    }

    friend constexpr bool operator==(const AABB& a, const AABB& b) {
        return a.min == b.min && a.max == b.max;
    }

    friend constexpr bool operator!=(const AABB& a, const AABB& b) {
        return !(a == b);
    }
};

struct Segment {
    Vec2 a{};
    Vec2 b{};

    constexpr Segment() = default;
    constexpr Segment(Vec2 a_, Vec2 b_) : a(a_), b(b_) {}
};

bool segment_intersects_aabb(const Segment& segment, const AABB& box);
bool segment_intersects_aabb(const Segment& segment, const AABB& box, Fix32& t_min, Fix32& t_max);

} // namespace nsync::core
