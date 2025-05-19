#pragma once

#include <cstdint>

#include <market_data/md_protocol.H>

/**
 * Internal multicast protocol for all system fills. This emulates the role of a clearing house in the exchange system.
 * The fill multicast will broadcast all client fills to allow tracking of positions and PNL by the "clearing house".
 */

 namespace ndfex::clearing {

    static constexpr uint64_t MAGIC_NUMBER = 0x12345678;

    enum class MSG_TYPE: uint8_t {
        HEARTBEAT = 0,
        FILL = 1,
    };

    struct clearing_header {

        uint64_t magic_number;
        uint16_t length;
        uint32_t seq_num;

        MSG_TYPE msg_type;
    } __attribute__((packed));

    struct fill {

        clearing_header header;

        uint32_t client_id;
        uint32_t symbol;
        uint32_t quantity;
        int32_t price;

        md::SIDE side;

    } __attribute__((packed));

}