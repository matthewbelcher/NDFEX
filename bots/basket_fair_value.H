#pragma once

#include "fair_value.H"
#include "md_client.H"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <memory>
#include <random>
#include <utility>
#include <vector>

namespace ndfex::bots {

/**
 * Fair value for an ETF / basket product computed from the underlying
 * components' market book. Each component is specified as a
 * (symbol_id, multiplier) pair.
 *
 * Three valuation queries are provided:
 *   process(ts)    → sum of component MIDs (legacy "fair value", used by
 *                    consumers that don't care about side)
 *   bid_value(ts)  → sum of component BIDs (lower bound — at this price the
 *                    LP can never be arb'd by selling components and buying
 *                    UNDY at LP_UNDY_BID, since they'd receive ≤ what they
 *                    pay for the basket)
 *   ask_value(ts)  → sum of component ASKs (upper bound — symmetric)
 *
 * The market maker quotes bid_value − W/2 and ask_value + W/2, so the
 * structural round-trip arb (sell UNDY at LP_BID, buy components at touches)
 * is always negative regardless of W.
 *
 * Pricing-error injection: with probability ERROR_RATE per call, bid_value
 * and ask_value return the (mid-based) process() value instead of the
 * touch-based one. That re-opens the static arb intermittently and rewards
 * teams who watch for these windows. Tunable via PRICING_ERROR_RATE.
 *
 * The bot using this fair value deliberately does NOT share the underlying
 * components' random-walk FV objects — it only sees the market book, the
 * way a real ETF arbitrageur would.
 *
 * If any component is missing a bid or ask, the last successfully computed
 * side value is returned instead (seeded with initial_fv on the first call).
 */
class BasketFairValue : public FairValue {
public:
    // ~0.01% of bid_value/ask_value calls return the mid-based "buggy" price
    // instead of the touch-based price, briefly creating an arb-able LP
    // quote. Tune this if the observed arb frequency is too generous or too
    // stingy after a session of live trading.
    static constexpr double PRICING_ERROR_RATE = 0.0001;

    BasketFairValue(MDClient& md,
                    std::vector<std::pair<uint32_t, int32_t>> components,
                    int32_t initial_fv,
                    std::shared_ptr<spdlog::logger> logger);

    int32_t process(uint64_t ts) override final;
    int32_t bid_value(uint64_t ts) override final;
    int32_t ask_value(uint64_t ts) override final;

    void apply_jump(double multiplier) override final;

    int32_t current_value() const { return last_fv; }

private:
    // Helper: compute touch-based side value. Returns last_*_fv on incomplete
    // book. `pick_bid_side` picks component bid for a buy-side basket value
    // and component ask for sell-side.
    int32_t compute_side(bool pick_bid_side, int32_t& cache);

    bool roll_pricing_error();

    MDClient& md;
    std::vector<std::pair<uint32_t, int32_t>> components;
    int32_t last_fv;             // mid-based fallback (legacy)
    int32_t last_bid_fv;         // sum(component_bids) fallback
    int32_t last_ask_fv;         // sum(component_asks) fallback
    std::shared_ptr<spdlog::logger> logger;

    // Independent RNG so the error injection isn't predictable from external
    // state. random_device seeded — no env-var override; we want students to
    // be unable to model the error timing.
    std::mt19937 rng;
    std::uniform_real_distribution<double> uni{0.0, 1.0};
};

} // namespace ndfex::bots
