#pragma once

#include <chrono>

namespace nsync::core {

struct StepAdvance {
    int steps = 0;
    double alpha = 0.0;
    double frame_seconds = 0.0;
    double dropped_seconds = 0.0;
};

class FixedStepClock {
public:
    using Clock = std::chrono::steady_clock;

    explicit FixedStepClock(int tick_rate = 60, int max_steps = 5);

    void reset(Clock::time_point now);
    StepAdvance advance(Clock::time_point now);

    [[nodiscard]] std::chrono::nanoseconds step_duration() const { return step_; }
    [[nodiscard]] std::chrono::nanoseconds time_until_next_step() const;
    [[nodiscard]] int tick_rate() const { return tick_rate_; }
    [[nodiscard]] int max_steps() const { return max_steps_; }
    [[nodiscard]] bool started() const { return started_; }

private:
    std::chrono::nanoseconds step_{};
    std::chrono::nanoseconds accumulator_{};
    Clock::time_point last_{};
    int tick_rate_ = 60;
    int max_steps_ = 5;
    bool started_ = false;
};

} // namespace nsync::core
