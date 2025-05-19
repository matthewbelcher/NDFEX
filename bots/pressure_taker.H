#pragma once

#include "oe_client.H"
#include "md_client.H"
#include "matching_engine/symbol_definition.H"

#include <spdlog/spdlog.h>
#include <random>

namespace ndfex::bots {

/**
 * Sends a sequence of buy or sell orders on one side to simulate pressure on the market in a single direction.
 */
    class PressureTaker {

    public:
        PressureTaker(OEClient& oe, MDClient& md,
                    std::vector<symbol_definition> symbols,
                    uint32_t quantity,
                    uint32_t& last_order_id,
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

        int32_t pressure_quantity{0};
        md::SIDE pressure_side{md::SIDE::BUY};

        std::random_device rd;
        std::mt19937 gen{rd()};
        std::uniform_int_distribution<uint64_t> time_dist;

    };

}