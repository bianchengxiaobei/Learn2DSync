#include "core/rng.h"

namespace nsync::core {
namespace {

constexpr std::uint64_t kMultiplier = 6364136223846793005ULL;

} // namespace

Pcg32::Pcg32(std::uint64_t seed_value, std::uint64_t stream) {
    seed(seed_value, stream);
}

void Pcg32::seed(std::uint64_t seed_value, std::uint64_t stream) {
    state_ = 0;
    increment_ = (stream << 1U) | 1U;
    next_u32();
    state_ += seed_value;
    next_u32();
}

std::uint32_t Pcg32::next_u32() {
    const std::uint64_t old_state = state_;
    state_ = old_state * kMultiplier + increment_;

    const std::uint32_t xorshifted =
        static_cast<std::uint32_t>(((old_state >> 18U) ^ old_state) >> 27U);
    const std::uint32_t rotation = static_cast<std::uint32_t>(old_state >> 59U);
    return (xorshifted >> rotation) | (xorshifted << ((0U - rotation) & 31U));
}

std::uint32_t Pcg32::bounded(std::uint32_t bound) {
    if (bound <= 1) {
        return 0;
    }

    const std::uint32_t threshold = (0U - bound) % bound;
    for (;;) {
        const std::uint32_t value = next_u32();
        if (value >= threshold) {
            return value % bound;
        }
    }
}

bool Pcg32::restore(const Pcg32State& state) {
    if ((state.increment & 1U) == 0) {
        return false;
    }
    state_ = state.state;
    increment_ = state.increment;
    return true;
}

} // namespace nsync::core
