#include "order_book.H"

#include <spdlog/spdlog.h>

namespace ndfex::bots {

OrderBook::OrderBook(std::shared_ptr<spdlog::logger> logger) : logger(logger) {}

void OrderBook::new_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags, uint32_t filled_quantity) {
    logger->info("New order: id={}, side={}, quantity={}, price={}, flags={}, filled_quantity={}", order_id, static_cast<int>(side),
    quantity, price, flags, filled_quantity);
}

void OrderBook::delete_order(uint64_t order_id) {
    logger->info("Delete order: id={}", order_id);
}

void OrderBook::modify_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price) {
    logger->info("Modify order: id={}, side={}, quantity={}, price={}", order_id, static_cast<int>(side), quantity, price);
}

void OrderBook::order_trade(uint64_t order_id, uint32_t quantity, int32_t price) {
    logger->info("Order trade: id={}, quantity={}, price={}", order_id, quantity, price);
}

OrderBook::PriceLevel OrderBook::get_best_bid() const {
    return {0, 0};
}

OrderBook::PriceLevel OrderBook::get_best_ask() const {
    return {0, 0};
}

} // namespace ndfex::bots

