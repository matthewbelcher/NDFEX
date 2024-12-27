#pragma once

#include "spsc_md_queue.H"

namespace ndfex {

class SPSC_Subscriber {

private:

    SPSCMDQueue& queue;

public:

    SPSC_Subscriber(SPSCMDQueue& queue) : queue(queue) {}
    ~SPSC_Subscriber() = default;

    void onNewOrder(uint64_t order_id, uint32_t symbol, md::SIDE side, uint32_t quantity, uint32_t price, uint8_t flags) {

        queue.emplace(md::MSG_TYPE::NEW_ORDER, order_id, symbol, side, quantity, price, flags);
    }

    void onDeleteOrder(uint64_t order_id) {
        queue.emplace(md::MSG_TYPE::DELETE_ORDER, order_id, 0, md::SIDE::BUY, 0, 0, 0);
    }

    void onTrade(uint64_t order_id, uint32_t quantity, uint32_t price) {
        queue.emplace(md::MSG_TYPE::TRADE, order_id, 0, md::SIDE::BUY, quantity, price, 0);
    }

    void onTradeSummary(uint32_t symbol, md::SIDE aggressor_side, uint32_t total_quantity, uint32_t last_price) {
        queue.emplace(md::MSG_TYPE::TRADE_SUMMARY, 0, symbol, aggressor_side, total_quantity, last_price, 0);
    }

    void onModifyOrder(uint64_t order_id, md::SIDE side, uint32_t quantity, uint32_t price) {
        queue.emplace(md::MSG_TYPE::MODIFY_ORDER, order_id, 0, side, quantity, price, 0);
    }

};

}  // namespace ndfex