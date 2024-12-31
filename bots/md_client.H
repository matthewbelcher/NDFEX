#pragma once

#include "order_book.H"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <string>
#include <memory>

namespace ndfex::bots {

/**
 * Listens to the market data feed and builds the order book.
 */

class MDClient {
public:

    MDClient(std::string ip, uint16_t port, std::string bind_ip, std::shared_ptr<spdlog::logger> logger);

    ~MDClient();

    void wait_for_hearbeat();

    void process();

    OrderBook::PriceLevel get_best_bid(uint32_t symbol) const;
    OrderBook::PriceLevel get_best_ask(uint32_t symbol) const;

private:

    uint8_t buf[4096];
    std::shared_ptr<spdlog::logger> logger;

    int mcast_fd;

    std::unordered_map<uint32_t, OrderBook*> symbol_to_order_book;
    std::unordered_map<uint64_t, uint32_t> order_to_symbol;

    void process_message(uint8_t* buf, size_t len);
};

} // namespace ndfex::bots
