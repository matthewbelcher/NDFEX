#pragma once

#include <ctime>
#include <cstdint>

namespace ndfex {

    uint64_t nanotime() {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return ts.tv_sec * 1e9 + ts.tv_nsec;
    }

} // namespace ndfex
