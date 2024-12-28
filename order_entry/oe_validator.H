#pragma once

#include "symbol_definition.H"
#include "market_data/md_protocol.H"
#include "oe_protocol.H"

#include <spdlog/spdlog.h>
#include <cstdint>
#include <vector>

namespace ndfex::oe {

class OrderEntryValidator {
public:
    OrderEntryValidator(std::unordered_map<uint32_t, symbol_definition>& symbols, std::shared_ptr<spdlog::logger> logger);

    REJECT_REASON validate_new_order(uint64_t order_id, uint32_t symbol, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags);

private:
    std::unordered_map<uint32_t, symbol_definition>& symbols;
    std::shared_ptr<spdlog::logger> logger;
};

}