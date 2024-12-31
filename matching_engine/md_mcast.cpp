#include "md_mcast.H"
#include "utils.H"

#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <spdlog/spdlog.h>

#include <iostream>

namespace ndfex {

MarketDataPublisher::MarketDataPublisher(
    const std::string mcastGroup,
    const std::uint16_t mcastPort,
    const std::string localMcastIface,
    std::shared_ptr<spdlog::logger> logger) : logger(logger) {

    multicast_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (multicast_fd < 0) {
        logger->error("Failed to create multicast socket: {}", strerror(errno));
        throw std::runtime_error("Failed to create multicast socket " + std::string(strerror(errno)));
    }

    mcast_addr.sin_family = AF_INET;
    mcast_addr.sin_port = htons(mcastPort);
    mcast_addr.sin_addr.s_addr = inet_addr(mcastGroup.c_str());

    struct in_addr localInterface;
    localInterface.s_addr = inet_addr(localMcastIface.c_str());
    int rc = setsockopt(multicast_fd, IPPROTO_IP, IP_MULTICAST_IF, (char*)&localInterface, sizeof(localInterface));
    if (rc < 0) {
        logger->error("Failed to bind to multicast address: {}", strerror(errno));
        throw std::runtime_error("Failed to bind to multicast address " + std::string(strerror(errno)));
    }

    // set mcast socket to non-blocking
    int flags = fcntl(multicast_fd, F_GETFL, 0);
    if (flags == -1) {
        logger->error("Failed to get socket flags: {}", strerror(errno));
        throw std::runtime_error("Failed to get socket flags " + std::string(strerror(errno)));
    }

    rc = fcntl(multicast_fd, F_SETFL, flags | O_NONBLOCK);
    if (rc == -1) {
        logger->error("Failed to set socket non-blocking: {}", strerror(errno));
        throw std::runtime_error("Failed to set socket non-blocking " + std::string(strerror(errno)));
    }

    char loopch = 1;
    if (setsockopt(multicast_fd, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&loopch, sizeof(loopch)) < 0)
    {
        throw std::runtime_error("Failed to enable multicast loopback");
    }
}

void MarketDataPublisher::publish_new_order(uint64_t order_id, uint32_t symbol, md::SIDE side, uint32_t quantity, uint32_t price, uint8_t flags) {
    md::new_order msg;
    msg.header.length = sizeof(md::new_order);
    msg.header.msg_type = md::MSG_TYPE::NEW_ORDER;
    msg.order_id = order_id;
    msg.symbol = symbol;
    msg.side = side;
    msg.quantity = quantity;
    msg.price = price;
    msg.flags = flags;

    write_msg(msg);
    send();
}

void MarketDataPublisher::publish_delete_order(uint64_t order_id) {
    md::delete_order msg;
    msg.header.length = sizeof(md::delete_order);
    msg.header.msg_type = md::MSG_TYPE::DELETE_ORDER;
    msg.order_id = order_id;

    write_msg(msg);
    send();
}

void MarketDataPublisher::publish_modify_order(uint64_t order_id, md::SIDE side, uint32_t quantity, uint32_t price) {
    md::modify_order msg;
    msg.header.length = sizeof(md::modify_order);
    msg.header.msg_type = md::MSG_TYPE::MODIFY_ORDER;
    msg.order_id = order_id;
    msg.side = side;
    msg.quantity = quantity;
    msg.price = price;

    write_msg(msg);
    send();
}

void MarketDataPublisher::queue_trade(uint64_t order_id, uint32_t quantity, uint32_t price) {
    md::trade msg;
    msg.header.length = sizeof(md::trade);
    msg.header.msg_type = md::MSG_TYPE::TRADE;
    msg.order_id = order_id;
    msg.quantity = quantity;
    msg.price = price;

    queued_trades.push_back(msg);
}

void MarketDataPublisher::publish_queued_trades(uint32_t symbol, md::SIDE aggressor_side) {
    if (queued_trades.empty()) {
        return;
    }

    // get total quantity and last price
    uint32_t total_quantity = 0;
    uint32_t last_price = 0;
    for (const auto& trade : queued_trades) {
        total_quantity += trade.quantity;
        last_price = trade.price;
    }

    md::trade_summary msg;
    msg.header.length = sizeof(md::trade_summary);
    msg.header.msg_type = md::MSG_TYPE::TRADE_SUMMARY;
    msg.symbol = symbol;
    msg.aggressor_side = aggressor_side;
    msg.total_quantity = total_quantity;
    msg.last_price = last_price;

    write_msg(msg);

    for (const auto& trade : queued_trades) {
        write_msg(trade);
    }
    queued_trades.clear();
    send();
}

void MarketDataPublisher::publish_hearbeat() {
    // heartbeats are empty messages
    if (buffer_size + sizeof(md::md_header) > sizeof(buffer)) {
        send();
    }

    md::md_header* header = reinterpret_cast<md::md_header*>(&buffer[buffer_size]);
    header->length = sizeof(md::md_header);
    header->magic_number = md::MAGIC_NUMBER;
    header->seq_num = seq_num++;
    header->timestamp = nanotime();
    header->msg_type = md::MSG_TYPE::HEARTBEAT;

    buffer_size += sizeof(md::md_header);
    send();
}

void MarketDataPublisher::send() {
    if (buffer_size == 0) {
        // nothing to send
        return;
    }

    int rc = sendto(multicast_fd, &buffer, buffer_size, 0, reinterpret_cast<struct sockaddr*>(&mcast_addr), sizeof(mcast_addr));
    if (rc < 0) {
        logger->error("Failed to send payload to multicast group: {}", strerror(errno));
        throw std::runtime_error("Failed to send payload to multicast group " + std::string(strerror(errno)));
    }
    buffer_size = 0;
}

}  // namespace ndfex
