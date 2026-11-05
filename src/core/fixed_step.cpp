#include "core/fixed_step.h"

#include <algorithm>
#include <cstdint>

namespace nsync::core {

FixedStepClock::FixedStepClock(int tick_rate, int max_steps)
    : tick_rate_(std::clamp(tick_rate, 1, 1000)),
      max_steps_(std::clamp(max_steps, 1, 60)) {
    step_ = std::chrono::nanoseconds(
        static_cast<std::int64_t>(1'000'000'000LL / tick_rate_));
}

void FixedStepClock::reset(Clock::time_point now) {
    last_ = now;
    accumulator_ = std::chrono::nanoseconds::zero();
    started_ = true;
}

StepAdvance FixedStepClock::advance(Clock::time_point now) {
    StepAdvance result;
    if (!started_) {
        reset(now);
        return result;
    }

    auto delta = now - last_;
    if (delta < std::chrono::nanoseconds::zero()) {
        delta = std::chrono::nanoseconds::zero();
    }
    last_ = now;

    result.frame_seconds =
        std::chrono::duration<double>(delta).count();

    const auto max_frame = step_ * max_steps_;
    if (delta > max_frame) {
        result.dropped_seconds +=
            std::chrono::duration<double>(delta - max_frame).count();
        delta = max_frame;
    }

    accumulator_ += delta;
    while (accumulator_ >= step_ && result.steps < max_steps_) {
        accumulator_ -= step_;
        ++result.steps;
    }

    if (result.steps == max_steps_ && accumulator_ >= step_) {
        result.dropped_seconds +=
            std::chrono::duration<double>(accumulator_).count();
        accumulator_ = std::chrono::nanoseconds::zero();
    }

    result.alpha = static_cast<double>(accumulator_.count()) /
        static_cast<double>(step_.count());
    return result;
}

std::chrono::nanoseconds FixedStepClock::time_until_next_step() const {
    if (accumulator_ >= step_) {
        return std::chrono::nanoseconds::zero();
    }
    return step_ - accumulator_;
}

} // namespace nsync::core
