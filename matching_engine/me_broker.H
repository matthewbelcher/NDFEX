#pragma once

#include "spsc_oe_queue.H"
#include "md_mcast.H"
#include "oe_protocol.H"
#include "order_ladder.H"
#include "clearing_mcast.H"
#include "utils.H"

#include <spdlog/logger.h>
#include <unordered_map>
#include <memory>

namespace ndfex {

/**
 * Handles the communication between the matching engine and the order entry server.
 */

constexpr uint64_t HEARTBEAT_INTERVAL = 500000000; // 500ms

class MatchingEngineBroker {

public:

    MatchingEngineBroker(SPSCOEQueue& to_client, SPSCOEQueue& from_client,
        MarketDataPublisher& publisher,
        clearing::ClearingPublisher& clearing_publisher,
        std::shared_ptr<spdlog::logger> logger)
        : to_client(to_client), from_client(from_client), publisher(publisher),
        clearing_publisher(clearing_publisher), logger(logger)
    {}

    ~MatchingEngineBroker() {
        for (auto& [symbol, ladder] : symbol_to_matching_engine) {
            delete ladder;
        }
    }

    void add_symbol(uint32_t symbol) {
        symbol_to_matching_engine[symbol] = new OrderLadder<MatchingEngineBroker>(this, symbol);
    }

    void onNewOrder(uint64_t order_id, uint32_t symbol, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags) {
        if (quantity > 0) {
            // don't publish a new order if it was fully filled in the matching engine or it was an IOC
            publisher.publish_new_order(order_id, symbol, side, quantity, price, flags);
            last_published_ts = nanotime();
        }

        to_client.emplace(oe::MSG_TYPE::ACK, order_id, symbol, client_seq, client_id, side, quantity, price, flags);
    }

    void onModifyOrder(uint64_t order_id, uint32_t symbol, md::SIDE side, uint32_t quantity, int32_t price) {
        publisher.publish_modify_order(order_id, side, quantity, price);
        last_published_ts = nanotime();

        to_client.emplace(oe::MSG_TYPE::ACK, order_id, symbol, client_seq, client_id, side, quantity, price, 0);
    }

    void onDeleteOrder(uint64_t order_id, bool publish) {
        if (publish) {
            publisher.publish_delete_order(order_id);
            last_published_ts = nanotime();
        }

        to_client.emplace(oe::MSG_TYPE::CLOSE, order_id, 0, client_seq, client_id, md::SIDE::BUY, 0, 0, 0);
    }

    void onCancelReject(uint64_t order_id) {
        to_client.emplace(oe::MSG_TYPE::REJECT, order_id, 0, client_seq, client_id, md::SIDE::BUY, 0, 0, 0);
    }

    void onTrade(uint64_t order_id, uint32_t quantity, int32_t price) {
        publisher.queue_trade(order_id, quantity, price);
        last_published_ts = nanotime();
    }

    void onFill(uint64_t order_id, uint32_t symbol, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags, bool active) {
        uint32_t client_seq_sent = active ? client_seq : 0;
        uint32_t fill_client_id = client_id;

        if (!active) {
            // if the order was not active, we need to look up the client id from the order id
            auto it = exch_order_id_to_client_id.find(order_id);
            if (it != exch_order_id_to_client_id.end()) {
                fill_client_id = it->second;
            } else {
                logger->error("Failed to find client id for order id {}", order_id);
                throw std::runtime_error("Failed to find client id for order id " + std::to_string(order_id));
            }
        }

        to_client.emplace(oe::MSG_TYPE::FILL, order_id, symbol, client_seq_sent, fill_client_id, side, quantity, price, flags);

        logger->info("AUTH: Fill for order id {} client id {} symbol {} quantity {} price {} side {} flags {}",
            order_id, fill_client_id, symbol, quantity, price, static_cast<uint8_t>(side), static_cast<uint8_t>(flags));

        clearing_publisher.publish_fill(order_id, fill_client_id, symbol, quantity, price, side);
    }

    void finishedPacket(uint32_t symbol) {
        publisher.publish_queued_trades(symbol, aggressor_side);
        last_published_ts = nanotime();
    }

    void process() {
        if (nanotime() - last_published_ts > HEARTBEAT_INTERVAL) {
            publisher.publish_hearbeat();
            last_published_ts = nanotime();
        }

        while (from_client.front()) {
            oe_payload& payload = *from_client.front();
            client_seq = payload.client_seq;
            client_id = payload.client_id;

            switch (payload.msg_type) {
                case oe::MSG_TYPE::NEW_ORDER: {
                    auto ladder = symbol_to_matching_engine[payload.symbol];
                    if (!ladder) {
                        throw std::runtime_error("Invalid symbol");
                    }

                    exch_order_id_to_client_id[payload.exch_order_id] = payload.client_id;

                    aggressor_side = payload.side;
                    ladder->new_order(payload.exch_order_id, payload.side, payload.quantity, payload.price, payload.flags);
                    break;
                }
                case oe::MSG_TYPE::DELETE_ORDER: {
                    auto ladder = symbol_to_matching_engine[payload.symbol];
                    if (!ladder) {
                        throw std::runtime_error("Invalid symbol");
                    }
                    ladder->delete_order(payload.exch_order_id);
                    break;
                }
                case oe::MSG_TYPE::MODIFY_ORDER: {
                    auto ladder = symbol_to_matching_engine[payload.symbol];
                    if (!ladder) {
                        throw std::runtime_error("Invalid symbol");
                    }
                    aggressor_side = payload.side;
                    ladder->modify_order(payload.exch_order_id, payload.side, payload.quantity, payload.price);
                    break;
                }
                case oe::MSG_TYPE::REJECT: {
                    // these need to be sent back to the client
                    to_client.emplace(payload.msg_type, payload.exch_order_id, payload.symbol, client_seq,
                        payload.client_id, payload.side, payload.quantity, payload.price, payload.flags);
                    break;
                }
                case oe::MSG_TYPE::ERROR:
                case oe::MSG_TYPE::LOGIN_RESPONSE:
                case oe::MSG_TYPE::ACK:
                case oe::MSG_TYPE::FILL:
                case oe::MSG_TYPE::CLOSE:
                case oe::MSG_TYPE::LOGIN:
                    throw std::runtime_error("Invalid message type " + std::to_string(static_cast<uint8_t>(payload.msg_type)));
            }
            finishedPacket(payload.symbol);
            from_client.pop();
        }
    }

private:

    uint32_t client_seq{0};
    uint32_t client_id{0};

    md::SIDE aggressor_side{md::SIDE::BUY};

    uint64_t last_published_ts{0};

    SPSCOEQueue& to_client;
    SPSCOEQueue& from_client;
    MarketDataPublisher& publisher;
    clearing::ClearingPublisher& clearing_publisher;

    std::unordered_map<uint32_t, OrderLadder<MatchingEngineBroker>*> symbol_to_matching_engine;
    std::unordered_map<uint32_t, uint32_t> exch_order_id_to_client_id;

    std::shared_ptr<spdlog::logger> logger;
};

} // namespace ndfex