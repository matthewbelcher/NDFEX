#pragma once

#include "spsc_md_queue.H"

class SPSC_Subscriber {

private:

    SPSCMDQueue& queue;

public:

    SPSC_Subscriber(SPSCMDQueue& queue) : queue(queue) {}
    ~SPSC_Subscriber() = default;

    void onNewOrder(uint64_t order_id, uint32_t symbol, SIDE side, uint32_t quantity, uint32_t price, uint8_t flags) {
        queue.emplace(MSG_TYPE::NEW_ORDER, order_id, symbol, side, quantity, price, flags);
    }

    void onDeleteOrder(uint64_t order_id) {
        queue.emplace(MSG_TYPE::DELETE_ORDER, order_id, 0, SIDE::BUY, 0, 0, 0);
    }

    void onTrade(uint64_t order_id, uint32_t quantity, uint32_t price) {
        queue.emplace(MSG_TYPE::TRADE, order_id, 0, SIDE::BUY, quantity, price, 0);
    }

    void onTradeSummary(uint32_t symbol, SIDE aggressor_side, uint32_t total_quantity, uint32_t last_price) {
        queue.emplace(MSG_TYPE::TRADE_SUMMARY, 0, symbol, aggressor_side, total_quantity, last_price, 0);
    }

    void onModifyOrder(uint64_t order_id, SIDE side, uint32_t quantity, uint32_t price) {
        queue.emplace(MSG_TYPE::MODIFY_ORDER, order_id, 0, side, quantity, price, 0);
    }

};