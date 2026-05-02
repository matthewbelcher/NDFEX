#pragma once

#include <spdlog/spdlog.h>

#include <cstdint>
#include <memory>
#include <set>

namespace ndfex::clearing {

    // Competition rule constants
    static constexpr int32_t POSITION_LIMIT = 10;
    static constexpr double MIN_PNL = -5000.0;
    static const std::set<uint32_t> EXEMPT_CLIENTS = {98, 99};

    /**
     * Listens to the clearing data feed, and tracks buy and sell prices for each client to calculate positions and P&L.
     */

    class ClearingClient {

        public:
            ClearingClient(std::string live_ip, uint16_t live_port,
                           std::string bind_ip, std::shared_ptr<spdlog::logger> logger);

            ~ClearingClient();

            void process();

            const std::unordered_map<uint32_t, std::unordered_map<uint32_t,int32_t>>& get_positions() const;
            const std::unordered_map<uint32_t, std::unordered_map<uint32_t,int32_t>>& get_raw_pnl() const;
            const std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>>& get_volume() const;

            // Competition rule breach tracking
            const std::unordered_map<uint32_t, std::unordered_map<uint32_t, int32_t>>& get_max_abs_position() const;
            const std::unordered_map<uint32_t, std::unordered_map<uint32_t, bool>>& get_position_limit_breached() const;
            const std::unordered_map<uint32_t, bool>& get_pnl_breached() const;
            const std::unordered_map<uint32_t, double>& get_frozen_pnl() const;

            void set_pnl_breached(uint32_t client_id, double frozen_pnl_value);

            // Apply a non-trading position delta sourced from the etf_service
            // adjustment log (ETF create/redeem). Affects positions and
            // breach tracking only — never touches volume, total_buy/sell,
            // or raw_pnl, since these are zero-cash conversions.
            void apply_etf_adjustment(uint32_t client_id, uint32_t symbol, int32_t delta);

        private:
            uint8_t buf[4096];
            std::shared_ptr<spdlog::logger> logger;

            uint64_t last_seq_num = 0;

            int live_fd;

            std::string bind_ip;

            // client -> (symbol -> position)
            std::unordered_map<uint32_t, std::unordered_map<uint32_t, int32_t>> positions;

            // client -> (symbol -> P&L)
            std::unordered_map<uint32_t, std::unordered_map<uint32_t, int32_t>> raw_pnl;

            // client -> (symbol -> total buy price)
            // client -> (symbol -> total sell price)
            std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> total_buy;
            std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> total_sell;

            // client -> (symbol -> volume)
            std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> volume;

            // Competition rule breach tracking
            // client -> (symbol -> high-water mark of abs(position))
            std::unordered_map<uint32_t, std::unordered_map<uint32_t, int32_t>> max_abs_position;

            // client -> (symbol -> true if position limit ever breached)
            std::unordered_map<uint32_t, std::unordered_map<uint32_t, bool>> position_limit_breached;

            // client -> true if aggregate PnL ever dropped below MIN_PNL
            std::unordered_map<uint32_t, bool> pnl_breached;

            // client -> snapshot of aggregate PnL at moment of breach
            std::unordered_map<uint32_t, double> frozen_pnl;
    };
}