#include "md_client.H"
#include "market_data/md_protocol.H"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

namespace ndfex::bots {

static int create_mcast_socket(const std::string& ip, uint16_t port, const std::string& bind_ip,
                               std::shared_ptr<spdlog::logger> logger) {
    int mcast_fd = socket(AF_INET, SOCK_DGRAM, 0);
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
    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (bind(mcast_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        logger->error("Failed to bind socket: {}", strerror(errno));
        throw std::runtime_error("Failed to bind socket");
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(ip.c_str());
    mreq.imr_interface.s_addr = inet_addr(bind_ip.c_str());

    if (setsockopt(mcast_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0) {
        throw std::runtime_error("Failed to join multicast group");
    }

    logger->info("Listening on {}:{}", ip, port);
    return mcast_fd;
}

MDClient::MDClient(std::string live_ip, uint16_t live_port,
        std::string snapshot_ip, uint16_t snapshot_port,
        std::string bind_ip, std::shared_ptr<spdlog::logger> logger)
        : logger(logger), snapshot_ip(snapshot_ip), bind_ip(bind_ip) {

    live_fd = create_mcast_socket(live_ip, live_port, bind_ip, logger);
    snapshot_fd = create_mcast_socket(snapshot_ip, snapshot_port, bind_ip, logger);
}

MDClient::~MDClient() {
    close(live_fd);
    close(snapshot_fd);
    for (auto& [symbol, order_book] : symbol_to_order_book) {
        delete order_book;
    }
}

void MDClient::wait_for_snapshot() {
    // store live updates in a buffer until we receive the snapshot
    std::unordered_map<uint32_t, ssize_t> symbol_to_bid_orders;
    std::unordered_map<uint32_t, ssize_t> symbol_to_ask_orders;
    symbol_to_bid_orders[1] = -1;
    symbol_to_ask_orders[1] = -1;
    symbol_to_bid_orders[2] = -1;
    symbol_to_ask_orders[2] = -1;

    snapshot_complete[1] = false;
    snapshot_complete[2] = false;

    while (!snapshot_complete[1] || !snapshot_complete[2]) {

        ssize_t len = recvfrom(live_fd, buf, sizeof(buf), 0, nullptr, nullptr);
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            logger->error("Failed to receive message: {}", strerror(errno));
            throw std::runtime_error("Failed to receive message");
        }
        live_buffer.insert(live_buffer.end(), buf, buf + len);

        ssize_t snapshot_len = recvfrom(snapshot_fd, buf, sizeof(buf), 0, nullptr, nullptr);
        if (snapshot_len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            logger->error("Failed to receive snapshot: {}", strerror(errno));
            throw std::runtime_error("Failed to receive snapshot");
        }

        size_t buf_offset = 0;
        while (snapshot_len > 0) {
            if (static_cast<size_t>(snapshot_len) < sizeof(md::md_header)) {
                logger->error("Snapshot message too short: {}", snapshot_len);
                throw std::runtime_error("Snapshot message too short");
            }

            md::md_header* header = reinterpret_cast<md::md_header*>(buf + buf_offset);
            if (header->magic_number != md::SNAPSHOT_MAGIC_NUMBER) {
                logger->error("Invalid magic number: {}", header->magic_number);
                throw std::runtime_error("Invalid magic number");
            }

            if (header->msg_type == md::MSG_TYPE::SNAPSHOT_INFO) {
                // we started receiving a snapshot
                md::snapshot_info* snapshot_info = reinterpret_cast<md::snapshot_info*>(buf + buf_offset);
                if (snapshot_info->last_md_seq_num <= get_first_live_seq_num()) {
                    logger->info("Skipping snapshot: last_md_seq_num={}, first_live_seq_num={}", snapshot_info->last_md_seq_num, get_first_live_seq_num());
                    snapshot_len = 0;
                    continue;
                }

                last_snapshot_seq_num = snapshot_info->last_md_seq_num;
                symbol_to_bid_orders[snapshot_info->symbol] = snapshot_info->bid_count;
                symbol_to_ask_orders[snapshot_info->symbol] = snapshot_info->ask_count;
                snapshot_buffer[snapshot_info->symbol].clear(); // clear the buffer before starting a new snapshot

                logger->info("Snapshot info: symbol={}, bid_count={}, ask_count={}, last_md_seq_num={}", snapshot_info->symbol, snapshot_info->bid_count,
                            snapshot_info->ask_count, snapshot_info->last_md_seq_num);

                if (symbol_to_bid_orders[snapshot_info->symbol] == 0 && symbol_to_ask_orders[snapshot_info->symbol] == 0) {
                    snapshot_complete[snapshot_info->symbol] = true;
                }
            } else if (header->msg_type == md::MSG_TYPE::NEW_ORDER) {

                // we are still receiving the snapshot
                md::new_order* new_order = reinterpret_cast<md::new_order*>(buf + buf_offset);
                if (new_order->side == md::SIDE::BUY) {
                    --symbol_to_bid_orders[new_order->symbol];
                } else {
                    --symbol_to_ask_orders[new_order->symbol];
                }
                logger->info("Snapshot order: id={}, symbol={}, side={}, quantity={}, price={}", new_order->order_id, new_order->symbol, static_cast<int>(new_order->side), new_order->quantity, new_order->price);
                snapshot_buffer[new_order->symbol].push_back(*new_order);

                if (symbol_to_bid_orders[new_order->symbol] == 0 && symbol_to_ask_orders[new_order->symbol] == 0) {
                    snapshot_complete[new_order->symbol] = true;
                }
            }

            snapshot_len -= header->length;
            buf_offset += header->length;
        }
    }

    // apply snapshot orders to the order book
    for (uint32_t symbol = 1; symbol <= 2; ++symbol) {
        for (auto& new_order : snapshot_buffer[symbol]) {
            if (symbol_to_order_book.find(new_order.symbol) == symbol_to_order_book.end()) {
                symbol_to_order_book[new_order.symbol] = new OrderBook(logger);
            }
            symbol_to_order_book[new_order.symbol]->new_order(new_order.order_id, new_order.side, new_order.quantity, new_order.price, new_order.flags);
            order_to_symbol[new_order.order_id] = new_order.symbol;
        }
    }

    // apply updates from the live buffer starting with sequence number last_snapshot_seq_num + 1
    last_md_seq_num = last_snapshot_seq_num;
    while (!live_buffer.empty()) {
        md::md_header* header = reinterpret_cast<md::md_header*>(live_buffer.data());
        if (header->seq_num <= last_snapshot_seq_num) {
            live_buffer.erase(live_buffer.begin(), live_buffer.begin() + header->length);
            continue;
        }

        process_message(live_buffer.data(), live_buffer.size());
        live_buffer.erase(live_buffer.begin(), live_buffer.begin() + header->length);
    }

    // unsubscribe from snapshot feed
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(snapshot_ip.c_str());
    mreq.imr_interface.s_addr = inet_addr(bind_ip.c_str());

    if (setsockopt(snapshot_fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0) {
        throw std::runtime_error("Failed to leave multicast group");
    }

    close(snapshot_fd);
}

void MDClient::process() {
    ssize_t len = recvfrom(live_fd, buf, sizeof(buf), 0, nullptr, nullptr);
    if (len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        logger->error("Failed to receive message: {}", strerror(errno));
        throw std::runtime_error("Failed to receive message");
    }
    process_message(buf, len);
}

uint32_t MDClient::get_first_live_seq_num() const {
    if (live_buffer.empty()) {
        return UINT32_MAX;
    }

    const md::md_header* header = reinterpret_cast<const md::md_header*>(live_buffer.data());
    return header->seq_num;
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

        if (header->seq_num != last_md_seq_num + 1) {
            logger->error("Sequence number out of order: expected={}, got={}", last_md_seq_num + 1, header->seq_num);
            return;
        }

        last_md_seq_num = header->seq_num;

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
//                std::cout << "new_order " << new_order->order_id << " " << new_order->price << std::endl;
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