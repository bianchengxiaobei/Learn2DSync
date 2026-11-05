#pragma once

#include <cstdint>

namespace nsync::core {

struct Pcg32State {
    std::uint64_t state = 0;
    std::uint64_t increment = 1;
};

class Pcg32 {
public:
    Pcg32() = default;
    explicit Pcg32(std::uint64_t seed, std::uint64_t stream = 0x9E3779B97F4A7C15ULL);

    void seed(std::uint64_t seed, std::uint64_t stream = 0x9E3779B97F4A7C15ULL);
    std::uint32_t next_u32();
    std::uint32_t bounded(std::uint32_t bound);

    [[nodiscard]] std::uint64_t state() const { return state_; }
    [[nodiscard]] std::uint64_t increment() const { return increment_; }

    [[nodiscard]] Pcg32State snapshot() const { return {state_, increment_}; }
    bool restore(const Pcg32State& state);

private:
    std::uint64_t state_ = 0;
    std::uint64_t increment_ = 1;
};

} // namespace nsync::core
