#include "resting_orders.H"

#include "matching_engine/utils.H"

namespace ndfex::bots {

    RestingOrders::RestingOrders(OEClient& oe, MDClient& md,
                                   std::vector<symbol_definition> symbols,
                                   uint32_t quantity,
                                   uint32_t& last_order_id,
                                   std::shared_ptr<spdlog::logger> logger)
        : oe(oe), md(md), symbols(symbols), quantity(quantity), last_order_id(last_order_id), logger(logger) {
        time_dist = std::uniform_int_distribution<uint64_t>(500000000, 10000000000);
    }

    void RestingOrders::process() {
        int64_t ts = nanotime();
        if (ts < last_ts) {
            return;
        }

        md::SIDE side = (rand() % 2 == 0) ? md::SIDE::BUY : md::SIDE::SELL;

        for (const auto& symbol : symbols) {
            if (side == md::SIDE::BUY) {
                logger->info("RestingOrders: Sending buy order {} for {} qty {} at {}", last_order_id, symbol.symbol,
                    quantity, md.get_best_bid(symbol.symbol).price - 3 * symbol.tick_size);
                oe.send_order(symbol.symbol, last_order_id, side, quantity,
                    md.get_best_bid(symbol.symbol).price - 3 * symbol.tick_size, 0);
            } else {
                logger->info("RestingOrders: Sending sell order {} for {} qty {} at {}", last_order_id, symbol.symbol,
                    quantity, md.get_best_ask(symbol.symbol).price + 3 * symbol.tick_size);
                oe.send_order(symbol.symbol, last_order_id, side, quantity,
                    md.get_best_ask(symbol.symbol).price + 3 * symbol.tick_size, 0);
            }
            last_order_id++;
        }

        // simulate customer orders by placing large orders at random intervals
        last_ts = nanotime() + time_dist(gen);
    }
}