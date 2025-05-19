#pragma once

#include "oe_client.H"
#include "md_client.H"
#include "matching_engine/symbol_definition.H"
#include "market_data/md_protocol.H"

#include <spdlog/spdlog.h>
#include <unordered_map>
#include <vector>

namespace ndfex::bots {

    class ImbalanceTaker
      : public MDClient::TradeSummaryListener {

    public:
        ImbalanceTaker(OEClient& oe, MDClient& md,
                       std::vector<symbol_definition> symbol,
                       uint32_t quantity,
                       std::vector<int32_t> imbalance_thresholds,
                       uint32_t& last_order_id,
                       std::shared_ptr<spdlog::logger> logger);

        void process();

        virtual void on_trade_summary(uint32_t symbol, uint32_t quantity, int32_t price, md::SIDE aggressor_side) override;

        private:
            OEClient& oe;

            std::vector<symbol_definition> symbols;
            const uint32_t quantity;
            std::vector<int32_t> imbalance_thresholds;
            std::unordered_map<uint32_t, int32_t> imbalance_quantities;
            uint32_t& last_order_id;
            uint64_t last_order_send_ts{0};

            std::shared_ptr<spdlog::logger> logger;
    };

} // namespace ndfex::bots