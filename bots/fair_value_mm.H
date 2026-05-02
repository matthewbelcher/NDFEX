#pragma once

#include "oe_client.H"
#include "md_client.H"
#include "fair_value.H"
#include "matching_engine/symbol_definition.H"

#include <spdlog/spdlog.h>
#include <unordered_map>
#include <vector>

namespace ndfex::bots {

/**
 * Provides liquidity around the fair value
 */
class FairValueMarketMaker {

public:

    struct open_order {
        uint32_t order_id;
        int32_t price;
        uint32_t quantity;

        open_order(uint32_t order_id, int32_t price, uint32_t quantity)
            : order_id(order_id), price(price), quantity(quantity) {}
    };

    // fv is a vector of FairValue objects (random walks consistent with the market)
    // variances is a vector of integers that will be added to the fair value to represent this
    //      bot's view of the market
    // symbols is a vector of symbol_definitions
    // width_in_ticks is the number of ticks away from the fair value that the bot will place orders.
    //      One value applied uniformly to every symbol (back-compat / convenience).

    FairValueMarketMaker(OEClient& oe, MDClient& md,
                         std::vector<FairValue*>& fv,
                         std::vector<int32_t> variances,
                         std::vector<symbol_definition> symbols,
                         uint32_t width_in_ticks,
                         uint32_t quantity,
                         uint32_t& last_order_id,
                         std::shared_ptr<spdlog::logger> logger);

    // Per-symbol widths: widths_in_ticks[i] is applied to symbols[i]. Used to
    // run a different spread on basket components / ETF symbol vs. stand-alone
    // symbols inside a single MM instance. Sizes must match.
    FairValueMarketMaker(OEClient& oe, MDClient& md,
                         std::vector<FairValue*>& fv,
                         std::vector<int32_t> variances,
                         std::vector<symbol_definition> symbols,
                         std::vector<uint32_t> widths_in_ticks,
                         uint32_t quantity,
                         uint32_t& last_order_id,
                         std::shared_ptr<spdlog::logger> logger);

    void process();

    // Cancels up to `budget` open orders this call (taken from the back of
    // each bid/ask vector). Returns the count of orders STILL OPEN after the
    // call. Caller (bot_runner thin-event drain loop) keeps invoking until
    // every MM returns 0, so the cancel burst respects the OE rate gate
    // instead of bursting past the matching engine's MAX_MSGS_PER_SECOND.
    size_t cancel_some_open_orders(size_t budget);

private:

    OEClient& oe;
    MDClient& md;
    std::vector<FairValue*>& fv;

    std::vector<int32_t> variances;
    std::vector<symbol_definition> symbols;

    // Parallel to symbols: widths_in_ticks[i] applies to symbols[i].
    const std::vector<uint32_t> widths_in_ticks;
    const uint32_t quantity;

    uint64_t last_order_send_ts{0};

    uint32_t& last_order_id;

    std::unordered_map<uint32_t, std::vector<open_order>> bid_orders;
    std::unordered_map<uint32_t, std::vector<open_order>> ask_orders;

    std::shared_ptr<spdlog::logger> logger;
};

} // namespace ndfex::bots