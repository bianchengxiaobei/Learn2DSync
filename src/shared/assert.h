#pragma once

#include "shared/log.h"

#include <cstdlib>

namespace nsync::shared {

[[noreturn]] inline void fatal(
    const char* expression,
    const char* file,
    int line,
    const char* message = nullptr) {
    NSYNC_LOG_ERROR(
        "assert",
        expression,
        " failed at ",
        file,
        ":",
        line,
        (message != nullptr ? ": " : ""),
        (message != nullptr ? message : ""));
    std::abort();
}

} // namespace nsync::shared

#ifndef NDEBUG
#define NSYNC_ASSERT(expression) \
    do { \
        if (!(expression)) { \
            ::nsync::shared::fatal(#expression, __FILE__, __LINE__); \
        } \
    } while (0)
#else
#define NSYNC_ASSERT(expression) ((void)0)
#endif

#define NSYNC_CHECK(expression) \
    do { \
        if (!(expression)) { \
            ::nsync::shared::fatal(#expression, __FILE__, __LINE__); \
        } \
    } while (0)
