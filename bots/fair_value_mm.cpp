#include "fair_value_mm.H"
#include "matching_engine/utils.H"

#include <iostream>
#include <stdexcept>

namespace ndfex::bots {

// Single-width convenience constructor: broadcasts the same width to every
// symbol this MM quotes. Delegates to the per-symbol constructor so there is
// only one place where the widths_in_ticks invariant lives.
FairValueMarketMaker::FairValueMarketMaker(OEClient& oe, MDClient& md,
                                           std::vector<FairValue*>& fv,
                                           std::vector<int32_t> variances,
                                           std::vector<symbol_definition> symbols,
                                           uint32_t width_in_ticks,
                                           uint32_t quantity,
                                           uint32_t& last_order_id,
                                           std::shared_ptr<spdlog::logger> logger)
    : FairValueMarketMaker(oe, md, fv, std::move(variances), symbols,
                           std::vector<uint32_t>(symbols.size(), width_in_ticks),
                           quantity, last_order_id, logger) {}

FairValueMarketMaker::FairValueMarketMaker(OEClient& oe, MDClient& md,
                                           std::vector<FairValue*>& fv,
                                           std::vector<int32_t> variances,
                                           std::vector<symbol_definition> symbols,
                                           std::vector<uint32_t> widths_in_ticks,
                                           uint32_t quantity,
                                           uint32_t& last_order_id,
                                           std::shared_ptr<spdlog::logger> logger)
    : oe(oe), md(md), fv(fv), variances(std::move(variances)),
      symbols(std::move(symbols)),
      widths_in_ticks(std::move(widths_in_ticks)),
      quantity(quantity), last_order_id(last_order_id), logger(logger) {
    if (this->widths_in_ticks.size() != this->symbols.size()) {
        throw std::runtime_error(
            "FairValueMarketMaker: widths_in_ticks.size() must equal symbols.size()");
    }
}

static int32_t round_to_tick_size(int32_t price, uint32_t tick_size, md::SIDE side) {
    if (side == md::SIDE::BUY) {
        return price - (price % tick_size);
    } else {
        return price + (tick_size - (price % tick_size));
    }
}

void FairValueMarketMaker::process() {

    // get the fair value (side-aware: bid uses bid_value, ask uses ask_value).
    // For RandomWalkFairValue both default to process() so non-basket symbols
    // are still symmetric around their walking mid. BasketFairValue overrides
    // these to use component touches, which makes the LP's UNDY quote
    // structurally arb-resistant.
    for (size_t i = 0; i < symbols.size(); i++) {
        const uint64_t now = nanotime();
        int32_t bid_fv = fv[i]->bid_value(now);
        int32_t ask_fv = fv[i]->ask_value(now);

        bid_fv += variances[i];
        ask_fv += variances[i];

        int32_t position = oe.get_position(symbols[i].symbol);
        if (position > 100) {
            position = 100;
        } else if (position < -100) {
            position = -100;
        }
        // Position skew lifts both sides up when short / down when long, the
        // same as the legacy single-fv code path.
        bid_fv -= position;
        ask_fv -= position;

        const int32_t floor_price = static_cast<int32_t>(symbols[i].min_price + 10 * symbols[i].tick_size);
        bid_fv = std::max(bid_fv, floor_price);
        ask_fv = std::max(ask_fv, floor_price);

        // place orders widths_in_ticks[i] ticks away from the side-fv
        const uint32_t w = widths_in_ticks[i];
        int32_t bid_price = round_to_tick_size(bid_fv - w * symbols[i].tick_size, symbols[i].tick_size, md::SIDE::BUY);
        int32_t ask_price = round_to_tick_size(ask_fv + w * symbols[i].tick_size, symbols[i].tick_size, md::SIDE::SELL);

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
            logger->info("Price was {} and now is {} bid_fv {} pos {} ts {}", bids.back().price, bid_price, bid_fv, position, nanotime());
            oe.cancel_order(bids.back().order_id);
            bids.pop_back();

            if (best_bid.price != 0) {
                logger->info("Best bid: price={}, quantity={}", best_bid.price, best_bid.quantity);
            }

        } else if (bids.size() == 0) {
            logger->info("Sending bid order: symbol={}, price={}, quantity={}", symbols[i].symbol, bid_price, quantity);
            logger->info("bid_fv {} pos {} ts {}", bid_fv, position, nanotime());
            oe.send_order(symbols[i].symbol, last_order_id, md::SIDE::BUY, quantity, bid_price, 0);
            bids.push_back({last_order_id++, bid_price, quantity});

            if (best_bid.price != 0) {
                logger->info("Best bid: price={}, quantity={}", best_bid.price, best_bid.quantity);
            }

            last_order_send_ts = nanotime();
        }

        if (asks.size() > 0 && (asks.back().price != ask_price || last_order_send_ts + 30e9 < nanotime())) {
            logger->info("Cancelling ask order: symbol={}, price={}, quantity={}", symbols[i].symbol, ask_price, quantity);
            logger->info("Price was {} and now is {} ask_fv {} pos {} ts {}", asks.back().price, ask_price, ask_fv, position, nanotime());
            oe.cancel_order(asks.back().order_id);
            asks.pop_back();

            if (best_ask.price != 0) {
                logger->info("Best ask: price={}, quantity={}", best_ask.price, best_ask.quantity);
            }

        } else if (asks.size() == 0) {
            logger->info("Sending ask order: symbol={}, price={}, quantity={}", symbols[i].symbol, ask_price, quantity);
            logger->info("ask_fv {} pos {} ts {}", ask_fv, position, nanotime());

            oe.send_order(symbols[i].symbol, last_order_id, md::SIDE::SELL, quantity, ask_price, 0);
            asks.push_back({last_order_id++, ask_price, quantity});

            if (best_ask.price != 0) {
                logger->info("Best ask: price={}, quantity={}", best_ask.price, best_ask.quantity);
            }

            last_order_send_ts = nanotime();
        }
    }
}

size_t FairValueMarketMaker::cancel_some_open_orders(size_t budget) {
    for (auto& [sym, bids] : bid_orders) {
        if (budget == 0) break;
        while (!bids.empty() && budget > 0) {
            oe.cancel_order(bids.back().order_id);
            bids.pop_back();
            --budget;
        }
    }
    for (auto& [sym, asks] : ask_orders) {
        if (budget == 0) break;
        while (!asks.empty() && budget > 0) {
            oe.cancel_order(asks.back().order_id);
            asks.pop_back();
            --budget;
        }
    }
    size_t remaining = 0;
    for (auto& [sym, bids] : bid_orders) remaining += bids.size();
    for (auto& [sym, asks] : ask_orders) remaining += asks.size();
    return remaining;
}

} // namespace ndfex::bots