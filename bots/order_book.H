#pragma once

#include "order_book.H"
#include "market_data/md_protocol.H"

#include <spdlog/spdlog.h>

namespace ndfex::bots {

/**
 * Simple order book implementation. We are not keeping the ordering of orders at the same price level, just the total quantity.
 */

class OrderBook {
public:

    struct PriceLevel {
        int32_t price{0};
        uint32_t quantity{0};
    };

    struct Order {
        md::SIDE side;
        int32_t price{0};
        uint32_t quantity{0};
    };

    OrderBook(std::shared_ptr<spdlog::logger> logger);

    void new_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags, uint32_t filled_quantity = 0);

    void delete_order(uint64_t order_id);

    void modify_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price);

    void order_trade(uint64_t order_id, uint32_t quantity, int32_t price);

    PriceLevel get_best_bid() const;
    PriceLevel get_best_ask() const;

private:

    template <typename PriceLevels>
    void delete_order_from_levels(uint64_t order_id, PriceLevels &levels);

    template <typename PriceLevels>
    void print_levels(const PriceLevels &levels);

    std::shared_ptr<spdlog::logger> logger;

    std::unordered_map<uint64_t, OrderBook::Order> orders;
    std::map<int32_t, uint32_t, std::greater<int32_t>> buy_levels;
    std::map<int32_t, uint32_t, std::less<int32_t>> sell_levels;

};

} // namespace ndfex::bots