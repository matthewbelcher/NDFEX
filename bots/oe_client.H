#pragma once

#include "oe_protocol.H"
#include <spdlog/logger.h>

#include <cstdint>
#include <deque>

namespace ndfex::bots {

struct user_info {
    std::string username;
    std::string password;
    uint32_t client_id;
};

class OEClient {

public:
    // max_msgs_per_sec should sit below the matching engine's OE rate limit
    // (oe_stream_parser.H:MAX_MSGS_PER_SECOND, currently 1000) to keep the
    // bot from getting disconnected when it bursts. 900 leaves a margin.
    OEClient(user_info user, std::string ip, uint16_t port, std::shared_ptr<spdlog::logger> logger,
             size_t max_msgs_per_sec = 900);

    bool login();

    void send_order(uint32_t symbol, uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags);

    void cancel_order(uint64_t order_id);

    void modify_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price);

    void process();

    int32_t get_position(uint32_t symbol) const;

    // Returns true if this client has sent fewer than max_msgs_per_sec
    // order-entry messages in the trailing 1 second window. Callers should
    // gate any bot-driven message emission on this so a burst from the
    // market makers never gets us rate-limited by the matching engine.
    bool can_send();

private:

    struct open_order {
        md::SIDE side;
        uint32_t symbol;
    };

    // Drop entries from send_timestamps older than 1 second from now.
    void prune_send_window(uint64_t now_ns);

    // Record that an OE message has been sent now; called from inside the
    // send_order / cancel_order / modify_order helpers.
    void record_send();

    int sock_fd;
    std::vector<char> buffer;
    uint32_t client_seq{0};
    uint64_t session_id{0};

    user_info user;
    const std::string ip;
    const uint16_t port;
    std::shared_ptr<spdlog::logger> logger;

    std::unordered_map<uint32_t, int32_t> positions;
    std::unordered_map<uint32_t, open_order> open_orders;

    // Sliding-window send rate limiter. Stores nanosecond timestamps of the
    // OE messages this client has written to the wire in the last second.
    const size_t max_msgs_per_sec;
    std::deque<uint64_t> send_timestamps;
};

} // namespace ndfex::bots
