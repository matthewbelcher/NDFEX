#include "fair_value_mm.H"
#include "matching_engine/utils.H"

#include <iostream>

namespace ndfex::bots {

FairValueMarketMaker::FairValueMarketMaker(OEClient& oe, MDClient& md,
                                           std::vector<FairValue*>& fv,
                                           std::vector<int32_t> variances,
                                           std::vector<symbol_definition> symbols,
                                           uint32_t width_in_ticks,
                                           uint32_t quantity,
                                           uint32_t& last_order_id,
                                           std::shared_ptr<spdlog::logger> logger)
    : oe(oe), md(md), fv(fv), variances(variances), symbols(symbols),
      width_in_ticks(width_in_ticks), quantity(quantity), last_order_id(last_order_id), logger(logger) {  }

static int32_t round_to_tick_size(int32_t price, uint32_t tick_size, md::SIDE side) {
    if (side == md::SIDE::BUY) {
        return price - (price % tick_size);
    } else {
        return price + (tick_size - (price % tick_size));
    }
}

void FairValueMarketMaker::process() {

    // get the fair value
    for (size_t i = 0; i < symbols.size(); i++) {
        int32_t fair_value = fv[i]->process(nanotime());
        fair_value += variances[i];

        // place orders 2 ticks away from the fair value
        int32_t bid_price = round_to_tick_size(fair_value - width_in_ticks * symbols[i].tick_size, symbols[i].tick_size, md::SIDE::BUY);
        int32_t ask_price = round_to_tick_size(fair_value + width_in_ticks * symbols[i].tick_size, symbols[i].tick_size, md::SIDE::SELL);

        auto bid_it = bid_orders.find(symbols[i].symbol);
        if (bid_it == bid_orders.end()) {
            bid_orders[symbols[i].symbol] = {};
        }
        auto ask_it = ask_orders.find(symbols[i].symbol);
        if (ask_it == ask_orders.end()) {
            ask_orders[symbols[i].symbol] = {};
        }

        auto best_bid = md.get_best_bid(symbols[i].symbol);
        auto best_ask = md.get_best_ask(symbols[i].symbol);

        auto& bids = bid_orders[symbols[i].symbol];
        auto& asks = ask_orders[symbols[i].symbol];

        if (bids.size() > 0 && (bids.back().price != bid_price || last_order_send_ts + 30e9 < nanotime())) {
            logger->info("Cancelling bid order: symbol={}, price={}, quantity={}", symbols[i].symbol, bid_price, quantity);
            logger->info("Price was {} and now is {} fv {} ts {}", bids.back().price, bid_price, fair_value, nanotime());
            oe.cancel_order(bids.back().order_id);
            bids.pop_back();

            if (best_bid.price != 0) {
                logger->info("Best bid: price={}, quantity={}", best_bid.price, best_bid.quantity);
            }

        } else if (bids.size() == 0) {
            logger->info("Sending bid order: symbol={}, price={}, quantity={}", symbols[i].symbol, bid_price, quantity);
            logger->info("fv {} ts {}", fair_value, nanotime());
            oe.send_order(symbols[i].symbol, last_order_id, md::SIDE::BUY, quantity, bid_price, 0);
            bids.push_back({last_order_id++, bid_price, quantity});

            if (best_bid.price != 0) {
                logger->info("Best bid: price={}, quantity={}", best_bid.price, best_bid.quantity);
            }

            last_order_send_ts = nanotime();
        }

        if (asks.size() > 0 && (asks.back().price != ask_price || last_order_send_ts + 30e9 < nanotime())) {
            logger->info("Cancelling ask order: symbol={}, price={}, quantity={}", symbols[i].symbol, ask_price, quantity);
            logger->info("Price was {} and now is {} fv {} ts {}", asks.back().price, ask_price, fair_value, nanotime());
            oe.cancel_order(asks.back().order_id);
            asks.pop_back();

            if (best_ask.price != 0) {
                logger->info("Best ask: price={}, quantity={}", best_ask.price, best_ask.quantity);
            }

        } else if (asks.size() == 0) {
            logger->info("Sending ask order: symbol={}, price={}, quantity={}", symbols[i].symbol, ask_price, quantity);
            logger->info("fv {} ts {}", fair_value, nanotime());

            oe.send_order(symbols[i].symbol, last_order_id, md::SIDE::SELL, quantity, ask_price, 0);
            asks.push_back({last_order_id++, ask_price, quantity});

            if (best_ask.price != 0) {
                logger->info("Best ask: price={}, quantity={}", best_ask.price, best_ask.quantity);
            }

            last_order_send_ts = nanotime();
        }
    }
}
} // namespace ndfex::bots