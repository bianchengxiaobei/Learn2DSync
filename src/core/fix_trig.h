#pragma once

#include "core/fix32.h"
#include "core/fix_trig_table.h"

namespace nsync::core {

inline Fix32 sin_deg(Fix32 degrees) {
    constexpr std::int32_t kFullTurnRaw = 360 * Fix32::kOne;
    std::int32_t normalized = degrees.raw() % kFullTurnRaw;
    if (normalized < 0) {
        normalized += kFullTurnRaw;
    }

    const std::int64_t index_fp =
        (static_cast<std::int64_t>(normalized) * 256) / 360;
    const std::int32_t index = static_cast<std::int32_t>(index_fp >> Fix32::kFracBits);
    const std::int32_t fraction = static_cast<std::int32_t>(index_fp & (Fix32::kOne - 1));

    const std::int32_t a = kSinTable[index & 255];
    const std::int32_t b = kSinTable[(index + 1) & 255];
    const std::int32_t delta = static_cast<std::int32_t>(
        (static_cast<std::int64_t>(b - a) * fraction) / Fix32::kOne);

    return Fix32::from_raw(a + delta);
}

inline Fix32 sin_deg(std::int32_t degrees) {
    return sin_deg(Fix32::from_int(degrees));
}

inline Fix32 cos_deg(Fix32 degrees) {
    return sin_deg(Fix32::from_raw(degrees.raw() + 90 * Fix32::kOne));
}

inline Fix32 cos_deg(std::int32_t degrees) {
    return cos_deg(Fix32::from_int(degrees));
}

} // namespace nsync::core
