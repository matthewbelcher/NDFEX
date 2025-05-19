#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace ndfex::clearing {

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
    };
}