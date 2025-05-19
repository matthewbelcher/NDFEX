#pragma once

#include "oe_client.H"
#include "md_client.H"
#include "matching_engine/symbol_definition.H"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <vector>
#include <set>

/**
 * Manages the stack of open orders for a market maker.
 * It is responsible for placing and cancelling orders based on the valuation.
 * It also keeps track of the open orders and their prices.
 */

namespace ndfex::bots {

class StackManager {

    public:

        struct open_order {
            uint32_t order_id;
            int32_t price;
            uint32_t quantity;

            open_order(uint32_t order_id, int32_t price, uint32_t quantity)
                : order_id(order_id), price(price), quantity(quantity) {}

            bool operator<(const open_order& other) const {
                return price < other.price;
            }
            bool operator>(const open_order& other) const {
                return price > other.price;
            }
        };

        StackManager(OEClient& oe,
                     symbol_definition symbol,
                     uint32_t width_in_ticks,
                     uint32_t quantity,
                     uint32_t stack_depth,
                     uint32_t& last_order_id,
                     std::shared_ptr<spdlog::logger> logger);

        void process(int32_t fair_value, int32_t bid_price, int32_t ask_price);

        void cancel_all_orders();

    private:

        template <typename SideOrders>
        void stack_side(int32_t price, SideOrders& orders, md::SIDE side);

        int32_t improve(int32_t price, md::SIDE side, uint32_t ticks);

        int32_t worsen(int32_t price, md::SIDE side, uint32_t ticks);

        OEClient& oe;
        symbol_definition symbol;

        const uint32_t width_in_ticks;
        const uint32_t quantity;
        const uint32_t stack_depth;

        uint64_t last_order_send_ts{0};
        uint32_t& last_order_id;

        std::set<open_order> bid_orders;

        // lowest ask price is the first element in the set
        std::set<open_order, std::greater<open_order>> ask_orders;

        std::shared_ptr<spdlog::logger> logger;
};

} // namespace ndfex::bots