#include "oe_validator.H"

namespace ndfex::oe {

OrderEntryValidator::OrderEntryValidator(std::unordered_map<uint32_t, symbol_definition>& symbols, std::shared_ptr<spdlog::logger> logger)
    : symbols(symbols), logger(logger) {
}

REJECT_REASON OrderEntryValidator::validate_new_order(uint64_t order_id, uint32_t symbol, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags) {
    auto it = symbols.find(symbol);
    if (it == symbols.end()) {
        logger->error("Symbol {} not found", symbol);
        return REJECT_REASON::UNKNOWN_SYMBOL;
    }

    if (quantity % it->second.min_lot_size != 0) {
        logger->error("Quantity {} not a multiple of min lot size {}", quantity, it->second.min_lot_size);
        return REJECT_REASON::INVALID_QUANTITY;
    }

    if (price % it->second.tick_size != 0) {
        logger->error("Price {} not a multiple of tick size {}", price, it->second.tick_size);
        return REJECT_REASON::INVALID_PRICE;
    }

    if (quantity > it->second.max_lot_size) {
        logger->error("Quantity {} greater than max lot size {}", quantity, it->second.max_lot_size);
        return REJECT_REASON::INVALID_QUANTITY;
    }

    if (price > it->second.max_price || price < it->second.min_price) {
        logger->error("Price {} outside of range [{}, {}]", price, it->second.min_price, it->second.max_price);
        return REJECT_REASON::INVALID_PRICE;
    }

    if (quantity == 0) {
        logger->error("Quantity is 0");
        return REJECT_REASON::INVALID_QUANTITY;
    }

    return REJECT_REASON::NONE;
}

}