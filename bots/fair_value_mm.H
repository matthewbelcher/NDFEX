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

    FairValueMarketMaker(OEClient& oe, MDClient& md,
                         std::vector<FairValue*>& fv,
                         std::vector<symbol_definition> symbols,
                         std::shared_ptr<spdlog::logger> logger);

    void process();

private:


    uint32_t last_order_id{1};

    OEClient& oe;
    MDClient& md;
    std::vector<FairValue*>& fv;

    std::vector<symbol_definition> symbols;
    std::unordered_map<uint32_t, std::vector<open_order>> bid_orders;
    std::unordered_map<uint32_t, std::vector<open_order>> ask_orders;

    std::shared_ptr<spdlog::logger> logger;
};

} // namespace ndfex::bots