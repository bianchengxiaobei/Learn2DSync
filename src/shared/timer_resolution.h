#pragma once

namespace nsync::shared {

// Requests 1ms OS timer resolution for the lifetime of the object.
// The default Windows timer makes std::this_thread::sleep_for(1ms) sleep
// 12-20ms, so the 60Hz server/client loops stepped in bursts (0, 1 or 2 ticks
// per update) and snapshots/inputs went out irregularly.
class HighResolutionTimer {
public:
    HighResolutionTimer();
    ~HighResolutionTimer();

    HighResolutionTimer(const HighResolutionTimer&) = delete;
    HighResolutionTimer& operator=(const HighResolutionTimer&) = delete;

private:
    bool active_ = false;
};

} // namespace nsync::shared
