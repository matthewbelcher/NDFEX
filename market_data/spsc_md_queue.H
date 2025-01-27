#pragma once

#include "include/rigtorp/SPSCQueue.h"
#include "md_protocol.H"

namespace ndfex {

using namespace md;

struct md_payload {
    MSG_TYPE msg_type;
    uint32_t seq_num;
    uint64_t order_id;
    uint32_t symbol;
    SIDE side;
    uint32_t quantity;
    int32_t price;

    uint8_t flags;

    md_payload(MSG_TYPE msg_type, uint32_t seq_num, uint64_t order_id, uint32_t symbol, SIDE side, uint32_t quantity, int32_t price, uint8_t flags) :
        msg_type(msg_type), seq_num(seq_num), order_id(order_id), symbol(symbol), side(side), quantity(quantity), price(price), flags(flags) {}

} __attribute__((packed));

using SPSCMDQueue = rigtorp::SPSCQueue<md_payload>;

}  // namespace ndfex