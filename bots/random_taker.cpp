#include "random_taker.H"

#include <order_entry/oe_protocol.H>
#include <matching_engine/utils.H>

namespace ndfex::bots {

RandomTaker::RandomTaker(OEClient& oe, MDClient& md,
                         std::vector<symbol_definition> symbols,
                         uint32_t quantity,
                         uint32_t& last_order_id,
                         uint64_t time_scale,
                         std::shared_ptr<spdlog::logger> logger)
    : oe(oe), md(md), symbols(symbols), quantity(quantity), last_order_id(last_order_id), logger(logger),
    time_dist(10000000 * time_scale, 500000000 * time_scale) {}

void RandomTaker::process() {
    int64_t ts = nanotime();
    if (ts < last_ts) {
        return;
    }

    for (const auto& symbol : symbols) {
        if (rand() % 2 == 0) {
            logger->info("Sending buy order {} for {} at {}", last_order_id, symbol.symbol,
                 md.get_best_ask(symbol.symbol).price);
            oe.send_order(symbol.symbol, last_order_id, md::SIDE::BUY, quantity,
             md.get_best_ask(symbol.symbol).price, static_cast<uint8_t>(oe::ORDER_FLAGS::IOC));
             last_order_id++;
        } else {
            logger->info("Sending sell order {} for {} at {}", last_order_id, symbol.symbol,
                 md.get_best_bid(symbol.symbol).price);
            oe.send_order(symbol.symbol, last_order_id, md::SIDE::SELL, quantity,
             md.get_best_bid(symbol.symbol).price, static_cast<uint8_t>(oe::ORDER_FLAGS::IOC));
             last_order_id++;
        }
    }

    last_ts = ts + time_dist(gen);
}

} // namespace ndfex::bots