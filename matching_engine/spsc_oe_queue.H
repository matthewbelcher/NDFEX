#pragma once

#include "order_entry/oe_protocol.H"
#include "include/rigtorp/SPSCQueue.h"

namespace ndfex {

using namespace oe;

struct oe_payload {
    oe::MSG_TYPE msg_type;
    uint64_t exch_order_id;
    uint32_t symbol;

    uint32_t client_seq;
    uint32_t client_id;

    md::SIDE side;
    uint32_t quantity;
    uint32_t price;

    uint8_t flags;

    oe_payload(oe::MSG_TYPE msg_type, uint64_t order_id, uint32_t symbol, uint32_t client_seq,
            uint32_t client_id, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags) :
        msg_type(msg_type), exch_order_id(order_id), symbol(symbol), client_seq(client_seq),
        client_id(client_id), side(side), quantity(quantity), price(price), flags(flags) {}

} __attribute__((packed));

using SPSCOEQueue = rigtorp::SPSCQueue<oe_payload>;

}  // namespace ndfex