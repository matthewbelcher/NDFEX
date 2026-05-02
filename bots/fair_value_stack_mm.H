#pragma once

#include "oe_client.H"
#include "md_client.H"
#include "fair_value.H"
#include "matching_engine/symbol_definition.H"
#include "stack_manager.H"

#include <spdlog/spdlog.h>
#include <unordered_map>
#include <vector>

namespace ndfex::bots {

/**
 * Provides liquidity around the fair value
 */
class FairValueStackingMarketMaker {

public:

    // width_in_ticks is the number of ticks away from the fair value that the bot will place orders

    // Legacy ctor: anchors stacking against the symbol's own market BBO via
    // MDClient. Appropriate for stand-alone symbols (GOLD, BLUE, dorms).
    FairValueStackingMarketMaker(OEClient& oe, MDClient& md,
        symbol_definition symbol,
        uint32_t width_in_ticks,
        uint32_t quantity,
        uint32_t& last_order_id,
        std::shared_ptr<spdlog::logger> logger);

    // Basket-aware ctor: anchors stacking against fv->bid_value() and
    // fv->ask_value(), so the stack stays *outside* the structural arb range
    // for ETF/basket symbols (e.g. UNDY). Without this, the stacker would
    // happily echo the symbol's own mid-of-BBO and re-introduce the arb that
    // BasketFairValue's touch-based pricing was designed to prevent.
    FairValueStackingMarketMaker(OEClient& oe, MDClient& md,
        symbol_definition symbol,
        FairValue* fv,
        uint32_t width_in_ticks,
        uint32_t quantity,
        uint32_t& last_order_id,
        std::shared_ptr<spdlog::logger> logger);

    void process();

    // Cancels up to `budget` open orders this call (delegated to StackManager).
    // Returns the count STILL OPEN after the call.
    size_t cancel_some_open_orders(size_t budget) {
        return stack_manager.cancel_some_orders(budget);
    }

private:

    OEClient& oe;
    MDClient& md;

    symbol_definition symbol;

    // nullptr for legacy (BBO-anchored) mode; set for basket-aware mode where
    // stacking is anchored on side-specific fair values. The pointer is
    // non-owning — bot_runner keeps fair_values alive for the bot's lifetime.
    FairValue* fv;

    // Stored in addition to handing it to StackManager so the basket-aware
    // process() path can compute the offset the StackManager's `improve` will
    // re-add to land at the structurally-safe ceiling/floor.
    const uint32_t width_in_ticks;

    StackManager stack_manager;

    std::shared_ptr<spdlog::logger> logger;
};

} // namespace ndfex::bots