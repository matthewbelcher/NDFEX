#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>
#include <arpa/inet.h>

#include "utils.H"

#include <spdlog/spdlog.h>
#include <market_data/md_protocol.H>


namespace ndfex {

class MarketDataPublisher {

public:
    MarketDataPublisher(const std::string mcastGroup,
        const std::uint16_t mcastPort,
        const std::string localMcastIface,
        std::shared_ptr<spdlog::logger> logger);

    void publish_new_order(uint64_t order_id, uint32_t symbol, md::SIDE side, uint32_t quantity, uint32_t price, uint8_t flags);

    void publish_delete_order(uint64_t order_id);

    void publish_modify_order(uint64_t order_id, md::SIDE side, uint32_t quantity, uint32_t price);

    void queue_trade(uint64_t order_id, uint32_t quantity, uint32_t price);

    void publish_queued_trades(uint32_t symbol, md::SIDE aggressor_side);

    void publish_hearbeat();

private:

    template <typename Msg>
    void write_msg(Msg& msg) {
        if (buffer_size + msg.header.length > sizeof(buffer)) {
            send();
        }

        md::md_header* header = reinterpret_cast<md::md_header*>(&buffer[buffer_size]);
        std::memcpy(&buffer[buffer_size], &msg, msg.header.length);

        header->magic_number = md::MAGIC_NUMBER;
        header->seq_num = seq_num++;
        header->timestamp = nanotime();
        header->msg_type = msg.header.msg_type;

        buffer_size +=  msg.header.length;
    }

    void send();

    std::shared_ptr<spdlog::logger> logger;
    int multicast_fd;
    struct sockaddr_in mcast_addr;

    char buffer[1500];
    size_t buffer_size{0};

    std::uint32_t seq_num{0};

    std::vector<md::trade> queued_trades; // trade messages are queued up to be sent in a single packet
};

}  // namespace ndfex