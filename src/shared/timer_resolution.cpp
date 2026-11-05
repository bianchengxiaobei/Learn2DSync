#include "shared/timer_resolution.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#endif

namespace nsync::shared {

HighResolutionTimer::HighResolutionTimer() {
#ifdef _WIN32
    active_ = timeBeginPeriod(1) == TIMERR_NOERROR;
#endif
}

HighResolutionTimer::~HighResolutionTimer() {
#ifdef _WIN32
    if (active_) {
        timeEndPeriod(1);
    }
#endif
}

} // namespace nsync::shared
