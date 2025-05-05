#include "oe_validator.H"
#include <iostream>

namespace ndfex::oe {

OrderEntryValidator::OrderEntryValidator(std::unordered_map<uint32_t, symbol_definition>& symbols, std::shared_ptr<spdlog::logger> logger)
    : symbols(symbols), logger(logger) {
}

REJECT_REASON OrderEntryValidator::validate_new_order(uint64_t order_id, uint32_t symbol, md::SIDE side, uint32_t quantity, uint32_t display_quantity, int32_t price, uint8_t flags) {
    auto it = symbols.find(symbol);
    if (it == symbols.end()) {
        std::cout << "1" << std::endl;
        logger->error("Symbol {} not found", symbol);
        return REJECT_REASON::UNKNOWN_SYMBOL;
    }

    if (quantity % it->second.min_lot_size != 0) {
        std::cout << "2" << std::endl;
        logger->error("Quantity {} not a multiple of min lot size {}", quantity, it->second.min_lot_size);
        return REJECT_REASON::INVALID_QUANTITY;
    }

    if (price % it->second.tick_size != 0) {
        std::cout << "3" << std::endl;
        logger->error("Price {} not a multiple of tick size {}", price, it->second.tick_size);
        return REJECT_REASON::INVALID_PRICE;
    }

    if (quantity > it->second.max_lot_size) {
        std::cout << "4" << std::endl;
        logger->error("Quantity {} greater than max lot size {}", quantity, it->second.max_lot_size);
        return REJECT_REASON::INVALID_QUANTITY;
    }

    if (price > it->second.max_price || price < it->second.min_price) {
        std::cout << "5" << std::endl;
        logger->error("Price {} outside of range [{}, {}]", price, it->second.min_price, it->second.max_price);
        return REJECT_REASON::INVALID_PRICE;
    }

    if (quantity == 0) {
        std::cout << "6" << std::endl;
        logger->error("Quantity is 0");
        return REJECT_REASON::INVALID_QUANTITY;
    }

    // Validation for special orders (AON and Iceberg)
    if (flags & static_cast<uint8_t>(ORDER_FLAGS::AON)) {
        // Limit AON max size
        uint32_t max_aon_qty = it->second.max_lot_size / 2; 
        if (quantity > max_aon_qty) {
            std::cout << "7" << std::endl;
            logger->error("AON order quantity {} exceeds maximum allowed size {} for this symbol", 
                         quantity, max_aon_qty);
            return REJECT_REASON::INVALID_QUANTITY;
        }
        
        // Limit AON min order
        uint32_t min_aon_qty = it->second.min_lot_size * 5; 
        if (quantity < min_aon_qty) {
            std::cout << "8" << std::endl;
            logger->error("AON order quantity {} below minimum size {} for this symbol", 
                         quantity, min_aon_qty);
            return REJECT_REASON::INVALID_QUANTITY;
        }
    }

    if (flags & static_cast<uint8_t>(ORDER_FLAGS::ICEBERG)) {
        /*if (display_quantity == 0 || display_quantity > quantity) {
            std::cout << "9" << std::endl;
            return REJECT_REASON::INVALID_QUANTITY;
        }*/

        uint32_t min_display_percentage = 5;
        uint32_t min_display = (quantity * min_display_percentage) / 100;
        if (min_display < it->second.min_lot_size) {
            std::cout << "min display %" << std::endl;
            min_display = it->second.min_lot_size;
        }
        
        if (display_quantity < min_display) {
            std::cout << "min display small" << std::endl;
            logger->error("Iceberg display quantity {} below minimum required display size {} ({}% of total)", 
                         display_quantity, min_display, min_display_percentage);
            return REJECT_REASON::INVALID_QUANTITY;
        }

        uint32_t max_refreshes = 50;
        uint32_t expected_refreshes = (quantity - 1) / display_quantity;
        if (expected_refreshes > max_refreshes) {
            std::cout << "max refresh" << std::endl;
            logger->error("Iceberg order would require {} refreshes, exceeding maximum allowed ({})",
                         expected_refreshes, max_refreshes);
            return REJECT_REASON::INVALID_QUANTITY;
        }
    }

    return REJECT_REASON::NONE;
}

}