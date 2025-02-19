#pragma once

#include "oe_client.H"
#include "md_client.H"
#include "fair_value.H"
#include "matching_engine/symbol_definition.H"

#include <spdlog/spdlog.h>
#include <unordered_map>
#include <vector>
#include <random>

namespace ndfex::bots {

/**
 * Crosses the market periodically with IOC orders
 */

class RandomTaker {

public:

    RandomTaker(OEClient& oe, MDClient& md,
                std::vector<symbol_definition> symbols,
                uint32_t quantity,
                uint32_t& last_order_id,
                uint64_t time_scale,
                std::shared_ptr<spdlog::logger> logger);

    void process();

private:

    OEClient& oe;
    MDClient& md;
    std::vector<symbol_definition> symbols;

    const uint32_t quantity;

    uint32_t& last_order_id;

    std::shared_ptr<spdlog::logger> logger;

    uint64_t last_ts{0};

    std::random_device rd;
    std::mt19937 gen{rd()};
    std::uniform_int_distribution<uint64_t> time_dist;
};

} // namespace ndfex::bots