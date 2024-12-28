#pragma once

#include <cstdint>

struct symbol_definition {
    uint32_t symbol;
    uint32_t tick_size;
    uint32_t min_lot_size;
    uint32_t max_lot_size;

    int32_t max_price;
    int32_t min_price;
};