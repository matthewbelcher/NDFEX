#pragma once

#include "spsc_md_queue.H"

#include <spdlog/spdlog.h>
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

};

} // namespace ndfex::md
