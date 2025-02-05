#include "order_book.H"

#include <spdlog/spdlog.h>

namespace ndfex::bots {

OrderBook::OrderBook(std::shared_ptr<spdlog::logger> logger) : logger(logger) {}

template <typename PriceLevels>
void OrderBook::print_levels(const PriceLevels &levels) {
    for (const auto& [price, quantity] : levels) {
        logger->info("Price: {}, Quantity: {}", price, quantity);
    }
}

void OrderBook::new_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags, uint32_t filled_quantity) {
    logger->info("New order: id={}, side={}, quantity={}, price={}, flags={}, filled_quantity={}", order_id, static_cast<int>(side),
    quantity, price, flags, filled_quantity);
    orders.emplace(order_id, Order{side, price, quantity});

    // check if the price level already exists on the opposite side (this should not happen in a real exchange)
    if (side == md::SIDE::BUY && sell_levels.find(price) != sell_levels.end()) {
        logger->warn("Price level {} already exists on the sell side", price);
    }

    if (side == md::SIDE::SELL && buy_levels.find(price) != buy_levels.end()) {
        logger->warn("Price level {} already exists on the buy side", price);
    }

    if (side == md::SIDE::BUY) {
        buy_levels[price] += quantity;
        print_levels(buy_levels);

    } else {
        sell_levels[price] += quantity;
        print_levels(sell_levels);
    }
}

void OrderBook::delete_order(uint64_t order_id) {
    logger->info("Delete order: id={}", order_id);
    auto it = orders.find(order_id);
    if (it == orders.end()) {
        logger->warn("Order {} not found", order_id);
        return;
    }

    auto& order = it->second;
    if (order.quantity == 0) {
        logger->warn("Order {} has no quantity", order_id);
        orders.erase(it);
        return;
    }

    if (md::SIDE::BUY == order.side) {
        delete_order_from_levels(order_id, buy_levels);
        print_levels(buy_levels);
    } else {
        delete_order_from_levels(order_id, sell_levels);
        print_levels(sell_levels);
    }

    orders.erase(it);
}

template <typename PriceLevels>
void OrderBook::delete_order_from_levels(uint64_t order_id, PriceLevels &levels) {
    auto it = orders.find(order_id);
    if (it == orders.end()) {
        logger->warn("Order {} not found", order_id);
        return;
    }

    auto& order = it->second;
    auto level_it = levels.find(order.price);
    if (level_it == levels.end()) {
        logger->warn("Price level {} not found", order.price);
        return;
    }


    if (level_it->second < order.quantity) {
        logger->warn("Price level {} has less quantity than order {}", order.price, order_id);
        return;
    }

    level_it->second -= order.quantity;
    if (level_it->second == 0) {
        levels.erase(level_it);
    }
}

void OrderBook::modify_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price) {
    logger->info("Modify order: id={}, side={}, quantity={}, price={}", order_id, static_cast<int>(side), quantity, price);
    auto it = orders.find(order_id);
    if (it == orders.end()) {
        logger->warn("Order {} not found", order_id);
        return;
    }

    auto& order = it->second;
    if (md::SIDE::BUY == order.side) {
        delete_order_from_levels(order_id, buy_levels);
    } else {
        delete_order_from_levels(order_id, sell_levels);
    }

    if (side == md::SIDE::BUY) {
        buy_levels[price] += quantity;
        print_levels(buy_levels);
    } else {
        sell_levels[price] += quantity;
        print_levels(sell_levels);
    }

    it->second.price = price;
    it->second.quantity = quantity;
    it->second.side = side;
}

void OrderBook::order_trade(uint64_t order_id, uint32_t quantity, int32_t price) {
    logger->info("Order trade: id={}, quantity={}, price={}", order_id, quantity, price);

    auto it = orders.find(order_id);
    if (it == orders.end()) {
        logger->warn("Order {} not found", order_id);
        return;
    }

    auto& order = it->second;
    if (order.quantity < quantity) {
        logger->warn("Order {} has less quantity than trade", order_id);
        return;
    }

    order.quantity -= quantity;
    if (order.quantity == 0) {
        orders.erase(it);
        if (md::SIDE::BUY == order.side) {
           buy_levels[order.price] -= quantity;
           delete_order_from_levels(order_id, buy_levels);
        } else {
           sell_levels[order.price] -= quantity;
           delete_order_from_levels(order_id, sell_levels);
        }
    } else {
        if (md::SIDE::BUY == order.side) {
            buy_levels[order.price] -= quantity;
            print_levels(buy_levels);
        } else {
            sell_levels[order.price] -= quantity;
            print_levels(sell_levels);
        }
    }
}

OrderBook::PriceLevel OrderBook::get_best_bid() const {
    return buy_levels.empty() ? PriceLevel{0, 0} : PriceLevel{buy_levels.begin()->first, buy_levels.begin()->second};
}

OrderBook::PriceLevel OrderBook::get_best_ask() const {
    return sell_levels.empty() ? PriceLevel{0, 0} : PriceLevel{sell_levels.begin()->first, sell_levels.begin()->second};
}

} // namespace ndfex::bots

