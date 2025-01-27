#pragma once

#include "spsc_md_queue.H"
#include <spdlog/spdlog.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <unordered_map>
#include <list>
#include <string>
#include <cstdint>

/**
 * The Snapshot Writer maintains the current state of open orders and periodically writes a snapshot to the multicast.
 */

namespace ndfex::md {

class SnapshotWriter {

public:

    SnapshotWriter(std::string ip, uint16_t port, std::string bind_ip, std::shared_ptr<spdlog::logger> logger,
                   SPSCMDQueue &queue);

    ~SnapshotWriter();

    void process();

    void write_order(const new_order& order);

    void write_snapshot_info(const snapshot_info& info);

    void send();

private:

    std::shared_ptr<spdlog::logger> logger;

    int mcast_fd;
    struct sockaddr_in mcast_addr;
    char buffer[1500];
    size_t buffer_size{0};

    std::uint32_t last_md_seq_num{0};
    std::uint64_t last_snapshot_ts{0};

    SPSCMDQueue &queue;

    std::unordered_map<uint32_t, std::vector<new_order>> symbol_to_bids;
    std::unordered_map<uint32_t, std::vector<new_order>> symbol_to_asks;

    std::unordered_map<uint64_t, uint32_t> order_to_symbol;
};

} // namespace ndfex::md
