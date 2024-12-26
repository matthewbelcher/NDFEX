#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>
#include <arpa/inet.h>

#include "spsc_md_queue.H"
#include <spdlog/spdlog.h>

#include <market_data/md_protocol.H>


namespace ndfex {

class MarketDataPublisher {

public:
    MarketDataPublisher(std::vector<std::unique_ptr<SPSCMDQueue>>& queues,
        const std::string mcastGroup,
        const std::uint16_t mcastPort,
        const std::string localMcastIface,
        std::shared_ptr<spdlog::logger> logger);

    void process();

private:
    std::vector<std::unique_ptr<SPSCMDQueue>>& queues;
    std::shared_ptr<spdlog::logger> logger;
    int multicast_fd;
    struct sockaddr_in mcast_addr;

    char buffer[1500];
    size_t buffer_size{0};

    std::uint32_t seq_num{0};
};

}  // namespace ndfex