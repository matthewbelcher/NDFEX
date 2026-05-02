#include "basket_fair_value.H"

#include <cmath>

namespace ndfex::bots {

BasketFairValue::BasketFairValue(MDClient& md,
                                 std::vector<std::pair<uint32_t, int32_t>> components,
                                 int32_t initial_fv,
                                 std::shared_ptr<spdlog::logger> logger)
    : md(md), components(std::move(components)),
      last_fv(initial_fv),
      last_bid_fv(initial_fv),
      last_ask_fv(initial_fv),
      logger(logger),
      rng(std::random_device{}()) {}

int32_t BasketFairValue::process(uint64_t /*ts*/) {
    int32_t fv = 0;
    for (const auto& [sym, mult] : components) {
        auto bid = md.get_best_bid(sym);
        auto ask = md.get_best_ask(sym);
        if (bid.price == 0 || ask.price == 0) {
            // component book is incomplete; fall back to the last value we
            // could compute cleanly. On the first call this is initial_fv.
            return last_fv;
        }
        const int32_t mid = (bid.price + ask.price) / 2;
        fv += mult * mid;
    }
    last_fv = fv;
    return fv;
}

int32_t BasketFairValue::compute_side(bool pick_bid_side, int32_t& cache) {
    int32_t fv = 0;
    for (const auto& [sym, mult] : components) {
        auto bid = md.get_best_bid(sym);
        auto ask = md.get_best_ask(sym);
        if (bid.price == 0 || ask.price == 0) {
            return cache;
        }
        const int32_t touch = pick_bid_side ? bid.price : ask.price;
        fv += mult * touch;
    }
    cache = fv;
    return fv;
}

bool BasketFairValue::roll_pricing_error() {
    return uni(rng) < PRICING_ERROR_RATE;
}

int32_t BasketFairValue::bid_value(uint64_t ts) {
    if (roll_pricing_error()) {
        // Briefly leak: quote against the basket MID instead of the bid touch.
        // For the consumer's BID quote this means LP_BID = mid − W/2, which sits
        // above sum(component_bids) − W/2 by sum(component_half_spreads). An
        // arber can sell components at the bids and buy UNDY at the (artificially
        // low) LP_ASK in the same window; or symmetric on the ask side via the
        // companion roll in ask_value(). Logged for post-session forensics.
        logger->warn("[basket] pricing-error injection: bid_value returned mid={}",
                     last_fv);
        return process(ts);
    }
    return compute_side(/*pick_bid_side=*/true, last_bid_fv);
}

int32_t BasketFairValue::ask_value(uint64_t ts) {
    if (roll_pricing_error()) {
        logger->warn("[basket] pricing-error injection: ask_value returned mid={}",
                     last_fv);
        return process(ts);
    }
    return compute_side(/*pick_bid_side=*/false, last_ask_fv);
}

void BasketFairValue::apply_jump(double multiplier) {
    // Components are recomputed from market touches on every call, so the
    // jump only matters while the book is incomplete (cache fallback). Apply
    // to all three caches so a jump-then-disconnect window stays consistent.
    last_fv     = static_cast<int32_t>(std::lround(last_fv     * multiplier));
    last_bid_fv = static_cast<int32_t>(std::lround(last_bid_fv * multiplier));
    last_ask_fv = static_cast<int32_t>(std::lround(last_ask_fv * multiplier));
}

} // namespace ndfex::bots
