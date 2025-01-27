#include "md_client.H"
#include "market_data/md_protocol.H"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

namespace ndfex::bots {

MDClient::MDClient(std::string ip, uint16_t port, std::string bind_ip, std::shared_ptr<spdlog::logger> logger) : logger(logger) {

    mcast_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (mcast_fd < 0) {
        logger->error("Failed to create socket: {}", strerror(errno));
        throw std::runtime_error("Failed to create socket");
    }

    // set mcast socket to non-blocking
    int flags = fcntl(mcast_fd, F_GETFL, 0);
    if (flags == -1) {
        logger->error("Failed to get socket flags: {}", strerror(errno));
        throw std::runtime_error("Failed to get socket flags");
    }

    if (fcntl(mcast_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        logger->error("Failed to set socket to non-blocking: {}", strerror(errno));
        throw std::runtime_error("Failed to set socket to non-blocking");
    }

    // set SO_REUSEPORT
    int optval = 1;
    if (setsockopt(mcast_fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) < 0) {
        logger->error("Failed to set SO_REUSEPORT: {}", strerror(errno));
        throw std::runtime_error("Failed to set SO_REUSEPORT");
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(mcast_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        logger->error("Failed to bind socket: {}", strerror(errno));
        throw std::runtime_error("Failed to bind socket");
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(ip.c_str());
    mreq.imr_interface.s_addr = inet_addr(bind_ip.c_str());

    if (setsockopt(mcast_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0)
    {
        throw std::runtime_error("Failed to join multicast group");
    }

    logger->info("Listening on {}:{}", ip, port);
}

MDClient::~MDClient() {
    close(mcast_fd);
    for (auto& [symbol, order_book] : symbol_to_order_book) {
        delete order_book;
    }
}

void MDClient::wait_for_hearbeat() {
    while (true) {
        ssize_t len = recvfrom(mcast_fd, buf, sizeof(buf), 0, nullptr, nullptr);
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            logger->error("Failed to receive message: {}", strerror(errno));
            throw std::runtime_error("Failed to receive message");
        }

        md::md_header* header = reinterpret_cast<md::md_header*>(buf);
        if (header->magic_number != md::MAGIC_NUMBER) {
            logger->error("Invalid magic number: {}", header->magic_number);
            return;
        }

        if (header->msg_type == md::MSG_TYPE::HEARTBEAT) {
            return;
        }
    }
}

void MDClient::process() {
    ssize_t len = recvfrom(mcast_fd, buf, sizeof(buf), 0, nullptr, nullptr);
    if (len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        logger->error("Failed to receive message: {}", strerror(errno));
        throw std::runtime_error("Failed to receive message");
    }
    process_message(buf, len);
}

OrderBook::PriceLevel MDClient::get_best_bid(uint32_t symbol) const {
    if (symbol_to_order_book.find(symbol) == symbol_to_order_book.end()) {
        return {0, 0};
    }
    return symbol_to_order_book.at(symbol)->get_best_bid();
}

OrderBook::PriceLevel MDClient::get_best_ask(uint32_t symbol) const {
    if (symbol_to_order_book.find(symbol) == symbol_to_order_book.end()) {
        return {0, 0};
    }
    return symbol_to_order_book.at(symbol)->get_best_ask();
}

void MDClient::process_message(uint8_t* buf, size_t len) {
    while (len > 0) {
        if (len < sizeof(md::md_header)) {
            logger->error("Message too short: {}", len);
            return;
        }

        md::md_header* header = reinterpret_cast<md::md_header*>(buf);
        if (header->magic_number != md::MAGIC_NUMBER) {
            logger->error("Invalid magic number: {}", header->magic_number);
            return;
        }

        logger->info("Message type: {}", static_cast<uint8_t>(header->msg_type));
        logger->info("Sequence number: {}", header->seq_num);

        switch (header->msg_type) {
            case md::MSG_TYPE::NEW_ORDER: {
                if (len < sizeof(md::new_order)) {
                    logger->error("Message too short for new order: {}", len);
                    return;
                }

                md::new_order* new_order = reinterpret_cast<md::new_order*>(buf);
                if (symbol_to_order_book.find(new_order->symbol) == symbol_to_order_book.end()) {
                    symbol_to_order_book[new_order->symbol] = new OrderBook(logger);
                }
                symbol_to_order_book[new_order->symbol]->new_order(new_order->order_id, new_order->side, new_order->quantity, new_order->price, new_order->flags);
                order_to_symbol[new_order->order_id] = new_order->symbol;
                break;
            }
            case md::MSG_TYPE::DELETE_ORDER: {
                if (len < sizeof(md::delete_order)) {
                    logger->error("Message too short for delete order: {}", len);
                    return;
                }

                md::delete_order* delete_order = reinterpret_cast<md::delete_order*>(buf);
                uint32_t symbol = order_to_symbol[delete_order->order_id];
                if (symbol_to_order_book.find(symbol) == symbol_to_order_book.end()) {
                    logger->error("Symbol not found for order: {}", delete_order->order_id);
                    return;
                }
                symbol_to_order_book[symbol]->delete_order(delete_order->order_id);
                break;
            }
            case md::MSG_TYPE::MODIFY_ORDER: {
                if (len < sizeof(md::modify_order)) {
                    logger->error("Message too short for modify order: {}", len);
                    return;
                }

                md::modify_order* modify_order = reinterpret_cast<md::modify_order*>(buf);
                uint32_t symbol = order_to_symbol[modify_order->order_id];
                if (symbol_to_order_book.find(symbol) == symbol_to_order_book.end()) {
                    logger->error("Symbol not found for order: {}", modify_order->order_id);
                    return;
                }
                symbol_to_order_book[symbol]->modify_order(modify_order->order_id, modify_order->side, modify_order->quantity, modify_order->price);
                break;
            }
            case md::MSG_TYPE::TRADE: {
                if (len < sizeof(md::trade)) {
                    logger->error("Message too short for trade: {}", len);
                    return;
                }

                md::trade* trade = reinterpret_cast<md::trade*>(buf);
                uint32_t symbol = order_to_symbol[trade->order_id];
                if (symbol_to_order_book.find(symbol) == symbol_to_order_book.end()) {
                    logger->error("Symbol not found for order: {}", trade->order_id);
                    return;
                }
                symbol_to_order_book[symbol]->order_trade(trade->order_id, trade->quantity, trade->price);
                break;
            }
            case md::MSG_TYPE::TRADE_SUMMARY: {
                if (len < sizeof(md::trade_summary)) {
                    logger->error("Message too short for trade summary: {}", len);
                    return;
                }

                md::trade_summary* trade_summary = reinterpret_cast<md::trade_summary*>(buf);
                if (symbol_to_order_book.find(trade_summary->symbol) == symbol_to_order_book.end()) {
                    logger->error("Symbol not found for trade summary: {}", trade_summary->symbol);
                    return;
                }
                logger->info("Trade summary: symbol={}, aggressor_side={}, total_quantity={}, last_price={}", trade_summary->symbol, static_cast<int>(trade_summary->aggressor_side), trade_summary->total_quantity, trade_summary->last_price);
                break;
            }
            case md::MSG_TYPE::HEARTBEAT: {
                md::md_header* header = reinterpret_cast<md::md_header*>(buf);
                logger->info("Heartbeat {}", header->seq_num);
                break;
            }
            default:
                logger->error("Unknown message type: {}", static_cast<uint8_t>(header->msg_type));
                return;
        }

        len -= header->length;
        buf += header->length;
    };
}

} // namespace ndfex::bots