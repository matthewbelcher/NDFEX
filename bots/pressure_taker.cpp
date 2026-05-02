#include "pressure_taker.H"

#include "matching_engine/utils.H"

namespace ndfex::bots {

PressureTaker::PressureTaker(OEClient& oe, MDClient& md,
                                 std::vector<symbol_definition> symbols,
                                 uint32_t quantity,
                                 uint32_t& last_order_id,
                                 std::shared_ptr<spdlog::logger> logger)
    : oe(oe), md(md), symbols(symbols), quantity(quantity), last_order_id(last_order_id), logger(logger) {
        time_dist = std::uniform_int_distribution<uint64_t>(200000000, 10000000000);

    }

void PressureTaker::process() {
    uint64_t ts = nanotime();
    if (ts < last_ts) {
        return;
    }

    if (pressure_quantity <= static_cast<int32_t>(quantity)) {
        pressure_quantity = (rand() % 100) + 1;
        pressure_side = (rand() % 2 == 0) ? md::SIDE::BUY : md::SIDE::SELL;
        logger->info("PressureTaker: Loading {} quantity for {} at {}", (pressure_side == md::SIDE::BUY ? "buy" : "sell"), pressure_quantity, symbols[0].symbol);
    }

    for (const auto& symbol : symbols) {
        if (pressure_side == md::SIDE::BUY) {
            auto ask = md.get_best_ask(symbol.symbol);
            if (ask.quantity == 0) { last_order_id++; continue; }
            logger->info("PressureTaker: Sending buy order {} for {} qty {} at {} remaining {}", last_order_id, symbol.symbol,
                quantity, ask.price, pressure_quantity);
            oe.send_order(symbol.symbol, last_order_id, pressure_side, quantity,
                ask.price, static_cast<uint8_t>(oe::ORDER_FLAGS::IOC));
        } else {
            auto bid = md.get_best_bid(symbol.symbol);
            if (bid.quantity == 0) { last_order_id++; continue; }
            logger->info("PressureTaker: Sending sell order {} for {} qty {} at {} remaining {}", last_order_id, symbol.symbol,
                quantity, bid.price, pressure_quantity);
            oe.send_order(symbol.symbol, last_order_id, pressure_side, quantity,
                bid.price, static_cast<uint8_t>(oe::ORDER_FLAGS::IOC));
        }
        pressure_quantity -= quantity;
        last_order_id++;
    }

    // simulate customer orders by placing large orders at random intervals

    last_ts = nanotime() + time_dist(gen);
}


}