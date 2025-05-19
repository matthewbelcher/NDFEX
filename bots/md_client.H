#pragma once

#include "order_book.H"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <string>
#include <memory>

namespace ndfex::bots {

/**
 * Listens to the market data feed and builds the order book.
 */

class MDClient {
public:

    class TradeSummaryListener {
    public:
        virtual void on_trade_summary(uint32_t symbol, uint32_t quantity, int32_t price, md::SIDE aggressor_side) = 0;
    };

    MDClient(std::string live_ip, uint16_t live_port,
            std::string snapshot_ip, uint16_t snapshot_port,
            std::string bind_ip, std::shared_ptr<spdlog::logger> logger,
            bool recover_on_drops = false);

    ~MDClient();

    void wait_for_snapshot();

    void process();

    void register_trade_summary_listener(TradeSummaryListener* listener);

    OrderBook::PriceLevel get_best_bid(uint32_t symbol) const;
    OrderBook::PriceLevel get_best_ask(uint32_t symbol) const;

private:

    uint8_t buf[4096];
    std::shared_ptr<spdlog::logger> logger;

    std::unordered_map<uint32_t, OrderBook*> symbol_to_order_book;
    std::unordered_map<uint64_t, uint32_t> order_to_symbol;

    uint32_t last_md_seq_num = 0;

    int live_fd;
    int snapshot_fd;

    std::vector<uint8_t> live_buffer;
    std::unordered_map<uint32_t, std::vector<md::new_order>> snapshot_buffer;

    std::unordered_map<uint32_t, bool> snapshot_complete;
    uint32_t last_snapshot_seq_num = 0;
    std::string snapshot_ip;
    std::string bind_ip;

    const bool recover_on_drops;

    void process_message(uint8_t* buf, size_t len);

    uint32_t get_first_live_seq_num() const;

    TradeSummaryListener* trade_summary_listener = nullptr;
};

} // namespace ndfex::bots
