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

    FairValueStackingMarketMaker(OEClient& oe, MDClient& md,
        symbol_definition symbol,
        uint32_t width_in_ticks,
        uint32_t quantity,
        uint32_t& last_order_id,
        std::shared_ptr<spdlog::logger> logger);

    void process();

private:

    OEClient& oe;
    MDClient& md;

    symbol_definition symbol;

    StackManager stack_manager;

    std::shared_ptr<spdlog::logger> logger;
};

} // namespace ndfex::bots