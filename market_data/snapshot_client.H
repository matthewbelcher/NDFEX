#pragma once

#include "spsc_md_queue.H"

#include <spdlog/spdlog.h>
#include <chrono>
#include <cstdint>


/**
 * The snapshot client listens for market data updates and puts them into an spsc queue
 */

namespace ndfex::md {

class SnapshotClient {

public:

    SnapshotClient(std::string ip, uint16_t port, std::string bind_ip, std::shared_ptr<spdlog::logger> logger,
                   SPSCMDQueue &queue);

    ~SnapshotClient();

    void process();

private:

    uint8_t buf[4096];
    std::shared_ptr<spdlog::logger> logger;

    int mcast_fd;

    SPSCMDQueue &queue;

    uint32_t last_seq_num = 0;

    // Throttle state for bad-magic / bad-seq logging. Async spdlog queues
    // these in memory; if they fire on every packet during a misroute the
    // queue can grow unboundedly and OOM the process.
    uint64_t bad_magic_suppressed = 0;
    uint64_t bad_seq_suppressed = 0;
    std::chrono::steady_clock::time_point bad_magic_window_start{};
    std::chrono::steady_clock::time_point bad_seq_window_start{};

};

} // namespace ndfex::md
