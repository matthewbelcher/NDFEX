/**
 * Order Entry Protocol definition
 *
 * Note all numeric fields are in little endian format. This is not typical of most network protocols
 * but IS typical of x86/x86_64 architectures. Many exchanges use this format to avoid byte swapping
 * since customers are typically running on x86/x86_64.
 */

#pragma once

#include <cstdint>
#include <market_data/md_protocol.H>

namespace ndfex::oe {

enum class MSG_TYPE: uint8_t {
    // Requests
    NEW_ORDER = 1,
    DELETE_ORDER = 2,
    MODIFY_ORDER = 3,

    // Login
    LOGIN = 99,
    LOGIN_RESPONSE = 100,

    // Responses
    ACK = 101,
    REJECT = 102,
    FILL = 103,
    CLOSE = 104,
    ERROR = 105
};

enum class REJECT_REASON: uint8_t {
    UNKNOWN_SYMBOL = 1,
    INVALID_ORDER = 2,
    DUPLICATE_ORDER_ID = 3,
    UKNOWN_ORDER_ID = 4,
    INVALID_PRICE = 5,
    INVALID_QUANTITY = 6,
    INVALID_SIDE = 7,
    UNKNOWN_SESSION_ID = 8,
};

enum class LOGIN_STATUS: uint8_t {
    SUCCESS = 0,

    // login reject reasons
    INVALID_USERNAME = 5,
    INVALID_PASSWORD = 6,
    INVALID_SESSION = 7,
    SESSION_ALREADY_ACTIVE = 8,
    DUPLICATE_LOGIN = 9,
    INVALID_CLIENT_ID = 10,
};

struct oe_header {
    uint16_t length;
    uint8_t msg_type;
    uint32_t seq_num;
    uint32_t client_id;
} __attribute__((packed));

struct login {
    oe_header header;
    uint64_t session_id;
    uint8_t username[16];
    uint8_t password[16];
} __attribute__((packed));

struct login_response {
    oe_header header;
    uint64_t session_id;
    uint8_t status;
} __attribute__((packed));

struct new_order {
    oe_header header;
    uint64_t order_id;
    uint32_t symbol;
    md::SIDE side;
    uint32_t quantity;
    uint32_t price;
    uint8_t flags;
} __attribute__((packed));

struct delete_order {
    oe_header header;
    uint64_t order_id;
} __attribute__((packed));

struct modify_order {
    oe_header header;
    uint64_t order_id;
    md::SIDE side;
    uint32_t quantity;
    uint32_t price;
} __attribute__((packed));

struct order_ack {
    oe_header header;
    uint64_t order_id;
    uint64_t exch_order_id;
    uint32_t quantity;
    uint32_t price;
} __attribute__((packed));

struct order_reject {
    oe_header header;
    uint64_t order_id;
    uint8_t reason_code;
} __attribute__((packed));

struct order_fill {
    oe_header header;
    uint64_t order_id;
    uint32_t quantity;
    uint32_t price;
    uint8_t flags;
} __attribute__((packed));

struct order_closed {
    oe_header header;
    uint64_t order_id;

} __attribute__((packed));

struct error_message {
    oe_header header;
    uint8_t error_code;
    uint16_t error_message_length;
    uint8_t error_message[32];
} __attribute__((packed));

} // namespace ndfex::oe