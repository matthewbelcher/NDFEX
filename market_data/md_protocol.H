#pragma once

#include <cstdint>
#include <string>

namespace ndfex::md {

static std::string go_irish = "GOIRISH!"; // 8 bytes
static const uint64_t MAGIC_NUMBER = *reinterpret_cast<const uint64_t*>(go_irish.c_str());

enum class MSG_TYPE: uint8_t {
    HEARTBEAT = 0,
    NEW_ORDER = 1,
    DELETE_ORDER = 2,
    MODIFY_ORDER = 3,
    TRADE = 4,
    TRADE_SUMMARY = 5,
};

enum class SIDE: uint8_t {
    BUY = 1,
    SELL = 2,
};

struct md_header {
    uint64_t magic_number;
    uint16_t length;
    uint32_t seq_num;
    uint64_t timestamp;

    MSG_TYPE msg_type;
} __attribute__((packed));

struct new_order {
    md_header header;

    uint64_t order_id;
    uint32_t symbol;
    SIDE side;
    uint32_t quantity;
    uint32_t price;

    uint8_t flags;
} __attribute__((packed));

struct delete_order {
    md_header header;

    uint64_t order_id;
} __attribute__((packed));

struct modify_order {
    md_header header;

    uint64_t order_id;
    SIDE side;
    uint32_t quantity;
    uint32_t price;
} __attribute__((packed));

struct trade {
    md_header header;

    uint64_t order_id;
    uint32_t quantity;
    uint32_t price;

} __attribute__((packed));

struct trade_summary {
    md_header header;

    uint32_t symbol;
    SIDE aggressor_side;
    uint32_t total_quantity;
    uint32_t last_price;

} __attribute__((packed));

static const uint16_t MAX_MSG_SIZE = std::max(sizeof(new_order), std::max(sizeof(delete_order), std::max(sizeof(modify_order), std::max(sizeof(trade), sizeof(trade_summary)))));

} // namespace ndfex::md