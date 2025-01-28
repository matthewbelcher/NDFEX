#include "snapshot_writer.H"
#include "spsc_md_queue.H"
#include "matching_engine/utils.H"
#include <spdlog/spdlog.h>

#include <iostream>

namespace ndfex::md {

constexpr uint32_t SNAPSHOT_INTERVAL = 500000000; // 500ms

SnapshotWriter::SnapshotWriter(std::string ip, uint16_t port, std::string bind_ip, std::shared_ptr<spdlog::logger> logger,
                               SPSCMDQueue &queue) :
    logger(logger), queue(queue) {

    mcast_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (mcast_fd < 0) {
        logger->error("Failed to create socket: {}", strerror(errno));
        throw std::runtime_error("Failed to create socket");
    }

    mcast_addr.sin_family = AF_INET;
    mcast_addr.sin_port = htons(port);
    mcast_addr.sin_addr.s_addr = inet_addr(ip.c_str());

    struct in_addr localInterface;
    localInterface.s_addr = inet_addr(bind_ip.c_str());
    int rc = setsockopt(mcast_fd, IPPROTO_IP, IP_MULTICAST_IF, (char*)&localInterface, sizeof(localInterface));
    if (rc < 0) {
        logger->error("Failed to bind to multicast address: {}", strerror(errno));
        throw std::runtime_error("Failed to bind to multicast address");
    }

    // set mcast socket to non-blocking
    int flags = fcntl(mcast_fd, F_GETFL, 0);
    if (flags == -1) {
        logger->error("Failed to get socket flags: {}", strerror(errno));
        throw std::runtime_error("Failed to get socket flags");
    }

    rc = fcntl(mcast_fd, F_SETFL, flags | O_NONBLOCK);
    if (rc == -1) {
        logger->error("Failed to set socket to non-blocking: {}", strerror(errno));
        throw std::runtime_error("Failed to set socket to non-blocking");
    }

    symbol_to_asks[1] = {};
    symbol_to_bids[1] = {};

    symbol_to_asks[2] = {};
    symbol_to_bids[2] = {};

    logger->info("Publishing to {}:{}", ip, port);

}

SnapshotWriter::~SnapshotWriter() {
    close(mcast_fd);
}

void SnapshotWriter::process() {
    while (queue.front()) {
        md_payload& payload = *queue.front();
        // apply the message to the open order state
        last_md_seq_num = payload.seq_num;
        switch (payload.msg_type) {
            case md::MSG_TYPE::NEW_ORDER: {
                if (payload.side == md::SIDE::BUY) {
                    auto& orders = symbol_to_bids[payload.symbol];
                    orders.push_back({{0, sizeof(md::new_order), 0, 0, md::MSG_TYPE::NEW_ORDER},
                        payload.order_id, payload.symbol, payload.side, payload.quantity, payload.price, payload.flags});
                } else {
                    auto& orders = symbol_to_asks[payload.symbol];
                    orders.push_back({{0, sizeof(md::new_order), 0, 0, md::MSG_TYPE::NEW_ORDER},
                        payload.order_id, payload.symbol, payload.side, payload.quantity, payload.price, payload.flags});
                }
                order_to_symbol[payload.order_id] = payload.symbol;
                break;
            }
            case md::MSG_TYPE::DELETE_ORDER: {
                uint32_t symbol = order_to_symbol[payload.order_id];

                auto& bid_orders = symbol_to_bids[symbol];
                bid_orders.erase(std::remove_if(bid_orders.begin(), bid_orders.end(), [&](const new_order& order) {
                    return order.order_id == payload.order_id;
                }), bid_orders.end());

                auto& ask_orders = symbol_to_asks[symbol];
                ask_orders.erase(std::remove_if(ask_orders.begin(), ask_orders.end(), [&](const new_order& order) {
                    return order.order_id == payload.order_id;
                }), ask_orders.end());

                order_to_symbol.erase(payload.order_id);
                break;
            }
            case md::MSG_TYPE::MODIFY_ORDER: {
                uint32_t symbol = order_to_symbol[payload.order_id];

                auto& bid_orders = symbol_to_bids[symbol];
                auto& ask_orders = symbol_to_asks[symbol];

                auto it = std::find_if(bid_orders.begin(), bid_orders.end(), [&](const new_order& order) {
                    return order.order_id == payload.order_id;
                });

                if (it != bid_orders.end() && payload.side == md::SIDE::BUY) {
                    it->quantity = payload.quantity;
                    it->price = payload.price;
                    it->flags = payload.flags;
                    break;

                } else if (it != bid_orders.end()) {
                    // order changed side
                    bid_orders.erase(it);

                    ask_orders.push_back({{0, sizeof(md::new_order), 0, 0, md::MSG_TYPE::NEW_ORDER},
                        payload.order_id, symbol, payload.side, payload.quantity, payload.price, payload.flags});
                    break;
                }

                // check ask orders
                it = std::find_if(ask_orders.begin(), ask_orders.end(), [&](const new_order& order) {
                    return order.order_id == payload.order_id;
                });

                if (it != ask_orders.end() && payload.side == md::SIDE::SELL) {
                    it->quantity = payload.quantity;
                    it->price = payload.price;
                    it->flags = payload.flags;
                    break;

                } else if (it != ask_orders.end()) {
                    // order changed side
                    ask_orders.erase(it);
                    bid_orders.push_back({{0, sizeof(md::new_order), 0, 0, md::MSG_TYPE::NEW_ORDER},
                        payload.order_id, symbol, payload.side, payload.quantity, payload.price, payload.flags});
                    break;
                }

                // order not found
                logger->error("Order not found for modify: {}", payload.order_id);
                break;

            }
            case md::MSG_TYPE::TRADE: {
                uint32_t symbol = order_to_symbol[payload.order_id];
                // reduce the quantity of the order by the trade amount
                auto& bid_orders = symbol_to_bids[symbol];
                auto it = std::find_if(bid_orders.begin(), bid_orders.end(), [&](const new_order& order) {
                    return order.order_id == payload.order_id;
                });
                if (it != bid_orders.end()) {
                    if (it->quantity < payload.quantity) {
                        logger->error("Trade quantity exceeds order quantity: {}", payload.order_id);
                        break;
                    }
                    it->quantity -= payload.quantity;
                    if (it->quantity == 0) {
                        bid_orders.erase(it);
                    }
                    break;
                }

                auto& ask_orders = symbol_to_asks[symbol];
                it = std::find_if(ask_orders.begin(), ask_orders.end(), [&](const new_order& order) {
                    return order.order_id == payload.order_id;
                });
                if (it != ask_orders.end()) {
                    if (it->quantity < payload.quantity) {
                        logger->error("Trade quantity exceeds order quantity: {}", payload.order_id);
                        break;
                    }
                    it->quantity -= payload.quantity;
                    if (it->quantity == 0) {
                        ask_orders.erase(it);
                    }
                    break;
                }
                break;
            }
            case md::MSG_TYPE::HEARTBEAT:
                break;
            default:
                logger->error("Unknown message type: {}", static_cast<int>(payload.msg_type));
                break;
        }
        queue.pop();
    }

    // if enough time has passed, write a snapshot
    if (nanotime() - last_snapshot_ts > SNAPSHOT_INTERVAL) {
        last_snapshot_ts = nanotime();

        // sort the orders by price
        for (auto& [symbol, bids] : symbol_to_bids) {
            std::sort(bids.begin(), bids.end(), [](const new_order& a, const new_order& b) {
                return a.price > b.price;
            });
        }

        for (auto& [symbol, asks] : symbol_to_asks) {
            std::sort(asks.begin(), asks.end(), [](const new_order& a, const new_order& b) {
                return a.price < b.price;
            });
        }

        // write each symbol's orders
        for (auto& [symbol, bids] : symbol_to_bids) {
            write_snapshot_info({{0, sizeof(md::snapshot_info), 0, 0, md::MSG_TYPE::SNAPSHOT_INFO},
                symbol, static_cast<uint32_t>(symbol_to_bids[symbol].size()),
                        static_cast<uint32_t>(symbol_to_asks[symbol].size()),
                last_md_seq_num});

            std::cout << "Writing snapshot for symbol " << symbol << std::endl;
            std::cout << "Bids: " << symbol_to_bids[symbol].size() << ", Asks: " << symbol_to_asks[symbol].size() << std::endl;

            for (const auto& bid : symbol_to_bids[symbol]) {
                write_order(bid);
            }

            for (const auto& ask : symbol_to_asks[symbol]) {
                write_order(ask);
            }

            send();
        }
    }
}

void SnapshotWriter::write_order(const new_order& order) {
    if (buffer_size + order.header.length > sizeof(buffer)) {
        send();
    }

    md::md_header* header = reinterpret_cast<md::md_header*>(&buffer[buffer_size]);
    std::memcpy(&buffer[buffer_size], &order, order.header.length);

    header->magic_number = md::SNAPSHOT_MAGIC_NUMBER;
    header->timestamp = nanotime();
    header->seq_num = 0;
    header->msg_type = order.header.msg_type;

    buffer_size += order.header.length;
}

void SnapshotWriter::write_snapshot_info(const snapshot_info& info) {
    if (buffer_size + info.header.length > sizeof(buffer)) {
        send();
    }

    md::md_header* header = reinterpret_cast<md::md_header*>(&buffer[buffer_size]);
    std::memcpy(&buffer[buffer_size], &info, info.header.length);

    header->magic_number = md::SNAPSHOT_MAGIC_NUMBER;
    header->seq_num = 0;
    header->timestamp = nanotime();
    header->msg_type = md::MSG_TYPE::SNAPSHOT_INFO;

    buffer_size += info.header.length;
}

void SnapshotWriter::send() {
    ssize_t rc = sendto(mcast_fd, buffer, buffer_size, 0, (struct sockaddr*)&mcast_addr, sizeof(mcast_addr));
    if (rc < 0) {
        logger->error("Failed to send snapshot: {}", strerror(errno));
        throw std::runtime_error("Failed to send snapshot");
    }

    buffer_size = 0;
}

} // namespace ndfex::md