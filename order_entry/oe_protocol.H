/**
 * Order Entry Protocol definition
 */

#pragma once

#include <cstdint>

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
    CLOSE = 104
};

enum class REJECT_REASON: uint8_t {
    UNKNOWN_SYMBOL = 1,
    INVALID_ORDER = 2,
    DUPLICATE_ORDER_ID = 3,
    UKNOWN_ORDER_ID = 4,
    INVALID_PRICE = 5,
    INVALID_QUANTITY = 6,
    INVALID_SIDE = 7,
};

enum class LOGIN_STATUS: uint8_t {
    SUCCESS = 0,

    // login reject reasons
    INVALID_USERNAME = 5,
    INVALID_PASSWORD = 6,
    INVALID_SESSION = 7,
    SESSION_ALREADY_ACTIVE = 8
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
    uint8_t side;
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
    uint8_t side;
    uint32_t quantity;
    uint32_t price;
} __attribute__((packed));

struct ack {
    oe_header header;
    uint64_t order_id;
    uint64_t exch_order_id;
    uint32_t quantity;
    uint32_t price;
} __attribute__((packed));

struct reject {
    oe_header header;
    uint64_t order_id;
    uint8_t reason_code;
} __attribute__((packed));

struct fill {
    oe_header header;
    uint64_t order_id;
    uint32_t quantity;
    uint32_t price;
    uint8_t flags;
} __attribute__((packed));

struct close {
    oe_header header;
    uint64_t order_id;

} __attribute__((packed));

} // namespace ndfex::oe