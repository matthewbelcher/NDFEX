#pragma once

#include "oe_protocol.H"
#include <spdlog/logger.h>

namespace ndfex::bots {

struct user_info {
    std::string username;
    std::string password;
    uint32_t client_id;
};

class OEClient {

public:
    OEClient(user_info user, std::string ip, uint16_t port, std::shared_ptr<spdlog::logger> logger);

    bool login();

    void send_order(uint32_t symbol, uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags);

    void cancel_order(uint64_t order_id);

    void modify_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price);

    void process();

    int32_t get_position(uint32_t symbol) const;

private:

    struct open_order {
        md::SIDE side;
        uint32_t symbol;
    };

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
};

} // namespace ndfex::bots
