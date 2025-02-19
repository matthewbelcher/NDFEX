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
    // width_in_ticks is the number of ticks away from the fair value that the bot will place orders

    FairValueMarketMaker(OEClient& oe, MDClient& md,
                         std::vector<FairValue*>& fv,
                         std::vector<int32_t> variances,
                         std::vector<symbol_definition> symbols,
                         uint32_t width_in_ticks,
                         uint32_t quantity,
                         uint32_t& last_order_id,
                         std::shared_ptr<spdlog::logger> logger);

    void process();

private:

    OEClient& oe;
    MDClient& md;
    std::vector<FairValue*>& fv;

    std::vector<int32_t> variances;
    std::vector<symbol_definition> symbols;

    const uint32_t width_in_ticks;
    const uint32_t quantity;

    uint64_t last_order_send_ts{0};

    uint32_t& last_order_id;

    std::unordered_map<uint32_t, std::vector<open_order>> bid_orders;
    std::unordered_map<uint32_t, std::vector<open_order>> ask_orders;

    std::shared_ptr<spdlog::logger> logger;
};

} // namespace ndfex::bots